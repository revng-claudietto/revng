/// Select narrower representations of integer computations, and rebuild
/// them accordingly.

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <algorithm>
#include <optional>
#include <utility>

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/KnownBits.h"

#include "revng/Support/CommandLine.h"
#include "revng/Support/Debug.h"
#include "revng/Support/IRBuilder.h"
#include "revng/TypeShrinking/RewritePlans.h"

#include "SCCPValueRanges.h"

using namespace llvm;
using std::max;
using std::min;

static cl::opt<uint32_t> MinimumWidth("min-width",
                                      cl::init(8),
                                      cl::desc("Minimum emitted integer width"),
                                      cl::value_desc("min-width"),
                                      cl::cat(MainCategory));

namespace TypeShrinking {

/// Return the integer width, or zero for a non-integer value.
static unsigned width(const Value *V) {
  return V->getType()->isIntegerTy() ? V->getType()->getIntegerBitWidth() : 0;
}

/// Whether the opcode is integer division or remainder.
static bool isDivision(unsigned Opcode) {
  return Opcode == Instruction::UDiv or Opcode == Instruction::SDiv
         or Opcode == Instruction::URem or Opcode == Instruction::SRem;
}

static unsigned roundWidth(unsigned Required, unsigned Original) {
  Required = max(Required, MinimumWidth.getValue());

  for (unsigned Candidate : { 8U, 16U, 32U, 64U }) {
    if (Candidate >= Required and Candidate < Original)
      return Candidate;
  }
  return Original;
}

/// Return the amount \p I shifts by, if it is a shift by a constant smaller
/// than its width. Such a shift moves windows of bits instead of computing on
/// them.
static std::optional<unsigned> getShiftAmount(const Instruction &I) {
  if (not I.isShift())
    return std::nullopt;

  auto *Amount = dyn_cast<ConstantInt>(I.getOperand(1));
  if (Amount == nullptr or Amount->getValue().uge(width(&I)))
    return std::nullopt;

  return Amount->getZExtValue();
}

std::optional<unsigned> IntegerRewrite::getOperandOffset(const Instruction &I,
                                                         unsigned Index) const {
  switch (I.getOpcode()) {
  case Instruction::Select:
    if (Index == 0)
      return std::nullopt;
    return Offset;

  // A shift by a constant moves the window, read from its first operand. A
  // shift by a variable amount computes from bit zero.
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr: {
    std::optional<unsigned> Shift = getShiftAmount(I);
    if (not Shift)
      return Offset;
    if (Index == 1)
      return std::nullopt;
    if (I.getOpcode() != Instruction::Shl)
      return Offset + *Shift;

    // Below its amount, a left shift brings in zeros
    return Offset > *Shift ? Offset - *Shift : 0;
  }

  // The low zeros of the constant multiplier shift the other operand up, see
  // getWindowLimit
  case Instruction::Mul: {
    unsigned Multiplier = isa<ConstantInt>(I.getOperand(1)) ? 1 : 0;
    return Index == Multiplier ? Offset : 0;
  }

  default:
    return Offset;
  }
}

unsigned PlanningFacts::getKnownTrailingZeros(Value *V) const {
  KnownBits Known = computeKnownBits(V,
                                     Layout,
                                     0,
                                     &Assumptions,
                                     nullptr,
                                     &Dominators);
  return Known.countMinTrailingZeros();
}

/// Return how far above bit zero a window of the result of \p I can start and
/// still be computed from windows of its operands: zero if \p I only computes
/// on low bits.
static unsigned getWindowLimit(Instruction &I, const PlanningFacts &Facts) {
  unsigned Width = width(&I);
  switch (I.getOpcode()) {
  case Instruction::PHI:
  case Instruction::Select:
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    return Width;

  // A carry enters the window unless one addend is zero below it
  case Instruction::Add:
    return max(Facts.getKnownTrailingZeros(I.getOperand(0)),
               Facts.getKnownTrailingZeros(I.getOperand(1)));

  // A borrow enters the window unless the subtrahend is zero below it
  case Instruction::Sub:
    return Facts.getKnownTrailingZeros(I.getOperand(1));

  // `x * 327680` is `(x * 5) << 16`: the low zeros of a constant multiplier
  // shift the other operand up
  case Instruction::Mul: {
    auto *Multiplier = dyn_cast<ConstantInt>(I.getOperand(1));
    if (Multiplier == nullptr)
      Multiplier = dyn_cast<ConstantInt>(I.getOperand(0));
    if (Multiplier == nullptr)
      return 0;
    return Multiplier->getValue().countTrailingZeros();
  }

  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    return getShiftAmount(I) ? Width : 0;

  default:
    return 0;
  }
}

/// Whether some operand of \p I already provides the window \p Window reads
/// from it: either its low bits, or a window planned at the same offset.
/// Otherwise every operand would be shifted down, and the result often shifted
/// back up, for no narrower arithmetic. Constants fold, so an expression of
/// constants alone is worthwhile.
static bool isWindowWorthwhile(const Instruction &I,
                               const IntegerRewrite &Window,
                               const PlanningFacts &Facts) {
  bool HasVariableOperand = false;
  for (unsigned Index = 0; Index < I.getNumOperands(); ++Index) {
    std::optional<unsigned> Offset = Window.getOperandOffset(I, Index);
    Value *Operand = I.getOperand(Index);
    if (not Offset or isa<Constant>(Operand))
      continue;

    if (*Offset == 0 or Facts.plans().isPlannedAsWindow(Operand, *Offset))
      return true;

    HasVariableOperand = true;
  }

  return not HasVariableOperand;
}

namespace {

/// The bits of a value from Offset to Offset + Width. Extending them as
/// Extension says, then shifting them left by Offset, rebuilds the value.
struct BitWindow {
  unsigned Offset = 0;
  unsigned Width = 0;
  ExtensionKind Extension = ExtensionKind::Zero;
};

} // namespace

/// Return the narrowest window of a \p W bits value that starts at most at
/// \p MaximumOffset and reaches bit \p Top, the highest one its extension
/// cannot reproduce. Rounding its width up lowers its start.
static BitWindow fitWindow(unsigned Top,
                           unsigned MaximumOffset,
                           unsigned W,
                           ExtensionKind Extension) {
  unsigned Width = roundWidth(Top - min(Top, MaximumOffset), W);
  return BitWindow{ .Offset = Top > Width ? Top - Width : 0,
                    .Width = Width,
                    .Extension = Extension };
}

/// Return the narrowest window of a \p W bits value that starts at most at
/// \p MaximumOffset, and reaches bit \p UnsignedTop if zero extended, or bit
/// \p SignedTop if sign extended. Among equally narrow windows the lowest
/// wins, then zero extension: so a window above bit zero is always narrower
/// than any at bit zero.
static BitWindow getNarrowestWindow(unsigned UnsignedTop,
                                    unsigned SignedTop,
                                    unsigned MaximumOffset,
                                    unsigned W) {
  BitWindow Zero = fitWindow(UnsignedTop,
                             MaximumOffset,
                             W,
                             ExtensionKind::Zero);
  BitWindow Sign = fitWindow(SignedTop, MaximumOffset, W, ExtensionKind::Sign);
  bool Lower = Sign.Width == Zero.Width and Sign.Offset < Zero.Offset;
  return Sign.Width < Zero.Width or Lower ? Sign : Zero;
}

/// Plan the result of \p I as its narrowest window starting at most at
/// \p Limit: as high as the bits below it allow, then as narrow as the bits
/// above it allow.
static IntegerRewrite planWindow(Instruction &I,
                                 const APInt &Demand,
                                 const PlanningFacts &Facts,
                                 unsigned Limit) {
  // The bits below the window must be either not demanded or known zero
  unsigned W = width(&I);
  unsigned KnownZeros = min(Facts.getKnownTrailingZeros(&I), W);
  unsigned Free = max(Demand.countTrailingZeros(), KnownZeros);
  unsigned MaximumOffset = min({ Limit, Free, W - 1 });

  // The window must reach the highest demanded bit that extending it, with
  // zeros or with copies of its sign, does not reproduce
  ConstantRange Bounds = Facts.ranges().range(&I);
  unsigned Demanded = Demand.getActiveBits();
  unsigned UnsignedTop = min(Demanded, Bounds.getActiveBits());
  unsigned SignedTop = min(Demanded, Bounds.getMinSignedBits());
  BitWindow Window = getNarrowestWindow(UnsignedTop,
                                        SignedTop,
                                        MaximumOffset,
                                        W);
  return IntegerRewrite{ .Offset = Window.Offset,
                         .ResultWidth = Window.Width,
                         .ComputationWidth = Window.Width,
                         .ResultExtension = Window.Extension };
}

/// Whether a comparison against the constant \p Boundary reads like a range
/// check, such as `x < 65536`, which is clearer than `(x >> 16) < 1`.
static bool isRangeCheckBoundary(const APInt &Boundary) {
  return Boundary.isZero() or Boundary.isPowerOf2()
         or Boundary.isNegatedPowerOf2();
}

/// Return the largest K such that comparing the operands of \p Compare without
/// their low K bits gives the same result.
///
/// For equality, both operands must be multiples of 2^K. For an ordering, say
/// `Lower < Upper` or its negation `Lower >= Upper`, one of them suffices: if
/// Upper is a multiple of 2^K, clearing the low K bits of Lower cannot move it
/// across Upper, and if Lower + 1 is, clearing those of Upper cannot either.
/// The high bits then compare as the operands do:
///
///     icmp ult i32 %x, 13172736    ; 201 << 16: K = 16, (x >> 16) < 201
///     icmp ugt i32 %x, 13172735    ; 0xc8ffff:  K = 16, (x >> 16) > 200
///
/// Compilers emit such comparisons for a field at offset 16. `>` and `<=` are
/// the orderings above with the operands swapped. A constant that reads like a
/// range check only contributes the zeros it shares with the other operand.
static unsigned getIgnoredLowBits(ICmpInst &Compare,
                                  const PlanningFacts &Facts) {
  Value *Lower = Compare.getOperand(0);
  Value *Upper = Compare.getOperand(1);
  unsigned Ignored = min(Facts.getKnownTrailingZeros(Lower),
                         Facts.getKnownTrailingZeros(Upper));
  CmpInst::Predicate Predicate = Compare.getPredicate();
  if (ICmpInst::isEquality(Predicate))
    return Ignored;

  if (ICmpInst::isGT(Predicate) or ICmpInst::isLE(Predicate))
    std::swap(Lower, Upper);

  auto *UpperConstant = dyn_cast<ConstantInt>(Upper);
  if (UpperConstant == nullptr
      or not isRangeCheckBoundary(UpperConstant->getValue())) {
    Ignored = max(Ignored, Facts.getKnownTrailingZeros(Upper));
  }

  if (auto *LowerConstant = dyn_cast<ConstantInt>(Lower)) {
    APInt Boundary = LowerConstant->getValue() + 1;
    if (not isRangeCheckBoundary(Boundary))
      Ignored = max(Ignored, Boundary.countTrailingZeros());
  }

  return Ignored;
}

/// Return the narrowest window of the operands of \p Compare starting at most
/// at \p Limit: up to the highest bit of either operand that extending the
/// window does not reproduce.
static BitWindow planComparisonWindow(ICmpInst &Compare,
                                      const PlanningFacts &Facts,
                                      unsigned Limit) {
  // Comparisons preserve the operands' values, rather than their low bits
  unsigned W = width(Compare.getOperand(0));
  const SCCPValueRanges &Ranges = Facts.ranges();
  ConstantRange A = Ranges.range(Compare.getOperand(0));
  ConstantRange B = Ranges.range(Compare.getOperand(1));
  unsigned UnsignedTop = max(A.getActiveBits(), B.getActiveBits());
  unsigned SignedTop = max(A.getMinSignedBits(), B.getMinSignedBits());
  return getNarrowestWindow(UnsignedTop, SignedTop, min(Limit, W - 1), W);
}

/// Whether comparing the window \p Window of the operands of \p Compare is
/// worth shifting them down, rather than comparing their low bits: whether
/// some operand already provides it, as a window planned at the same offset,
/// or as its top bits, as when comparing a field at the top of a value.
/// Constants fold, so comparing constants alone is worthwhile.
static bool isWindowWorthwhile(ICmpInst &Compare,
                               const BitWindow &Window,
                               const PlanningFacts &Facts) {
  const RewritePlans &Plans = Facts.plans();
  bool HasVariableOperand = false;
  for (Value *Operand : Compare.operands()) {
    if (isa<Constant>(Operand))
      continue;

    if (Plans.isPlannedAsWindow(Operand, Window.Offset))
      return true;

    // A planned operand is rebuilt up to the end of its window
    unsigned End = width(Operand);
    auto *I = dyn_cast<Instruction>(Operand);
    const RewritePlan *Plan = I != nullptr ? Plans.find(I) : nullptr;
    if (Plan != nullptr and Plan->getIntegerRewrite() != nullptr) {
      const IntegerRewrite &Integer = *Plan->getIntegerRewrite();
      End = Integer.Offset + Integer.ResultWidth;
    }

    if (Window.Offset + Window.Width == End)
      return true;

    HasVariableOperand = true;
  }

  return not HasVariableOperand;
}

/// Plan \p Compare on the narrowest window of its operands, above the bits
/// that cannot affect its result, see getIgnoredLowBits. Return std::nullopt
/// for non-integer operands.
static std::optional<ComparisonRewrite>
planComparison(ICmpInst &Compare, const PlanningFacts &Facts) {
  unsigned W = width(Compare.getOperand(0));
  if (W == 0)
    return std::nullopt;

  // A window above bit zero must read, from some operand, bits it already
  // provides. Otherwise, the window starts at bit zero.
  unsigned Ignored = getIgnoredLowBits(Compare, Facts);
  BitWindow Window = planComparisonWindow(Compare, Facts, Ignored);
  if (Window.Offset != 0 and not isWindowWorthwhile(Compare, Window, Facts))
    Window = planComparisonWindow(Compare, Facts, 0);

  // A zero extended window below the top bit makes both operands nonnegative,
  // so unsigned predicates preserve their order. Otherwise, the top bit of the
  // window is the sign of the operands, and every predicate is preserved.
  auto Predicate = Compare.getPredicate();
  bool Zero = Window.Extension == ExtensionKind::Zero;
  if (Zero and Window.Offset + Window.Width < W)
    Predicate = Compare.getUnsignedPredicate();

  return ComparisonRewrite{ .Offset = Window.Offset,
                            .OperandWidth = Window.Width,
                            .Predicate = Predicate };
}

RewritePlan RewritePlan::create(Instruction &I,
                                const APInt &Demand,
                                const PlanningFacts &Facts) {
  if (auto *Compare = dyn_cast<ICmpInst>(&I)) {
    if (auto Plan = planComparison(*Compare, Facts))
      return RewritePlan(*Plan);
    return RewritePlan();
  }

  unsigned W = width(&I);
  if (W == 0)
    return RewritePlan();

  // Only plan operations supported by the rebuilder.
  switch (I.getOpcode()) {
  case Instruction::PHI:
  case Instruction::Select:
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::UDiv:
  case Instruction::URem:
  case Instruction::SDiv:
  case Instruction::SRem:
    break;
  default:
    return RewritePlan();
  }

  // A window above bit zero must read, from some operand, bits it already
  // provides. Otherwise, the window starts at bit zero.
  IntegerRewrite P = planWindow(I, Demand, Facts, getWindowLimit(I, Facts));
  if (P.Offset != 0 and not isWindowWorthwhile(I, P, Facts))
    P = planWindow(I, Demand, Facts, 0);

  // Divisions and shifts by a variable amount compute from bit zero, on low
  // bits of their operands, and can need more of them than the result has.
  const SCCPValueRanges &Ranges = Facts.ranges();
  if (I.isShift() and not getShiftAmount(I)) {
    // Shift execution must retain the source bits and exceed every amount.
    unsigned Required = min(Demand.getActiveBits(), P.ResultWidth);
    ConstantRange Amounts = Ranges.range(I.getOperand(1));
    unsigned Maximum = Amounts.getUnsignedMax().getLimitedValue(W);
    if (Maximum >= W) {
      // The range does not prove that narrowing keeps the shift defined.
      P.ComputationWidth = W;
    } else {
      unsigned Input = Required;
      if (I.getOpcode() != Instruction::Shl) {
        // Right shifts pull in high bits, capped by the exact input width.
        ConstantRange InputBounds = Ranges.range(I.getOperand(0));
        unsigned Exact = I.getOpcode() == Instruction::LShr ?
                           InputBounds.getActiveBits() :
                           InputBounds.getMinSignedBits();
        Input = min(Required + Maximum, Exact);
      }
      unsigned Execution = max({ P.ResultWidth, Input, Maximum + 1 });
      P.ComputationWidth = roundWidth(Execution, W);
    }
  } else if (isDivision(I.getOpcode())) {
    // Division and remainder need exact operands even for a partial result.
    bool Signed = I.getOpcode() == Instruction::SDiv
                  or I.getOpcode() == Instruction::SRem;
    ConstantRange Left = Ranges.range(I.getOperand(0));
    ConstantRange Right = Ranges.range(I.getOperand(1));
    unsigned Input = Signed ?
                       max(Left.getMinSignedBits(), Right.getMinSignedBits()) :
                       max(Left.getActiveBits(), Right.getActiveBits());
    P.ComputationWidth = roundWidth(max(P.ResultWidth, Input), W);

    // Narrowing must not introduce signed-minimum / -1 overflow.
    if (Signed) {
      while (P.ComputationWidth < W) {
        APInt Minimum = APInt::getSignedMinValue(P.ComputationWidth).sext(W);
        bool CanOverflow = Left.contains(Minimum)
                           and Right.contains(APInt::getAllOnes(W));
        if (not CanOverflow)
          break;

        P.ComputationWidth = roundWidth(P.ComputationWidth + 1, W);
      }
    }
  }
  return RewritePlan(P);
}

void RewritePlans::insert(Instruction &I, const RewritePlan &Plan) {
  revng_assert(Plan);
  bool Inserted = Plans.insert({ &I, Plan }).second;
  revng_assert(Inserted);

  NarrowsAnything |= Plan.getResultWidth() < width(&I);
  if (auto *Compare = Plan.getComparisonRewrite())
    NarrowsAnything |= Compare->OperandWidth < width(I.getOperand(0));
}

bool RewritePlans::isPlannedAsWindow(Value *V, unsigned Offset) const {
  auto *I = dyn_cast<Instruction>(V);
  const RewritePlan *Plan = I != nullptr ? find(I) : nullptr;
  if (Plan == nullptr or Offset == 0)
    return false;

  const IntegerRewrite *Integer = Plan->getIntegerRewrite();
  return Integer != nullptr and Integer->Offset == Offset;
}

bool RewritePlans::apply() {
  if (not NarrowsAnything)
    return false;

  // First, build a replacement for each planned instruction, right before it:
  // the same operation, performed at the widths its plan selects, on operands
  // converted to those widths.
  //
  // Since plans are in reverse post-order, the planned operands of an
  // instruction other than a PHI already have a replacement.
  revng_assert(Replacements.empty());
  for (const auto &[Original, Plan] : Plans)
    Replacements[Original] = rebuild(*Original, Plan);

  // Then, now that every replacement exists, add the incoming values of the
  // new PHIs, each adapted at the end of the predecessor it arrives from.
  finalizePhis();

  // Then, make the instructions without a plan use the replacements. This is
  // not a replaceAllUsesWith, because a replacement can be narrower than its
  // original.
  replaceUses();

  // Finally, the original instructions are only used by each other, and can
  // be erased.
  eraseOriginals();

  return true;
}

Value *RewritePlans::rebuild(Instruction &I, const RewritePlan &Plan) {
  revng::IRBuilder Builder(&I, I.getDebugLoc());

  if (auto *Compare = Plan.getComparisonRewrite()) {
    unsigned Offset = Compare->Offset;
    unsigned Width = Compare->OperandWidth;
    Value *LHS = adapt(Builder, I.getOperand(0), Offset, Width);
    Value *RHS = adapt(Builder, I.getOperand(1), Offset, Width);
    return Builder.CreateICmp(Compare->Predicate, LHS, RHS);
  }

  const IntegerRewrite &Integer = *Plan.getIntegerRewrite();
  auto *ResultType = Builder.getIntNTy(Integer.ResultWidth);
  if (auto *Phi = dyn_cast<PHINode>(&I))
    return Builder.CreatePHI(ResultType, Phi->getNumIncomingValues());

  if (auto *Select = dyn_cast<SelectInst>(&I)) {
    Value *Condition = adapt(Builder, Select->getCondition(), 0, 1);
    Value *True = adaptOperand(Builder, I, Integer, 1);
    Value *False = adaptOperand(Builder, I, Integer, 2);
    return Builder.CreateSelect(Condition, True, False);
  }

  // Casts and shifts by a constant move the window across their operand
  if (isa<CastInst>(&I) or getShiftAmount(I))
    return moveWindow(Builder, I, Integer);

  // Fresh operations do not inherit nowrap/exact flags, which narrowing can
  // invalidate even when the original operation had those flags.
  Value *LHS = adaptOperand(Builder, I, Integer, 0);
  Value *RHS = adaptOperand(Builder, I, Integer, 1);
  auto Opcode = Instruction::BinaryOps(I.getOpcode());
  Value *Operation = Builder.CreateBinOp(Opcode, LHS, RHS);
  return Builder.CreateTrunc(Operation, ResultType);
}

/// Return the bits of \p V from \p Offset to \p Offset + \p Width. Those past
/// its top are copies of its sign if \p Signed, zeros otherwise.
static Value *extractBits(revng::IRBuilder &Builder,
                          Value *V,
                          unsigned Offset,
                          unsigned Width,
                          bool Signed) {
  auto *Type = Builder.getIntNTy(Width);
  unsigned Available = width(V);
  bool PastTop = Offset + Width > Available;
  if (Offset >= Available) {
    if (not Signed)
      return ConstantInt::get(Type, 0);
    Offset = Available - 1;
  }

  if (Offset != 0) {
    V = Signed and PastTop ? Builder.CreateAShr(V, Offset) :
                             Builder.CreateLShr(V, Offset);
  }

  return Builder.CreateIntCast(V, Type, Signed);
}

/// Rebuild \p I, a cast or a shift by a constant, by moving the window \p Plan
/// selects across its operand, see getOperandOffset.
Value *RewritePlans::moveWindow(revng::IRBuilder &Builder,
                                Instruction &I,
                                const IntegerRewrite &Plan) {
  Value *Operand = I.getOperand(0);
  unsigned Read = *Plan.getOperandOffset(I, 0);
  unsigned Width = Plan.ResultWidth;

  // A left shift reads its operand below the window, but not below bit zero:
  // the zeros it brings in there remain to be shifted in
  if (I.getOpcode() == Instruction::Shl) {
    unsigned Remaining = *getShiftAmount(I) + Read - Plan.Offset;
    if (Remaining >= Width)
      return ConstantInt::get(Builder.getIntNTy(Width), 0);

    Value *Window = adapt(Builder, Operand, Read, Width);
    if (Remaining == 0)
      return Window;
    return Builder.CreateShl(Window, Remaining);
  }

  // Right shifts and extensions read their operand from above, and past its
  // top get copies of its sign or zeros, as they do
  if (Read + Width <= width(Operand))
    return adapt(Builder, Operand, Read, Width);

  bool Signed = isa<SExtInst>(&I) or I.getOpcode() == Instruction::AShr;
  Value *Whole = adapt(Builder, Operand, 0, width(Operand));
  return extractBits(Builder, Whole, Read, Width, Signed);
}

/// Return the window of operand \p Index of \p I that computing the window
/// \p Plan selects for the result of \p I reads, see getOperandOffset, at the
/// width of the computation.
Value *RewritePlans::adaptOperand(revng::IRBuilder &Builder,
                                  Instruction &I,
                                  const IntegerRewrite &Plan,
                                  unsigned Index) {
  std::optional<unsigned> Offset = Plan.getOperandOffset(I, Index);
  revng_assert(Offset);
  return adapt(Builder, I.getOperand(Index), *Offset, Plan.ComputationWidth);
}

void RewritePlans::finalizePhis() {
  for (const auto &[Original, Plan] : Plans) {
    auto *Phi = dyn_cast<PHINode>(Original);
    if (Phi == nullptr)
      continue;

    auto *NewPhi = cast<PHINode>(Replacements.lookup(Phi));
    unsigned Offset = Plan.getIntegerRewrite()->Offset;
    unsigned Width = Plan.getResultWidth();
    for (unsigned Index = 0; Index < Phi->getNumIncomingValues(); ++Index) {
      BasicBlock *Predecessor = Phi->getIncomingBlock(Index);
      Value *Incoming = Phi->getIncomingValue(Index);
      const DebugLoc &Location = Phi->getDebugLoc();
      Value *Adapted = adaptOnEdge(Predecessor,
                                   Incoming,
                                   Offset,
                                   Width,
                                   Location);
      NewPhi->addIncoming(Adapted, Predecessor);
    }
  }
}

void RewritePlans::replaceUses() {
  for (Instruction *Original : make_first_range(Plans)) {
    unsigned Width = Original->getType()->getIntegerBitWidth();
    const DebugLoc &Location = Original->getDebugLoc();
    for (Use &Operand : make_early_inc_range(Original->uses())) {
      auto *User = cast<Instruction>(Operand.getUser());
      if (find(User) != nullptr)
        continue;

      if (auto *Phi = dyn_cast<PHINode>(User)) {
        BasicBlock *Predecessor = Phi->getIncomingBlock(Operand);
        Operand.set(adaptOnEdge(Predecessor, Original, 0, Width, Location));
      } else {
        revng::IRBuilder Builder(User, Location);
        Operand.set(adapt(Builder, Original, 0, Width));
      }

      // The replacement only preserves the demanded bits, and demand analysis
      // ignores flags: a user with flags could become poison. Instructions
      // with flags get a plan, dropping them, unless dead or unreachable.
      // Handling those would take dropping their flags, or tracking which
      // replacements reproduce the whole original value.
      bool IsInteger = User->getType()->isIntegerTy();
      revng_assert(not IsInteger or not User->hasPoisonGeneratingFlags());
    }
  }
}

void RewritePlans::eraseOriginals() {
  for (Instruction *Original : make_first_range(Plans))
    Original->dropAllReferences();

  for (Instruction *Original : make_first_range(Plans)) {
    revng_assert(Original->use_empty());
    Original->eraseFromParent();
  }
}

/// Return the bits of \p V from \p Offset to \p Offset + \p Width, at the
/// insertion point of \p Builder. A planned instruction is represented by its
/// replacement, which holds the bits of its window: those below are zero or not
/// demanded, those above are its extension.
Value *RewritePlans::adapt(revng::IRBuilder &Builder,
                           Value *V,
                           unsigned Offset,
                           unsigned Width) {
  using namespace PatternMatch;

  // A mask keeping every bit of the window is transparent to it
  Value *Masked = nullptr;
  const APInt *Mask = nullptr;
  if (Offset != 0 and match(V, m_And(m_Value(Masked), m_APInt(Mask)))
      and Offset + Width <= Mask->getBitWidth()
      and Mask->extractBits(Width, Offset).isAllOnes()) {
    return adapt(Builder, Masked, Offset, Width);
  }

  bool Signed = false;
  unsigned Start = 0;
  if (auto *I = dyn_cast<Instruction>(V)) {
    if (const RewritePlan *Plan = find(I)) {
      // Planning in reverse post-order rebuilds every operand before the
      // instruction using it, except across the back edges of PHIs.
      V = Replacements.lookup(I);
      revng_assert(V != nullptr);

      if (auto *Integer = Plan->getIntegerRewrite()) {
        Signed = Integer->ResultExtension == ExtensionKind::Sign;
        Start = Integer->Offset;
      }
    }
  }

  // Below the replacement, the bits are zero: shift it up
  auto *Type = Builder.getIntNTy(Width);
  if (Offset < Start) {
    unsigned Shift = Start - Offset;
    if (Shift >= Width)
      return ConstantInt::get(Type, 0);
    return Builder.CreateShl(Builder.CreateIntCast(V, Type, Signed), Shift);
  }

  // Above it, shift it down: past its top, its bits are its extension
  return extractBits(Builder, V, Offset - Start, Width, Signed);
}

/// Adapt \p V at the end of \p Predecessor, where its edge to a PHI is taken.
/// The result is shared, because LLVM requires a PHI to receive the same value
/// on every edge from the same predecessor.
Value *RewritePlans::adaptOnEdge(BasicBlock *Predecessor,
                                 Value *V,
                                 unsigned Offset,
                                 unsigned Width,
                                 const DebugLoc &Location) {
  EdgeValueKey Key{ Predecessor, V, { Offset, Width } };
  auto [It, Inserted] = EdgeValues.try_emplace(Key);
  if (Inserted) {
    revng::IRBuilder Builder(Predecessor->getTerminator(), Location);
    It->second = adapt(Builder, V, Offset, Width);
  }
  return It->second;
}

class TypeShrinkingAnnotatedWriter : public AssemblyAnnotationWriter {
private:
  const RewritePlans &Plans;

public:
  TypeShrinkingAnnotatedWriter(const RewritePlans &Plans) : Plans(Plans) {}

public:
  void emitInstructionAnnot(const Instruction *I,
                            formatted_raw_ostream &Stream) final {
    const RewritePlan *Plan = Plans.find(const_cast<Instruction *>(I));
    if (Plan == nullptr)
      return;

    if (auto *Integer = Plan->getIntegerRewrite()) {
      bool Signed = Integer->ResultExtension == ExtensionKind::Sign;
      StringRef Extension = Signed ? "sign" : "zero";
      Stream << "  ; Offset: " << Integer->Offset << "\n";
      Stream << "  ; Result width: " << Integer->ResultWidth << "\n";
      Stream << "  ; Computation width: " << Integer->ComputationWidth << "\n";
      Stream << "  ; Result extension: " << Extension << "\n";
    } else {
      const auto &Compare = *Plan->getComparisonRewrite();
      Stream << "  ; Offset: " << Compare.Offset << "\n";
      Stream << "  ; Operand width: " << Compare.OperandWidth << "\n";
      Stream << "  ; Predicate: "
             << CmpInst::getPredicateName(Compare.Predicate) << "\n";
    }
  }
};

void RewritePlans::dump() const {
  TypeShrinkingAnnotatedWriter Annotator(*this);
  raw_os_ostream Stream(dbg);
  F.print(Stream, &Annotator);
}

} // namespace TypeShrinking
