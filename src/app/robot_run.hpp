#pragma once

#include "argus/robot/control.hpp"

namespace argus::app {

inline void run_drive_probe(robot::MotionController &controller, std::uint32_t steps) noexcept {
    const robot::Command forward{.target = {MetersPerSecond{1.2}, robot::RadiansPerSecond{0.0}},
                                 .deadman = true};
    for (std::uint32_t step = 0; step < steps; ++step) {
        controller.step(forward);
    }
}

} // namespace argus::app
