#include "argus/audit/stockroom.hpp"
#include "argus/navigation/traffic.hpp"
#include "argus/simulation/fleet.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <thread>

namespace {

using namespace argus;
using namespace argus::simulation;

struct Sample {
    robot::Telemetry telemetry;
    robot::Pose truth;
    bool close_lidar_return;
};

using Trace = std::array<std::vector<Sample>, 2>;

Trace sample_pair(const Warehouse &world, std::array<RobotStart, 2> starts,
                  std::array<robot::Command, 2> commands, unsigned ticks) {
    FleetClock clock;
    Trace trace;
    const auto sample = [&](std::size_t index) {
        const auto id = RobotId{index + 1};
        auto made = RobotSimulation::create_at(world, starts.at(index));
        auto *robot = std::get_if<RobotSimulation>(&made);
        if (!robot) {
            clock.close();
            return;
        }
        auto config = audit::stockroom_sensor_config();
        config.lidar.missing_probability = 0;
        config.lidar.range_noise_m = 0;
        auto rig = std::get<SensorRig>(SensorRig::create(starts.at(index).seed, config));
        for (unsigned tick = 0; tick < ticks && clock.step(id, *robot, commands.at(index));
             ++tick) {
            rig.capture(*robot);
            bool nearby = false;
            while (const auto observation = rig.pop()) {
                if (const auto *scan = std::get_if<sensors::LidarScan>(&*observation)) {
                    for (const auto &range : scan->ranges_m) {
                        nearby = nearby || (range && *range < 0.52);
                    }
                }
            }
            trace.at(index).push_back({robot->telemetry(), robot->debug_truth().pose, nearby});
        }
    };
    std::jthread first{sample, 0};
    std::jthread second{sample, 1};
    first.join();
    second.join();
    return trace;
}

TEST(SharedFleet, HeadOnAtNarrowLaneKeepsPhysicalGapAndProducesPeerReturns) {
    const auto world = std::get<Warehouse>(audit::shared_stockroom_world());
    const robot::Command forward{.target = {MetersPerSecond{0.6}, robot::RadiansPerSecond{0.0}},
                                 .deadman = true};
    const auto trace = sample_pair(world,
                                   {{{{9770, 8210}, robot::Radians{std::numbers::pi / 2}, 1},
                                     {{9770, 10250}, robot::Radians{-std::numbers::pi / 2}, 2}}},
                                   {forward, forward}, 88);
    ASSERT_EQ(trace[0].size(), 88U);
    ASSERT_EQ(trace[1].size(), 88U);
    bool blocked = false;
    bool peer_seen = false;
    for (std::size_t tick = 0; tick < trace[0].size(); ++tick) {
        const auto &a = trace[0][tick];
        const auto &b = trace[1][tick];
        EXPECT_EQ(a.telemetry.sample_tick, b.telemetry.sample_tick);
        EXPECT_EQ(a.telemetry.world_tick, b.telemetry.world_tick);
        EXPECT_GT(std::hypot(a.truth.x.value() - b.truth.x.value(),
                             a.truth.y.value() - b.truth.y.value()),
                  0.6);
        blocked = blocked || (a.telemetry.collision_blocked && b.telemetry.collision_blocked);
        peer_seen = peer_seen || (a.close_lidar_return && b.close_lidar_return);
    }
    EXPECT_TRUE(blocked);
    EXPECT_TRUE(peer_seen);
}

TEST(SharedFleet, MovingObstructionFreezesTheCommonWorldClock) {
    const auto world = std::get<Warehouse>(audit::shared_stockroom_world());
    const auto scene = world.snapshot(0);
    ASSERT_EQ(scene.actors.size(), 1U);
    const auto &bounds = scene.actors.front().bounds;
    const auto center_y = (bounds.min.y + bounds.max.y) / 2;
    const auto avoided_start = center_y < 3500 ? 4200 : 2800;
    const auto trace = sample_pair(
        world,
        {{{{15250, avoided_start}, robot::Radians{0}, 1}, audit::stockroom_berth(RobotId{2})}}, {},
        150);
    ASSERT_EQ(trace[0].size(), 150U);
    ASSERT_EQ(trace[1].size(), 150U);
    EXPECT_TRUE(trace[0].back().telemetry.world_held);
    EXPECT_TRUE(trace[1].back().telemetry.world_held);
    EXPECT_EQ(trace[0].back().telemetry.world_tick, trace[1].back().telemetry.world_tick);
    EXPECT_LT(trace[0].back().telemetry.world_tick, trace[0].back().telemetry.sample_tick);
}

TEST(SharedFleet, RejectsIndependentWorldCopiesAndReleasesBothParticipants) {
    const auto a = std::get<Warehouse>(audit::shared_stockroom_world());
    const auto b = std::get<Warehouse>(audit::shared_stockroom_world());
    auto first = std::get<RobotSimulation>(
        RobotSimulation::create_at(a, audit::stockroom_berth(RobotId{1})));
    auto second = std::get<RobotSimulation>(
        RobotSimulation::create_at(b, audit::stockroom_berth(RobotId{2})));
    FleetClock clock;
    bool stepped = true;
    std::jthread peer{[&] { stepped = clock.step(RobotId{2}, second, {}); }};
    EXPECT_FALSE(clock.step(RobotId{1}, first, {}));
    peer.join();
    EXPECT_FALSE(stepped);
    EXPECT_EQ(clock.stop_reason(), FleetStop::invalid_pair);
    EXPECT_EQ(first.telemetry().sample_tick, 0U);
    EXPECT_EQ(second.telemetry().sample_tick, 0U);
}

TEST(SharedFleet, MissingPeerClosesTheClockWithoutCommittingAPartialTick) {
    const auto world = std::get<Warehouse>(audit::shared_stockroom_world());
    auto first = std::get<RobotSimulation>(
        RobotSimulation::create_at(world, audit::stockroom_berth(RobotId{1})));
    auto second = std::get<RobotSimulation>(
        RobotSimulation::create_at(world, audit::stockroom_berth(RobotId{2})));
    FleetClock clock{std::chrono::milliseconds{100}};
    const robot::Command forward{.target = {MetersPerSecond{0.6}, robot::RadiansPerSecond{0.0}},
                                 .deadman = true};
    bool advanced = false;
    std::jthread peer{[&] { advanced = clock.step(RobotId{2}, second, forward); }};
    const bool committed = clock.step(RobotId{1}, first, forward);
    peer.join();
    ASSERT_TRUE(advanced);
    ASSERT_TRUE(committed);
    EXPECT_FALSE(clock.step(RobotId{1}, first, forward));
    EXPECT_FALSE(clock.step(RobotId{2}, second, forward));
    clock.close();
    EXPECT_EQ(clock.stop_reason(), FleetStop::peer_timeout);
    EXPECT_EQ(first.telemetry().sample_tick, 1U);
    EXPECT_EQ(second.telemetry().sample_tick, 1U);
    EXPECT_EQ(first.telemetry().world_tick, 1U);
    EXPECT_EQ(second.telemetry().world_tick, 1U);
}

TEST(AisleAccess, PendingRequestYieldsUntilActiveOwnerClears) {
    navigation::AisleAccess access;
    const RobotId first{1};
    const RobotId second{2};
    ASSERT_TRUE(access.request(first, {Meters{4}, Meters{10}}));
    ASSERT_TRUE(access.request(second, {Meters{9}, Meters{11}}));
    EXPECT_FALSE(access.admit(second));
    ASSERT_TRUE(access.admit(first));
    access.finish(first, false);
    EXPECT_TRUE(access.retained(first));
    EXPECT_FALSE(access.admit(second));
    ASSERT_TRUE(access.request_clearance(first));
    EXPECT_TRUE(access.admit(first));
    access.finish(first, true);
    EXPECT_FALSE(access.retained(first));
    EXPECT_TRUE(access.admit(second));
    access.finish(second, true);
    EXPECT_FALSE(access.working());
}

TEST(AisleAccess, DisjointWorkCanRunAndWaitingCancellationReleasesTheClaim) {
    navigation::AisleAccess access;
    const RobotId first{1};
    const RobotId second{2};
    ASSERT_TRUE(access.request(first, {Meters{2}, Meters{5}}));
    ASSERT_TRUE(access.request(second, {Meters{6}, Meters{10}}));
    EXPECT_TRUE(access.admit(first));
    EXPECT_TRUE(access.admit(second));
    access.finish(second, true);
    ASSERT_TRUE(access.request(second, {Meters{4}, Meters{10}}));
    EXPECT_FALSE(access.admit(second));
    access.finish(second, false);
    EXPECT_FALSE(access.retained(second));
    EXPECT_TRUE(access.request(second, {Meters{6}, Meters{10}}));
}

} // namespace
