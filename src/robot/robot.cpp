#include "argus/simulation/robot.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace argus::simulation {
namespace {

using robot::Pose;
using robot::Radians;
using robot::RadiansPerSecond;
using robot::Twist;
using world::PrecisePointMm;

constexpr double step_s = static_cast<double>(robot::step_ms) / 1000.0;
constexpr double mm_per_m = 1000.0;
constexpr double full_turn = 2.0 * std::numbers::pi;

bool valid(MotionConfig motion, BatteryConfig battery) noexcept {
    const auto in = [](double value, double min, double max) {
        return value >= min && value <= max;
    };
    constexpr auto max_power_mw = 1000000U;
    constexpr std::uint64_t max_capacity_uj = 1000000000000ULL;
    return in(motion.max_linear_mps, 0.01, 5.0) && in(motion.max_angular_radps, 0.01, 10.0) &&
           in(motion.linear_acceleration_mps2, 0.01, 50.0) &&
           in(motion.linear_braking_mps2, 0.01, 50.0) &&
           in(motion.angular_acceleration_radps2, 0.01, 100.0) &&
           in(motion.angular_braking_radps2, 0.01, 100.0) && in(motion.track_m, 0.05, 2.0) &&
           in(motion.encoder_step_m, 0.00001, 0.01) && battery.capacity_uj > 0 &&
           battery.capacity_uj <= max_capacity_uj && battery.initial_uj <= battery.capacity_uj &&
           battery.low_threshold_uj < battery.capacity_uj && battery.idle_mw > 0 &&
           battery.idle_mw <= max_power_mw && battery.drive_mw <= max_power_mw &&
           battery.sensor_mw <= max_power_mw && battery.charge_mw <= max_power_mw &&
           battery.charge_mw > battery.idle_mw + battery.sensor_mw;
}

PrecisePointMm point(Pose pose) noexcept {
    return {pose.x.value() * mm_per_m, pose.y.value() * mm_per_m};
}

bool stopped(Twist twist) noexcept {
    return twist.linear.value() == 0 && twist.angular.value() == 0;
}

Twist bounded(Twist target, MotionConfig config) noexcept {
    return {MetersPerSecond{
                std::clamp(target.linear.value(), -config.max_linear_mps, config.max_linear_mps)},
            RadiansPerSecond{std::clamp(target.angular.value(), -config.max_angular_radps,
                                        config.max_angular_radps)}};
}

double slew(double current, double target, double acceleration, double braking) noexcept {
    if ((current > 0.0 && target < 0.0) || (current < 0.0 && target > 0.0)) {
        target = 0.0;
    }
    const auto limit = (std::abs(target) > std::abs(current) ? acceleration : braking) * step_s;
    if (std::abs(target - current) <= limit) {
        return target;
    }
    return current + std::copysign(limit, target - current);
}

Twist accelerate(Twist current, Twist target, MotionConfig config) noexcept {
    return {
        MetersPerSecond{slew(current.linear.value(), target.linear.value(),
                             config.linear_acceleration_mps2, config.linear_braking_mps2)},
        RadiansPerSecond{slew(current.angular.value(), target.angular.value(),
                              config.angular_acceleration_radps2, config.angular_braking_radps2)}};
}

Pose integrate(Pose pose, Twist twist) noexcept {
    const auto half_angle = twist.angular.value() * step_s / 2.0;
    const auto sinc = std::abs(half_angle) < 1e-7 ? 1.0 - half_angle * half_angle / 6.0
                                                  : std::sin(half_angle) / half_angle;
    const auto distance = twist.linear.value() * step_s * sinc;
    const auto middle = pose.heading.value() + half_angle;
    return {Meters{pose.x.value() + distance * std::cos(middle)},
            Meters{pose.y.value() + distance * std::sin(middle)},
            Radians{std::remainder(pose.heading.value() + 2.0 * half_angle, full_turn)}};
}

} // namespace

Result<RobotSimulation, RobotError> RobotSimulation::create(Warehouse warehouse, std::uint64_t tick,
                                                            MotionConfig motion,
                                                            BatteryConfig battery,
                                                            EncoderEffects encoders) {
    if (!valid(motion, battery) || !valid_encoder_effects(encoders)) {
        return RobotError::invalid_configuration;
    }
    const auto spawn = warehouse.snapshot(tick);
    if (warehouse.collides(spawn.robot_spawn, spawn.robot_radius_mm, tick)) {
        return RobotError::invalid_spawn;
    }
    return RobotSimulation{std::move(warehouse),
                           tick,
                           spawn.robot_spawn,
                           spawn.robot_radius_mm,
                           motion,
                           battery,
                           encoders,
                           spawn.seed};
}

Result<RobotSimulation, RobotError>
RobotSimulation::create_at(Warehouse warehouse, RobotStart start, MotionConfig motion,
                           BatteryConfig battery, EncoderEffects encoders) {
    if (!valid(motion, battery) || !valid_encoder_effects(encoders) ||
        !(std::abs(start.heading.value()) <= std::numbers::pi)) {
        return RobotError::invalid_configuration;
    }
    const auto radius = warehouse.snapshot(0).robot_radius_mm;
    if (warehouse.collides(start.position, radius, 0)) {
        return RobotError::invalid_spawn;
    }
    return RobotSimulation{
        std::move(warehouse), 0, start.position, radius, motion, battery, encoders, start.seed,
        start.heading};
}

RobotSimulation::RobotSimulation(Warehouse warehouse, std::uint64_t tick, world::PointMm spawn,
                                 std::int32_t radius, MotionConfig motion, BatteryConfig battery,
                                 EncoderEffects encoders, std::uint32_t seed, Radians heading)
    : warehouse_{std::move(warehouse)}, radius_mm_{radius}, motion_{motion}, battery_{battery},
      encoders_{motion.track_m, motion.encoder_step_m, encoders, seed},
      truth_{{Meters{spawn.x / mm_per_m}, Meters{spawn.y / mm_per_m}, heading}, {}},
      telemetry_{.world_tick = tick,
                 .estimated_pose = truth_.pose,
                 .energy_uj = battery.initial_uj,
                 .capacity_uj = battery.capacity_uj,
                 .low_battery = battery.initial_uj <= battery.low_threshold_uj,
                 .depleted = battery.initial_uj == 0,
                 .docked = warehouse_.charging_contact(point(truth_.pose), radius),
                 .sensors_powered = battery.initial_uj > 0} {}

void RobotSimulation::step(robot::Command command) noexcept {
    const auto proposed = prepare(command);
    commit(proposed, command.sensors_on, !proposed.world_held);
}

RobotSimulation::Step RobotSimulation::prepare(robot::Command command) const noexcept {
    const bool invalid = !std::isfinite(command.target.linear.value()) ||
                         !std::isfinite(command.target.angular.value());
    auto latched = telemetry_.emergency_latched;
    if (command.emergency_stop) {
        latched = true;
    } else if (command.reset_stop && !command.deadman && stopped(command.target)) {
        latched = false;
    }
    const bool hard_stop = latched || invalid;
    const auto commanded =
        !hard_stop && command.deadman ? bounded(command.target, motion_) : Twist{};

    auto applied = hard_stop ? Twist{} : accelerate(truth_.applied, commanded, motion_);
    auto next_pose = integrate(truth_.pose, applied);
    const auto start = point(truth_.pose);
    // Constant twist gives acceleration |v * omega| along the arc. Bound the chord error by a*T²/8.
    const auto clearance = std::abs(applied.linear.value() * applied.angular.value()) * step_s *
                           step_s * mm_per_m / 8.0;
    const bool blocked = warehouse_.sweep_collides(start, point(next_pose), radius_mm_ + clearance,
                                                   telemetry_.world_tick);
    if (blocked) {
        applied = {};
        next_pose = truth_.pose;
    }

    const bool power_limited =
        !stopped(applied) && telemetry_.energy_uj < energy_draw(applied, command.sensors_on);
    if (power_limited) {
        applied = {};
        next_pose = truth_.pose;
    }
    const bool world_held =
        (blocked || power_limited) &&
        warehouse_.sweep_collides(start, start, radius_mm_, telemetry_.world_tick);
    return {commanded, applied, next_pose,     clearance, invalid,
            latched,   blocked, power_limited, world_held};
}

void RobotSimulation::commit(const Step &step, bool sensors_on, bool advance_world) noexcept {
    telemetry_.emergency_latched = step.latched;
    telemetry_.invalid_command = step.invalid;
    telemetry_.commanded = step.command;
    telemetry_.world_held = !advance_world;
    telemetry_.collision_blocked = step.blocked || telemetry_.world_held;
    if (advance_world && telemetry_.world_tick < std::numeric_limits<std::uint64_t>::max()) {
        ++telemetry_.world_tick;
    }

    truth_ = {step.goal, step.motion};
    update_odometry(step.motion);
    telemetry_.docked =
        stopped(step.motion) && warehouse_.charging_contact(point(truth_.pose), radius_mm_);
    update_battery(energy_draw(step.motion, sensors_on), telemetry_.docked, step.power_limited);
    telemetry_.sensors_powered = sensors_on && !telemetry_.depleted;
    if (telemetry_.sample_tick < std::numeric_limits<std::uint64_t>::max()) {
        ++telemetry_.sample_tick;
    }
}

void RobotSimulation::update_odometry(Twist motion) noexcept {
    const auto center = motion.linear.value() * step_s;
    const auto half_turn = motion.angular.value() * step_s * motion_.track_m / 2.0;
    telemetry_.wheels = encoders_.sample(center - half_turn, center + half_turn);
    const auto left = telemetry_.wheels.left_delta.value();
    const auto right = telemetry_.wheels.right_delta.value();
    telemetry_.measured = {MetersPerSecond{(left + right) / (2.0 * step_s)},
                           RadiansPerSecond{(right - left) / (motion_.track_m * step_s)}};
    telemetry_.estimated_pose = integrate(telemetry_.estimated_pose, telemetry_.measured);
}

std::uint64_t RobotSimulation::energy_draw(Twist motion, bool sensors_on) const noexcept {
    const auto throttle = std::max(std::abs(motion.linear.value()) / motion_.max_linear_mps,
                                   std::abs(motion.angular.value()) / motion_.max_angular_radps);
    const auto drive = static_cast<std::uint64_t>(std::ceil(battery_.drive_mw * throttle));
    const auto power = battery_.idle_mw + drive + (sensors_on ? battery_.sensor_mw : 0U);
    return power * robot::step_ms;
}

void RobotSimulation::update_battery(std::uint64_t draw, bool docked, bool power_limited) noexcept {
    if (power_limited) {
        telemetry_.energy_uj = 0;
    }
    telemetry_.charging = docked && telemetry_.energy_uj < battery_.capacity_uj;
    if (docked) {
        const auto net = battery_.charge_mw * robot::step_ms - draw;
        telemetry_.energy_uj += std::min(net, battery_.capacity_uj - telemetry_.energy_uj);
    } else {
        telemetry_.energy_uj -= std::min(draw, telemetry_.energy_uj);
    }
    telemetry_.low_battery = telemetry_.energy_uj <= battery_.low_threshold_uj;
    telemetry_.depleted = telemetry_.energy_uj == 0;
}

} // namespace argus::simulation
