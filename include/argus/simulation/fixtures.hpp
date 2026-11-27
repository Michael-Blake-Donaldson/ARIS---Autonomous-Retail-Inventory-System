#pragma once

#include "argus/simulation/warehouse.hpp"

#include <string_view>

namespace argus::simulation {

// Authored example bytes, including fictional placements. Use only for fixture loading or export.
[[nodiscard]] Result<std::string_view, ScenarioError> fixture_json(std::string_view name) noexcept;

} // namespace argus::simulation
