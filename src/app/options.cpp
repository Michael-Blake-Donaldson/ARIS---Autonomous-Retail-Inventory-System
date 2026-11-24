#include "options.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <limits>
#include <optional>
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

template <typename Integer> std::optional<Integer> read_decimal(const char *argument) noexcept {
    constexpr std::size_t digits = std::numeric_limits<Integer>::digits10 + 1;
    const auto value = bounded_argument(argument, digits);
    if (value.empty() || value.size() > digits) {
        return std::nullopt;
    }
    Integer number{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return std::nullopt;
    }
    return number;
}

Result<Options, Error> warehouse_options(WarehouseSource source,
                                         std::span<const char *const> args) noexcept {
    Options result{.warehouse_source = source};
    if (source != WarehouseSource::standard_input) {
        if (args.empty()) {
            return Error::invalid_arguments;
        }
        constexpr std::size_t name_limit = 32;
        const auto name = bounded_argument(args.front(), name_limit);
        if (name.empty() || name.size() > name_limit || name.front() < 'a' || name.front() > 'z' ||
            !std::ranges::all_of(name, [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
            })) {
            return Error::invalid_arguments;
        }
        result.fixture = name;
        args = args.subspan(1);
    }
    if (source == WarehouseSource::export_fixture && !args.empty()) {
        return Error::invalid_arguments;
    }
    constexpr std::string_view drive_option = "--drive-steps";
    bool has_tick = false;
    while (!args.empty()) {
        if (args.size() < 2) {
            return Error::invalid_arguments;
        }
        const auto option = bounded_argument(args.front(), drive_option.size());
        if (option == "--tick" && !has_tick) {
            const auto tick = read_decimal<std::uint64_t>(args[1]);
            if (!tick) {
                return Error::invalid_tick;
            }
            result.tick = *tick;
            has_tick = true;
        } else if (option == drive_option && result.drive_steps == 0) {
            constexpr std::uint32_t max_drive_steps = 2000;
            const auto steps = read_decimal<std::uint32_t>(args[1]);
            if (!steps || *steps == 0 || *steps > max_drive_steps) {
                return Error::invalid_drive_steps;
            }
            result.drive_steps = *steps;
        } else {
            return Error::invalid_arguments;
        }
        args = args.subspan(2);
    }
    return result;
}

} // namespace

Result<Options, Error> parse_options(std::span<const char *const> arguments) noexcept {
    constexpr std::string_view export_option = "--export-warehouse";
    if (arguments.empty()) {
        return Options{};
    }
    if (arguments.size() > 6) {
        return Error::invalid_arguments;
    }
    const auto option = bounded_argument(arguments.front(), export_option.size());
    if (arguments.size() == 1) {
        if (option == "--help") {
            return Options{.action = Action::help};
        }
        if (option == "--version") {
            return Options{.action = Action::version};
        }
    }
    if (option == "--warehouse") {
        return warehouse_options(WarehouseSource::fixture, arguments.subspan(1));
    }
    if (option == "--scenario-stdin") {
        return warehouse_options(WarehouseSource::standard_input, arguments.subspan(1));
    }
    if (option == export_option) {
        return warehouse_options(WarehouseSource::export_fixture, arguments.subspan(1));
    }
    if (option == "--seed" && arguments.size() == 2) {
        const auto seed = read_decimal<std::uint32_t>(arguments.back());
        if (seed) {
            return Options{.seed = *seed};
        }
        return Error::invalid_seed;
    }
    return Error::invalid_arguments;
}

} // namespace argus::app
