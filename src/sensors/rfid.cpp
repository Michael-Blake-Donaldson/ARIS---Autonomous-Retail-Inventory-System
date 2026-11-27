#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace argus::simulation::detail {
namespace {

constexpr double mm_per_m = 1000.0;
constexpr double turn = 2.0 * std::numbers::pi;

bool inside(world::RectMm bounds, world::PrecisePointMm point) noexcept {
    return point.x >= bounds.min.x && point.x <= bounds.max.x && point.y >= bounds.min.y &&
           point.y <= bounds.max.y;
}

double transmission(const std::vector<world::RectMm> &obstacles, world::PrecisePointMm origin,
                    world::PrecisePointMm target, double coefficient) noexcept {
    unsigned obstructions = 0;
    for (const auto rect : obstacles) {
        // The target rack is represented by the tag's orientation and distance model.
        if (inside(rect, target)) {
            continue;
        }
        const auto hit = ray_interval(rect, origin, {target.x - origin.x, target.y - origin.y});
        if (hit && hit->entry < 1.0 && hit->exit > 0.0 && ++obstructions == 4) {
            break;
        }
    }
    double gain = 1.0;
    for (unsigned i = 0; i < obstructions; ++i) {
        gain *= coefficient;
    }
    return gain;
}

} // namespace

sensors::RfidScan scan_rfid(const SensorQuery &world, robot::Pose physical, sensors::Header header,
                            const RfidConfig &config, RfidDwell &dwell, Random &random,
                            const std::optional<RfidFocus> &focus,
                            const std::optional<world::RectMm> &peer) {
    sensors::RfidScan scan{header, {}};
    const auto heading = physical.heading.value();
    const auto cos_heading = std::cos(heading);
    const auto sin_heading = std::sin(heading);
    const world::PrecisePointMm origin{
        mm_per_m * (physical.x.value() + config.antenna_forward_m * cos_heading -
                    config.antenna_left_m * sin_heading),
        mm_per_m * (physical.y.value() + config.antenna_forward_m * sin_heading +
                    config.antenna_left_m * cos_heading)};
    if (!inside(world.floor(), origin)) {
        dwell.clear();
        return scan;
    }

    const auto region = region_near(origin, config.range_m * mm_per_m);
    auto obstacles = world.obstacles(region, header.world_tick);
    if (peer) {
        obstacles.push_back(*peer);
    }
    scan.reads.reserve(config.max_reads);
    world.visit_tags(region, [&](const TagTruth &tag) {
        const auto x = static_cast<double>(tag.position.x);
        const auto y = static_cast<double>(tag.position.y);
        if (focus) {
            const auto east = x / mm_per_m - physical.x.value();
            const auto north = y / mm_per_m - physical.y.value();
            const auto forward = east * cos_heading + north * sin_heading;
            const auto left = north * cos_heading - east * sin_heading;
            const auto height = tag.height_mm / mm_per_m;
            if (std::abs(forward - focus->forward.value()) > focus->half_depth.value() ||
                std::abs(left - focus->left.value()) > focus->half_width.value() ||
                std::abs(height - focus->height.value()) > focus->half_height.value()) {
                return;
            }
        }
        const auto dx = (x - origin.x) / mm_per_m;
        const auto dy = (y - origin.y) / mm_per_m;
        const auto dz = tag.height_mm / mm_per_m - config.antenna_height_m;
        const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const auto bearing =
            dx == 0.0 && dy == 0.0 ? heading + config.antenna_yaw_rad : std::atan2(dy, dx);
        const auto off_axis = std::remainder(bearing - heading - config.antenna_yaw_rad, turn);
        if (distance > config.range_m || std::abs(off_axis) > config.field_of_view_rad / 2.0) {
            return;
        }

        auto &exposure = dwell.try_emplace(tag.epc).first->second;
        exposure.consecutive_scans = std::min(exposure.consecutive_scans + 1U, config.dwell_scans);
        exposure.last_tick = header.sample_tick;
        if (exposure.consecutive_scans < config.dwell_scans) {
            return;
        }

        const auto angle_gain = 0.35 + 0.65 * std::max(0.0, std::cos(off_axis));
        const auto orientation = tag.heading_deg * std::numbers::pi / 180.0;
        const auto tag_gain = 0.2 + 0.8 * std::abs(std::cos(orientation - bearing));
        const auto normalized = distance / config.range_m;
        const auto range_gain = 1.0 - 0.65 * normalized * normalized;
        const auto walls = transmission(obstacles, origin, {x, y}, config.obstacle_transmission);
        const auto gain = angle_gain * tag_gain * walls;
        if (random.unit_interval() >= config.detection_probability * range_gain * gain) {
            return;
        }

        const auto jitter = 2.0 * (2.0 * random.unit_interval() - 1.0);
        const auto path_loss = 20.0 * std::log10(std::max(0.1, distance));
        const auto signal =
            std::clamp(-42.0 - path_loss + 10.0 * std::log10(gain) + jitter, -110.0, -20.0);
        if (scan.reads.size() < config.max_reads) {
            scan.reads.push_back({tag.epc, signal});
        } else {
            ++scan.truncated_reads;
        }
    });
    std::erase_if(dwell, [tick = header.sample_tick](const auto &entry) {
        return entry.second.last_tick != tick;
    });
    return scan;
}

} // namespace argus::simulation::detail
