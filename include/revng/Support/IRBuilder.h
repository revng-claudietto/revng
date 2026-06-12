#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"

#include "revng/Support/Debug.h"

namespace revng {

namespace detail {

// NOLINTNEXTLINE
using LLVMBuilderBase = llvm::IRBuilder<>;

} // namespace detail

/// This is a wrapper over llvm's IR builder that force-sets a debug location
/// even when its insertion point is a basic block.
class IRBuilder : public detail::LLVMBuilderBase {
public:
  //
  // These explicit `llvm::DebugLoc` overloads are revng-specific,
  // prefer them whenever applicable.
  //
  void SetInsertPoint(llvm::BasicBlock *BB, const llvm::DebugLoc &DL) {
    detail::LLVMBuilderBase::SetInsertPoint(BB);
    if (DL)
      detail::LLVMBuilderBase::SetCurrentDebugLocation(DL);
  }

  void SetInsertPoint(llvm::Instruction *I, const llvm::DebugLoc &DL) {
    detail::LLVMBuilderBase::SetInsertPoint(I);
    if (DL)
      detail::LLVMBuilderBase::SetCurrentDebugLocation(DL);
  }

  void SetInsertPoint(llvm::BasicBlock *BB,
                      llvm::BasicBlock::iterator I,
                      const llvm::DebugLoc &DL) {
    detail::LLVMBuilderBase::SetInsertPoint(BB, I);
    if (DL)
      detail::LLVMBuilderBase::SetCurrentDebugLocation(DL);
  }

  void SetInsertPointPastAllocas(llvm::Function *F, const llvm::DebugLoc &DL) {
    detail::LLVMBuilderBase::SetInsertPointPastAllocas(F);
    if (DL)
      detail::LLVMBuilderBase::SetCurrentDebugLocation(DL);
  }

public:
  //
  // These mirror the corresponding interfaces of llvm's IR builder,
  //
  void SetInsertPoint(llvm::BasicBlock *BB) {
    detail::LLVMBuilderBase::SetInsertPoint(BB);
    if (BB->getTerminator()) {
      auto DL = BB->getTerminator()->getDebugLoc();
      detail::LLVMBuilderBase::SetCurrentDebugLocation(DL);
    }
  }
  void SetInsertPoint(llvm::Instruction *I) {
    detail::LLVMBuilderBase::SetInsertPoint(I);
  }
  void SetInsertPoint(llvm::BasicBlock *BB, llvm::BasicBlock::iterator I) {
    detail::LLVMBuilderBase::SetInsertPoint(BB, I);
  }
  void SetInsertPointPastAllocas(llvm::Function *F) {
    detail::LLVMBuilderBase::SetInsertPointPastAllocas(F);
    auto DL = detail::LLVMBuilderBase::GetInsertPoint()->getDebugLoc();
    detail::LLVMBuilderBase::SetCurrentDebugLocation(DL);
  }

public:
  // NOLINTNEXTLINE
  explicit IRBuilder(llvm::LLVMContext &C) : detail::LLVMBuilderBase(C) {}

  // NOLINTNEXTLINE
  IRBuilder(llvm::BasicBlock *BB, const llvm::DebugLoc &DL) :
    // NOLINTNEXTLINE
    IRBuilder(BB->getContext()) {

    SetInsertPoint(BB, DL);
  }

  // NOLINTNEXTLINE
  IRBuilder(llvm::Instruction *I, const llvm::DebugLoc &DL) :
    // NOLINTNEXTLINE
    IRBuilder(I->getContext()) {

    SetInsertPoint(I, DL);
  }

  /// This overload should be avoided in favor of the one that explicitly
  /// provides a debug location.
  // NOLINTNEXTLINE
  explicit IRBuilder(llvm::BasicBlock *BB) : IRBuilder(BB->getContext()) {
    SetInsertPoint(BB);
  }

  // NOLINTNEXTLINE
  explicit IRBuilder(llvm::Instruction *I) : IRBuilder(I->getContext()) {
    SetInsertPoint(I);
  }

  // NOLINTNEXTLINE
  IRBuilder(llvm::BasicBlock *BB, llvm::BasicBlock::iterator I) :
    // NOLINTNEXTLINE
    IRBuilder(BB->getContext()) {
    SetInsertPoint(BB, I);
  }
};

} // namespace revng
