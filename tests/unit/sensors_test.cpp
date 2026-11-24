#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace argus;
using namespace argus::sensors;
using namespace argus::simulation;

constexpr std::string_view document = R"({
    "schemaVersion":"1.0.0","scenarioId":401,"seed":5489,"sizeMm":[14000,12000],
    "robot":{"spawnMm":[2000,5000],"radiusMm":350},
    "fixtures":[{"id":"rack","kind":"rack","boundsMm":[6500,4400,900,800],"aisle":"aisle","shelfHeightsMm":[800]}],
    "actors":[],
    "items":[{"epc":"A00000000000000000000001","rack":"rack","shelf":1,"offsetMm":[100,600],"headingDeg":0}],
    "zones":[
        {"id":"aisle","kind":"aisle","boundsMm":[4600,4200,1900,1900]},
        {"id":"door","kind":"doorway","boundsMm":[100,100,700,600]},
        {"id":"receiving","kind":"receiving","boundsMm":[100,2400,2500,1600]},
        {"id":"dock","kind":"dock","boundsMm":[1500,8000,3000,3000]},
        {"id":"charging","kind":"charging","boundsMm":[1900,8200,1200,1400]}
    ]
})";
constexpr std::string_view wall =
    R"({"id":"divider","kind":"wall","boundsMm":[4300,4200,150,1600]},)";

RobotSimulation fixture(bool blocked = false, int heading_deg = 0, std::uint64_t start = 0) {
    std::string json{document};
    if (blocked) {
        const auto at = json.find("[{", json.find("\"fixtures\""));
        json.insert(at + 1, wall);
    }
    constexpr std::string_view heading = "\"headingDeg\":0";
    json.replace(json.find(heading), heading.size(),
                 "\"headingDeg\":" + std::to_string(heading_deg));
    auto world = std::get<Warehouse>(Warehouse::from_json(json));
    return std::get<RobotSimulation>(
        RobotSimulation::create(std::move(world), start, {}, {}, warehouse_encoder_effects));
}

SensorConfig config() {
    SensorConfig result;
    result.lidar = {.beams = 1,
                    .field_of_view_rad = 1.0,
                    .range_m = 12.0,
                    .range_noise_m = 0.0,
                    .missing_probability = 0.0,
                    .period_ticks = 1};
    result.rfid = {.antenna_forward_m = 0.0,
                   .antenna_left_m = 0.0,
                   .antenna_height_m = 0.8,
                   .antenna_yaw_rad = 0.0,
                   .field_of_view_rad = std::numbers::pi,
                   .range_m = 8.0,
                   .detection_probability = 0.95,
                   .obstacle_transmission = 0.35,
                   .period_ticks = 1,
                   .dwell_scans = 1,
                   .max_reads = 64};
    return result;
}

SensorRig rig(SensorConfig settings = config(), std::uint32_t seed = 721U) {
    return std::get<SensorRig>(SensorRig::create(seed, settings));
}

struct Frame {
    WheelSample wheel;
    LidarScan lidar;
    RfidScan rfid;
};

Frame capture(RobotSimulation &robot, SensorRig &sensors) {
    robot.step({});
    EXPECT_TRUE(sensors.capture(robot));
    return {std::get<WheelSample>(sensors.pop().value()),
            std::get<LidarScan>(sensors.pop().value()), std::get<RfidScan>(sensors.pop().value())};
}

template <typename T>
concept ExposesGroundTruth = requires(const T &value) {
    value.position;
} || requires(const T &value) { value.true_pose; } || requires(const T &value) { value.rack; };
static_assert(!ExposesGroundTruth<Header>);
static_assert(!ExposesGroundTruth<RfidRead>);
static_assert(!ExposesGroundTruth<LidarScan>);

TEST(Lidar, FindsNearestGeometryAndBoundaryAtKnownDistances) {
    auto blocked = fixture(true);
    auto open = fixture();
    auto front = rig();
    auto open_front = rig();
    const auto nearest = capture(blocked, front).lidar;
    ASSERT_EQ(nearest.ranges_m.size(), 1U);
    EXPECT_DOUBLE_EQ(nearest.ranges_m.front().value(), 2.3);
    EXPECT_DOUBLE_EQ(capture(open, open_front).lidar.ranges_m.front().value(), 4.5);

    auto full_config = config();
    full_config.lidar.field_of_view_rad = 2.0 * std::numbers::pi;
    full_config.lidar.beams = 5;
    auto full = rig(full_config);
    const auto scan = capture(blocked, full).lidar;
    EXPECT_NEAR(scan.angle_step_rad, 2.0 * std::numbers::pi / 5.0, 1e-12);
    EXPECT_NEAR(scan.first_angle_rad, -4.0 * std::numbers::pi / 5.0, 1e-12);
    EXPECT_NEAR(scan.ranges_m.at(0).value(), 2.0 / std::cos(std::numbers::pi / 5.0), 1e-9);
    EXPECT_DOUBLE_EQ(scan.ranges_m.at(2).value(), 2.3);
}

TEST(Lidar, HandlesGrazingRaysRangeEdgesAndMissingReturns) {
    auto settings = config();
    settings.lidar.beams = 3;
    settings.lidar.field_of_view_rad = 3.0 * std::atan2(800.0, 2300.0);
    auto grazing = rig(settings);
    auto blocked = fixture(true);
    const auto corner = capture(blocked, grazing).lidar.ranges_m.at(2);
    ASSERT_TRUE(corner);
    EXPECT_NEAR(*corner, std::hypot(2.3, 0.8), 1e-9);

    settings = config();
    settings.lidar.range_m = 2.29;
    auto too_short = rig(settings);
    EXPECT_FALSE(capture(blocked, too_short).lidar.ranges_m.front());
    settings.lidar.range_m = 2.3;
    auto just_enough = rig(settings);
    const auto edge = capture(blocked, just_enough).lidar.ranges_m.front();
    ASSERT_TRUE(edge);
    EXPECT_DOUBLE_EQ(*edge, 2.3);
    settings.lidar.missing_probability = 1.0;
    auto missing = rig(settings);
    EXPECT_EQ(capture(blocked, missing).lidar.return_count, 0U);
}

TEST(Lidar, NoiseIsBoundedSeededAndIndependentFromRfid) {
    auto settings = config();
    settings.lidar.beams = 31;
    settings.lidar.range_noise_m = 0.1;
    settings.lidar.missing_probability = 0.35;
    auto first = rig(settings);
    auto repeat = rig(settings);
    auto alternate = rig(settings, 998U);
    auto one = fixture(true);
    auto two = fixture(true);
    auto three = fixture(true);
    bool different = false;
    for (int step = 0; step < 16; ++step) {
        const auto a = capture(one, first);
        const auto b = capture(two, repeat);
        const auto c = capture(three, alternate);
        EXPECT_EQ(a.lidar.ranges_m, b.lidar.ranges_m);
        different = different || a.lidar.ranges_m != c.lidar.ranges_m;
        if (a.lidar.ranges_m.at(15)) {
            EXPECT_NEAR(*a.lidar.ranges_m.at(15), 2.3, 0.1);
        }
    }
    EXPECT_TRUE(different);

    auto narrow = rig();
    auto wide = rig(settings);
    auto stationary = fixture(true);
    for (int step = 0; step < 20; ++step) {
        stationary.step({});
        EXPECT_TRUE(narrow.capture(stationary));
        EXPECT_TRUE(wide.capture(stationary));
        for (int item = 0; item < 2; ++item) {
            EXPECT_TRUE(narrow.pop());
            EXPECT_TRUE(wide.pop());
        }
        EXPECT_EQ(std::get<RfidScan>(narrow.pop().value()).reads,
                  std::get<RfidScan>(wide.pop().value()).reads);
    }
}

TEST(Rfid, ReproducesMissesDuplicatesAndCrossAisleAttenuation) {
    auto open_robot = fixture();
    auto blocked_robot = fixture(true);
    auto blocked_replay = fixture(true);
    auto rotated_robot = fixture(false, 90);
    auto open_rig = rig();
    auto blocked_rig = rig();
    auto replay = rig();
    auto rotated_rig = rig();
    std::uint32_t open_reads = 0;
    std::uint32_t blocked_reads = 0;
    std::uint32_t rotated_reads = 0;
    for (int trial = 0; trial < 1000; ++trial) {
        open_reads += static_cast<std::uint32_t>(capture(open_robot, open_rig).rfid.reads.size());
        const auto blocked = capture(blocked_robot, blocked_rig).rfid;
        EXPECT_EQ(blocked.reads, capture(blocked_replay, replay).rfid.reads);
        if (!blocked.reads.empty()) {
            ++blocked_reads;
            EXPECT_EQ(blocked.reads.front().epc, "A00000000000000000000001");
            EXPECT_GE(blocked.reads.front().signal_dbm, -110.0);
            EXPECT_LE(blocked.reads.front().signal_dbm, -20.0);
        }
        rotated_reads +=
            static_cast<std::uint32_t>(capture(rotated_robot, rotated_rig).rfid.reads.size());
    }
    EXPECT_GE(open_reads, 675U);
    EXPECT_LE(open_reads, 820U);
    EXPECT_GE(blocked_reads, 180U);
    EXPECT_LE(blocked_reads, 350U);
    EXPECT_GE(rotated_reads, 80U);
    EXPECT_LE(rotated_reads, 220U);
}

TEST(Rfid, RequiresConsecutiveDwellAndRespectsAntennaDirectionAndOffset) {
    auto settings = config();
    settings.rfid.dwell_scans = 3;
    settings.rfid.range_m = 25.0;
    settings.rfid.detection_probability = 1.0;
    auto sensor = rig(settings);
    auto robot = fixture();
    EXPECT_TRUE(capture(robot, sensor).rfid.reads.empty());
    EXPECT_TRUE(capture(robot, sensor).rfid.reads.empty());
    robot.step({});
    EXPECT_TRUE(capture(robot, sensor).rfid.reads.empty());
    EXPECT_TRUE(capture(robot, sensor).rfid.reads.empty());
    EXPECT_EQ(capture(robot, sensor).rfid.reads.size(), 1U);

    settings.rfid.dwell_scans = 1;
    settings.rfid.antenna_yaw_rad = std::numbers::pi;
    auto backward = rig(settings);
    EXPECT_TRUE(capture(robot, backward).rfid.reads.empty());
    settings.rfid.antenna_yaw_rad = 0;
    settings.rfid.range_m = 4.5;
    auto out_of_range = rig(settings);
    EXPECT_TRUE(capture(robot, out_of_range).rfid.reads.empty());
    settings.rfid.antenna_forward_m = 0.3;
    auto in_range = rig(settings);
    std::size_t reads = 0;
    for (int scan = 0; scan < 30; ++scan) {
        reads += capture(robot, in_range).rfid.reads.size();
    }
    EXPECT_GT(reads, 0U);
}

TEST(SensorQueue, CountsPerStreamAndDropsOldestAtCapacity) {
    auto robot = fixture(false, 0, 100);
    auto settings = config();
    settings.capacity = 3;
    auto sensor = rig(settings);
    EXPECT_FALSE(sensor.capture(robot));
    for (int step = 0; step < 10; ++step) {
        robot.step({});
        EXPECT_TRUE(sensor.capture(robot));
        EXPECT_FALSE(sensor.capture(robot));
        EXPECT_EQ(sensor.throughput().pending, 3U);
    }
    const auto before = sensor.throughput();
    for (const auto &counter : before.streams) {
        EXPECT_EQ(counter.produced, 10U);
        EXPECT_EQ(counter.dropped, 9U);
        EXPECT_EQ(counter.consumed, 0U);
    }
    const auto wheel = std::get<WheelSample>(sensor.pop().value());
    const auto lidar = std::get<LidarScan>(sensor.pop().value());
    const auto radio = std::get<RfidScan>(sensor.pop().value());
    for (const auto header : {wheel.header, lidar.header, radio.header}) {
        EXPECT_EQ(header.sample_tick, 10U);
        EXPECT_EQ(header.timestamp_ms, 500U);
        EXPECT_EQ(header.world_tick, 110U);
        EXPECT_EQ(header.provenance, Provenance::simulated);
        EXPECT_DOUBLE_EQ(header.estimated_pose.x.value(),
                         robot.telemetry().estimated_pose.x.value());
    }
    EXPECT_FALSE(sensor.pop());
    for (const auto &counter : sensor.throughput().streams) {
        EXPECT_EQ(counter.consumed, 1U);
    }
}

TEST(SensorQueue, HonorsCadencePowerAndConfigLimits) {
    auto robot = fixture();
    auto settings = config();
    settings.lidar.period_ticks = 2;
    settings.rfid.period_ticks = 3;
    auto sensor = rig(settings);
    for (int step = 0; step < 6; ++step) {
        robot.step({});
        EXPECT_TRUE(sensor.capture(robot));
    }
    const auto before = sensor.throughput().streams;
    EXPECT_EQ(before.at(index(SensorId::wheel_encoder)).produced, 6U);
    EXPECT_EQ(before.at(index(SensorId::lidar)).produced, 3U);
    EXPECT_EQ(before.at(index(SensorId::rfid)).produced, 2U);
    for (int step = 0; step < 6; ++step) {
        robot.step(robot::Command{.sensors_on = false});
        EXPECT_TRUE(sensor.capture(robot));
    }
    const auto after = sensor.throughput().streams;
    EXPECT_EQ(after.at(index(SensorId::wheel_encoder)).produced, 12U);
    EXPECT_EQ(after.at(index(SensorId::lidar)).produced, 3U);
    EXPECT_EQ(after.at(index(SensorId::rfid)).produced, 2U);

    settings.capacity = 65;
    EXPECT_EQ(std::get<SensorError>(SensorRig::create(0, settings)),
              SensorError::invalid_configuration);
    settings = config();
    settings.lidar.beams = 721;
    EXPECT_TRUE(std::holds_alternative<SensorError>(SensorRig::create(0, settings)));
    settings = config();
    settings.rfid.range_m = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(std::holds_alternative<SensorError>(SensorRig::create(0, settings)));
}

} // namespace
