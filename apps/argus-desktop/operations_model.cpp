#include "operations_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace argus::desktop {

void DriveInput::deadman(bool pressed, bool fresh) noexcept {
    if (pressed && stop_) {
        return;
    }
    if (!pressed || fresh) {
        held_ = pressed;
    }
}

void DriveInput::direction(DriveKey key, bool pressed) noexcept {
    const auto index = static_cast<std::size_t>(key);
    if (index < directions_.size()) {
        directions_[index] = pressed;
    }
}

void DriveInput::disarm() noexcept {
    held_ = false;
    directions_.fill(false);
}

void DriveInput::emergency() noexcept {
    stop_ = true;
    disarm();
}

robot::Command DriveInput::command() noexcept {
    const auto axis = [&](DriveKey positive, DriveKey negative) {
        return held_ ? static_cast<int>(directions_[static_cast<std::size_t>(positive)]) -
                           static_cast<int>(directions_[static_cast<std::size_t>(negative)])
                     : 0;
    };
    return {.target = {MetersPerSecond{axis(DriveKey::forward, DriveKey::reverse) * 1.2},
                       robot::RadiansPerSecond{axis(DriveKey::left, DriveKey::right) * 1.6}},
            .deadman = held_,
            .emergency_stop = std::exchange(stop_, false)};
}

LiveAction prioritize(LiveAction pending, LiveAction requested) noexcept {
    if (pending == LiveAction::emergency_stop || requested == LiveAction::none) {
        return pending;
    }
    return requested;
}

void Camera::orbit(double x, double y) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || mode != CameraMode::orbit) {
        return;
    }
    yaw = std::remainder(yaw + x * 0.4, 360.0);
    pitch = std::clamp(pitch + y * 0.35, 15.0, 82.0);
}

void Camera::scale(double scroll) noexcept {
    if (std::isfinite(scroll)) {
        zoom = std::clamp(zoom * std::pow(1.10, std::clamp(scroll, -10.0, 10.0)), 0.48, 4.0);
    }
}

MapStatus map_status(robot::Telemetry telemetry, const sensors::LidarScan *scan,
                     const std::optional<mapping::Estimate> &pose) noexcept {
    const auto tick = telemetry.sample_tick;
    const bool aligned = pose && pose->sample_tick != 0 && pose->sample_tick <= tick &&
                         tick - pose->sample_tick <= navigation::max_pose_age_ticks;
    const auto pose_state = !pose                       ? PoseState::waiting
                            : aligned && pose->reliable ? PoseState::current
                                                        : PoseState::uncertain;
    if (!telemetry.sensors_powered) {
        return {ScanState::powered_off, {}, pose_state};
    }
    if (!scan || scan->header.sample_tick == 0 || scan->header.sample_tick > tick) {
        return {ScanState::waiting, {}, pose_state};
    }
    const auto age = tick - scan->header.sample_tick;
    constexpr auto max_ticks = std::numeric_limits<std::uint64_t>::max() / robot::step_ms;
    const auto millis =
        age > max_ticks ? std::numeric_limits<std::uint64_t>::max() : age * robot::step_ms;
    return {age <= navigation::max_scan_age_ticks ? ScanState::fresh : ScanState::stale, millis,
            pose_state};
}

const char *describe(navigation::State state) noexcept {
    switch (state) {
    case navigation::State::waiting:
        return "Holding";
    case navigation::State::exploring:
        return "Exploring";
    case navigation::State::yielding:
        return "Yielding";
    case navigation::State::returning:
        return "Returning";
    case navigation::State::finished:
        return "Run ended";
    }
    return "Unavailable";
}

const char *describe(navigation::Reason reason) noexcept {
    switch (reason) {
    case navigation::Reason::none:
        return "";
    case navigation::Reason::stale_scan:
        return "Waiting for a fresh LiDAR scan";
    case navigation::Reason::uncertain_pose:
        return "Localization confidence is too low";
    case navigation::Reason::emergency_stop:
        return "Emergency stop latched";
    case navigation::Reason::depleted:
        return "Battery depleted";
    case navigation::Reason::invalid_configuration:
        return "Map configuration changed";
    case navigation::Reason::unsafe_position:
        return "No clear route from the estimated pose";
    case navigation::Reason::obstacle:
        return "Obstacle in the planned path";
    case navigation::Reason::no_frontiers:
        return "No unexplored map frontiers";
    case navigation::Reason::unreachable_frontiers:
        return "Unexplored areas are unreachable";
    case navigation::Reason::home_unreachable:
        return "Launch point unreachable";
    case navigation::Reason::dock_unreachable:
        return "Charging dock unreachable";
    case navigation::Reason::home_reached:
        return "Launch point reached";
    case navigation::Reason::docked:
        return "Charging dock reached";
    case navigation::Reason::destination_reached:
        return "Destination reached";
    case navigation::Reason::destination_unreachable:
        return "Destination unreachable";
    }
    return "Decision unavailable";
}

const char *describe(navigation::ReplanCause cause) noexcept {
    switch (cause) {
    case navigation::ReplanCause::obstacle:
        return "Obstacle in path";
    case navigation::ReplanCause::unsafe_route:
        return "Route lost clearance";
    case navigation::ReplanCause::waypoint_timeout:
        return "Waypoint timed out";
    }
    return "Cause unavailable";
}

} // namespace argus::desktop
