#include "scenario.hpp"

#include <algorithm>

namespace argus::simulation::detail {

world::RectMm sweep(const Actor &actor) noexcept {
    return {{std::min(actor.start.x, actor.end.x), std::min(actor.start.y, actor.end.y)},
            {std::max(actor.start.x, actor.end.x) + actor.size.x,
             std::max(actor.start.y, actor.end.y) + actor.size.y}};
}

world::RectMm actor_bounds(const Actor &actor, std::uint64_t tick) noexcept {
    const auto period = actor.leg_ticks * 2U;
    const auto phase = (tick % period + actor.phase) % period;
    const auto distance = std::min(phase, period - phase);
    const auto interpolate = [distance, &actor](std::int32_t start, std::int32_t end) {
        const auto offset = static_cast<std::int64_t>(end - start) *
                            static_cast<std::int64_t>(distance) / actor.leg_ticks;
        return start + static_cast<std::int32_t>(offset);
    };
    const world::PointMm point{interpolate(actor.start.x, actor.end.x),
                               interpolate(actor.start.y, actor.end.y)};
    return {point, {point.x + actor.size.x, point.y + actor.size.y}};
}

bool inside_floor(world::RectMm floor, world::PointMm center, std::int32_t radius) noexcept {
    return radius > 0 && radius <= max_robot_radius_mm && center.x > floor.min.x + radius &&
           center.y > floor.min.y + radius && center.x < floor.max.x - radius &&
           center.y < floor.max.y - radius;
}

} // namespace argus::simulation::detail
