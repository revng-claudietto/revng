#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/IR/Module.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"

#include "revng/BasicAnalyses/RootFunction.h"
#include "revng/EarlyFunctionAnalysis/FunctionSummaryOracle.h"
#include "revng/EarlyFunctionAnalysis/TemporaryOpaqueFunction.h"
#include "revng/Model/FunctionTags.h"
#include "revng/Model/NamedTypedRegister.h"
#include "revng/Support/OpaqueFunctionsPool.h"
#include "revng/Support/UniqueValuePtr.h"

namespace efa {

class CallHandler;
class OutlinedFunctionsMap;

/// Direct branches to a function entry that are not tail calls: owning
/// function, address following the branch and destination.
using LocalBranchSet = std::set<
  std::tuple<MetaAddress, MetaAddress, MetaAddress>>;

/// \return the direct branches of \p Blocks, the control-flow graph of the
///         function at \p Entry
LocalBranchSet
collectLocalBranches(const MetaAddress &Entry,
                     const SortedVector<efa::BasicBlock> &Blocks);

/// An outlined function helper object.
struct OutlinedFunction {
public:
  MetaAddress Address;

  /// The marker for indirect jumps
  // NOTE: do not move this field, F holds a reference to it and therefore it
  //       needs to be destroyed first.
  UniqueValuePtr<llvm::Function> IndirectBranchInfoMarker;

  /// The actual LLVM outlined function
  UniqueValuePtr<llvm::Function> Function;

  // Special blocks
  llvm::BasicBlock *AnyPCCloned = nullptr;
  llvm::BasicBlock *UnexpectedPCCloned = nullptr;

  std::vector<MetaAddress> InlinedFunctionsByIndex = { MetaAddress::invalid() };

public:
  OutlinedFunction() = default;

  OutlinedFunction(const OutlinedFunction &Other) = delete;
  OutlinedFunction &operator=(const OutlinedFunction &) = delete;

  OutlinedFunction(OutlinedFunction &&Other) { *this = std::move(Other); }

  OutlinedFunction &operator=(OutlinedFunction &&Other) {
    if (this != &Other) {
      Address = Other.Address;
      Function = std::move(Other.Function);
      IndirectBranchInfoMarker = std::move(Other.IndirectBranchInfoMarker);
      AnyPCCloned = Other.AnyPCCloned;
      UnexpectedPCCloned = Other.UnexpectedPCCloned;
      InlinedFunctionsByIndex = Other.InlinedFunctionsByIndex;

      Other.Address = MetaAddress::invalid();
      Other.Function = nullptr;
      Other.IndirectBranchInfoMarker.reset();
      Other.AnyPCCloned = nullptr;
      Other.UnexpectedPCCloned = nullptr;
    }
    return *this;
  }

public:
  llvm::Function *releaseFunction() {
    auto *ToReturn = Function.release();
    Function = nullptr;
    return ToReturn;
  }
};

/// This class, given an Oracle, can outline functions from root
class Outliner {
private:
  llvm::Module &M;
  RootFunction &RootF;
  const CSVGlobals &Globals;
  const model::Binary &Binary;
  FunctionSummaryOracle &Oracle;

  /// UnexpectedPCMarker is used to indicate that `unexpectedpc` basic
  /// block of a function to inline need to be adjusted to jump to
  /// `unexpectedpc` of their caller.
  TemporaryOpaqueFunction UnexpectedPCMarker;

  /// Keeps the destination of a candidate direct tail call through inlining.
  TemporaryOpaqueFunction DirectTailCallMarker;

  OpaqueFunctionsPool<llvm::Type *> OpaqueReturnAddress;

  llvm::CodeExtractorAnalysisCache CEAC;

public:
  Outliner(llvm::Module &M,
           RootFunction &RootF,
           const CSVGlobals &Globals,
           const model::Binary &Binary,
           FunctionSummaryOracle &Oracle) :
    M(M),
    RootF(RootF),
    Globals(Globals),
    Binary(Binary),
    Oracle(Oracle),
    UnexpectedPCMarker(initializeUnexpectedPCMarker(M)),
    DirectTailCallMarker(initializeDirectTailCallMarker(M)),
    OpaqueReturnAddress(&M, false),
    CEAC(*M.getFunction("root")) {

    using namespace llvm;
    auto Effects = MemoryEffects::inaccessibleMemOnly(llvm::ModRefInfo::Ref);
    OpaqueReturnAddress.setMemoryEffects(Effects);
    OpaqueReturnAddress.addFnAttribute(Attribute::NoUnwind);
    OpaqueReturnAddress.addFnAttribute(Attribute::WillReturn);
    OpaqueReturnAddress.setTags({ &FunctionTags::OpaqueReturnAddressFunction,
                                  &FunctionTags::UniquedByPrototype });
    OpaqueReturnAddress
      .initializeFromReturnType(FunctionTags::OpaqueReturnAddressFunction);
  }

public:
  OutlinedFunction outline(const MetaAddress &EntryAddress,
                           CallHandler *TheCallHandler,
                           const LocalBranchSet &LocalBranches);

private:
  static TemporaryOpaqueFunction
  initializeDirectTailCallMarker(llvm::Module &M) {
    auto &Context = M.getContext();
    auto *Void = llvm::Type::getVoidTy(Context);
    auto *Pointer = llvm::Type::getInt8PtrTy(Context);
    return { llvm::FunctionType::get(Void, { Pointer }, false),
             "direct_tail_call",
             &M };
  }

  static TemporaryOpaqueFunction initializeUnexpectedPCMarker(llvm::Module &M) {
    return { llvm::FunctionType::get(llvm::Type::getVoidTy(M.getContext()),
                                     false),
             "unexpectedpc_hook",
             &M };
  }

private:
  OutlinedFunction
  outlineFunctionInternal(CallHandler *TheCallHandler,
                          const MetaAddress &EntryAddress,
                          const LocalBranchSet &LocalBranches,
                          OutlinedFunctionsMap &FunctionsToInline);

  /// \return true if the edge from \p Source to \p Target, in the function at
  ///         \p Function, is a direct branch to the entry of a function that
  ///         might be a tail call
  bool isTailCallCandidate(const MetaAddress &Function,
                           const LocalBranchSet &LocalBranches,
                           llvm::BasicBlock *Source,
                           llvm::BasicBlock *Target) const;

  /// \return a description of the call and boolean indicating whether the call
  ///         site is a tail call or not.
  std::pair<const FunctionSummary *, bool>
  getCallSiteInfo(MetaAddress CallerFunction,
                  llvm::BasicBlock *CallerBlock,
                  llvm::CallInst *FunctionCall,
                  llvm::CallInst *JumpToSymbol,
                  MetaAddress Callee);

  void integrateFunctionCallee(CallHandler *TheCallHandler,
                               MetaAddress Caller,
                               llvm::BasicBlock *CallerBlock,
                               llvm::CallInst *FunctionCall,
                               llvm::CallInst *JumpToSymbol,
                               MetaAddress Callee,
                               OutlinedFunctionsMap &FunctionsToInline);

  llvm::Function *
  createFunctionToInline(CallHandler *TheCallHandler,
                         const MetaAddress &Entry,
                         const LocalBranchSet &LocalBranches,
                         OutlinedFunctionsMap &FunctionsToInline);

  void createAnyPCHooks(CallHandler *TheCallHandler, OutlinedFunction *F);
};

} // namespace efa
