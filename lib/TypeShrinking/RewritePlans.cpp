/// Rebuild integer computations in the narrower representations their plans
/// select.

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <algorithm>

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/FormattedStream.h"

#include "revng/Support/Debug.h"
#include "revng/Support/IRBuilder.h"
#include "revng/TypeShrinking/RewritePlans.h"

using namespace llvm;
using std::min;

namespace TypeShrinking {

unsigned width(const Value *V) {
  return V->getType()->isIntegerTy() ? V->getType()->getIntegerBitWidth() : 0;
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
  unsigned ResultWidth = Plan.getResultWidth();
  auto *ResultType = Builder.getIntNTy(ResultWidth);

  Value *Result = nullptr;
  if (auto *Compare = Plan.getComparisonRewrite()) {
    Value *LHS = adapt(Builder, I.getOperand(0), Compare->OperandWidth);
    Value *RHS = adapt(Builder, I.getOperand(1), Compare->OperandWidth);
    Result = Builder.CreateICmp(Compare->Predicate, LHS, RHS);
  } else if (auto *Phi = dyn_cast<PHINode>(&I)) {
    Result = Builder.CreatePHI(ResultType, Phi->getNumIncomingValues());
  } else if (auto *Select = dyn_cast<SelectInst>(&I)) {
    Value *Condition = adapt(Builder, Select->getCondition(), 1);
    Value *True = adapt(Builder, Select->getTrueValue(), ResultWidth);
    Value *False = adapt(Builder, Select->getFalseValue(), ResultWidth);
    Result = Builder.CreateSelect(Condition, True, False);
  } else if (auto *Cast = dyn_cast<CastInst>(&I)) {
    unsigned SourceWidth = Cast->getSrcTy()->getIntegerBitWidth();
    unsigned Width = min(SourceWidth, ResultWidth);
    Value *Operand = adapt(Builder, Cast->getOperand(0), Width);
    Result = Builder.CreateIntCast(Operand, ResultType, isa<SExtInst>(Cast));
  } else {
    unsigned Width = Plan.getIntegerRewrite()->ComputationWidth;
    Value *LHS = adapt(Builder, I.getOperand(0), Width);
    Value *RHS = adapt(Builder, I.getOperand(1), Width);
    // Fresh operations do not inherit nowrap/exact flags, which narrowing
    // can invalidate even when the original operation had those flags.
    auto Opcode = Instruction::BinaryOps(I.getOpcode());
    Value *Operation = Builder.CreateBinOp(Opcode, LHS, RHS);
    Result = Builder.CreateTrunc(Operation, ResultType);
  }

  return Result;
}

void RewritePlans::finalizePhis() {
  for (const auto &[Original, Plan] : Plans) {
    auto *Phi = dyn_cast<PHINode>(Original);
    if (Phi == nullptr)
      continue;

    auto *NewPhi = cast<PHINode>(Replacements.lookup(Phi));
    unsigned Width = Plan.getResultWidth();
    for (unsigned Index = 0; Index < Phi->getNumIncomingValues(); ++Index) {
      BasicBlock *Predecessor = Phi->getIncomingBlock(Index);
      Value *Incoming = Phi->getIncomingValue(Index);
      const DebugLoc &Location = Phi->getDebugLoc();
      Value *Adapted = adaptOnEdge(Predecessor, Incoming, Width, Location);
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
        Operand.set(adaptOnEdge(Predecessor, Original, Width, Location));
      } else {
        revng::IRBuilder Builder(User, Location);
        Operand.set(adapt(Builder, Original, Width));
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

/// Convert \p V to \p Width at the insertion point of \p Builder. A planned
/// instruction is represented by its replacement, extended as its plan selects.
Value *
RewritePlans::adapt(revng::IRBuilder &Builder, Value *V, unsigned Width) {
  bool Signed = false;
  if (auto *I = dyn_cast<Instruction>(V)) {
    if (const RewritePlan *Plan = find(I)) {
      // Planning in reverse post-order rebuilds every operand before the
      // instruction using it, and PHIs are only completed at the end.
      V = Replacements.lookup(I);
      revng_assert(V != nullptr);

      if (auto *Integer = Plan->getIntegerRewrite())
        Signed = Integer->ResultExtension == ExtensionKind::Sign;
    }
  }
  return Builder.CreateIntCast(V, Builder.getIntNTy(Width), Signed);
}

/// Adapt \p V at the end of \p Predecessor, where its edge to a PHI is taken.
/// The result is shared, because LLVM requires a PHI to receive the same value
/// on every edge from the same predecessor.
Value *RewritePlans::adaptOnEdge(BasicBlock *Predecessor,
                                 Value *V,
                                 unsigned Width,
                                 const DebugLoc &Location) {
  auto [It, Inserted] = EdgeValues.try_emplace({ Predecessor, V, Width });
  if (Inserted) {
    revng::IRBuilder Builder(Predecessor->getTerminator(), Location);
    It->second = adapt(Builder, V, Width);
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
      Stream << "  ; Result width: " << Integer->ResultWidth << "\n";
      Stream << "  ; Computation width: " << Integer->ComputationWidth << "\n";
      Stream << "  ; Result extension: " << Extension << "\n";
    } else {
      const auto &Compare = *Plan->getComparisonRewrite();
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
