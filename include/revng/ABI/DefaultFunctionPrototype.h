#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <string>

#include "revng/Model/ABI.h"
#include "revng/Model/Binary.h"

namespace abi {

/// Creates the "default" prototype for a function.
///
/// Such a prototype considers all the possible ways to pass
/// an argument in or an return value out as used.
///
/// If `ABI` parameter was not provided, `BinaryToRecordTheTypeAt.DefaultABI`
/// is used instead.
model::UpcastableType
registerDefaultFunctionPrototype(model::Binary &Binary,
                                 const std::string &ABI = "");

} // namespace abi
