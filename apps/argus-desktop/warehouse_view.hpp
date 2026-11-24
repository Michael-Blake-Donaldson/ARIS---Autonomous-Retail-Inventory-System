#pragma once

#include "argus/robot/control.hpp"
#include "argus/world/snapshot.hpp"

#include <optional>

namespace argus::desktop {

void draw_warehouse(const world::Snapshot &view, robot::Pose estimated,
                    std::optional<robot::Pose> truth_overlay, int width, int height);

} // namespace argus::desktop
