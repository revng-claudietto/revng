/// Select narrower representations of integer computations.

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <algorithm>
#include <optional>
#include <utility>

#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/KnownBits.h"

#include "revng/Support/CommandLine.h"

#include "Planner.h"
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

/// Whether a comparison against the constant \p Boundary reads like a range
/// check, such as `x < 65536`, which is clearer than `(x >> 16) < 1`.
static bool isRangeCheckBoundary(const APInt &Boundary) {
  return Boundary.isZero() or Boundary.isPowerOf2()
         or Boundary.isNegatedPowerOf2();
}

RewritePlan Planner::plan(Instruction &I) {
  if (not I.getType()->isIntegerTy() or Demands.isInstructionDead(&I))
    return RewritePlan();

  if (auto *Compare = dyn_cast<ICmpInst>(&I))
    return planComparison(*Compare);

  return planInteger(I);
}

/// Return the bits of the result of \p I its users demand.
APInt Planner::getDemand(Instruction &I) {
  // This pass leaves dead code for DCE, but a dead instruction still
  // executes, and demand analysis attributes nothing to it. Narrowing an
  // operand it reads could therefore change what it computes:
  //
  //     %d = or i64 %x, 256       ; never zero, bit 8 is always set
  //     %unused = udiv i64 42, %d ; dead, but defined for every %x
  //     %r = trunc i64 %d to i8   ; the only live use wants 8 bits
  //
  // Narrowing %d to the 8 demanded bits drops bit 8 and leaves %unused
  // dividing by zero. Count a dead user as reading every bit, which keeps
  // such an operand wide. Narrowing driven by \ref SCCPValueRanges stays
  // available, as bounds hold for the value a dead user reads too.
  for (User *U : I.users()) {
    if (Demands.isInstructionDead(cast<Instruction>(U)))
      return APInt::getAllOnes(width(&I));
  }

  return Demands.getDemandedBits(&I);
}

/// Return how many low bits of \p V are known to be zero.
unsigned Planner::getKnownTrailingZeros(Value *V) const {
  KnownBits Known = computeKnownBits(V,
                                     Layout,
                                     0,
                                     &Assumptions,
                                     nullptr,
                                     &Dominators);
  return Known.countMinTrailingZeros();
}

/// Whether \p V is planned as a window starting at \p Offset, above bit zero.
bool Planner::isPlannedAsWindow(Value *V, unsigned Offset) const {
  auto *I = dyn_cast<Instruction>(V);
  const RewritePlan *Plan = I != nullptr ? Plans.find(I) : nullptr;
  if (Plan == nullptr or Offset == 0)
    return false;

  const IntegerRewrite *Integer = Plan->getIntegerRewrite();
  return Integer != nullptr and Integer->Offset == Offset;
}

/// Plan \p I, which has an integer result, on the narrowest window of its
/// result. Return an empty plan for an opcode the rebuilding does not support.
RewritePlan Planner::planInteger(Instruction &I) {
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

  // A window above bit zero must read bits some operand already provides,
  // see isIntegerWindowWorthwhile. Otherwise, the window starts at bit zero.
  APInt Demand = getDemand(I);
  IntegerRewrite Rewrite = planIntegerWindow(I, Demand, getWindowLimit(I));
  if (not isIntegerWindowWorthwhile(I, Rewrite))
    Rewrite = planIntegerWindow(I, Demand, 0);

  return RewritePlan(Rewrite);
}

/// Plan the result of \p I as its narrowest window starting at most at
/// \p Limit: as high as the bits below it allow, then as narrow as the bits
/// above it allow.
IntegerRewrite Planner::planIntegerWindow(Instruction &I,
                                          const APInt &Demand,
                                          unsigned Limit) const {
  // The bits below the window must be either not demanded or known zero
  unsigned W = width(&I);
  unsigned KnownZeros = min(getKnownTrailingZeros(&I), W);
  unsigned Free = max(Demand.countTrailingZeros(), KnownZeros);
  unsigned MaximumOffset = min({ Limit, Free, W - 1 });

  // The window must reach the highest demanded bit that extending it, with
  // zeros or with copies of its sign, does not reproduce
  ConstantRange Bounds = Ranges.range(&I);
  unsigned Demanded = Demand.getActiveBits();
  unsigned UnsignedTop = min(Demanded, Bounds.getActiveBits());
  unsigned SignedTop = min(Demanded, Bounds.getMinSignedBits());
  BitWindow Window = getNarrowestWindow(UnsignedTop,
                                        SignedTop,
                                        MaximumOffset,
                                        W);
  return IntegerRewrite{
    .Offset = Window.Offset,
    .ResultWidth = Window.Width,
    .ComputationWidth = getComputationWidth(I, Demand, Window.Width),
    .ResultExtension = Window.Extension
  };
}

/// Return how far above bit zero a window of the result of \p I can start and
/// still be computed from windows of its operands: zero if \p I only computes
/// on low bits.
unsigned Planner::getWindowLimit(Instruction &I) const {
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
    return max(getKnownTrailingZeros(I.getOperand(0)),
               getKnownTrailingZeros(I.getOperand(1)));

  // A borrow enters the window unless the subtrahend is zero below it
  case Instruction::Sub:
    return getKnownTrailingZeros(I.getOperand(1));

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

/// Whether the window \p Rewrite plans for the result of \p I reads, from some
/// operand, bits it already provides: either its low bits, or a window planned
/// at the same offset. Otherwise, shifting every operand down to the window,
/// and the result back up, costs more than the narrower arithmetic saves:
///
///     %field = and i64 %x, 4294901760 ; 0xffff0000
///     ret i64 %field
///
/// The window of %field from bit 16 reads %x from bit 16, which %x does not
/// provide. So %field is planned from bit zero, masking the low 32 bits of
/// %x, rather than shifting %x down by 16, masking 16 bits, and shifting the
/// result back up.
///
/// A window from bit zero, and an expression of constants alone, which fold,
/// are always worthwhile.
bool Planner::isIntegerWindowWorthwhile(const Instruction &I,
                                        const IntegerRewrite &Rewrite) const {
  if (Rewrite.Offset == 0)
    return true;

  bool HasVariableOperand = false;
  for (unsigned Index = 0; Index < I.getNumOperands(); ++Index) {
    std::optional<unsigned> Offset = Rewrite.getOperandOffset(I, Index);
    Value *Operand = I.getOperand(Index);
    if (not Offset or isa<Constant>(Operand))
      continue;

    if (*Offset == 0 or isPlannedAsWindow(Operand, *Offset))
      return true;

    HasVariableOperand = true;
  }

  return not HasVariableOperand;
}

/// Return the width computing a result of \p ResultWidth bits takes. Only
/// divisions and shifts by a variable amount need more: they compute from bit
/// zero, on low bits of their operands, and can need more of them than the
/// result has.
unsigned Planner::getComputationWidth(Instruction &I,
                                      const APInt &Demand,
                                      unsigned ResultWidth) const {
  unsigned W = width(&I);
  if (I.isShift() and not getShiftAmount(I)) {
    // Shift execution must retain the source bits and exceed every amount.
    unsigned Required = min(Demand.getActiveBits(), ResultWidth);
    ConstantRange Amounts = Ranges.range(I.getOperand(1));
    unsigned Maximum = Amounts.getUnsignedMax().getLimitedValue(W);

    // The range does not prove that narrowing keeps the shift defined.
    if (Maximum >= W)
      return W;

    unsigned Input = Required;
    if (I.getOpcode() != Instruction::Shl) {
      // Right shifts pull in high bits, capped by the exact input width.
      ConstantRange InputBounds = Ranges.range(I.getOperand(0));
      unsigned Exact = I.getOpcode() == Instruction::LShr ?
                         InputBounds.getActiveBits() :
                         InputBounds.getMinSignedBits();
      Input = min(Required + Maximum, Exact);
    }
    return roundWidth(max({ ResultWidth, Input, Maximum + 1 }), W);
  }

  if (isDivision(I.getOpcode())) {
    // Division and remainder need exact operands even for a partial result.
    bool Signed = I.getOpcode() == Instruction::SDiv
                  or I.getOpcode() == Instruction::SRem;
    ConstantRange Left = Ranges.range(I.getOperand(0));
    ConstantRange Right = Ranges.range(I.getOperand(1));
    unsigned Input = Signed ?
                       max(Left.getMinSignedBits(), Right.getMinSignedBits()) :
                       max(Left.getActiveBits(), Right.getActiveBits());
    unsigned Width = roundWidth(max(ResultWidth, Input), W);

    // Narrowing must not introduce signed-minimum / -1 overflow.
    if (Signed) {
      while (Width < W) {
        APInt Minimum = APInt::getSignedMinValue(Width).sext(W);
        bool CanOverflow = Left.contains(Minimum)
                           and Right.contains(APInt::getAllOnes(W));
        if (not CanOverflow)
          break;

        Width = roundWidth(Width + 1, W);
      }
    }
    return Width;
  }

  return ResultWidth;
}

/// Plan \p Compare on the narrowest window of its operands, above the bits
/// that cannot affect its result, see getIgnoredLowBits. Return an empty plan
/// for non-integer operands.
RewritePlan Planner::planComparison(ICmpInst &Compare) const {
  unsigned W = width(Compare.getOperand(0));
  if (W == 0)
    return RewritePlan();

  // A window above bit zero must read bits some operand already provides,
  // see isComparisonWindowWorthwhile. Otherwise, the window starts at bit
  // zero.
  BitWindow Window = planComparisonWindow(Compare, getIgnoredLowBits(Compare));
  if (not isComparisonWindowWorthwhile(Compare, Window))
    Window = planComparisonWindow(Compare, 0);

  // A window that is sign extended, or that holds the top bit, keeps the sign
  // of the operands, and so every predicate:
  //
  //     icmp slt i64 (sext i8 %x), (sext i8 %y)  ->  icmp slt i8 %x, %y
  //     icmp ult i64 (sext i8 %x), (sext i8 %y)  ->  icmp ult i8 %x, %y
  //     icmp slt i32 %x, 13172736  ->  icmp slt i16 (%x >> 16), 201
  //
  // A zero extended window below the top bit makes both operands
  // nonnegative, and so signed predicates order them as unsigned ones do:
  //
  //     icmp slt i64 (zext i8 %x), (zext i8 %y)  ->  icmp ult i8 %x, %y
  CmpInst::Predicate Predicate = Compare.getPredicate();
  bool KeepsSign = Window.Extension == ExtensionKind::Sign
                   or Window.Offset + Window.Width == W;
  if (not KeepsSign)
    Predicate = Compare.getUnsignedPredicate();

  return RewritePlan(ComparisonRewrite{ .Offset = Window.Offset,
                                        .OperandWidth = Window.Width,
                                        .Predicate = Predicate });
}

/// Return the narrowest window of the operands of \p Compare starting at most
/// at \p Limit: up to the highest bit of either operand that extending the
/// window does not reproduce. For instance, zero extending it:
///
///     %a = zext i16 %x to i64
///     %c = icmp ult i64 %a, 1000
///
/// Both operands are in [0, 2^16): the window is their low 16 bits, and zero
/// extending it reproduces the other 48. Instead, sign extending it:
///
///     %a = sext i8 %x to i64
///     %c = icmp slt i64 %a, -42
///
/// Both operands are in [-2^7, 2^7): the window is their low 8 bits, and
/// sign extending it reproduces the other 56. Zero extending would need all
/// 64 bits, as negative values have the top one set.
BitWindow Planner::planComparisonWindow(ICmpInst &Compare,
                                        unsigned Limit) const {
  // Comparisons preserve the operands' values, rather than their low bits
  unsigned W = width(Compare.getOperand(0));
  ConstantRange A = Ranges.range(Compare.getOperand(0));
  ConstantRange B = Ranges.range(Compare.getOperand(1));
  unsigned UnsignedTop = max(A.getActiveBits(), B.getActiveBits());
  unsigned SignedTop = max(A.getMinSignedBits(), B.getMinSignedBits());
  return getNarrowestWindow(UnsignedTop, SignedTop, min(Limit, W - 1), W);
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
unsigned Planner::getIgnoredLowBits(ICmpInst &Compare) const {
  Value *Lower = Compare.getOperand(0);
  Value *Upper = Compare.getOperand(1);
  unsigned Ignored = min(getKnownTrailingZeros(Lower),
                         getKnownTrailingZeros(Upper));
  CmpInst::Predicate Predicate = Compare.getPredicate();
  if (ICmpInst::isEquality(Predicate))
    return Ignored;

  if (ICmpInst::isGT(Predicate) or ICmpInst::isLE(Predicate))
    std::swap(Lower, Upper);

  auto *UpperConstant = dyn_cast<ConstantInt>(Upper);
  if (UpperConstant == nullptr
      or not isRangeCheckBoundary(UpperConstant->getValue())) {
    Ignored = max(Ignored, getKnownTrailingZeros(Upper));
  }

  if (auto *LowerConstant = dyn_cast<ConstantInt>(Lower)) {
    APInt Boundary = LowerConstant->getValue() + 1;
    if (not isRangeCheckBoundary(Boundary))
      Ignored = max(Ignored, Boundary.countTrailingZeros());
  }

  return Ignored;
}

/// Whether the window \p Window of the operands of \p Compare reads, from
/// some operand, bits it already provides: either a window planned at the same
/// offset, or its top bits, as when comparing a field at the top of a value.
/// Otherwise, shifting the operands down to the window costs more than the
/// narrower comparison saves:
///
///     %a = zext i8 %x to i64
///     %b = zext i8 %y to i64
///     %sum = add i64 %a, %b
///     %c = icmp slt i64 %sum, 300
///
/// %sum is planned on 16 bits, from bit zero. The narrowest window of the
/// operands of %c reads 8 bits from bit 1, which %sum does not provide. So
/// %c compares %sum with 300 on 16 bits, rather than `%sum >> 1` with 150 on
/// 8 bits.
///
/// A window from bit zero, and comparing constants alone, which fold, are
/// always worthwhile.
bool Planner::isComparisonWindowWorthwhile(ICmpInst &Compare,
                                           const BitWindow &Window) const {
  if (Window.Offset == 0)
    return true;

  bool HasVariableOperand = false;
  for (Value *Operand : Compare.operands()) {
    if (isa<Constant>(Operand))
      continue;

    if (isPlannedAsWindow(Operand, Window.Offset))
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

} // namespace TypeShrinking
