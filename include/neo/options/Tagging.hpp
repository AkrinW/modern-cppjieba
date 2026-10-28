#pragma once

#include "neo/Fwd.hpp"

#include <cstdint>

namespace neo_cppjieba {

// Selects whether a Jieba entry point attaches part-of-speech tags; untagged calls never look tags up.
enum class Tagging : uint8_t { None, Pos };

} // namespace neo_cppjieba
