#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <cstdint>

#include "revng/Model/Generated/Early/ScalarType.h"

namespace model {

class ScalarType : public generated::ScalarType {
public:
  using generated::ScalarType::ScalarType;

  uint64_t alignedAt() const {
    revng_assert(Size() != 0);
    return AlignedAt() != 0 ? AlignedAt() : Size();
  }
};

} // namespace model

#include "revng/Model/Generated/Late/ScalarType.h"
