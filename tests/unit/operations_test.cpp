#include "operations_model.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace {

using namespace argus::desktop;

TEST(OperationsModel, FocusLossClearsMotionAndHeldRepeatCannotRearm) {
    DriveInput input;
    input.direction(DriveKey::forward, true);
    EXPECT_FALSE(input.command().deadman);
    input.deadman(true, true);
    EXPECT_DOUBLE_EQ(input.command().target.linear.value(), 1.2);
    input.disarm();
    input.deadman(true, false);
    EXPECT_FALSE(input.command().deadman);
    EXPECT_DOUBLE_EQ(input.command().target.linear.value(), 0.0);
}

TEST(OperationsModel, EmergencyStopSurvivesFocusAndOtherPendingCommands) {
    DriveInput input;
    input.deadman(true, true);
    input.direction(DriveKey::left, true);
    input.emergency();
    input.disarm();
    input.deadman(true, true);
    const auto emergency = input.command();
    EXPECT_TRUE(emergency.emergency_stop);
    EXPECT_FALSE(emergency.deadman);
    EXPECT_DOUBLE_EQ(emergency.target.angular.value(), 0.0);
    EXPECT_FALSE(input.command().emergency_stop);
    EXPECT_EQ(prioritize(LiveAction::emergency_stop, LiveAction::pause),
              LiveAction::emergency_stop);
    EXPECT_EQ(prioritize(LiveAction::explore, LiveAction::emergency_stop),
              LiveAction::emergency_stop);
}

TEST(OperationsModel, CameraBoundsAndTruthVisibilityHoldAcrossAllPanels) {
    Camera camera;
    camera.orbit(10.0, 10000.0);
    EXPECT_DOUBLE_EQ(camera.pitch, 82.0);
    camera.scale(100.0);
    EXPECT_LE(camera.zoom, 4.0);
    camera.scale(std::numeric_limits<double>::quiet_NaN());
    EXPECT_GE(camera.zoom, 0.48);
    const auto yaw = camera.yaw;
    camera.mode = CameraMode::plan;
    camera.orbit(20.0, -20.0);
    EXPECT_DOUBLE_EQ(camera.yaw, yaw);
    for (std::size_t index = 0; index < panel_names.size(); ++index) {
        const auto panel = static_cast<Panel>(index);
        EXPECT_FALSE(truth_visible(panel, false));
        EXPECT_EQ(truth_visible(panel, true), panel == Panel::diagnostics);
    }
}

TEST(OperationsModel, MapScanStatusUsesTheNavigationAgeBoundary) {
    using namespace argus;
    robot::Telemetry telemetry{.sample_tick = 30, .sensors_powered = true};
    sensors::LidarScan scan{{sensors::SensorId::lidar, 27, 1350, 27, {}}, 0, 1, 0, {}};
    EXPECT_EQ(map_status(telemetry, nullptr, {}).scan, ScanState::waiting);
    const auto fresh = map_status(telemetry, &scan, {});
    EXPECT_EQ(fresh.scan, ScanState::fresh);
    EXPECT_EQ(fresh.scan_age_ms, 150U);
    ++telemetry.sample_tick;
    const auto stale = map_status(telemetry, &scan, {});
    EXPECT_EQ(stale.scan, ScanState::stale);
    EXPECT_EQ(stale.scan_age_ms, 200U);

    scan.header.sample_tick = 32;
    const auto future = map_status(telemetry, &scan, {});
    EXPECT_EQ(future.scan, ScanState::waiting);
    EXPECT_FALSE(future.scan_age_ms);
    telemetry.sensors_powered = false;
    EXPECT_EQ(map_status(telemetry, &scan, {}).scan, ScanState::powered_off);

    telemetry.sample_tick = std::numeric_limits<std::uint64_t>::max();
    telemetry.sensors_powered = true;
    EXPECT_EQ(map_status(telemetry, &scan, {}).scan_age_ms, telemetry.sample_tick);
}

TEST(OperationsModel, MapLocalizationStatusUsesOnlyTheMapperEstimate) {
    using namespace argus;
    const robot::Telemetry telemetry{.sample_tick = 30};
    mapping::Estimate pose{{}, 0.3, 0.4, 28, true};
    EXPECT_EQ(map_status(telemetry, nullptr, {}).pose, PoseState::waiting);
    EXPECT_EQ(map_status(telemetry, nullptr, pose).pose, PoseState::current);
    pose.sample_tick = 27;
    EXPECT_EQ(map_status(telemetry, nullptr, pose).pose, PoseState::uncertain);
    pose.sample_tick = 31;
    EXPECT_EQ(map_status(telemetry, nullptr, pose).pose, PoseState::uncertain);
    pose.sample_tick = 30;
    pose.reliable = false;
    EXPECT_EQ(map_status(telemetry, nullptr, pose).pose, PoseState::uncertain);
}

} // namespace
