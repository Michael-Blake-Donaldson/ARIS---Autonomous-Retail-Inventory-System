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

} // namespace
