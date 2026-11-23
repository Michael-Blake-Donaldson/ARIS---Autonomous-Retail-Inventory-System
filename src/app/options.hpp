#pragma once

#include "argus/core/result.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace argus::app {

enum class Action { run, help, version };
enum class WarehouseSource { none, fixture, standard_input, export_fixture };

enum class Error {
    invalid_arguments,
    invalid_seed,
    invalid_tick,
    video_initialization,
    window_creation,
    graphics_context,
    graphics_present,
    window_events,
    output_failure,
};

struct Options {
    Action action{Action::run};
    std::uint32_t seed{5489U};
    WarehouseSource warehouse_source{WarehouseSource::none};
    std::string_view fixture{};
    std::uint64_t tick{};
};

// Accepts NUL-terminated process arguments after argv[0]. Options can borrow a fixture argument.
[[nodiscard]] Result<Options, Error> parse_options(std::span<const char *const> arguments) noexcept;

} // namespace argus::app
