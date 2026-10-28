#pragma once

#include "neo/Fwd.hpp"

#include <cstdint>

namespace neo_cppjieba {

// Each mode names a complete behavior, including the modes that disable HMM.
enum class CutMode : uint8_t { MIX, MIX_NO_HMM, FULL, SEARCH, SEARCH_NO_HMM, HMM, MP };

} // namespace neo_cppjieba
