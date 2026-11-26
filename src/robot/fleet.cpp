#include "argus/simulation/fleet.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace argus::simulation {
namespace {

world::PrecisePointMm position(robot::Pose pose) noexcept {
    return {pose.x.value() * 1000.0, pose.y.value() * 1000.0};
}

bool intersects(world::PrecisePointMm a, world::PrecisePointMm next_a, world::PrecisePointMm b,
                world::PrecisePointMm next_b, double limit) noexcept {
    const auto x = a.x - b.x;
    const auto y = a.y - b.y;
    const auto dx = next_a.x - next_b.x - x;
    const auto dy = next_a.y - next_b.y - y;
    const auto distance = dx * dx + dy * dy;
    const auto at = distance > 0.0 ? std::clamp(-(x * dx + y * dy) / distance, 0.0, 1.0) : 0.0;
    return std::hypot(x + at * dx, y + at * dy) <= limit;
}

world::RectMm bounds(world::PrecisePointMm center, double radius) noexcept {
    return {{static_cast<std::int32_t>(std::floor(center.x - radius)),
             static_cast<std::int32_t>(std::floor(center.y - radius))},
            {static_cast<std::int32_t>(std::ceil(center.x + radius)),
             static_cast<std::int32_t>(std::ceil(center.y + radius))}};
}

} // namespace

bool FleetClock::step(RobotId id, RobotSimulation &robot, robot::Command command) {
    if (id.value() < 1 || id.value() > members_.size()) {
        return false;
    }
    std::unique_lock lock{mutex_};
    if (closed_) {
        return false;
    }
    auto &member = members_.at(static_cast<std::size_t>(id.value() - 1));
    if (member.waiting || (member.robot && member.robot != &robot)) {
        closed_ = true;
        condition_.notify_all();
        return false;
    }
    const auto generation = generation_;
    member = {&robot, command, true};
    if (members_[0].waiting && members_[1].waiting) {
        if (!advance()) {
            closed_ = true;
        } else {
            ++generation_;
            members_[0].waiting = members_[1].waiting = false;
        }
        condition_.notify_all();
    } else {
        condition_.wait(lock, [&] { return closed_ || generation != generation_; });
    }
    return generation != generation_;
}

void FleetClock::close() noexcept {
    std::scoped_lock lock{mutex_};
    closed_ = true;
    condition_.notify_all();
}

bool FleetClock::advance() noexcept {
    auto &a = *members_[0].robot;
    auto &b = *members_[1].robot;
    const auto tick = a.telemetry_.world_tick;
    if (&a == &b || !a.warehouse_.shares_layout(b.warehouse_) || tick != b.telemetry_.world_tick ||
        a.telemetry_.sample_tick != b.telemetry_.sample_tick ||
        generation_ == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    auto first = a.prepare(members_[0].command);
    auto second = b.prepare(members_[1].command);
    const auto start_a = position(a.truth_.pose);
    const auto start_b = position(b.truth_.pose);
    const double radii = static_cast<double>(a.radius_mm_) + b.radius_mm_;
    if (intersects(start_a, start_a, start_b, start_b, radii)) {
        return false;
    }
    const auto peer_block =
        intersects(start_a, position(first.goal), start_b, position(second.goal),
                   radii + first.clearance_mm + second.clearance_mm);
    const auto stop = [](RobotSimulation::Step &step, robot::Pose pose) {
        step.blocked = true;
        step.motion = {};
        step.goal = pose;
    };
    const bool hold =
        first.world_held || second.world_held ||
        (peer_block && (a.warehouse_.sweep_collides(start_a, start_a, a.radius_mm_, tick) ||
                        b.warehouse_.sweep_collides(start_b, start_b, b.radius_mm_, tick)));
    if (peer_block || hold) {
        stop(first, a.truth_.pose);
        stop(second, b.truth_.pose);
    }
    a.commit(first, members_[0].command.sensors_on, !hold);
    b.commit(second, members_[1].command.sensors_on, !hold);
    a.peer_ = bounds(position(b.truth_.pose), b.radius_mm_);
    b.peer_ = bounds(position(a.truth_.pose), a.radius_mm_);
    return true;
}

} // namespace argus::simulation
