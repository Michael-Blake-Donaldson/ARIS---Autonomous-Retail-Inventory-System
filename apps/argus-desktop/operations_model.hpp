#pragma once

#include "argus/navigation/navigator.hpp"
#include "argus/robot/control.hpp"
#include "argus/sensors/observations.hpp"
#include "argus/world/snapshot.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace argus::desktop {

enum class Panel {
    overview,
    live_map,
    find_product,
    inventory_audit,
    missions,
    history,
    settings,
    diagnostics
};
enum class CameraMode { orbit, plan, follow };
[[nodiscard]] inline CameraMode view_camera_mode(CameraMode mode, bool archived) noexcept {
    return archived && mode == CameraMode::follow ? CameraMode::plan : mode;
}
enum class LiveAction { none, explore, pause, emergency_stop, reset_stop, restart };
enum class DriveKey { forward, reverse, left, right };

class DriveInput {
  public:
    void deadman(bool pressed, bool fresh) noexcept;
    void direction(DriveKey key, bool pressed) noexcept;
    void disarm() noexcept;
    void emergency() noexcept;
    [[nodiscard]] bool armed() const noexcept { return held_; }
    [[nodiscard]] robot::Command command() noexcept;

  private:
    std::array<bool, 4> directions_{};
    bool held_{};
    bool stop_{};
};

[[nodiscard]] LiveAction prioritize(LiveAction pending, LiveAction requested) noexcept;

inline constexpr std::array<std::string_view, 8> panel_names{
    "Overview", "Live Map",          "Find Product", "Inventory Audit",
    "Missions", "Lineage / History", "Settings",     "Diagnostics"};

struct Overlays {
    bool lidar{true};
    bool occupancy{};
    bool route{true};
    bool trace{true};
    bool antenna{};
    bool uncertainty{true};
};

struct Camera {
    CameraMode mode{CameraMode::orbit};
    double yaw{-30.0};
    double pitch{46.0};
    double zoom{1.0};
    void orbit(double x, double y) noexcept;
    void scale(double scroll) noexcept;
};

struct Layout {
    ScenarioId scenario;
    world::RectMm floor;
    std::int32_t radius_mm;
    std::vector<world::SolidView> solids;
    std::vector<world::ZoneView> zones;
};

enum class ScanState { powered_off, waiting, fresh, stale };
enum class PoseState { waiting, current, uncertain };

struct MapStatus {
    ScanState scan;
    std::optional<std::uint64_t> scan_age_ms;
    PoseState pose;
};

[[nodiscard]] MapStatus map_status(robot::Telemetry telemetry, const sensors::LidarScan *scan,
                                   const std::optional<mapping::Estimate> &pose) noexcept;
[[nodiscard]] const char *describe(navigation::Reason reason) noexcept;
[[nodiscard]] const char *describe(navigation::State state) noexcept;
[[nodiscard]] const char *describe(navigation::ReplanCause cause) noexcept;

struct LiveFrame {
    robot::Telemetry telemetry;
    const mapping::Grid &grid;
    const sensors::LidarScan *lidar;
    std::span<const mapping::Point> route;
    std::span<const mapping::Point> trace;
    std::size_t rfid_reads;
    std::size_t unique_epcs;
    MapStatus map;
    std::optional<mapping::Estimate> estimate;
    std::optional<navigation::Progress> navigation;
    RunId run{0};
    bool archived{};
    std::uint64_t map_revision{};
    [[nodiscard]] bool exploring() const noexcept {
        return navigation && navigation->state != navigation::State::finished;
    }
};

struct DiagnosticsFrame {
    robot::Pose true_pose;
    std::vector<world::ActorView> actors;
};

[[nodiscard]] inline bool truth_visible(Panel panel, bool enabled) noexcept {
    return panel == Panel::diagnostics && enabled;
}

} // namespace argus::desktop
