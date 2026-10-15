/// Select narrower representations of integer computations.

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <algorithm>

#include "llvm/Analysis/DemandedBits.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"

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

RewritePlan Planner::plan(Instruction &I) {
  if (not I.getType()->isIntegerTy() or Demands.isInstructionDead(&I))
    return RewritePlan();

  if (auto *Compare = dyn_cast<ICmpInst>(&I))
    return planComparison(*Compare);

  return planInteger(I);
}

/// Return how many low bits of the result of \p I its users demand, up to
/// the highest demanded one.
unsigned Planner::getDemand(Instruction &I) {
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
      return width(&I);
  }

  return Demands.getDemandedBits(&I).getActiveBits();
}

/// Plan \p I, which has an integer result, on the narrowest low bits of its
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

  // Keep the cheaper zero/sign representation of the demanded result bits.
  unsigned W = width(&I);
  unsigned Demand = getDemand(I);
  ConstantRange Bounds = Ranges.range(&I);
  unsigned U = roundWidth(min(Demand, Bounds.getActiveBits()), W);
  unsigned S = roundWidth(min(Demand, Bounds.getMinSignedBits()), W);
  unsigned ResultWidth = min(U, S);
  bool SignExtend = S < U;
  IntegerRewrite Rewrite{ ResultWidth,
                          getComputationWidth(I, Demand, ResultWidth),
                          SignExtend ? ExtensionKind::Sign :
                                       ExtensionKind::Zero };
  return RewritePlan(Rewrite);
}

/// Return the width computing a result of \p ResultWidth bits takes, out of
/// the low \p Demand ones. Only divisions and shifts need more: they compute
/// on low bits of their operands, and can need more of them than the result
/// has.
unsigned Planner::getComputationWidth(Instruction &I,
                                      unsigned Demand,
                                      unsigned ResultWidth) const {
  unsigned W = width(&I);
  if (I.isShift()) {
    // Shift execution must retain the source bits and exceed every amount.
    unsigned Required = min(Demand, ResultWidth);
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

/// Plan \p Compare on the narrowest low bits of its operands. Return an empty
/// plan for non-integer operands.
RewritePlan Planner::planComparison(ICmpInst &Compare) const {
  // Comparisons preserve the operands' values, rather than their low bits.
  unsigned OperandWidth = width(Compare.getOperand(0));
  if (OperandWidth == 0)
    return RewritePlan();
  ConstantRange A = Ranges.range(Compare.getOperand(0));
  ConstantRange B = Ranges.range(Compare.getOperand(1));
  unsigned U = max(A.getActiveBits(), B.getActiveBits());
  unsigned S = max(A.getMinSignedBits(), B.getMinSignedBits());
  U = roundWidth(U, OperandWidth);
  S = roundWidth(S, OperandWidth);

  // A narrower unsigned representation makes both operands nonnegative
  // at their original width, so unsigned predicates preserve their order.
  auto Predicate = Compare.getPredicate();
  if (U <= S and U < OperandWidth)
    Predicate = Compare.getUnsignedPredicate();

  return RewritePlan(ComparisonRewrite{ min(U, S), Predicate });
}

} // namespace TypeShrinking
