#pragma once

#include "application.hpp"

namespace argus::app {

[[nodiscard]] mapping::Spec map_spec(const world::Snapshot &scene, std::uint32_t cell_mm = 250);
[[nodiscard]] Status run_map(const Options &options);

} // namespace argus::app
