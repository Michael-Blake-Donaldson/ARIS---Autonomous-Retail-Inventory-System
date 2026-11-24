#include "argus/navigation/navigator.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <numbers>
#include <queue>
#include <utility>

namespace {

using namespace argus;
using namespace argus::mapping;
using namespace argus::navigation;

Grid grid_of(std::span<const std::string_view> rows) {
    auto grid = std::get<Grid>(
        Grid::create({ScenarioId{1}, static_cast<std::uint32_t>(rows.front().size()) * 250,
                      static_cast<std::uint32_t>(rows.size()) * 250, 250}));
    std::vector<std::int8_t> evidence;
    for (const auto row : rows) {
        for (const char cell : row) {
            evidence.push_back(cell == '#' ? 6 : cell == '.' ? -6 : 0);
        }
    }
    EXPECT_TRUE(grid.restore(evidence));
    return grid;
}

// Independent uniform-cost reference uses all pairs of adjacent traversable cells.
std::uint32_t reference_cost(const Costmap &map, Cell start, Cell goal) {
    const auto columns = map.columns();
    const auto size = columns * map.rows();
    const auto pos = [columns](Cell at) { return at.y * columns + at.x; };
    const auto infinite = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> costs(size, infinite);
    std::vector<bool> settled(size);
    costs.at(pos(start)) = 0;
    for (std::uint32_t step = 0; step < size; ++step) {
        auto chosen = size;
        for (std::uint32_t i = 0; i < size; ++i) {
            if (!settled.at(i) && costs.at(i) != infinite &&
                (chosen == size || costs.at(i) < costs.at(chosen))) {
                chosen = i;
            }
        }
        if (chosen == size || chosen == pos(goal)) {
            break;
        }
        const Cell at{chosen % columns, chosen / columns};
        settled.at(chosen) = true;
        for (std::uint32_t y = 0; y < map.rows(); ++y) {
            for (std::uint32_t x = 0; x < columns; ++x) {
                const auto dx = std::max(x, at.x) - std::min(x, at.x);
                const auto dy = std::max(y, at.y) - std::min(y, at.y);
                const Cell next{x, y};
                if (dx <= 1 && dy <= 1 && map.traversable(next) && map.traversable({at.x, y}) &&
                    map.traversable({x, at.y})) {
                    const auto cost = costs.at(chosen) + (dx == 1 && dy == 1 ? 14U : 10U);
                    costs.at(pos(next)) = std::min(cost, costs.at(pos(next)));
                }
            }
        }
    }
    return costs.at(pos(goal));
}

TEST(Navigation, AStarMatchesUniformCostAndOctileIsAdmissible) {
    constexpr std::array<std::string_view, 7> rows{".......", ".##....", "....#..", "....#..",
                                                   "...##..", ".......", "......."};
    const auto grid = grid_of(rows);
    const auto map = Costmap::build(grid, Meters{0.08}).value();
    const Cell start{0, 0};
    for (std::uint32_t y = 0; y < grid.rows(); ++y) {
        for (std::uint32_t x = 0; x < grid.columns(); ++x) {
            const Cell goal{x, y};
            if (!map.traversable(goal)) {
                EXPECT_FALSE(plan(map, start, goal));
                continue;
            }
            const auto expected = reference_cost(map, start, goal);
            const auto route = plan(map, start, goal);
            ASSERT_TRUE(route);
            EXPECT_EQ(route->cost, expected);
            EXPECT_LE(octile(start, goal), expected);
            EXPECT_EQ(route->cells.front(), start);
            EXPECT_EQ(route->cells.back(), goal);
        }
    }
    constexpr std::array<std::string_view, 2> corner{".#", "#."};
    const auto corner_map = Costmap::build(grid_of(corner), Meters{0.08}).value();
    EXPECT_FALSE(plan(corner_map, {0, 0}, {1, 1}));
}

TEST(Navigation, InflatesOccupiedAndNoGoCellsAndRejectsUnboundedInput) {
    constexpr std::array<std::string_view, 7> rows{".......", ".......", ".......", ".....#.",
                                                   ".......", ".......", "......."};
    const auto grid = grid_of(rows);
    const std::array<NoGo, 1> zones{{{{0.83, 0.0}, {0.92, 1.75}}}};
    const auto map = Costmap::build(grid, Meters{0.16}, zones).value();
    EXPECT_FALSE(map.traversable({5, 2}));
    EXPECT_FALSE(map.traversable({4, 3}));
    EXPECT_FALSE(map.traversable({3, 4}));
    EXPECT_FALSE(plan(map, {1, 2}, {5, 5}));
    EXPECT_FALSE(Costmap::build(grid, Meters{std::numeric_limits<double>::quiet_NaN()}));
    EXPECT_FALSE(Costmap::build(grid, Meters{0.08}, std::array<NoGo, 33>{}));
}

TEST(Navigation, DistinguishesNoFrontiersAndUnreachableFrontiersAndReservesReturnEnergy) {
    constexpr std::array<std::string_view, 3> rows{".#...?", ".#...?", ".#...?"};
    const auto grid = grid_of(rows);
    const auto map = Costmap::build(grid, Meters{0.08}).value();
    const auto isolated = select_frontier(grid, map, {0, 1}, 500);
    EXPECT_FALSE(isolated.goal);
    EXPECT_TRUE(isolated.inaccessible);
    const Cell home{2, 1};
    const auto outbound = select_frontier(grid, map, home, 39);
    const auto round_trip = select_frontier(grid, map, home, 39, {}, home);
    EXPECT_TRUE(outbound.goal);
    EXPECT_FALSE(round_trip.goal);
    EXPECT_TRUE(round_trip.inaccessible);
    EXPECT_TRUE(select_frontier(grid, map, home, 40, {}, home).goal);

    constexpr std::array<std::string_view, 2> explored{"..", ".."};
    const auto known = grid_of(explored);
    const auto clear = Costmap::build(known, Meters{0.08}).value();
    const auto finished = select_frontier(known, clear, {0, 0}, 500);
    EXPECT_FALSE(finished.goal);
    EXPECT_FALSE(finished.inaccessible);
}

TEST(Navigation, StopsOnExpiredScanAndRetainsReserveAtLaunchPoint) {
    const robot::Pose pose{Meters{1.125}, Meters{1.125}, robot::Radians{0}};
    auto mapper = std::get<Mapper>(Mapper::create({ScenarioId{1}, 2250, 2250, 250}));
    auto navigator = Navigator::create(mapper.grid(), {.radius = Meters{0.08}}).value();
    const sensors::Header wheel{sensors::SensorId::wheel_encoder, 5, 250, 5, pose};
    ASSERT_EQ(mapper.ingest(sensors::WheelSample{wheel, {}, true}), Update::accepted);
    auto lidar = sensors::LidarScan{{sensors::SensorId::lidar, 5, 250, 5, pose},
                                    -std::numbers::pi,
                                    std::numbers::pi / 90.0,
                                    0.0,
                                    std::vector<std::optional<double>>(180, 1.0),
                                    180};
    ASSERT_EQ(mapper.ingest(lidar), Update::accepted);
    robot::Telemetry telemetry{
        .sample_tick = 5, .estimated_pose = pose, .energy_uj = 250000000, .sensors_powered = true};
    EXPECT_FALSE(navigator.update(mapper, telemetry, nullptr).deadman);
    EXPECT_EQ(navigator.progress().reason, Reason::stale_scan);
    telemetry.sample_tick = 6;
    EXPECT_FALSE(navigator.update(mapper, telemetry, &lidar).deadman);
    EXPECT_EQ(navigator.progress().reason, Reason::home_reached);
    EXPECT_EQ(navigator.progress().state, State::finished);
}

} // namespace
