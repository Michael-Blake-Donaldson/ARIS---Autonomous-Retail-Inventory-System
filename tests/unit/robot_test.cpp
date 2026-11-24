#include "argus/core/random.hpp"
#include "argus/simulation/robot.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace argus;
using namespace argus::robot;
using namespace argus::simulation;

constexpr std::string_view empty_layout = R"({
    "schemaVersion":"1.0.0","scenarioId":501,"seed":5489,"sizeMm":[50000,40000],
    "robot":{"spawnMm":[25000,15000],"radiusMm":350},"fixtures":[],"actors":[],"items":[],
    "zones":[
        {"id":"aisle","kind":"aisle","boundsMm":[6000,7000,10000,6000]},
        {"id":"door","kind":"doorway","boundsMm":[100,900,1000,150]},
        {"id":"staging","kind":"receiving","boundsMm":[6000,2000,8000,3000]},
        {"id":"dock","kind":"dock","boundsMm":[2000,2000,3000,3000]},
        {"id":"charger","kind":"charging","boundsMm":[2600,2500,1500,1500]}
    ]
})";

std::string replace(std::string_view from, std::string_view to) {
    std::string json{empty_layout};
    return json.replace(json.find(from), from.size(), to);
}

Warehouse load(std::string_view json = empty_layout) {
    return std::get<Warehouse>(Warehouse::from_json(json));
}

MotionConfig instant_motion() {
    MotionConfig config;
    config.max_linear_mps = 1.0;
    config.max_angular_radps = 2.0;
    config.linear_acceleration_mps2 = 50.0;
    config.angular_acceleration_radps2 = 100.0;
    return config;
}

RobotSimulation make(Warehouse warehouse = load(), MotionConfig config = {},
                     BatteryConfig battery = {}) {
    return std::get<RobotSimulation>(
        RobotSimulation::create(std::move(warehouse), 0, config, battery));
}

Command drive(double linear, double angular = 0.0) {
    return {.target = {MetersPerSecond{linear}, RadiansPerSecond{angular}}, .deadman = true};
}

template <typename T>
concept ExposesTruth = requires(const T &control) { control.debug_truth(); };
static_assert(!ExposesTruth<MotionController>);
static_assert(ExposesTruth<RobotSimulation>);

TEST(RobotMotion, FollowsAnalyticalStraightSpinAndArcTrajectories) {
    auto straight = make(load(), instant_motion());
    auto spin = make(load(), instant_motion());
    auto arc = make(load(), instant_motion());
    for (int step = 0; step < 20; ++step) {
        straight.step(drive(1.0));
        spin.step(drive(0.0, 1.0));
        arc.step(drive(1.0, 1.0));
    }
    EXPECT_NEAR(straight.debug_truth().pose.x.value(), 26.0, 1e-10);
    EXPECT_NEAR(straight.debug_truth().pose.y.value(), 15.0, 1e-10);
    EXPECT_NEAR(spin.debug_truth().pose.x.value(), 25.0, 1e-10);
    EXPECT_NEAR(spin.debug_truth().pose.heading.value(), 1.0, 1e-10);
    EXPECT_NEAR(arc.debug_truth().pose.x.value(), 25.0 + std::sin(1.0), 1e-10);
    EXPECT_NEAR(arc.debug_truth().pose.y.value(), 16.0 - std::cos(1.0), 1e-10);
    EXPECT_NEAR(arc.debug_truth().pose.heading.value(), 1.0, 1e-10);
}

TEST(RobotMotion, EncoderQuantizationEstimatesWithoutCopyingTruePose) {
    auto config = instant_motion();
    config.encoder_step_m = 0.007;
    auto sim = make(load(), config);
    MotionController &controller = sim;
    controller.step(drive(1.0, 1.0));
    const auto read = controller.telemetry();
    EXPECT_NE(read.estimated_pose.x.value(), sim.debug_truth().pose.x.value());
    EXPECT_NE(read.measured.angular.value(), sim.debug_truth().applied.angular.value());
    EXPECT_NEAR(read.estimated_pose.x.value(), sim.debug_truth().pose.x.value(), 0.008);
    EXPECT_NEAR(read.estimated_pose.heading.value(), sim.debug_truth().pose.heading.value(), 0.04);
}

TEST(RobotMotion, SaturatesCommandsAndUsesConfiguredSlewAndDeadmanBraking) {
    const MotionConfig limits;
    auto sim = make();
    auto last = sim.debug_truth().applied;
    for (int step = 0; step < 60; ++step) {
        sim.step(drive(100.0, -100.0));
        const auto current = sim.debug_truth().applied;
        EXPECT_DOUBLE_EQ(sim.telemetry().commanded.linear.value(), limits.max_linear_mps);
        EXPECT_DOUBLE_EQ(sim.telemetry().commanded.angular.value(), -limits.max_angular_radps);
        EXPECT_LE(std::abs(current.linear.value()), limits.max_linear_mps);
        EXPECT_LE(std::abs(current.angular.value()), limits.max_angular_radps);
        EXPECT_LE(std::abs(current.linear.value() - last.linear.value()), 0.040000001);
        EXPECT_LE(std::abs(current.angular.value() - last.angular.value()), 0.150000001);
        last = current;
    }
    sim.step({});
    EXPECT_DOUBLE_EQ(sim.telemetry().commanded.linear.value(), 0.0);
    EXPECT_NEAR(sim.debug_truth().applied.linear.value(), 1.11, 1e-9);
    EXPECT_NEAR(sim.debug_truth().applied.angular.value(), -1.75, 1e-9);
    for (int step = 0; step < 15; ++step) {
        sim.step({});
    }
    EXPECT_DOUBLE_EQ(sim.debug_truth().applied.linear.value(), 0.0);
    EXPECT_DOUBLE_EQ(sim.debug_truth().applied.angular.value(), 0.0);
}

TEST(RobotMotion, EmergencyStopLatchesAndRequiresReleasedNeutralControlToReset) {
    auto sim = make();
    sim.step(drive(1.0));
    const auto before = sim.debug_truth().pose;
    auto stop = drive(1.0);
    stop.emergency_stop = true;
    stop.reset_stop = true;
    sim.step(stop);
    EXPECT_TRUE(sim.telemetry().emergency_latched);
    EXPECT_DOUBLE_EQ(sim.debug_truth().pose.x.value(), before.x.value());
    auto reset_while_enabled = drive(0.0);
    reset_while_enabled.reset_stop = true;
    sim.step(reset_while_enabled);
    EXPECT_TRUE(sim.telemetry().emergency_latched);
    sim.step(drive(1.0));
    EXPECT_DOUBLE_EQ(sim.debug_truth().pose.x.value(), before.x.value());
    sim.step(Command{.reset_stop = true});
    EXPECT_FALSE(sim.telemetry().emergency_latched);
    EXPECT_DOUBLE_EQ(sim.debug_truth().pose.x.value(), before.x.value());
    sim.step(drive(1.0));
    EXPECT_GT(sim.debug_truth().pose.x.value(), before.x.value());
    const auto moving = sim.debug_truth().pose;
    sim.step(drive(std::numeric_limits<double>::infinity()));
    EXPECT_TRUE(sim.telemetry().invalid_command);
    EXPECT_DOUBLE_EQ(sim.debug_truth().pose.x.value(), moving.x.value());
}

TEST(RobotCollision, BlocksAOneMillimeterWallAtMaximumSpeed) {
    const auto world =
        load(replace("\"fixtures\":[]",
                     R"("fixtures":[{"id":"wall","kind":"wall","boundsMm":[25500,0,1,40000]}])"));
    auto config = instant_motion();
    config.max_linear_mps = 5.0;
    auto sim = make(world, config);
    for (int step = 0; step < 20; ++step) {
        sim.step(drive(5.0));
        EXPECT_LT(sim.debug_truth().pose.x.value() + 0.35, 25.5);
    }
    EXPECT_TRUE(sim.telemetry().collision_blocked);
    EXPECT_DOUBLE_EQ(sim.debug_truth().applied.linear.value(), 0.0);
    EXPECT_DOUBLE_EQ(sim.debug_truth().pose.x.value(), 25.125);
    EXPECT_FALSE(sim.telemetry().world_held);
}

TEST(RobotCollision, HoldsAnUnsafeActorUpdateAndAllowsAnEscape) {
    const auto world = load(replace(
        "\"actors\":[]",
        R"("actors":[{"id":"staging-cart","kind":"temporary-obstruction","startMm":[26080,14800],"endMm":[26280,14800],"sizeMm":[400,600],"legTicks":10,"activeTicks":[5,100]}])"));
    auto config = instant_motion();
    config.max_linear_mps = 5.0;
    auto sim = make(world, config);
    for (int step = 0; step < 4; ++step) {
        sim.step(drive(5.0));
    }
    ASSERT_EQ(sim.telemetry().world_tick, 4U);
    const auto before = sim.debug_truth().pose.x.value();
    sim.step(drive(5.0));
    EXPECT_TRUE(sim.telemetry().world_held);
    EXPECT_EQ(sim.telemetry().world_tick, 4U);
    EXPECT_DOUBLE_EQ(sim.debug_truth().pose.x.value(), before);
    sim.step(drive(-5.0));
    EXPECT_FALSE(sim.telemetry().world_held);
    EXPECT_EQ(sim.telemetry().world_tick, 5U);
    EXPECT_LT(sim.debug_truth().pose.x.value(), before);
}

TEST(RobotCollision, BoundedRandomizedCommandsStayOutsideShelvesAndWalls) {
    const auto warehouse = load(replace(
        "\"fixtures\":[]",
        R"("fixtures":[{"id":"wall","kind":"wall","boundsMm":[25500,0,1,40000]},{"id":"shelf","kind":"rack","boundsMm":[20700,13500,1500,3000],"aisle":"aisle","shelfHeightsMm":[500]}])"));
    auto config = instant_motion();
    config.max_linear_mps = 5.0;
    config.max_angular_radps = 10.0;
    auto sim = make(warehouse, config);
    Random random{5743};
    int blocks = 0;
    for (int leg = 0; leg < 70; ++leg) {
        const double linear = (static_cast<double>(random.next() % 2001U) - 1000.0) / 150.0;
        const double angular = (static_cast<double>(random.next() % 2001U) - 1000.0) / 80.0;
        for (int step = 0; step < 24; ++step) {
            sim.step(drive(linear, angular));
            const auto pose = sim.debug_truth().pose;
            EXPECT_LT(pose.x.value() + 0.35, 25.5);
            const world::PointMm rounded{
                static_cast<std::int32_t>(std::lround(pose.x.value() * 1000.0)),
                static_cast<std::int32_t>(std::lround(pose.y.value() * 1000.0))};
            EXPECT_FALSE(warehouse.collides(rounded, 349, sim.telemetry().world_tick));
            if (sim.telemetry().collision_blocked) {
                ++blocks;
            }
        }
    }
    EXPECT_GT(blocks, 0);
}

TEST(RobotBattery, ConservesIntegerEnergyAndStopsAtExhaustion) {
    const BatteryConfig config{1000, 900, 500, 3, 5, 2, 9};
    auto sim = make(load(), instant_motion(), config);
    sim.step({});
    EXPECT_EQ(sim.telemetry().energy_uj, 650U);
    EXPECT_FALSE(sim.telemetry().low_battery);
    sim.step(Command{.sensors_on = false});
    EXPECT_EQ(sim.telemetry().energy_uj, 500U);
    EXPECT_TRUE(sim.telemetry().low_battery);
    sim.step(drive(1.0));
    EXPECT_DOUBLE_EQ(sim.debug_truth().pose.x.value(), 25.05);
    EXPECT_TRUE(sim.telemetry().depleted);
    sim.step(drive(1.0));
    EXPECT_DOUBLE_EQ(sim.debug_truth().pose.x.value(), 25.05);
    EXPECT_DOUBLE_EQ(sim.debug_truth().applied.linear.value(), 0.0);
    EXPECT_EQ(sim.telemetry().energy_uj, 0U);
    auto underpowered = make(load(), instant_motion(), BatteryConfig{1000, 499, 500, 3, 5, 2, 9});
    underpowered.step(drive(1.0));
    EXPECT_DOUBLE_EQ(underpowered.debug_truth().pose.x.value(), 25.0);
    EXPECT_EQ(underpowered.telemetry().energy_uj, 0U);
}

TEST(RobotBattery, ChargesAtContactIncludingEmptyPackAndClampsAtCapacity) {
    const auto world = load(replace("[25000,15000]", "[3300,3200]"));
    auto sim = make(world, instant_motion(), BatteryConfig{450, 0, 150, 3, 5, 2, 9});
    EXPECT_TRUE(sim.telemetry().docked);
    sim.step({});
    EXPECT_TRUE(sim.telemetry().charging);
    EXPECT_EQ(sim.telemetry().energy_uj, 200U);
    EXPECT_FALSE(sim.telemetry().depleted);
    sim.step({});
    EXPECT_EQ(sim.telemetry().energy_uj, 400U);
    sim.step({});
    sim.step({});
    EXPECT_EQ(sim.telemetry().energy_uj, 450U);
    EXPECT_FALSE(sim.telemetry().charging);
}

TEST(RobotConfiguration, RejectsNonFiniteMotionAndInconsistentBatteryLimits) {
    auto config = instant_motion();
    config.track_m = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(std::get<RobotError>(RobotSimulation::create(load(), 0, config)),
              RobotError::invalid_configuration);
    BatteryConfig battery;
    battery.charge_mw = 1;
    EXPECT_EQ(std::get<RobotError>(RobotSimulation::create(load(), 0, {}, battery)),
              RobotError::invalid_configuration);
}

TEST(RobotMotion, EncoderEffectsRepeatBySeedAndExposeUncertainty) {
    auto first = std::get<RobotSimulation>(
        RobotSimulation::create(load(), 50, instant_motion(), {}, warehouse_encoder_effects));
    auto replay = std::get<RobotSimulation>(
        RobotSimulation::create(load(), 50, instant_motion(), {}, warehouse_encoder_effects));
    auto different = std::get<RobotSimulation>(
        RobotSimulation::create(load(replace("\"seed\":5489", "\"seed\":2137")), 50,
                                instant_motion(), {}, warehouse_encoder_effects));
    bool differs = false;
    for (int step = 0; step < 80; ++step) {
        first.step(drive(1.0, 0.2));
        replay.step(drive(1.0, 0.2));
        different.step(drive(1.0, 0.2));
        EXPECT_EQ(first.telemetry().wheels, replay.telemetry().wheels);
        differs = differs || first.telemetry().wheels != different.telemetry().wheels;
    }
    EXPECT_TRUE(differs);
    EXPECT_EQ(first.telemetry().sample_tick, 80U);
    EXPECT_EQ(first.telemetry().world_tick, 130U);
    EXPECT_GT(first.telemetry().wheels.distance_error_m, 0.1);
    EXPECT_GT(first.telemetry().wheels.heading_error_rad, 0.1);
    EXPECT_NE(first.telemetry().estimated_pose.x.value(), first.debug_truth().pose.x.value());
}

TEST(RobotMotion, EncoderSlipChangesMeasurementsWithoutChangingMotion) {
    const EncoderEffects slip{0.0, 0.0, 1.0, 0.6};
    auto slipping =
        std::get<RobotSimulation>(RobotSimulation::create(load(), 0, instant_motion(), {}, slip));
    for (int step = 0; step < 20; ++step) {
        slipping.step(drive(1.0));
    }
    EXPECT_NEAR(slipping.debug_truth().pose.x.value(), 26.0, 1e-9);
    EXPECT_NEAR(slipping.telemetry().estimated_pose.x.value(), 25.4, 0.002);
    EXPECT_GE(slipping.telemetry().wheels.distance_error_m, 0.6);
    EXPECT_EQ(std::get<RobotError>(
                  RobotSimulation::create(load(), 0, {}, {}, EncoderEffects{0.0, 0.0, 1.1, 0.6})),
              RobotError::invalid_configuration);
}

} // namespace
