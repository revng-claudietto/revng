/// Rebuild integer computations in the narrower representations their plans
/// select.

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <optional>

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/FormattedStream.h"

#include "revng/Support/Debug.h"
#include "revng/Support/IRBuilder.h"
#include "revng/TypeShrinking/RewritePlans.h"

using namespace llvm;

namespace TypeShrinking {

unsigned width(const Value *V) {
  return V->getType()->isIntegerTy() ? V->getType()->getIntegerBitWidth() : 0;
}

std::optional<unsigned> getShiftAmount(const Instruction &I) {
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
  // Planner::getWindowLimit
  case Instruction::Mul: {
    unsigned Multiplier = isa<ConstantInt>(I.getOperand(1)) ? 1 : 0;
    return Index == Multiplier ? Offset : 0;
  }

  default:
    return Offset;
  }
}

void RewritePlans::insert(Instruction &I, const RewritePlan &Plan) {
  revng_assert(Plan);
  bool Inserted = Plans.insert({ &I, Plan }).second;
  revng_assert(Inserted);

  NarrowsAnything |= Plan.getResultWidth() < width(&I);
  if (auto *Compare = Plan.getComparisonRewrite())
    NarrowsAnything |= Compare->OperandWidth < width(I.getOperand(0));
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
