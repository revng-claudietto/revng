#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <map>
#include <optional>
#include <variant>

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace TypeShrinking {

enum class ExtensionKind {
  Zero,
  Sign
};

/// How to compute and represent an integer result. Reconstructing this
/// representation preserves the demanded low bits of the original result;
/// higher bits need not agree. Demand is private backward-analysis state.
///
/// For example, when the full result of this remainder is returned:
///
///     %a = sext i8 %x to i64
///     %b = sext i8 %y to i64
///     %r = srem i64 %a, %b
///     ret i64 %r
///
/// The plan for %r is { 8, 16, ExtensionKind::Sign }: the remainder fits in a
/// signed byte, but computing it in i8 could introduce the INT_MIN / -1 case,
/// so it executes in i16. Rebuilding produces:
///
///     %1 = sext i8 %x to i16
///     %2 = sext i8 %y to i16
///     %3 = srem i16 %1, %2
///     %4 = trunc i16 %3 to i8
///     %5 = sext i8 %4 to i64
///     ret i64 %5
///
/// The sources are sign-extended to the computation width instead of to i64,
/// and only the return pays for the full width.
struct IntegerRewrite {
  /// Width of the retained result: 8 in the remainder example because every
  /// defined remainder fits in a signed byte.
  unsigned ResultWidth = 0;
  /// Width used to execute a binary operation, before truncating its result:
  /// 16 in the example to avoid introducing signed division overflow.
  /// For casts and merges this equals ResultWidth; casts retain their original
  /// conversion semantics when adapting their source operands.
  unsigned ComputationWidth = 0;
  /// How consumers reconstruct demanded bits above ResultWidth: Sign in the
  /// example because zero extension would change negative remainders.
  ExtensionKind ResultExtension = ExtensionKind::Zero;
};

/// How to compare narrowed operands. The result is always i1.
///
/// For example:
///
///     %a = zext i8 %x to i64
///     %b = zext i8 %y to i64
///     %c = icmp slt i64 %a, %b
///
/// The plan for %c is { 8, ICMP_ULT }: the original operands are nonnegative,
/// whereas their i8 representations can have their sign bit set, so the signed
/// predicate has to become unsigned. Rebuilding produces:
///
///     %1 = icmp ult i8 %x, %y
///
/// Nothing is emitted for %a and %b. They are planned at i8 as well, so their
/// replacements are %x and %y themselves and the extensions disappear.
struct ComparisonRewrite {
  /// Common operand width: 8 in the example because both values fit unsigned
  /// bytes. This is independent of the comparison's i1 result width.
  unsigned OperandWidth = 0;
  /// Predicate of the rebuilt comparison: ICMP_ULT in the example to preserve
  /// the ordering of the original zero-extended values.
  llvm::CmpInst::Predicate Predicate = {};
};

class ValueRanges;

/// A plan to rebuild either an integer result or a comparison.
/// Creation checks opcode requirements and preserves the demanded result bits.
class RewritePlan {
private:
  std::variant<IntegerRewrite, ComparisonRewrite> Plan;

private:
  explicit RewritePlan(IntegerRewrite Integer) : Plan(Integer) {}
  explicit RewritePlan(ComparisonRewrite Comparison) : Plan(Comparison) {}

public:
  /// Select a representation preserving the demanded prefix of the original
  /// result. Value bounds can make this representation narrower than the
  /// demand. Opcode requirements can make computation wider than the retained
  /// result.
  ///
  /// For example, with the default minimum width of 8:
  ///
  ///     %wide = zext i16 %x to i64
  ///     %shifted = lshr i64 %wide, 8
  ///     ret i64 %shifted
  ///
  /// The return demands all 64 bits of %shifted, but ranges prove it lies in
  /// [0, 256), so its plan is ResultWidth = 8, ResultExtension = Zero and
  /// ComputationWidth = 16. Computation stays wider than the result because
  /// the retained byte comes from bits 8 to 16 of the input, which an i8
  /// computation would have discarded before shifting. %wide is planned too,
  /// with an i16 result, which its i16 operand already satisfies. Rebuilding
  /// these two plans produces:
  ///
  ///     %1 = lshr i16 %x, 8
  ///     %2 = trunc i16 %1 to i8
  ///     %3 = zext i8 %2 to i64
  ///     ret i64 %3
  ///
  /// ComputationWidth chose the i16 shift, ResultWidth the truncation to i8,
  /// and ResultExtension the zero extension the i64 return demands. Nothing
  /// is emitted for %wide, whose replacement is %x itself.
  /// Return no plan for zero demand or an unsupported instruction.
  static std::optional<RewritePlan>
  create(llvm::Instruction &I, unsigned Demand, const ValueRanges &Ranges);

public:
  /// Return the integer rewrite, or nullptr for a comparison.
  const IntegerRewrite *getIntegerRewrite() const {
    return std::get_if<IntegerRewrite>(&Plan);
  }

  /// Return the comparison rewrite, or nullptr for an integer result.
  const ComparisonRewrite *getComparisonRewrite() const {
    return std::get_if<ComparisonRewrite>(&Plan);
  }

  /// Width of the retained result; comparisons always produce i1.
  unsigned getResultWidth() const {
    if (auto *Integer = getIntegerRewrite())
      return Integer->ResultWidth;
    return 1;
  }
};

/// Only instructions participating in rebuilding have a plan. This includes
/// supported instructions whose width stays unchanged but whose operands may
/// acquire new representations. An absent instruction is not rebuilt.
using RewritePlans = std::map<llvm::Instruction *, RewritePlan>;

/// Rebuild live computations after removing instructions LLVM proves dead.
struct TypeShrinkingAnalysisResults {
  RewritePlans Plans;
  /// Remove the whole set together, including any cycles of dead instructions.
  llvm::SmallVector<llvm::Instruction *, 16> DeadInstructions;
};

class TypeShrinkingAnalysisWrapperPass : public llvm::FunctionPass {
private:
  TypeShrinkingAnalysisResults Result;

public:
  static char ID;

public:
  TypeShrinkingAnalysisWrapperPass() : FunctionPass(ID) {}

public:
  TypeShrinkingAnalysisResults &getResult() { return Result; }

public:
  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  void dump(llvm::Function &F) const;

public:
  bool runOnFunction(llvm::Function &F) override;
};

class TypeShrinkingAnalysisPass
  : public llvm::AnalysisInfoMixin<TypeShrinkingAnalysisPass> {
  friend llvm::AnalysisInfoMixin<TypeShrinkingAnalysisPass>;

public:
  using Result = TypeShrinkingAnalysisResults;

private:
  static llvm::AnalysisKey Key;

public:
  /// Plan rewrites using LLVM demanded bits and SCCP value ranges.
  /// Plan only reachable blocks and leave the IR unchanged.
  Result run(llvm::Function &F, llvm::FunctionAnalysisManager &);
};

} // namespace TypeShrinking
