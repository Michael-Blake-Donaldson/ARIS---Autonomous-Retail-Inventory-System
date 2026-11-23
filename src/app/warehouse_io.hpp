#pragma once

#include "application.hpp"

namespace argus::app {

[[nodiscard]] Result<simulation::Warehouse, simulation::ScenarioError>
open_warehouse(const Options &options);

[[nodiscard]] Status export_warehouse(std::string_view name);

} // namespace argus::app
