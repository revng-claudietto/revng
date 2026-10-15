#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/IR/Instructions.h"

#include "revng/TypeShrinking/RewritePlans.h"

namespace llvm {
class DemandedBits;
} // namespace llvm

namespace TypeShrinking {

class SCCPValueRanges;

/// Select the plan of each instruction of a function, from the bits its users
/// demand and the ranges of the values it uses.
class Planner {
private:
  const SCCPValueRanges &Ranges;
  llvm::DemandedBits &Demands;

public:
  Planner(const SCCPValueRanges &Ranges, llvm::DemandedBits &Demands) :
    Ranges(Ranges), Demands(Demands) {}

public:
  /// Select a representation of the result of \p I preserving its low bits,
  /// up to the highest demanded one. Return an empty plan when \p I is dead,
  /// or its opcode is unsupported. A result with no demanded bits still gets
  /// a plan, so that its computation is rebuilt without flags.
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
  /// to [0, 256), its plan is ResultWidth = 8, ResultExtension = Zero and
  /// ComputationWidth = 16. %wide is planned too, with an i16 result, which
  /// its i16 operand already satisfies, so nothing is emitted for it.
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
  /// Opcode requirements can make ComputationWidth wider than ResultWidth.
  /// Assuming the ranges again bound %r to [0, 256), ResultWidth is 8, but
  /// reaching bit 56 keeps the shift itself at i64:
  ///
  /// before:
  ///
  ///     %r = lshr i64 %x, 56
  ///     ret i64 %r
  ///
  /// after:
  ///
  ///     %1 = lshr i64 %x, 56
  ///     %2 = trunc i64 %1 to i8
  ///     %3 = zext i8 %2 to i64
  ///     ret i64 %3
  RewritePlan plan(llvm::Instruction &I);

private:
  unsigned getDemand(llvm::Instruction &I);

  RewritePlan planInteger(llvm::Instruction &I);
  unsigned getComputationWidth(llvm::Instruction &I,
                               unsigned Demand,
                               unsigned ResultWidth) const;

  RewritePlan planComparison(llvm::ICmpInst &Compare) const;
};

} // namespace TypeShrinking
