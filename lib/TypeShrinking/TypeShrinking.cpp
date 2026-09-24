/// Rebuild integer computations from value-width and backward-demand plans.

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <algorithm>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/Local.h"

#include "revng/Support/IRBuilder.h"
#include "revng/TypeShrinking/TypeShrinking.h"
#include "revng/TypeShrinking/TypeShrinkingAnalysis.h"

using namespace llvm;
using std::min;

char TypeShrinking::TypeShrinkingWrapperPass::ID = 0;
using Register = RegisterPass<TypeShrinking::TypeShrinkingWrapperPass>;
static Register
  X("type-shrinking", "Shrink integer computations", false, false);

namespace TypeShrinking {

void TypeShrinkingWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TypeShrinkingAnalysisWrapperPass>();
}

/// Materialize completed rewrite plans.
/// A replacement represents the low ResultWidth bits of the original
/// instruction. ResultExtension tells consumers how to recover any demanded
/// higher bits.
///
/// For example, a merge of sign-extended bytes can stay narrow until its
/// return:
///
///     %a = sext i8 %x to i64
///     %b = sext i8 %y to i64
///     %selected = select i1 %condition, i64 %a, i64 %b
///     ret i64 %selected
///
/// Given a plan with an i8 result and sign extension, the replacement graph is:
///
///     %selected = select i1 %condition, i8 %x, i8 %y
///     %wide = sext i8 %selected to i64
///     ret i64 %wide
///
/// Arithmetic uses the same representation and operand adaptation. PHIs only
/// need different placement: their incoming casts belong on predecessor edges.
class Rebuilder {
private:
  const RewritePlans &Plans;
  const SmallPtrSetImpl<BasicBlock *> &Reachable;
  DenseMap<Instruction *, Value *> Replacements;
  revng::IRBuilder B;

public:
  Rebuilder(Function &F,
            const RewritePlans &Plans,
            const SmallPtrSetImpl<BasicBlock *> &Reachable) :
    Plans(Plans), Reachable(Reachable), B(F.getContext()) {}

public:
  void run(Function &F) {
    SmallVector<Instruction *, 32> Originals;
    DenseMap<Instruction *, unsigned> Pending;
    SmallVector<Instruction *, 32> Ready;

    // Break all cyclic dependencies before constructing any ordinary operation.
    for (Instruction &I : instructions(F)) {
      auto It = Plans.find(&I);
      if (It == Plans.end())
        continue;
      Originals.push_back(&I);

      if (auto *Phi = dyn_cast<PHINode>(&I)) {
        B.SetInsertPoint(Phi, Phi->getDebugLoc());
        auto *Type = B.getIntNTy(It->second.getResultWidth());
        Replacements[Phi] = B.CreatePHI(Type, Phi->getNumIncomingValues());
      }
    }

    for (Instruction *I : Originals) {
      if (isa<PHINode>(I))
        continue;
      unsigned Count = 0;
      for (Value *V : I->operands()) {
        if (auto *Operand = dyn_cast<Instruction>(V)) {
          if (Plans.contains(Operand) and not Replacements.count(Operand))
            ++Count;
        }
      }
      Pending[I] = Count;

      if (Count == 0)
        Ready.push_back(I);
    }

    while (not Ready.empty()) {
      Instruction *I = Ready.pop_back_val();
      Replacements[I] = rebuild(*I);

      // Count uses, not distinct users: an operand can occur more than once.
      for (Use &U : I->uses()) {
        auto *User = dyn_cast<Instruction>(U.getUser());
        auto It = Pending.find(User);
        if (It != Pending.end() and --It->second == 0)
          Ready.push_back(User);
      }
    }
    revng_assert(Replacements.size() == Originals.size());

    for (Instruction *I : Originals) {
      auto *Phi = dyn_cast<PHINode>(I);
      if (Phi == nullptr)
        continue;
      auto *NewPhi = cast<PHINode>(Replacements.lookup(Phi));
      unsigned Width = Plans.at(Phi).getResultWidth();
      for (unsigned Index = 0; Index < Phi->getNumIncomingValues(); ++Index) {
        BasicBlock *Predecessor = Phi->getIncomingBlock(Index);
        // Dead edges need a correctly typed operand, but no computation.
        if (not Reachable.contains(Predecessor)) {
          NewPhi->addIncoming(PoisonValue::get(NewPhi->getType()), Predecessor);
          continue;
        }

        // LLVM requires identical incoming values for duplicate CFG edges.
        if (int First = NewPhi->getBasicBlockIndex(Predecessor); First >= 0) {
          NewPhi->addIncoming(NewPhi->getIncomingValue(First), Predecessor);
          continue;
        }
        B.SetInsertPoint(Predecessor->getTerminator(), Phi->getDebugLoc());
        NewPhi->addIncoming(adapt(Phi->getIncomingValue(Index), Width),
                            Predecessor);
      }
    }

    for (Instruction *I : Originals) {
      DenseMap<BasicBlock *, Value *> EdgeCasts;
      for (Use &U : llvm::make_early_inc_range(I->uses())) {
        auto *User = cast<Instruction>(U.getUser());
        if (Plans.contains(User))
          continue;

        // A deleted definition can still have uses in unreachable code.
        if (not Reachable.contains(User->getParent())) {
          U.set(PoisonValue::get(I->getType()));
          continue;
        }

        unsigned Width = I->getType()->getIntegerBitWidth();
        Value *Replacement = nullptr;
        if (auto *Phi = dyn_cast<PHINode>(User)) {
          BasicBlock *Predecessor = Phi->getIncomingBlock(U);
          if (not Reachable.contains(Predecessor)) {
            U.set(PoisonValue::get(I->getType()));
            continue;
          }

          auto [It, Inserted] = EdgeCasts.try_emplace(Predecessor);
          if (Inserted) {
            B.SetInsertPoint(Predecessor->getTerminator(), I->getDebugLoc());
            It->second = adapt(I, Width);
          }
          Replacement = It->second;
        } else {
          B.SetInsertPoint(User, I->getDebugLoc());
          Replacement = adapt(I, Width);
        }
        U.set(Replacement);
        // A retained instruction's flags can observe operand bits beyond its
        // ordinary result demand. Do not introduce poison by changing them.
        User->dropPoisonGeneratingFlags();
      }
    }

    for (Instruction *I : Originals)
      I->dropAllReferences();

    for (Instruction *I : Originals) {
      revng_assert(I->use_empty());
      I->eraseFromParent();
    }
  }

private:
  Value *adapt(Value *V, unsigned Width) {
    bool Signed = false;
    if (auto *I = dyn_cast<Instruction>(V)) {
      if (auto It = Replacements.find(I); It != Replacements.end()) {
        V = It->second;

        if (auto *P = Plans.at(I).getIntegerRewrite())
          Signed = P->ResultExtension == ExtensionKind::Sign;
      }
    }
    return B.CreateIntCast(V, B.getIntNTy(Width), Signed);
  }

  Value *rebuild(Instruction &I) {
    const auto &Plan = Plans.at(&I);
    B.SetInsertPoint(&I, I.getDebugLoc());

    if (auto *Compare = Plan.getComparisonRewrite()) {
      Value *LHS = adapt(I.getOperand(0), Compare->OperandWidth);
      Value *RHS = adapt(I.getOperand(1), Compare->OperandWidth);
      return B.CreateICmp(Compare->Predicate, LHS, RHS);
    }
    const auto &P = *Plan.getIntegerRewrite();
    if (auto *Select = dyn_cast<SelectInst>(&I)) {
      Value *Condition = adapt(Select->getCondition(), 1);
      Value *True = adapt(Select->getTrueValue(), P.ResultWidth);
      Value *False = adapt(Select->getFalseValue(), P.ResultWidth);
      return B.CreateSelect(Condition, True, False);
    }

    if (auto *Cast = dyn_cast<CastInst>(&I)) {
      unsigned SourceWidth = Cast->getSrcTy()->getIntegerBitWidth();
      unsigned Width = min(SourceWidth, P.ResultWidth);
      Value *Operand = adapt(Cast->getOperand(0), Width);
      auto *Type = B.getIntNTy(P.ResultWidth);
      return B.CreateIntCast(Operand, Type, isa<SExtInst>(Cast));
    }
    Value *LHS = adapt(I.getOperand(0), P.ComputationWidth);
    Value *RHS = adapt(I.getOperand(1), P.ComputationWidth);
    // Fresh operations do not inherit nowrap/exact flags, which narrowing
    // can invalidate even when the original operation had those flags.
    auto Opcode = Instruction::BinaryOps(I.getOpcode());
    Value *Result = B.CreateBinOp(Opcode, LHS, RHS);
    return B.CreateTrunc(Result, B.getIntNTy(P.ResultWidth));
  }
};

static bool runTypeShrinking(Function &F,
                             const TypeShrinkingAnalysisResults &Analysis) {
  // Drop dead computations before changing their operands. Break references
  // first so dead cycles can be erased together, as in LLVM's BDCE.
  for (Instruction *I : llvm::reverse(Analysis.DeadInstructions)) {
    salvageDebugInfo(*I);
    I->dropAllReferences();
  }

  for (Instruction *I : Analysis.DeadInstructions)
    I->eraseFromParent();
  bool Changed = not Analysis.DeadInstructions.empty();
  const auto &Plans = Analysis.Plans;

  SmallPtrSet<BasicBlock *, 16> Reachable;
  for (BasicBlock *Block : depth_first(&F))
    Reachable.insert(Block);

  // Edge casts need an executable predecessor and an available input value.
  // Keep the original graph if a plan would require splitting an exceptional
  // edge or moving a terminator-defined value across it.
  for (BasicBlock &Block : F) {
    if (not Reachable.contains(&Block))
      continue;

    for (PHINode &Phi : Block.phis()) {
      if (not Phi.getType()->isIntegerTy())
        continue;
      auto It = Plans.find(&Phi);
      unsigned Target = Phi.getType()->getIntegerBitWidth();
      if (It != Plans.end())
        Target = It->second.getResultWidth();

      for (unsigned Index = 0; Index < Phi.getNumIncomingValues(); ++Index) {
        BasicBlock *Predecessor = Phi.getIncomingBlock(Index);
        if (not Reachable.contains(Predecessor))
          continue;

        Value *Incoming = Phi.getIncomingValue(Index);
        unsigned Source = Incoming->getType()->getIntegerBitWidth();
        if (auto *I = dyn_cast<Instruction>(Incoming)) {
          if (auto Input = Plans.find(I); Input != Plans.end())
            Source = Input->second.getResultWidth();
        }
        auto InsertionPoint = Predecessor->getFirstInsertionPt();
        bool NoInsertionPoint = InsertionPoint == Predecessor->end();
        bool TerminatorValue = Incoming == Predecessor->getTerminator();
        if (Source != Target and (TerminatorValue or NoInsertionPoint))
          return Changed;
      }
    }
  }

  bool Rebuild = false;
  for (const auto &[I, P] : Plans) {
    if (P.getResultWidth() < I->getType()->getIntegerBitWidth())
      Rebuild = true;

    if (auto *Compare = P.getComparisonRewrite()) {
      unsigned OperandWidth = I->getOperand(0)->getType()->getIntegerBitWidth();
      Rebuild |= Compare->OperandWidth < OperandWidth;
    }
  }

  if (Rebuild)
    Rebuilder(F, Plans, Reachable).run(F);
  return Changed or Rebuild;
}

bool TypeShrinkingWrapperPass::runOnFunction(Function &F) {
  auto &Analysis = getAnalysis<TypeShrinkingAnalysisWrapperPass>();
  return runTypeShrinking(F, Analysis.getResult());
}

PreservedAnalyses TypeShrinkingPass::run(Function &F,
                                         FunctionAnalysisManager &FAM) {
  const auto &Plans = FAM.getResult<TypeShrinkingAnalysisPass>(F);
  bool Changed = runTypeShrinking(F, Plans);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace TypeShrinking
