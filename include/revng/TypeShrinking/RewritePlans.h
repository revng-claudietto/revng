#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <tuple>
#include <variant>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"

#include "revng/Support/Assert.h"
#include "revng/Support/Debug.h"

namespace revng {
class IRBuilder;
} // namespace revng

namespace TypeShrinking {

/// Return the integer width, or zero for a non-integer value.
unsigned width(const llvm::Value *V);

enum class ExtensionKind {
  Zero,
  Sign
};

/// How to compute and represent an integer result.
/// Applying an instance { 8, 16, ExtensionKind::Sign } to %r:
///
/// before:
///
///     %a = sext i8 %x to i64
///     %b = sext i8 %y to i64
///     %r = srem i64 %a, %b
///     ret i64 %r
///
/// after:
///
///     %1 = sext i8 %x to i16     ; operands adapted to ComputationWidth
///     %2 = sext i8 %y to i16
///     %3 = srem i16 %1, %2       ; ComputationWidth
///     %4 = trunc i16 %3 to i8    ; ResultWidth
///     %5 = sext i8 %4 to i64     ; ResultExtension, for the i64 return
///     ret i64 %5
struct IntegerRewrite {
  unsigned ResultWidth = 0;
  unsigned ComputationWidth = 0;
  ExtensionKind ResultExtension = ExtensionKind::Zero;
};

/// How to compare narrowed operands; the result is always i1.
/// Applying an instance { 8, ICMP_ULT } to %c:
///
/// before:
///
///     %a = zext i8 %x to i64
///     %b = zext i8 %y to i64
///     %c = icmp slt i64 %a, %b
///
/// after:
///
///     %1 = icmp ult i8 %x, %y    ; OperandWidth, Predicate
struct ComparisonRewrite {
  unsigned OperandWidth = 0;
  llvm::CmpInst::Predicate Predicate = {};
};

/// A plan to rebuild either an integer result or a comparison, or no plan at
/// all. An empty plan converts to false and leaves its instruction alone.
class RewritePlan {
private:
  std::variant<std::monostate, IntegerRewrite, ComparisonRewrite> Plan;

public:
  /// An empty plan, rebuilding nothing.
  RewritePlan() = default;
  explicit RewritePlan(IntegerRewrite Integer) : Plan(Integer) {}
  explicit RewritePlan(ComparisonRewrite Comparison) : Plan(Comparison) {}

public:
  /// Whether this plan rebuilds anything.
  explicit operator bool() const {
    return not std::holds_alternative<std::monostate>(Plan);
  }

public:
  /// Return the integer rewrite, or nullptr for a comparison or an empty plan.
  const IntegerRewrite *getIntegerRewrite() const {
    return std::get_if<IntegerRewrite>(&Plan);
  }

  /// Return the comparison rewrite, or nullptr for an integer result or an
  /// empty plan.
  const ComparisonRewrite *getComparisonRewrite() const {
    return std::get_if<ComparisonRewrite>(&Plan);
  }

  /// Width of the retained result; comparisons always produce i1.
  unsigned getResultWidth() const {
    revng_assert(*this);
    if (auto *Integer = getIntegerRewrite())
      return Integer->ResultWidth;
    return 1;
  }
};

/// The plans for rebuilding the integer computations of a function.
///
/// Only instructions participating in rebuilding have a plan. This includes
/// supported instructions whose width stays unchanged but whose operands may
/// acquire new representations. An absent instruction is not rebuilt. Dead
/// instructions get no plan and are left for DCE.
class RewritePlans {
private:
  llvm::Function &F;
  llvm::MapVector<llvm::Instruction *, RewritePlan> Plans;

  /// Whether some plan narrows a result, or the operands of a comparison.
  bool NarrowsAnything = false;

  /// The narrow value built for each planned instruction.
  llvm::DenseMap<llvm::Instruction *, llvm::Value *> Replacements;

  /// The values adapted at the end of a predecessor, by predecessor, original
  /// value and width.
  using EdgeValueKey = std::tuple<llvm::BasicBlock *, llvm::Value *, unsigned>;
  llvm::DenseMap<EdgeValueKey, llvm::Value *> EdgeValues;

public:
  explicit RewritePlans(llvm::Function &F) : F(F) {}

public:
  /// Plan \p I. Instructions must be planned in reverse post-order, because
  /// \ref apply rebuilds them in this order, and an instruction other than a
  /// PHI can only be rebuilt after its operands.
  void insert(llvm::Instruction &I, const RewritePlan &Plan);

  /// Return the plan for \p I, or nullptr if \p I is not rebuilt.
  const RewritePlan *find(llvm::Instruction *I) const {
    auto It = Plans.find(I);
    return It == Plans.end() ? nullptr : &It->second;
  }

  /// Replace each planned instruction with the computation its plan selects,
  /// and adapt the instructions using it. Return whether the function changed.
  ///
  /// For example, assuming the plans give %a, %merged and %low an i8 result:
  ///
  /// before:
  ///
  ///     left:
  ///       %a = sext i8 %x to i64
  ///       br label %join
  ///     right:
  ///       br label %join
  ///     join:
  ///       %merged = phi i64 [ %a, %left ], [ %y, %right ]
  ///       %low = trunc i64 %merged to i8
  ///       ret i8 %low
  ///
  /// after:
  ///
  ///     left:
  ///       br label %join
  ///     right:
  ///       %0 = trunc i64 %y to i8
  ///       br label %join
  ///     join:
  ///       %1 = phi i8 [ %x, %left ], [ %0, %right ]
  ///       ret i8 %1
  ///
  /// The planned instructions are erased, so the plans can be applied once.
  bool apply();

  /// Print the function to the debug stream, annotating each planned
  /// instruction with the widths and extension its plan selected.
  void dump() const debug_function;

private:
  llvm::Value *rebuild(llvm::Instruction &I, const RewritePlan &Plan);
  void finalizePhis();
  void replaceUses();
  void eraseOriginals();
  llvm::Value *adapt(revng::IRBuilder &Builder, llvm::Value *V, unsigned Width);
  llvm::Value *adaptOnEdge(llvm::BasicBlock *Predecessor,
                           llvm::Value *V,
                           unsigned Width,
                           const llvm::DebugLoc &Location);
};

} // namespace TypeShrinking
