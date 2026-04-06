#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng/Model/Binary.h"

namespace abi::FunctionType {

/// Best effort `CABIFunctionDefinition` to `RawFunctionDefinition` conversion.
///
/// If `ABI` is not specified, `Binary.DefaultABI` is used instead.
///
/// \param UseSoftRegisterStateDeductions For specifics see the difference
///        between `model::ABIDefinition::tryDeducingArgumentRegisterState` (`true`)
///        and `model::ABIDefinition::enforceArgumentRegisterState` (`false`).
std::optional<model::UpcastableType>
tryConvertToCABI(const model::RawFunctionDefinition &Function,
                 TupleTree<model::Binary> &Binary,
                 const std::string &ABI = "",
                 bool UseSoftRegisterStateDeductions = true);

} // namespace abi::FunctionType
