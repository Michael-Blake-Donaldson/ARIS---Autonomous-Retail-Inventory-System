#pragma once

#include <variant>

namespace argus {

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

// Expected failures cross module boundaries as codes. Applications map them to diagnostics.
template <typename T> using Result = std::variant<T, Error>;
using Status = Result<std::monostate>;

} // namespace argus
