#pragma once

#include "argus/core/result.hpp"

#include <cstdint>
#include <span>

namespace argus::app {

enum class Action { run, help, version };

enum class Error {
    invalid_arguments,
    invalid_seed,
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
};

// Accepts process arguments after argv[0]. Entries must be NUL-terminated.
[[nodiscard]] Result<Options, Error> parse_options(std::span<const char *const> arguments) noexcept;

} // namespace argus::app
