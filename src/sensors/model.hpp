#pragma once

#include "argus/core/random.hpp"
#include "argus/simulation/sensor_query.hpp"
#include "argus/simulation/sensors.hpp"

#include <map>

namespace argus::simulation::detail {

struct RayInterval {
    double entry;
    double exit;
};

struct Exposure {
    std::uint32_t consecutive_scans{};
    std::uint64_t last_tick{};
};

using RfidDwell = std::map<std::string, Exposure>;

[[nodiscard]] std::optional<RayInterval> ray_interval(world::RectMm rectangle,
                                                      world::PrecisePointMm origin,
                                                      world::PrecisePointMm direction) noexcept;
[[nodiscard]] world::RectMm region_near(world::PrecisePointMm origin, double range_mm) noexcept;

[[nodiscard]] sensors::LidarScan scan_lidar(const SensorQuery &world, robot::Pose physical,
                                            sensors::Header header, const LidarConfig &config,
                                            Random &random);
[[nodiscard]] sensors::RfidScan scan_rfid(const SensorQuery &world, robot::Pose physical,
                                          sensors::Header header, const RfidConfig &config,
                                          RfidDwell &dwell, Random &random,
                                          const std::optional<RfidFocus> &focus = {});

} // namespace argus::simulation::detail
