#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <map>
#include <set>
#include <utility>

#include "llvm/ADT/SmallVector.h"

#include "revng/Support/MetaAddress.h"

namespace llvm {
class BasicBlock;
class Function;
class Instruction;
class Module;
} // namespace llvm

/// Lazily collected information about the generated root function.
class RootFunctionInfo {
private:
  llvm::Function *RootFunction = nullptr;
  llvm::Function *NewPC = nullptr;

  mutable llvm::BasicBlock *Dispatcher = nullptr;
  mutable llvm::BasicBlock *DispatcherFail = nullptr;
  mutable llvm::BasicBlock *AnyPC = nullptr;
  mutable llvm::BasicBlock *UnexpectedPC = nullptr;
  mutable std::map<MetaAddress, llvm::BasicBlock *> JumpTargets;
  mutable bool RootParsed = false;

public:
  explicit RootFunctionInfo(llvm::Module &M);

  /// Return the basic block associated to \p PC.
  ///
  /// Returns nullptr if the PC doesn't have a basic block.
  llvm::BasicBlock *getBlockAt(MetaAddress PC) const;

  bool isJump(llvm::BasicBlock *BB) const;

  /// Return true if \p T represents a jump in the input assembly.
  ///
  /// Return true if \p T targets include only dispatcher-related basic blocks
  /// and jump targets.
  bool isJump(llvm::Instruction *T) const;

  std::set<llvm::BasicBlock *> getBlocksGeneratedByPC(MetaAddress PC) const;

  llvm::BasicBlock *anyPC() const;
  llvm::BasicBlock *unexpectedPC() const;
  llvm::BasicBlock *dispatcher() const;
  llvm::Function *root() const;

  llvm::SmallVector<std::pair<llvm::BasicBlock *, bool>, 4>
  blocksByPCRange(MetaAddress Start, MetaAddress End) const;

private:
  void parseRoot() const;
};
