//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng/ABI/DefaultFunctionPrototype.h"
#include "revng/Model/ABIDefinition.h"
#include "revng/ABI/FunctionType/Support.h"
#include "revng/Support/EnumSwitch.h"

static model::UpcastableType defaultPrototype(model::Binary &Binary,
                                              llvm::StringRef ABI) {
  auto &&[Definition, Type] = Binary.makeRawFunctionDefinition();

  revng_assert(!ABI.empty());
  Definition.Architecture() = model::ABI::getArchitecture(ABI);

  const model::ABIDefinition &Defined = model::ABIDefinition::get(ABI);
  for (const auto &Register : Defined.GeneralPurposeArgumentRegisters())
    Definition.addArgument(Register, model::PrimitiveType::make(Register));

  for (const auto &Register : Defined.GeneralPurposeReturnValueRegisters())
    Definition.addReturnValue(Register, model::PrimitiveType::make(Register));

  for (const auto &Register : Defined.CalleeSavedRegisters())
    Definition.PreservedRegisters().insert(Register);

  Definition.FinalStackOffset() = getCallPushSize(Binary.Architecture());

  return Type;
}

model::UpcastableType
abi::registerDefaultFunctionPrototype(model::Binary &Binary,
                                      const std::string &ABI) {
  std::string EffectiveABI = ABI;
  if (EffectiveABI.empty())
    EffectiveABI = Binary.DefaultABI();
  revng_assert(!EffectiveABI.empty());
  return defaultPrototype(Binary, EffectiveABI);
}
