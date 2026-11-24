#pragma once

#include "argus/mapping/grid.hpp"

#include <iosfwd>
#include <variant>

namespace argus::mapping {

// Versioned little-endian bytes. The caller owns the stream and any atomic file replacement.
[[nodiscard]] Result<std::monostate, MapError> save(const Grid &grid, std::ostream &output);
[[nodiscard]] Result<Grid, MapError> load(std::istream &input, Spec expected);

} // namespace argus::mapping
