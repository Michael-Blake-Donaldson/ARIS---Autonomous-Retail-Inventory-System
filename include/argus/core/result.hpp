#pragma once

#include <variant>

namespace argus {

// Fallible interfaces return a value or a module-specific error code.
template <typename Value, typename Error> using Result = std::variant<Value, Error>;

} // namespace argus
