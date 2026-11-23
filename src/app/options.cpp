#include "options.hpp"

#include <charconv>
#include <cstddef>
#include <limits>
#include <string_view>
#include <system_error>

namespace argus::app {
namespace {

// Keep one extra character so a longer input cannot match a valid prefix.
std::string_view bounded_argument(const char *value, std::size_t limit) noexcept {
    std::size_t length = 0;
    while (length <= limit && value[length] != '\0') {
        ++length;
    }
    return {value, length};
}

} // namespace

Result<Options, Error> parse_options(std::span<const char *const> arguments) noexcept {
    constexpr std::string_view help = "--help";
    constexpr std::string_view version = "--version";
    constexpr std::string_view seed_option = "--seed";
    if (arguments.empty()) {
        return Options{};
    }
    if (arguments.size() == 1) {
        const auto option = bounded_argument(arguments.front(), version.size());
        if (option == help) {
            return Options{.action = Action::help};
        }
        if (option == version) {
            return Options{.action = Action::version};
        }
    }
    if (arguments.size() != 2 ||
        bounded_argument(arguments.front(), seed_option.size()) != seed_option) {
        return Error::invalid_arguments;
    }

    constexpr std::size_t max_seed_digits = std::numeric_limits<std::uint32_t>::digits10 + 1;
    const auto value = bounded_argument(arguments.back(), max_seed_digits);
    if (value.empty() || value.size() > max_seed_digits) {
        return Error::invalid_seed;
    }
    std::uint32_t seed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), seed);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return Error::invalid_seed;
    }
    return Options{.seed = seed};
}

} // namespace argus::app
