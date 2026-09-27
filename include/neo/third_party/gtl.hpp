#pragma once

// Keep third-party diagnostics inside the dependency boundary without modifying upstream headers.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC system_header
#endif

namespace gtl {
template<class K, class V, class Hash, class Eq, class Alloc>
class flat_hash_map;
}

#include "gtl/phmap.hpp"
