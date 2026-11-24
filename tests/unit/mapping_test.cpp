#include "argus/core/random.hpp"
#include "argus/mapping/mapper.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <numbers>
#include <variant>

namespace {

using namespace argus;
using namespace argus::mapping;

constexpr Spec setting{ScenarioId{5}, 4100, 4400, 500};

robot::Pose pose(double x, double y, double heading = 0.0) {
    return {Meters{x}, Meters{y}, robot::Radians{heading}};
}

sensors::WheelSample wheel(std::uint64_t tick, robot::Pose position, double budget = 0.0) {
    return {{sensors::SensorId::wheel_encoder, tick, tick * robot::step_ms, tick, position},
            {Meters{0.02}, Meters{0.02}, budget, budget},
            true};
}

sensors::LidarScan east(std::uint64_t tick, double range) {
    return {{sensors::SensorId::lidar, tick, tick * robot::step_ms, tick, pose(0.1, 0.1)},
            0.0,
            0.1,
            0.0,
            {range},
            1};
}

TEST(MapGrid, RoundTripsCellCentersAndRejectsOutsideCoordinates) {
    auto grid = std::get<Grid>(Grid::create(setting));
    EXPECT_EQ(grid.columns(), 9U);
    EXPECT_EQ(grid.rows(), 9U);
    EXPECT_FALSE(grid.locate({4.1, 2.0}));
    EXPECT_FALSE(grid.locate({-0.01, 2.0}));
    EXPECT_FALSE(grid.locate({0.5, std::numeric_limits<double>::quiet_NaN()}));
    EXPECT_FALSE(grid.center({9, 8}));
    Random random{308};
    for (int i = 0; i < 2000; ++i) {
        const Cell cell{random.next() % grid.columns(), random.next() % grid.rows()};
        EXPECT_EQ(grid.locate(grid.center(cell).value()), cell);
    }
    EXPECT_TRUE(
        std::holds_alternative<MapError>(Grid::create({ScenarioId{1}, 100000, 100000, 50})));
}

TEST(MapGrid, TracesKnownShapeSaturatesAndClipsWithoutWritingOutside) {
    auto grid = std::get<Grid>(Grid::create(setting));
    for (int i = 0; i < 40; ++i) {
        for (std::uint32_t y = 0; y < 8; ++y) {
            const auto line = grid.center({1, y}).value().y_m;
            grid.observe_hit({0.3, line}, {3.1, line});
        }
    }
    const auto counts = grid.counts();
    EXPECT_EQ(counts.occupied, 8U);
    EXPECT_EQ(counts.free, 48U);
    EXPECT_EQ(counts.unknown, 25U);
    EXPECT_EQ(grid.evidence()[6], Grid::max_evidence);
    EXPECT_EQ(grid.evidence()[0], -Grid::max_evidence);
    grid.observe_hit({0.3, 4.2}, {20.0, 4.2});
    EXPECT_EQ(grid.occupancy({8, 8}), Occupancy::free);
    EXPECT_EQ(grid.occupancy({9, 8}), Occupancy::unknown);
    EXPECT_EQ(grid.counts().occupied, 8U);
    grid.observe_hit({3.7, 4.2}, {0.0, 4.2});
    EXPECT_EQ(grid.occupancy({0, 8}), Occupancy::occupied);
    EXPECT_EQ(grid.evidence().size(), 81U);
}

TEST(Mapper, MatchesWheelClockAndRejectsMalformedOrDuplicateScansAtomically) {
    auto mapper = std::get<Mapper>(Mapper::create(setting));
    EXPECT_EQ(mapper.ingest(east(1, 2.0)), Update::missing_pose);
    EXPECT_EQ(mapper.ingest(wheel(1, pose(0.25, 1.25))), Update::accepted);
    auto bad = east(1, 2.0);
    bad.ranges_m.push_back(std::numeric_limits<double>::infinity());
    EXPECT_EQ(mapper.ingest(bad), Update::invalid);
    EXPECT_EQ(mapper.grid().counts().unknown, 81U);
    const auto scan = east(1, 2.0);
    EXPECT_EQ(mapper.ingest(scan), Update::accepted);
    EXPECT_EQ(mapper.grid().occupancy({4, 2}), Occupancy::occupied);
    EXPECT_EQ(mapper.grid().occupancy({4, 0}), Occupancy::unknown);
    EXPECT_EQ(mapper.ingest(scan), Update::invalid);
    EXPECT_EQ(mapper.ingest(wheel(1, pose(1.0, 1.0))), Update::invalid);
    EXPECT_EQ(mapper.ingest(wheel(2, pose(0.25, 1.25))), Update::accepted);
    auto missing = east(2, 2.0);
    missing.ranges_m[0].reset();
    missing.return_count = 0;
    const auto before = mapper.grid().counts().free;
    EXPECT_EQ(mapper.ingest(missing), Update::accepted);
    EXPECT_EQ(mapper.grid().counts().free, before);
}

TEST(Mapper, SuspendsOnUncertaintyAndFollowsExplicitAnchorInNewFrame) {
    auto mapper = std::get<Mapper>(Mapper::create(setting, Limits{0.5, 1.0}));
    ASSERT_EQ(mapper.ingest(wheel(1, pose(0.25, 0.25), 0.1)), Update::accepted);
    ASSERT_EQ(mapper.ingest(east(1, 1.0)), Update::accepted);
    EXPECT_EQ(mapper.ingest(wheel(2, pose(0.50, 0.25), 0.7)), Update::suspended);
    EXPECT_EQ(mapper.ingest(east(2, 1.0)), Update::suspended);
    EXPECT_EQ(mapper.grid().counts().occupied, 1U);
    EXPECT_FALSE(mapper.reanchor(pose(99.0, 2.0), 0.05, 0.05));
    ASSERT_TRUE(mapper.reanchor(pose(2.0, 2.0, std::numbers::pi / 2.0), 0.05, 0.05));
    EXPECT_EQ(mapper.grid().counts().unknown, 81U);
    EXPECT_EQ(mapper.ingest(wheel(3, pose(0.75, 0.25), 0.8)), Update::accepted);
    ASSERT_TRUE(mapper.estimate());
    EXPECT_NEAR(mapper.estimate()->pose.x.value(), 2.0, 1e-9);
    EXPECT_NEAR(mapper.estimate()->pose.y.value(), 2.25, 1e-9);
    EXPECT_NEAR(mapper.estimate()->distance_error_m, 0.15, 1e-9);
    EXPECT_EQ(mapper.ingest(east(3, 1.0)), Update::accepted);
    EXPECT_EQ(mapper.grid().occupancy({4, 6}), Occupancy::occupied);
    mapper.reset();
    EXPECT_FALSE(mapper.estimate());
    EXPECT_EQ(mapper.grid().counts().unknown, 81U);
}

} // namespace
