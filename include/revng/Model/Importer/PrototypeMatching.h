#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <map>
#include <optional>
#include <string>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include "revng/Model/Binary.h"
#include "revng/Model/CABIFunctionDefinition.h"
#include "revng/TupleTree/TupleTree.h"

using ModelMap = std::map<std::string, TupleTree<model::Binary>>;

struct FunctionInfo {
  const model::TypeDefinition &Prototype;
  const TrackingMutableSet<model::FunctionAttribute::Values> &Attributes;
  llvm::StringRef ModuleName = {};
};

// model::Function is queried by its ExportedNames (its dynamic-symbol names).
// Name is a local identifier and is never used as a lookup key.
inline llvm::SmallVector<llvm::StringRef, 4>
lookupNames(const model::Function &Function) {
  llvm::SmallVector<llvm::StringRef, 4> Names;
  for (const auto &Name : Function.ExportedNames())
    Names.emplace_back(Name);
  return Names;
}

inline llvm::SmallVector<llvm::StringRef, 4>
lookupNames(const model::DynamicFunction &Function) {
  if (Function.Name().empty())
    return {};
  return { llvm::StringRef(Function.Name()) };
}

template<typename T>
inline std::optional<FunctionInfo>
findPrototypeInLocalFunctions(T &Functions,
                              llvm::StringRef FunctionName,
                              llvm::StringRef ModuleName) {
  for (auto &Function : Functions) {
    if (not llvm::is_contained(Function.ExportedNames(), FunctionName))
      continue;

    if (const model::TypeDefinition *Prototype = Function.prototype())
      return FunctionInfo{ .Prototype = *Prototype,
                           .Attributes = Function.Attributes(),
                           .ModuleName = ModuleName };
  }

  return std::nullopt;
}

template<typename T>
inline std::optional<FunctionInfo>
findPrototypeInDynamicFunctions(T &Functions,
                                llvm::StringRef FunctionName,
                                llvm::StringRef ModuleName) {
  auto It = Functions.find(FunctionName.str());
  if (It == Functions.end())
    return std::nullopt;

  if (const model::TypeDefinition *Prototype = It->prototype())
    return FunctionInfo{ .Prototype = *Prototype,
                         .Attributes = It->Attributes(),
                         .ModuleName = ModuleName };

  return std::nullopt;
}

inline std::optional<FunctionInfo>
findPrototype(llvm::StringRef Function, ModelMap &ModelsOfDynamicLibraries) {
  for (const auto &[Module, Model] : ModelsOfDynamicLibraries) {
    const auto &Ls = Model->Functions();
    if (std::optional R = findPrototypeInLocalFunctions(Ls, Function, Module))
      return R;

    const auto &Ds = Model->ImportedDynamicFunctions();
    if (std::optional R = findPrototypeInDynamicFunctions(Ds, Function, Module))
      return R;
  }

  return std::nullopt;
}
