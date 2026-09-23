#pragma once

#include <concepts>
#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace neo_cppjieba {

namespace compile_config {
#ifdef NDEBUG
inline constexpr auto is_debug_build = false;
#else
inline constexpr auto is_debug_build = true;
#endif
} // namespace compile_config

// Log failures use an application exception that accepts the formatted message.
template <typename Exception>
concept LogException = std::derived_from<Exception, std::exception> && std::constructible_from<Exception, std::string>;

// A configuration supplies an exception type and a boolean usable in constant expressions.
template <typename Config>
concept LogConfiguration = requires {
    requires LogException<typename Config::Exception>;
    { Config::show_source_location } -> std::same_as<const bool &>;
    typename std::bool_constant<Config::show_source_location>;
};

// Default diagnostics expose source locations and throw a standard runtime error.
// Edit these defaults here to configure the library globally.
// Individual logging and check calls may still pass an explicit configuration type.
struct LogConfig {
    static constexpr auto show_source_location = true;
    using Exception = std::runtime_error;
};

} // namespace neo_cppjieba
