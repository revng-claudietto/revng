#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <optional>
#include <tuple>
#include <utility>
#include <variant>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"

#include "revng/Support/Assert.h"
#include "revng/Support/Debug.h"

namespace llvm {
class AssumptionCache;
class DominatorTree;
} // namespace llvm

namespace revng {
class IRBuilder;
} // namespace revng

namespace TypeShrinking {

enum class ExtensionKind {
  Zero,
  Sign
};

/// How to compute and represent an integer result: as the window of its bits
/// from Offset to Offset + ResultWidth. Extending the window as
/// ResultExtension says, then shifting it left by Offset, rebuilds the
/// demanded bits of the original value.
///
/// The window is computed from windows of the operands, see getOperandOffset,
/// at ComputationWidth. Only divisions and shifts by a variable amount need it
/// wider than ResultWidth, and they compute from bit zero. Usually Offset is
/// zero and the window holds the low bits. Applying
/// { 0, 8, 16, ExtensionKind::Sign } to %r:
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
///
/// A window above bit zero requires the bits below it to be known zero or not
/// demanded. Applying { 16, 16, 16, ExtensionKind::Zero } to %field:
///
/// before:
///
///     %product = mul i64 %x, 327680         ; 5 << 16
///     %field = and i64 %product, 4294901760 ; 0xffff0000
///     ret i64 %field
///
/// after:
///
///     %1 = trunc i64 %x to i16
///     %2 = mul i16 %1, 5         ; %product, as a window from bit 16 too
///     %3 = and i16 %2, -1        ; ResultWidth
///     %4 = zext i16 %3 to i64    ; ResultExtension, for the i64 return
///     %5 = shl i64 %4, 16        ; Offset
///     ret i64 %5
struct IntegerRewrite {
  unsigned Offset = 0;
  unsigned ResultWidth = 0;
  unsigned ComputationWidth = 0;
  ExtensionKind ResultExtension = ExtensionKind::Zero;

  /// Return where the window of operand \p Index of \p I starts, which
  /// computing the window this plan selects for the result of \p I reads.
  /// Return std::nullopt for an operand that is not read as a window, such as
  /// the condition of a select. With an Offset of 16:
  ///
  ///     and i64 %x, %y        ; %x and %y from bit 16
  ///     lshr i64 %x, 8        ; %x from bit 24
  ///     shl i64 %x, 16        ; %x from bit 0
  ///     mul i64 %x, 327680    ; %x from bit 0, 327680 from bit 16, so 5
  ///
  /// Past the top of their operand, right shifts and extensions read copies
  /// of its sign or zeros, as they do.
  std::optional<unsigned> getOperandOffset(const llvm::Instruction &I,
                                           unsigned Index) const;
};

/// How to compare narrowed operands; the result is always i1. The operands are
/// compared on their bits from Offset to Offset + OperandWidth: the bits below
/// cannot change the result, and those above extend the compared ones.
///
/// Applying { 0, 8, ICMP_ULT } to %c:
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
///
/// Applying { 16, 16, ICMP_ULT } to %c, whose boundary is 201 << 16:
///
/// before:
///
///     %c = icmp ult i32 %x, 13172736
///
/// after:
///
///     %1 = lshr i32 %x, 16       ; Offset
///     %2 = trunc i32 %1 to i16   ; OperandWidth
///     %3 = icmp ult i16 %2, 201
struct ComparisonRewrite {
  unsigned Offset = 0;
  unsigned OperandWidth = 0;
  llvm::CmpInst::Predicate Predicate = {};
};

class SCCPValueRanges;
class RewritePlans;

/// The analyses planning consults about the values an instruction uses.
class PlanningFacts {
private:
  const SCCPValueRanges &Ranges;
  const llvm::DataLayout &Layout;
  llvm::AssumptionCache &Assumptions;
  const llvm::DominatorTree &Dominators;
  const RewritePlans &Plans;

public:
  PlanningFacts(const SCCPValueRanges &Ranges,
                const llvm::DataLayout &Layout,
                llvm::AssumptionCache &Assumptions,
                const llvm::DominatorTree &Dominators,
                const RewritePlans &Plans) :
    Ranges(Ranges),
    Layout(Layout),
    Assumptions(Assumptions),
    Dominators(Dominators),
    Plans(Plans) {}

public:
  const SCCPValueRanges &ranges() const { return Ranges; }

  /// The plans made so far: in reverse post-order, those of the operands.
  const RewritePlans &plans() const { return Plans; }

  /// Return how many low bits of \p V are known to be zero.
  unsigned getKnownTrailingZeros(llvm::Value *V) const;
};

/// A plan to rebuild either an integer result or a comparison, or no plan at
/// all. An empty plan converts to false and leaves its instruction alone.
class RewritePlan {
private:
  std::variant<std::monostate, IntegerRewrite, ComparisonRewrite> Plan;

private:
  explicit RewritePlan(IntegerRewrite Integer) : Plan(Integer) {}
  explicit RewritePlan(ComparisonRewrite Comparison) : Plan(Comparison) {}

public:
  /// An empty plan, rebuilding nothing.
  RewritePlan() = default;

public:
  /// Select a representation preserving the bits \p Demand of I's result.
  /// Return an empty plan when I's opcode is unsupported. A \p Demand of zero
  /// still gets a plan, so that its computation is rebuilt without flags.
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
  /// The ranges can make the result narrower than \p Demand. Assuming they
  /// bound %r to [0, 256), ResultWidth is 8 even though the return demands 64
  /// bits:
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
  static RewritePlan create(llvm::Instruction &I,
                            const llvm::APInt &Demand,
                            const PlanningFacts &Facts);

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
  /// value and window, as offset and width.
  using Window = std::pair<unsigned, unsigned>;
  using EdgeValueKey = std::tuple<llvm::BasicBlock *, llvm::Value *, Window>;
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

  /// Whether \p V is planned as a window starting at \p Offset, above bit
  /// zero.
  bool isPlannedAsWindow(llvm::Value *V, unsigned Offset) const;

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
  llvm::Value *moveWindow(revng::IRBuilder &Builder,
                          llvm::Instruction &I,
                          const IntegerRewrite &Plan);
  void finalizePhis();
  void replaceUses();
  void eraseOriginals();
  llvm::Value *adapt(revng::IRBuilder &Builder,
                     llvm::Value *V,
                     unsigned Offset,
                     unsigned Width);
  llvm::Value *adaptOperand(revng::IRBuilder &Builder,
                            llvm::Instruction &I,
                            const IntegerRewrite &Plan,
                            unsigned Index);
  llvm::Value *adaptOnEdge(llvm::BasicBlock *Predecessor,
                           llvm::Value *V,
                           unsigned Offset,
                           unsigned Width,
                           const llvm::DebugLoc &Location);
};

} // namespace TypeShrinking
