#pragma once

#include "argus/navigation/navigator.hpp"
#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"

#include <functional>

namespace argus::simulation {

// A synchronous job borrows an existing robot, sensor RNG, and mapper from one owner.
struct ObservedRun {
    RobotSimulation &robot;
    SensorRig &sensors;
    mapping::Mapper &mapper;
    std::optional<sensors::LidarScan> &lidar;
    std::function<bool(const navigation::Navigator &, const sensors::RfidScan *)> on_step{};
    Meters radius;
    std::function<bool(robot::Command)> on_motion{};
};

struct RunInterrupted {};

[[nodiscard]] inline std::optional<sensors::RfidScan>
step_observed(ObservedRun &run, robot::Command command, const navigation::Navigator &navigator) {
    if (run.on_motion) {
        if (!run.on_motion(command)) {
            throw RunInterrupted{};
        }
    } else {
        run.robot.step(command);
    }
    run.sensors.capture(run.robot);
    std::optional<sensors::RfidScan> radio;
    while (auto reading = run.sensors.pop()) {
        const auto accepted = run.mapper.ingest(*reading);
        if (auto *scan = std::get_if<sensors::LidarScan>(&*reading);
            scan && accepted == mapping::Update::accepted) {
            run.lidar = std::move(*scan);
        } else if (auto *rfid = std::get_if<sensors::RfidScan>(&*reading)) {
            radio = std::move(*rfid);
        }
    }
    if (run.on_step && !run.on_step(navigator, radio ? &*radio : nullptr)) {
        throw RunInterrupted{};
    }
    return radio;
}

} // namespace argus::simulation
