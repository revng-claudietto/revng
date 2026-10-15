#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/ADT/APInt.h"
#include "llvm/IR/Instructions.h"

#include "revng/TypeShrinking/RewritePlans.h"

namespace llvm {
class AssumptionCache;
class DataLayout;
class DemandedBits;
class DominatorTree;
} // namespace llvm

namespace TypeShrinking {

class SCCPValueRanges;

/// The bits of a value from Offset to Offset + Width. Extending them as
/// Extension says, then shifting them left by Offset, rebuilds the value.
struct BitWindow {
  unsigned Offset = 0;
  unsigned Width = 0;
  ExtensionKind Extension = ExtensionKind::Zero;
};

/// Select the plan of each instruction of a function, from the bits its users
/// demand, what is known about the values it uses, and the plans of its
/// operands.
class Planner {
private:
  const SCCPValueRanges &Ranges;
  llvm::DemandedBits &Demands;
  const llvm::DataLayout &Layout;
  llvm::AssumptionCache &Assumptions;
  const llvm::DominatorTree &Dominators;

  /// The plans made so far: in reverse post-order, those of the operands.
  const RewritePlans &Plans;

public:
  Planner(const SCCPValueRanges &Ranges,
          llvm::DemandedBits &Demands,
          const llvm::DataLayout &Layout,
          llvm::AssumptionCache &Assumptions,
          const llvm::DominatorTree &Dominators,
          const RewritePlans &Plans) :
    Ranges(Ranges),
    Demands(Demands),
    Layout(Layout),
    Assumptions(Assumptions),
    Dominators(Dominators),
    Plans(Plans) {}

public:
  /// Select a representation of the result of \p I preserving its demanded
  /// bits. Return an empty plan when \p I is dead, or its opcode is
  /// unsupported. A result with no demanded bits still gets a plan, so that
  /// its computation is rebuilt without flags.
  ///
  /// For example, with the default minimum width of 8:
  ///
  /// before:
  ///
  ///     %wide = zext i16 %x to i64
  ///     %shifted = lshr i64 %wide, 8
  ///     ret i64 %shifted
  ///
  /// after:
  ///
  ///     %1 = lshr i16 %x, 8
  ///     %2 = trunc i16 %1 to i8
  ///     %3 = zext i8 %2 to i64
  ///     ret i64 %3
  ///
  /// The return demands all 64 bits of %shifted. Assuming the ranges bound it
  /// to [0, 256), its plan is ResultWidth = 8 and ResultExtension = Zero, and
  /// it reads %wide from bit 8. %wide is planned too, with an i16 result,
  /// which its i16 operand already satisfies, so nothing is emitted for it.
  ///
  /// The ranges can make the result narrower than its demanded bits. Assuming
  /// they bound %r to [0, 256), ResultWidth is 8 even though the return
  /// demands 64 bits:
  ///
  /// before:
  ///
  ///     %r = and i64 %x, 255
  ///     ret i64 %r
  ///
  /// after:
  ///
  ///     %1 = trunc i64 %x to i8
  ///     %2 = and i8 %1, -1
  ///     %3 = zext i8 %2 to i64
  ///     ret i64 %3
  ///
  /// A division needs its operands whole, which can make ComputationWidth
  /// wider than ResultWidth. The truncation demands 8 bits of %r, but %a and
  /// %b need 16:
  ///
  /// before:
  ///
  ///     %a = zext i16 %x to i64
  ///     %b = zext i16 %y to i64
  ///     %r = udiv i64 %a, %b
  ///     %low = trunc i64 %r to i8
  ///
  /// after:
  ///
  ///     %1 = udiv i16 %x, %y
  ///     %2 = trunc i16 %1 to i8
  ///
  /// The window starts above bit zero only if some operand already provides
  /// the bits it reads, as %product does in the example of IntegerRewrite.
  RewritePlan plan(llvm::Instruction &I);

private:
  llvm::APInt getDemand(llvm::Instruction &I);
  unsigned getKnownTrailingZeros(llvm::Value *V) const;
  bool isPlannedAsWindow(llvm::Value *V, unsigned Offset) const;

  RewritePlan planInteger(llvm::Instruction &I);
  IntegerRewrite planIntegerWindow(llvm::Instruction &I,
                                   const llvm::APInt &Demand,
                                   unsigned Limit) const;
  unsigned getWindowLimit(llvm::Instruction &I) const;
  bool isIntegerWindowWorthwhile(const llvm::Instruction &I,
                                 const IntegerRewrite &Rewrite) const;
  unsigned getComputationWidth(llvm::Instruction &I,
                               const llvm::APInt &Demand,
                               unsigned ResultWidth) const;

  RewritePlan planComparison(llvm::ICmpInst &Compare) const;
  BitWindow planComparisonWindow(llvm::ICmpInst &Compare, unsigned Limit) const;
  unsigned getIgnoredLowBits(llvm::ICmpInst &Compare) const;
  bool isComparisonWindowWorthwhile(llvm::ICmpInst &Compare,
                                    const BitWindow &Window) const;
};

} // namespace TypeShrinking
