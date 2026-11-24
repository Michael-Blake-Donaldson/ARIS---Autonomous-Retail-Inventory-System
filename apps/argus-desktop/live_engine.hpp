#pragma once

#include "application.hpp"
#include "operations_model.hpp"

#include "argus/navigation/navigator.hpp"
#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"

#include <set>

namespace argus::desktop {

class LiveEngine {
  public:
    [[nodiscard]] static Result<LiveEngine, app::Failure> create(simulation::Warehouse warehouse,
                                                                 std::uint64_t start_tick);

    void step(robot::Command manual, LiveAction action, bool armed);
    [[nodiscard]] const Layout &layout() const noexcept { return layout_; }
    [[nodiscard]] LiveFrame frame() const noexcept;
    [[nodiscard]] DiagnosticsFrame diagnostics() const;

  private:
    LiveEngine(simulation::Warehouse warehouse, simulation::RobotSimulation robot,
               simulation::SensorRig sensors, mapping::Mapper mapper, Layout layout);
    void capture();
    void restart();

    simulation::Warehouse warehouse_;
    simulation::RobotSimulation initial_;
    simulation::RobotSimulation robot_;
    simulation::SensorRig sensors_;
    mapping::Mapper mapper_;
    Layout layout_;
    std::optional<navigation::Navigator> navigator_;
    std::optional<sensors::LidarScan> lidar_;
    std::vector<mapping::Point> route_;
    std::vector<mapping::Point> trace_;
    std::set<std::string, std::less<>> epcs_;
    std::size_t reads_{};
};

} // namespace argus::desktop
