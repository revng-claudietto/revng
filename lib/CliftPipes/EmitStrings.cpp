//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/ADT/SmallVector.h"

#include "revng/Clift/LocationAddresses.h"
#include "revng/CliftPipes/EmitStrings.h"
#include "revng/Model/Binary.h"
#include "revng/SegmentReferences/StringConstants.h"
#include "revng/Support/Debug.h"

using namespace clift;

static Logger Log("emit-strings");

namespace {

/// The size of a character of the string \p Type spells, if it spells one.
///
/// This is the Clift side of the shape `detect-c-strings` gives a string in
/// the model, see getConstCharArrayElementSize. A typedef naming that shape is
/// deliberately not looked through: the string takes the place of the access
/// with the very same type, and only an array type makes a `clift.str`.
///
/// \return 1 for `uint8_t const[]`, 2 for `uint16_t const[]`, 0 for anything
///         else.
unsigned getCharacterSize(mlir::Type Type) {
  auto Array = mlir::dyn_cast<ArrayType>(Type);
  if (not Array or not isConst(Array))
    return 0;

  auto Character = mlir::dyn_cast<IntegerType>(Array.getElementType());
  if (not Character or Character.getKind() != IntegerKind::Unsigned)
    return 0;

  if (Character.getSize() != 1 and Character.getSize() != 2)
    return 0;

  return Character.getSize();
}

/// Erase \p Access along with what fed it and nothing else: the string stands
/// for the whole chain, down to the `clift.use` naming the segment.
void eraseAccessChain(mlir::Operation *Access) {
  mlir::Operation *Dead = Access;

  while (Dead != nullptr and Dead->use_empty()) {
    mlir::Operation *Next = nullptr;
    if (Dead->getNumOperands() == 1)
      Next = Dead->getOperand(0).getDefiningOp();

    Dead->erase();
    Dead = Next;
  }
}

} // namespace

namespace revng::pypeline::piperuns {

void EmitStrings::runOnCliftFunction(const model::Function &Function,
                                     clift::FunctionOp MLIRFunction) {
  // The accesses are collected up front, since replacing one erases the
  // operations it was built out of. Both `a.b` and `a->b` can name a string,
  // so the walk goes through the interface the two share.
  llvm::SmallVector<AccessOpInterface> Accesses;
  MLIRFunction.walk([&Accesses](AccessOpInterface Access) {
    if (getCharacterSize(Access.getOperation()->getResult(0).getType()) != 0)
      Accesses.push_back(Access);
  });

  mlir::OpBuilder Builder(MLIRFunction.getContext());

  for (AccessOpInterface Access : Accesses) {
    mlir::Operation *Operation = Access.getOperation();
    mlir::Value Result = Operation->getResult(0);
    mlir::Type Type = Result.getType();
    unsigned CharacterSize = getCharacterSize(Type);

    // A wide string wants a prefix and escapes of its own, which the C backend
    // does not emit. Leaving the access alone spells the characters out one by
    // one, which is at least not a narrow string that lies about them.
    if (CharacterSize != 1) {
      revng_log(Log, "Ignoring a string of " << CharacterSize << "-byte chars");
      continue;
    }

    std::optional<MetaAddress> Address = getSegmentAddress(Result);
    if (not Address.has_value()) {
      revng_log(Log, "Ignoring an access landing at no known address");
      continue;
    }

    uint64_t ByteCount = mlir::cast<ArrayType>(Type).getElementsCount()
                         * CharacterSize;

    UnicodeCStringView String = readString(BinaryView,
                                           *Address,
                                           ByteCount,
                                           CharacterSize);
    if (not String.isValid()) {
      revng_log(Log, "No string at " << Address->toString() << " after all");
      continue;
    }

    revng_log(Log, "Emitting the string at " << Address->toString());

    // `readString` spans the terminator, which a `clift.str` leaves implicit.
    llvm::StringRef Characters = String.data().drop_back(CharacterSize);

    Builder.setInsertionPoint(Operation);
    auto Literal = Builder.create<StringOp>(Operation->getLoc(),
                                            Type,
                                            Characters);

    Result.replaceAllUsesWith(Literal.getResult());
    eraseAccessChain(Operation);
  }
}

} // namespace revng::pypeline::piperuns
