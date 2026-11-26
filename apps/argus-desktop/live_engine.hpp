#pragma once

#include "application.hpp"
#include "operations_model.hpp"

#include "argus/navigation/navigator.hpp"
#include "argus/simulation/fleet.hpp"
#include "argus/simulation/observed_run.hpp"
#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"

#include <set>

namespace argus::desktop {

// Owns the observation-only data for rendering while the worker owns the live run.
struct ObservedFrame {
    explicit ObservedFrame(const LiveFrame &frame);
    [[nodiscard]] LiveFrame view() const noexcept;

    robot::Telemetry telemetry;
    mapping::Grid grid;
    std::optional<sensors::LidarScan> lidar;
    std::vector<mapping::Point> route;
    std::vector<mapping::Point> trace;
    std::size_t reads{};
    std::size_t unique{};
    std::optional<mapping::Estimate> estimate;
    std::optional<navigation::Progress> navigation;
    RunId run;
    bool archived{};
    std::uint64_t map_revision{};
    bool traffic_yield{};
    bool returning_to_berth{};
};

struct FleetAttachment {
    RobotId id;
    std::shared_ptr<simulation::FleetClock> clock;
};

class LiveEngine {
  public:
    [[nodiscard]] static Result<LiveEngine, app::Failure>
    create(simulation::Warehouse warehouse, std::uint64_t start_tick, bool shared_demo = false,
           std::optional<FleetAttachment> fleet = {});

    bool step(robot::Command manual, LiveAction action, bool armed, bool lidar_dropout = false);
    [[nodiscard]] const Layout &layout() const noexcept { return layout_; }
    [[nodiscard]] LiveFrame frame() const noexcept;
    [[nodiscard]] DiagnosticsFrame diagnostics() const;
    [[nodiscard]] simulation::ObservedRun borrow_run(
        std::function<bool(const navigation::Navigator &, const sensors::RfidScan *)> observer,
        std::function<bool()> should_continue);
    void observe_job(const navigation::Navigator &navigator, const sensors::RfidScan *scan);
    void finish_job(bool emergency);
    void traffic_yield(bool yielding) noexcept { traffic_yield_ = yielding; }
    void returning_to_berth() noexcept { returning_to_berth_ = true; }

  private:
    LiveEngine(simulation::Warehouse warehouse, simulation::RobotSimulation robot,
               simulation::SensorRig sensors, mapping::Mapper mapper, Layout layout,
               std::optional<FleetAttachment> fleet);
    void capture(bool lidar_dropout);
    void restart();
    void update_trace();
    void update_route(const navigation::Navigator *navigator);
    void count_reads(const sensors::RfidScan &radio);
    [[nodiscard]] bool advance(robot::Command command);

    simulation::Warehouse warehouse_;
    simulation::RobotSimulation initial_;
    simulation::RobotSimulation robot_;
    simulation::SensorRig sensors_;
    mapping::Mapper mapper_;
    Layout layout_;
    std::optional<navigation::Navigator> navigator_;
    std::optional<navigation::Progress> job_progress_;
    std::optional<sensors::LidarScan> lidar_;
    std::vector<mapping::Point> route_;
    std::vector<mapping::Point> trace_;
    std::set<std::string, std::less<>> epcs_;
    std::size_t reads_{};
    RunId id_;
    std::optional<FleetAttachment> fleet_;
    bool traffic_yield_{};
    bool returning_to_berth_{};
};

} // namespace argus::desktop
