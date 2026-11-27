#pragma once

#include "application.hpp"

#include <cstdio>

namespace argus::app {

[[nodiscard]] bool use_binary_mode(std::FILE *stream) noexcept;

[[nodiscard]] Result<simulation::Warehouse, simulation::ScenarioError>
open_warehouse(const Options &options);

[[nodiscard]] Status export_warehouse(std::string_view name);

} // namespace argus::app
