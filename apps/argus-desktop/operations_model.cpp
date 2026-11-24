#include "operations_model.hpp"

#include <algorithm>
#include <cmath>
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

} // namespace argus::desktop
