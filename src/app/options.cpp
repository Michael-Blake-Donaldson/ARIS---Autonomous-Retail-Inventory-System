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
    constexpr std::string_view sensor_option = "--sensor-steps";
    constexpr std::string_view explore_option = "--explore-steps";
    constexpr std::uint32_t max_probe_steps = 2000;
    bool has_tick = false;
    bool has_cell = false;
    bool has_dock = false;
    while (!args.empty()) {
        const auto option = bounded_argument(args.front(), explore_option.size());
        const bool no_probe =
            result.drive_steps == 0 && result.sensor_steps == 0 && result.map_mode == MapMode::none;
        if (option == "--map-stdin" && no_probe && source == WarehouseSource::fixture) {
            result.map_mode = MapMode::load_map;
            args = args.subspan(1);
            continue;
        }
        if (args.size() < 2) {
            return Error::invalid_arguments;
        }
        if (option == "--map-output" && result.map_directory.empty() && args.size() >= 3) {
            result.map_directory = bounded_argument(args[1], 4096);
            result.map_leaf = bounded_argument(args[2], 80);
            if (result.map_directory.empty() || result.map_directory.size() > 4096 ||
                result.map_leaf.empty() || result.map_leaf.size() > 80) {
                return Error::invalid_arguments;
            }
            args = args.subspan(3);
            continue;
        } else if (option == "--map-sha256" && result.map_digest.empty()) {
            result.map_digest = bounded_argument(args[1], 64);
            if (result.map_digest.size() != 64 ||
                !std::ranges::all_of(result.map_digest, [](char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                })) {
                return Error::invalid_arguments;
            }
        } else if (option == "--tick" && !has_tick) {
            const auto tick = read_decimal<std::uint64_t>(args[1]);
            if (!tick) {
                return Error::invalid_tick;
            }
            result.tick = *tick;
            has_tick = true;
        } else if ((option == drive_option || option == sensor_option) && no_probe) {
            const auto steps = read_decimal<std::uint32_t>(args[1]);
            if (!steps || *steps == 0 || *steps > max_probe_steps) {
                return option == drive_option ? Error::invalid_drive_steps
                                              : Error::invalid_sensor_steps;
            }
            if (option == drive_option) {
                result.drive_steps = *steps;
            } else {
                result.sensor_steps = *steps;
            }
        } else if ((option == "--map-steps" || option == "--export-map" ||
                    option == explore_option) &&
                   no_probe) {
            const auto steps = read_decimal<std::uint32_t>(args[1]);
            const bool explore = option == explore_option;
            if (!steps || *steps == 0 || *steps > max_probe_steps) {
                return explore ? Error::invalid_explore_steps : Error::invalid_map_steps;
            }
            result.map_mode = explore                   ? MapMode::explore
                              : option == "--map-steps" ? MapMode::summary
                                                        : MapMode::export_map;
            result.map_steps = *steps;
        } else if (option == "--dock-mm" && !has_dock && args.size() >= 3) {
            const auto x = read_decimal<std::uint32_t>(args[1]);
            const auto y = read_decimal<std::uint32_t>(args[2]);
            if (!x || !y || *x == 0 || *y == 0) {
                return Error::invalid_dock;
            }
            result.dock_x_mm = *x;
            result.dock_y_mm = *y;
            has_dock = true;
            args = args.subspan(3);
            continue;
        } else if (option == "--cell-mm" && !has_cell) {
            const auto size = read_decimal<std::uint32_t>(args[1]);
            if (!size || *size < 50 || *size > 2000) {
                return Error::invalid_cell_size;
            }
            result.cell_mm = *size;
            has_cell = true;
        } else {
            return Error::invalid_arguments;
        }
        args = args.subspan(2);
    }
    if ((has_dock && result.map_mode != MapMode::explore) ||
        (has_cell && result.map_mode == MapMode::none) ||
        (!result.map_directory.empty() && result.map_mode != MapMode::export_map) ||
        (!result.map_digest.empty() && result.map_mode != MapMode::load_map) ||
        (has_tick && result.map_mode == MapMode::load_map)) {
        return Error::invalid_arguments;
    }
    return result;
}

} // namespace

Result<Options, Error> parse_options(std::span<const char *const> arguments) noexcept {
    constexpr std::string_view export_option = "--export-warehouse";
    if (arguments.empty()) {
        return Options{};
    }
    if (arguments.size() > 11) {
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
