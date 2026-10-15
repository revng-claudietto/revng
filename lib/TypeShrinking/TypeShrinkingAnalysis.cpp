/// Plan integer rewrites using LLVM value ranges and demanded bits.

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"

#include "revng/TypeShrinking/TypeShrinkingAnalysis.h"

#include "Planner.h"
#include "SCCPValueRanges.h"

using namespace llvm;

namespace TypeShrinking {

AnalysisKey TypeShrinkingAnalysisPass::Key;

TypeShrinkingAnalysisPass::Result
TypeShrinkingAnalysisPass::run(Function &F, FunctionAnalysisManager &) {
  RewritePlans Result(F);

  // Leave functions with an `invoke` alone. A cast adapting an incoming value
  // of a PHI goes before the terminator of the predecessor, where the result
  // of an `invoke` does not exist yet.
  for (BasicBlock &Block : F) {
    if (isa<InvokeInst>(Block.getTerminator()))
      return Result;
  }

  AssumptionCache AC(F);
  DominatorTree DT(F);
  TargetLibraryInfoImpl Impl(Triple(F.getParent()->getTargetTriple()));
  TargetLibraryInfo TLI(Impl);

  // Solve ranges with SCCP, including loop invariants. Use them to refine
  // backward demand before creating rewrite plans.
  SCCPValueRanges Ranges(F, TLI);
  DemandedBits Demands(F, AC, DT, [&Ranges](const Use &U) {
    return Ranges.range(U.get());
  });

  const DataLayout &Layout = F.getParent()->getDataLayout();
  Planner Planner(Ranges, Demands, Layout, AC, DT, Result);

  // Planning finishes before rebuilding invalidates the analyses it reads
  ReversePostOrderTraversal<Function *> Blocks(&F);
  for (BasicBlock *Block : Blocks) {
    for (Instruction &I : *Block) {
      if (RewritePlan Plan = Planner.plan(I))
        Result.insert(I, Plan);
    }
  }
  return Result;
}

} // namespace TypeShrinking
