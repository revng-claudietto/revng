#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <map>
#include <set>
#include <utility>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

#include "revng/Support/MetaAddress.h"

namespace llvm {
class BasicBlock;
class Function;
class GlobalVariable;
class Instruction;
class Module;
class Value;
} // namespace llvm

namespace model {
class Binary;
}

/// Information about the CPU state variables in a generated module.
class CPUStateVariableInfo {
private:
  llvm::GlobalVariable *SP = nullptr;
  llvm::GlobalVariable *RA = nullptr;
  std::vector<llvm::GlobalVariable *> CSVs;
  std::vector<llvm::GlobalVariable *> ABIRegisters;
  std::set<llvm::GlobalVariable *> ABIRegistersSet;

public:
  CPUStateVariableInfo(const model::Binary &Binary, llvm::Module &M);

  llvm::GlobalVariable *spReg() const { return SP; }
  llvm::GlobalVariable *raReg() const { return RA; }

  bool isSPReg(const llvm::GlobalVariable *GV) const;
  bool isSPReg(const llvm::Value *V) const;

  llvm::ArrayRef<llvm::GlobalVariable *> csvs() const { return CSVs; }

  const std::vector<llvm::GlobalVariable *> &abiRegisters() const {
    return ABIRegisters;
  }

  bool isABIRegister(llvm::GlobalVariable *CSV) const {
    return ABIRegistersSet.contains(CSV);
  }
};

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
