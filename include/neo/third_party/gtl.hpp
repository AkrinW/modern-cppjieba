#pragma once

// Keep third-party diagnostics inside the dependency boundary without modifying upstream headers.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC system_header
#endif

#include "gtl/phmap.hpp"
