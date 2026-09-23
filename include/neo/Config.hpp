#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
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

// LogLevel represents the severity level of a log message, ranging from debug to fatal.
enum class LogLevel : uint8_t { LL_DEBUG, LL_INFO, LL_WARNING, LL_ERROR, LL_FATAL };

// Unicode input validation is always enabled because it also protects memory access bounds.

// The inline child capacity trades trie node size against hash lookup frequency.
struct TrieConfig {
    static constexpr auto flat_threshold = std::size_t{3};
};

// Log failures use an application exception that accepts the formatted message.
template <typename Exception>
concept LogException = std::derived_from<Exception, std::exception> && std::constructible_from<Exception, std::string>;

// A configuration supplies an exception type and a boolean usable in constant expressions.
// Its log threshold and positive buffer capacity must also be compile-time constants.
template <typename Config>
concept LogConfiguration = requires {
    requires LogException<typename Config::Exception>;
    { Config::show_source_location } -> std::same_as<const bool &>;
    typename std::bool_constant<Config::show_source_location>;
    { Config::min_level } -> std::same_as<const LogLevel &>;
    typename std::integral_constant<LogLevel, Config::min_level>;
    requires(Config::min_level >= LogLevel::LL_DEBUG && Config::min_level <= LogLevel::LL_FATAL);
    { Config::buffer_size } -> std::same_as<const std::size_t &>;
    typename std::integral_constant<std::size_t, Config::buffer_size>;
    requires(Config::buffer_size > 0 && Config::buffer_size <= std::numeric_limits<std::ptrdiff_t>::max());
};

// Default diagnostics expose source locations and throw a standard runtime error.
// Edit these defaults here to configure the library globally.
// Individual logging and check calls may still pass an explicit configuration type.
struct LogConfig {
    static constexpr auto show_source_location = true;
    static constexpr auto min_level = compile_config::is_debug_build ? LogLevel::LL_DEBUG : LogLevel::LL_INFO;
    static constexpr auto buffer_size = std::size_t{512};
    using Exception = std::runtime_error;
};

} // namespace neo_cppjieba
