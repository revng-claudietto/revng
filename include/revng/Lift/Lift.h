#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/ADT/StringRef.h"
#include "llvm/Pass.h"

#include "revng/Lift/JTReason.h"
#include "revng/PipeboxCommon/BinariesContainer.h"
#include "revng/PipeboxCommon/LLVMContainer.h"
#include "revng/PipeboxCommon/Model.h"
#include "revng/Support/Debug.h"

namespace revng::lift::internal {

llvm::Error checkPrecondition(const model::Binary &Model);

std::tuple<bool, std::map<MetaAddress, bool>>
collectJumpTargets(const llvm::Module &);

bool shouldInvalidateRoot(const std::map<MetaAddress, bool> &JumpTargets,
                          const TupleTreeDiff<model::Binary> &Diff);

} // namespace revng::lift::internal

namespace revng::pypeline::piperuns {

class Lift {
public:
  static constexpr llvm::StringRef Name = "lift";
  using Arguments = TypeList<
    PipeRunArgument<const BinariesContainer, "Input", "Input binaries to lift">,
    PipeRunArgument<LLVMRootContainer,
                    "Output",
                    "LLVM Module containing the lifted binaries",
                    Access::Write>>;

private:
  const Model &TheModel;
  const BinariesContainer &Binary;
  LLVMRootContainer &ModuleContainer;

public:
  Lift(const class Model &Model,
       llvm::StringRef Config,
       llvm::StringRef DynamicConfig,
       const BinariesContainer &Binary,
       LLVMRootContainer &ModuleContainer);

  CustomInvalidationData run();

public:
  static llvm::Error checkPrecondition(const class Model &Model);

  static bool requiresCustomInvalidation(const ModelDiff &Diff);

  static std::vector<std::set<ObjectID>>
  processCustomInvalidation(const InvalidationData &Data,
                            const ModelDiff &Diff);
};

} // namespace revng::pypeline::piperuns
