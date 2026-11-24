#pragma once

#include "argus/mapping/mapper.hpp"
#include "argus/navigation/planner.hpp"

#include <string_view>

namespace argus::navigation {

struct Settings {
    Meters radius{0.26};
    std::optional<Point> dock{};
    std::uint64_t reserve_uj{250000000};
    std::uint64_t energy_per_meter_uj{80000000};
};

struct Destination {
    Point point;
    robot::Radians heading;
};

enum class State { waiting, exploring, yielding, returning, finished };
enum class Reason {
    none,
    stale_scan,
    uncertain_pose,
    emergency_stop,
    depleted,
    invalid_configuration,
    unsafe_position,
    obstacle,
    no_frontiers,
    unreachable_frontiers,
    home_unreachable,
    dock_unreachable,
    home_reached,
    docked,
    destination_reached,
    destination_unreachable
};

struct Progress {
    State state{State::waiting};
    Reason reason{Reason::none};
    std::uint32_t plans{};
    std::uint32_t yields{};
    std::uint32_t moving_yields{};
    std::uint32_t replans{};
    std::uint32_t stuck{};
};

[[nodiscard]] std::string_view label(State state) noexcept;
[[nodiscard]] std::string_view label(Reason reason) noexcept;

class Navigator {
  public:
    [[nodiscard]] static std::optional<Navigator>
    create(const mapping::Grid &grid, Settings settings = {}, std::span<const NoGo> zones = {});
    [[nodiscard]] robot::Command update(const mapping::Mapper &mapper, robot::Telemetry telemetry,
                                        const sensors::LidarScan *scan);
    [[nodiscard]] bool set_destination(Destination destination) noexcept;
    [[nodiscard]] bool request_return() noexcept;
    [[nodiscard]] Progress progress() const noexcept { return progress_; }
    [[nodiscard]] std::span<const Cell> route() const noexcept {
        return route_ ? std::span<const Cell>{route_->cells} : std::span<const Cell>{};
    }

  private:
    struct Detection {
        Point position;
        Point velocity;
        bool confirmed{};
    };

    Navigator(mapping::Spec spec, Settings settings, std::span<const NoGo> zones);
    robot::Command stop(State state, Reason reason) noexcept;
    bool observe(const sensors::LidarScan &scan);
    [[nodiscard]] bool blocked(robot::Pose pose, double speed, bool &moving) const noexcept;
    [[nodiscard]] std::uint32_t budget(std::uint64_t energy) const noexcept;
    [[nodiscard]] robot::Command follow(const mapping::Grid &grid, Cell start,
                                        const robot::Telemetry &telemetry, robot::Pose pose);

    mapping::Spec spec_;
    Settings settings_;
    std::vector<NoGo> zones_;
    Progress progress_;
    std::optional<Point> home_;
    std::optional<Destination> destination_;
    std::optional<Costmap> costmap_;
    std::optional<Route> route_;
    std::vector<Cell> excluded_;
    std::vector<Point> returns_;
    std::vector<Detection> detections_;
    std::size_t waypoint_{};
    std::uint64_t scan_tick_{};
    std::uint64_t control_tick_{};
    std::uint64_t map_tick_{};
    std::uint64_t blocked_since_{};
    std::uint64_t last_obstacle_tick_{};
    std::uint64_t waypoint_since_{};
    std::uint64_t dock_arrival_{};
    std::uint64_t return_wait_since_{};
    std::uint64_t retry_after_{};
    std::uint32_t return_retries_{};
    std::uint32_t destination_retries_{};
    bool returning_{};
};

} // namespace argus::navigation
