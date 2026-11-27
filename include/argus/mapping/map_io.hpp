#pragma once

#include "argus/mapping/grid.hpp"

#include <filesystem>
#include <iosfwd>
#include <string_view>
#include <variant>

namespace argus::mapping {

// Versioned little-endian bytes. New files carry a SHA-256 digest of their header and payload.
[[nodiscard]] Result<std::monostate, MapError> save(const Grid &grid, std::ostream &output);
// The directory and its ancestors must be controlled by the operator. The leaf is a simple
// lowercase filename ending in .argmap. Publication replaces the directory entry atomically.
[[nodiscard]] Result<std::monostate, MapError>
save_atomic(const Grid &grid, const std::filesystem::path &directory, std::string_view leaf);
// A trusted digest pins the version 2 header and payload. An empty pin permits legacy files.
[[nodiscard]] Result<Grid, MapError> load(std::istream &input, Spec expected,
                                          std::string_view trusted_digest = {});

} // namespace argus::mapping
