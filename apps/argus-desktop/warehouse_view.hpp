#pragma once

#include "argus/mapping/mapper.hpp"
#include "argus/robot/control.hpp"
#include "argus/sensors/observations.hpp"
#include "argus/world/snapshot.hpp"

#include <optional>

namespace argus::desktop {

void draw_warehouse(const world::Snapshot &view, robot::Pose estimated,
                    std::optional<robot::Pose> truth_overlay, const sensors::LidarScan *lidar,
                    int width, int height);
void draw_occupancy(const mapping::Mapper &mapper, int width, int height);

} // namespace argus::desktop
