#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace argus::simulation::detail {
namespace {

constexpr double mm_per_m = 1000.0;
constexpr double intersection_epsilon = 1e-8;

} // namespace

std::optional<RayInterval> ray_interval(world::RectMm box, world::PrecisePointMm origin,
                                        world::PrecisePointMm direction) noexcept {
    RayInterval span{-std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::infinity()};
    const auto clip = [&span](double low, double high, double start, double heading) {
        if (std::abs(heading) < 1e-12) {
            return start >= low && start <= high;
        }
        const auto a = (low - start) / heading;
        const auto b = (high - start) / heading;
        span.entry = std::max(span.entry, std::min(a, b));
        span.exit = std::min(span.exit, std::max(a, b));
        return span.entry <= span.exit + intersection_epsilon;
    };
    if (!clip(box.min.x, box.max.x, origin.x, direction.x) ||
        !clip(box.min.y, box.max.y, origin.y, direction.y) || span.exit < -intersection_epsilon) {
        return std::nullopt;
    }
    return span;
}

world::RectMm region_near(world::PrecisePointMm origin, double range) noexcept {
    return {{static_cast<std::int32_t>(std::floor(origin.x - range)),
             static_cast<std::int32_t>(std::floor(origin.y - range))},
            {static_cast<std::int32_t>(std::ceil(origin.x + range)),
             static_cast<std::int32_t>(std::ceil(origin.y + range))}};
}

sensors::LidarScan scan_lidar(const SensorQuery &world, robot::Pose physical,
                              sensors::Header header, const LidarConfig &config, Random &random,
                              const std::optional<world::RectMm> &peer) {
    const world::PrecisePointMm origin{physical.x.value() * mm_per_m,
                                       physical.y.value() * mm_per_m};
    const auto range_mm = config.range_m * mm_per_m;
    auto obstacles = world.obstacles(region_near(origin, range_mm), header.world_tick);
    if (peer) {
        obstacles.push_back(*peer);
    }
    const auto floor = world.floor();
    const auto spacing = config.field_of_view_rad / config.beams;
    sensors::LidarScan scan{
        header, -config.field_of_view_rad / 2.0 + spacing / 2.0, spacing, config.range_noise_m, {}};
    scan.ranges_m.reserve(config.beams);
    for (std::uint32_t beam = 0; beam < config.beams; ++beam) {
        const auto angle = physical.heading.value() + scan.first_angle_rad + beam * spacing;
        const world::PrecisePointMm direction{std::cos(angle), std::sin(angle)};
        const auto boundary = ray_interval(floor, origin, direction);
        auto nearest = boundary ? boundary->exit : std::numeric_limits<double>::infinity();
        for (const auto rect : obstacles) {
            if (const auto hit = ray_interval(rect, origin, direction)) {
                nearest = std::min(nearest, std::max(0.0, hit->entry));
            }
        }
        const auto drop = random.unit_interval();
        const auto jitter = config.range_noise_m * (2.0 * random.unit_interval() - 1.0);
        if (nearest > range_mm + intersection_epsilon || drop < config.missing_probability) {
            scan.ranges_m.push_back(std::nullopt);
        } else {
            scan.ranges_m.push_back(std::clamp(nearest / mm_per_m + jitter, 0.0, config.range_m));
            ++scan.return_count;
        }
    }
    return scan;
}

} // namespace argus::simulation::detail
