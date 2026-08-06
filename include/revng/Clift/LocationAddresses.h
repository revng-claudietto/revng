#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <optional>

#include "llvm/ADT/SmallPtrSet.h"

#include "revng/ADT/SortedVector.h"
#include "revng/Clift/Clift.h"
#include "revng/Ranks/Location.h"
#include "revng/Ranks/Ranks.h"
#include "revng/Support/MetaAddress.h"

namespace clift {

/// The address \p V reaches into, when it reaches into a segment at an offset
/// known here. A bare reference to a segment is an offset of zero.
///
/// The chain an access is made of is walked backwards, from the outermost
/// expression down to the `clift.use` of the segment, accumulating the offset
/// on the way. Anything else, in particular an index that is not a constant,
/// makes the address unknown.
inline std::optional<MetaAddress> getSegmentAddress(mlir::Value V) {
  mlir::Operation *Op = V.getDefiningOp();
  if (Op == nullptr)
    return std::nullopt;

  if (auto Use = mlir::dyn_cast<UseOp>(Op)) {
    auto Module = Op->getParentOfType<mlir::ModuleOp>();
    if (not Module)
      return std::nullopt;

    auto *Symbol = mlir::SymbolTable::lookupSymbolIn(Module,
                                                     Use.getSymbolNameAttr());
    auto Global = mlir::dyn_cast_or_null<GlobalVariableOp>(Symbol);
    if (not Global)
      return std::nullopt;

    // Only a segment has an address; any other global variable does not.
    auto Location = pipeline::locationFromString(revng::ranks::Segment,
                                                 Global.getHandle());
    if (not Location)
      return std::nullopt;

    return std::get<0>(Location->at(revng::ranks::Segment));
  }

  if (auto Addressof = mlir::dyn_cast<AddressofOp>(Op))
    return getSegmentAddress(Addressof.getObject());

  if (auto Indirection = mlir::dyn_cast<IndirectionOp>(Op))
    return getSegmentAddress(Indirection.getPointer());

  // Both `a.b` and `a->b` reach the field at the same offset from whatever
  // their operand designates, so the two are handled through the interface
  // they share.
  if (auto Access = mlir::dyn_cast<AccessOpInterface>(Op)) {
    std::optional<MetaAddress> Base = getSegmentAddress(Access.getValue());
    if (not Base)
      return std::nullopt;

    return *Base + Access.getFieldAttr().getOffset();
  }

  if (auto Cast = mlir::dyn_cast<CastOpInterface>(Op))
    return getSegmentAddress(Cast.getValue());

  if (auto Add = mlir::dyn_cast<PtrAddOp>(Op)) {
    // `ptr_add` is commutative, so either operand can be the pointer.
    mlir::Value Pointer = Add.getLhs();
    mlir::Value Index = Add.getRhs();
    auto PointerType = unwrapped_dyn_cast<clift::PointerType>(Pointer
                                                                .getType());
    if (not PointerType) {
      std::swap(Pointer, Index);
      PointerType = unwrapped_dyn_cast<clift::PointerType>(Pointer.getType());
    }
    if (not PointerType)
      return std::nullopt;

    auto Immediate = Index.getDefiningOp<ImmediateOp>();
    if (not Immediate)
      return std::nullopt;

    // An index that does not fit a 64-bit address cannot land in a segment.
    const llvm::APInt &Value = Immediate.getValue();
    if (Value.getActiveBits() > 64)
      return std::nullopt;

    std::optional<MetaAddress> Base = getSegmentAddress(Pointer);
    if (not Base)
      return std::nullopt;

    // As in C, the index counts pointees, not bytes.
    auto Pointee = mlir::cast<ObjectType>(PointerType.getPointeeType());
    return *Base + Value.getZExtValue() * Pointee.getObjectSize();
  }

  return std::nullopt;
}

/// The address of the instruction an operation was lifted from, or an invalid
/// address if it carries none.
inline MetaAddress getOperationAddress(mlir::Operation *Op) {
  if (auto Loc = mlir::dyn_cast_or_null<mlir::NameLoc>(Op->getLoc()))
    if (auto L = pipeline::locationFromString(revng::ranks::Instruction,
                                              Loc.getName().str()))
      return L->back();
  return MetaAddress::invalid();
}

/// Gather the set of instruction addresses identifying a value, i.e. a local
/// variable or a label: the addresses attached to the operations that use it.
///
/// This matches the address set rev.ng reports for that value, so a
/// model::LocalVariable or model::GotoLabel located by it is picked up when
/// names and types are assigned. Returns an empty set if any user lacks a valid
/// address.
inline SortedVector<MetaAddress> getUserAddressSet(mlir::Value Value) {
  SortedVector<MetaAddress> Addresses;
  for (mlir::Operation *User : Value.getUsers()) {
    MetaAddress Address = getOperationAddress(User);
    if (not Address.isValid()) {
      Addresses.clear();
      break;
    }
    Addresses.insert(Address);
  }
  return Addresses;
}

/// Whether \p Address identifies \p Jump on its own, i.e. no other jump of the
/// same function was lifted from that same instruction.
///
/// The conditional branch closing a loop lifts to a `break_to` and a
/// `continue_to` sharing its address, and a `goto` can share one with a jump
/// too, so an address is not always enough to tell one jump from another.
inline bool isUnsharedJumpAddress(mlir::Operation *Jump, MetaAddress Address) {
  mlir::Operation *Root = Jump->getParentOfType<clift::FunctionOp>();
  if (Root == nullptr) {
    Root = Jump;
    while (mlir::Operation *Parent = Root->getParentOp())
      Root = Parent;
  }

  bool Unshared = true;
  Root->walk([&](clift::JumpStatementOpInterface Other) {
    if (Other.getOperation() == Jump)
      return mlir::WalkResult::advance();
    if (getOperationAddress(Other) != Address)
      return mlir::WalkResult::advance();
    Unshared = false;
    return mlir::WalkResult::interrupt();
  });
  return Unshared;
}

/// Gather the set of instruction addresses identifying a statement.
///
/// The addresses are those attached to the operations in the statement's own
/// expression regions, i.e. all of its regions except the ones holding nested
/// statements (loop and branch bodies): those addresses identify the nested
/// statements, not this one. So a `return` or an expression statement is
/// identified by the addresses of its expression, while an `if` or a loop is
/// identified by the addresses of its condition alone.
///
/// Moreover, statements that declare or name something are identified by the
/// addresses of the statements using it (see getUserAddressSet).
///
/// Finally a `goto` is identified by the address of the branch instruction it
/// was lifted from, which is the only instruction it stands for. The other
/// jumps are left with no address set on purpose.
///
/// This is the address set used to place comments (see CommentPlacementHelper),
/// so a comment whose location is set to the result of this function matches
/// the statement exactly.
inline SortedVector<MetaAddress>
getStatementExpressionAddresses(mlir::Operation *Op) {
  // First, handle "special" operations
  if (auto Variable = mlir::dyn_cast<clift::LocalVariableOp>(Op)) {
    // Local variable
    return getUserAddressSet(Variable.getResult());

  } else if (auto AssignLabel = mlir::dyn_cast<clift::AssignLabelOp>(Op)) {
    // `goto` label
    return getUserAddressSet(AssignLabel.getLabel());

  } else if (mlir::isa<clift::JumpStatementOpInterface>(Op)) {

    // A jump stands for a single instruction, the branch it was lifted from, so
    // that address identifies it. Two jumps lifted from the same branch, as the
    // `break_to` and `continue_to` closing a loop are, cannot be told apart
    // that way, and a comment on either would as often as not come out on the
    // other: leave those with no address set, which keeps them out of comment
    // placement altogether.
    SortedVector<MetaAddress> Addresses;
    MetaAddress Address = getOperationAddress(Op);
    if (Address.isValid() and isUnsharedJumpAddress(Op, Address))
      Addresses.insert(Address);

    return Addresses;
  }

  // Identify all other operations by the addresses in their operands
  SortedVector<MetaAddress> Addresses;

  auto GatherFromRegion = [&Addresses](mlir::Region &Region) {
    Region.walk([&Addresses](mlir::Operation *Nested) {
      auto Loc = mlir::dyn_cast_or_null<mlir::NameLoc>(Nested->getLoc());
      if (not Loc)
        return;
      if (auto L = pipeline::locationFromString(revng::ranks::Instruction,
                                                Loc.getName().str())) {
        if (L->back().isValid())
          Addresses.insert(L->back());
      }
    });
  };

  llvm::SmallPtrSet<mlir::Region *, 4> StatementRegions;
  if (auto SRI = mlir::dyn_cast<clift::StatementRegionOpInterface>(Op))
    for (mlir::Region &Region : SRI.getStatementRegions())
      StatementRegions.insert(&Region);

  for (mlir::Region &Region : Op->getRegions())
    if (not StatementRegions.contains(&Region))
      GatherFromRegion(Region);

  return Addresses;
}

} // namespace clift
