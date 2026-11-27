#pragma once

#include "argus/robot/control.hpp"

namespace argus::app {

inline constexpr robot::Command probe_command{
    .target = {MetersPerSecond{1.2}, robot::RadiansPerSecond{0.0}}, .deadman = true};

inline void run_drive_probe(robot::MotionController &controller, std::uint32_t steps) noexcept {
    for (std::uint32_t step = 0; step < steps; ++step) {
        controller.step(probe_command);
    }
}

} // namespace argus::app
