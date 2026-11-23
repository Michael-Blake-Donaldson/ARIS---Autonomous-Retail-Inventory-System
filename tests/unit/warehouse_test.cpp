#include "argus/core/random.hpp"
#include "argus/simulation/fixtures.hpp"
#include "argus/simulation/sensor_query.hpp"
#include "argus/simulation/warehouse.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <ios>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace argus::simulation;
using namespace argus::world;

template <typename T>
concept HasPlacementData = requires(const T &view) { view.items; } || requires(const T &view) {
    view.tags;
} || requires(const T &view) { view.epc; };
static_assert(!HasPlacementData<Snapshot>);
static_assert(!HasPlacementData<SolidView>);
static_assert(!HasPlacementData<ActorView>);

std::string_view small_fixture() {
    return std::get<std::string_view>(fixture_json("small-backroom"));
}

Warehouse small_warehouse() { return std::get<Warehouse>(Warehouse::from_json(small_fixture())); }

std::string replace(std::string_view original, std::string_view from, std::string_view to) {
    std::string modified{original};
    return modified.replace(modified.find(from), from.size(), to);
}

ScenarioError rejected(std::string_view document) {
    return std::get<ScenarioError>(Warehouse::from_json(document));
}

TEST(Warehouse, LoadsAllThreeLayoutsAndKeepsTheirSpawnsClear) {
    constexpr std::array names{"small-backroom", "cluttered-receiving", "broad-stockroom"};
    constexpr std::array expected_ids{201U, 202U, 203U};
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto fixture = fixture_json(names.at(index));
        ASSERT_TRUE(std::holds_alternative<std::string_view>(fixture));
        const auto loaded = Warehouse::from_json(std::get<std::string_view>(fixture));
        ASSERT_TRUE(std::holds_alternative<Warehouse>(loaded)) << names.at(index);
        const auto &warehouse = std::get<Warehouse>(loaded);
        const auto view = warehouse.snapshot(0);
        EXPECT_EQ(view.scenario_id.value(), expected_ids.at(index));
        EXPECT_GE(view.solids.size(), 4U);
        EXPECT_GE(view.zones.size(), 5U);
        EXPECT_FALSE(warehouse.collides(view.robot_spawn, view.robot_radius_mm, 0));
        EXPECT_NE(view.actors, warehouse.snapshot(1).actors);
        const SensorQuery sensor{warehouse};
        EXPECT_GE(sensor.tags(view.floor).size(), 3U);
    }
    EXPECT_EQ(std::get<ScenarioError>(fixture_json("../small-backroom")),
              ScenarioError::unknown_fixture);
}

TEST(Warehouse, UsesClosedCollisionBoundariesAndOpenDoorGap) {
    const auto warehouse = small_warehouse();
    EXPECT_TRUE(warehouse.collides({350, 1800}, 350, 0));
    EXPECT_FALSE(warehouse.collides({351, 1800}, 350, 0));
    EXPECT_TRUE(warehouse.collides({2000, 3050}, 350, 0));
    EXPECT_FALSE(warehouse.collides({5500, 3450}, 350, 0));
    EXPECT_TRUE(warehouse.collides({5500, 3450}, 0, 0));
    EXPECT_TRUE(warehouse.collides({5500, 3450}, 5001, 0));
    EXPECT_TRUE(warehouse.collides({std::numeric_limits<std::int32_t>::min(), 0}, 350, 0));
    const auto actor = warehouse.snapshot(0).actors.front();
    EXPECT_TRUE(warehouse.collides(actor.bounds.min, 350, 0));
}

TEST(Warehouse, SpatialQueriesMatchUnindexedChecks) {
    const auto warehouse = small_warehouse();
    argus::Random random{5236};
    for (std::uint64_t tick = 0; tick < 400; ++tick) {
        const auto view = warehouse.snapshot(tick);
        const auto x = static_cast<std::int32_t>(random.next() % 14001U);
        const auto y = static_cast<std::int32_t>(random.next() % 10501U);
        const auto radius = static_cast<std::int32_t>(1U + random.next() % 5000U);
        const PointMm center{x, y};
        const auto touches = [center, radius](const auto &obstacle) {
            return touches_disk(obstacle.bounds, center, radius);
        };
        const bool expected = x <= radius || y <= radius || x + radius >= view.floor.max.x ||
                              y + radius >= view.floor.max.y ||
                              std::ranges::any_of(view.solids, touches) ||
                              std::ranges::any_of(view.actors, touches);
        EXPECT_EQ(warehouse.collides(center, radius, tick), expected);
    }
}

TEST(Warehouse, ReplaysExactActorMotionAndHandlesMaximumTick) {
    const auto first = small_warehouse();
    const auto second = small_warehouse();
    for (const auto tick : {0ULL, 1ULL, 59ULL, 60ULL, 120ULL, 151ULL, 1200ULL,
                            std::numeric_limits<unsigned long long>::max()}) {
        EXPECT_EQ(first.snapshot(tick).actors, second.snapshot(tick).actors);
    }
    const auto initial = first.snapshot(0).actors;
    EXPECT_NE(initial.front().bounds, first.snapshot(1).actors.front().bounds);
    EXPECT_EQ(initial.front().bounds, first.snapshot(120).actors.front().bounds);
    EXPECT_EQ(initial, first.snapshot(600).actors);
    const auto altered =
        Warehouse::from_json(replace(small_fixture(), "\"seed\": 5489", "\"seed\": 17"));
    ASSERT_TRUE(std::holds_alternative<Warehouse>(altered));
    EXPECT_NE(initial, std::get<Warehouse>(altered).snapshot(0).actors);
}

TEST(Warehouse, KeepsRendererCopiesIndependentAndSensorQueriesRegional) {
    auto warehouse = small_warehouse();
    auto local = warehouse.snapshot(0);
    local.solids.clear();
    EXPECT_EQ(warehouse.snapshot(0).solids.size(), 4U);

    const SensorQuery sensor{warehouse};
    const RectMm rack_corner{{2200, 6200}, {2400, 6400}};
    const auto tags = sensor.tags(rack_corner);
    ASSERT_EQ(tags.size(), 1U);
    EXPECT_EQ(tags.front().epc, "A00000000000000000000001");
    EXPECT_EQ(tags.front().position, (PointMm{2300, 6300}));
    EXPECT_EQ(tags.front().height_mm, 420);
    EXPECT_EQ(sensor.obstacles(rack_corner, 0).size(), 1U);
    EXPECT_EQ(sensor.obstacles({{3400, 4600}, {3400, 4600}}, 0).size(), 0U);
    EXPECT_EQ(sensor.obstacles({{-1000, -1000}, {20000, 20000}}, 0).size(), 6U);
    EXPECT_TRUE(sensor.tags({{2400, 6400}, {2200, 6200}}).empty());
}

TEST(ScenarioImport, RejectsMalformedDuplicateAndUnsupportedJson) {
    EXPECT_EQ(rejected("{"), ScenarioError::malformed_json);
    EXPECT_EQ(rejected(std::string{small_fixture()} + " {}"), ScenarioError::malformed_json);
    EXPECT_EQ(rejected(std::string{small_fixture()} + std::string{"\0{}", 3}),
              ScenarioError::malformed_json);
    EXPECT_EQ(
        rejected(replace(small_fixture(), "\"seed\": 5489", "\"seed\": 5489, \"s\\u0065ed\": 99")),
        ScenarioError::duplicate_key);
    EXPECT_EQ(rejected(replace(small_fixture(), "\"1.0.0\"", "\"0.1.0\"")),
              ScenarioError::unsupported_version);
    EXPECT_EQ(rejected(replace(small_fixture(), "\"seed\": 5489", "\"seed\": 1.0")),
              ScenarioError::invalid_schema);
    EXPECT_EQ(rejected(replace(small_fixture(), "\"seed\": 5489", "\"seed\": 4294967296")),
              ScenarioError::invalid_schema);
    EXPECT_EQ(rejected(replace(small_fixture(), "\"seed\": 5489", "\"seed\": 1, \"extra\": 1")),
              ScenarioError::invalid_schema);
    EXPECT_EQ(
        rejected(replace(small_fixture(), "worker-a", std::string(1, static_cast<char>(0xFF)))),
        ScenarioError::malformed_json);
}

TEST(ScenarioImport, BoundsBytesDepthEventsAndStreamReads) {
    const std::string huge(scenario_byte_limit + 128, ' ');
    EXPECT_EQ(rejected(huge), ScenarioError::too_large);
    std::istringstream huge_input{huge};
    EXPECT_EQ(std::get<ScenarioError>(Warehouse::from_stream(huge_input)),
              ScenarioError::too_large);
    EXPECT_EQ(huge_input.tellg(), static_cast<std::streamoff>(scenario_byte_limit + 1));
    EXPECT_EQ(rejected(std::string(32, '[')), ScenarioError::too_large);
    std::string many{"[0"};
    for (int index = 0; index < 33000; ++index) {
        many += ",0";
    }
    EXPECT_EQ(rejected(many + "]"), ScenarioError::too_large);
    std::istringstream failed{std::string{small_fixture()}};
    failed.setstate(std::ios::badbit);
    EXPECT_EQ(std::get<ScenarioError>(Warehouse::from_stream(failed)),
              ScenarioError::input_failure);
    std::istringstream valid{std::string{small_fixture()}};
    valid.exceptions(std::ios::failbit | std::ios::badbit);
    EXPECT_TRUE(std::holds_alternative<Warehouse>(Warehouse::from_stream(valid)));
}

TEST(ScenarioImport, RejectsOverlapsOutsideExtentsAndBrokenReferences) {
    EXPECT_EQ(rejected(replace(small_fixture(), "[5500,1900]", "[5500,350]")),
              ScenarioError::invalid_geometry);
    EXPECT_EQ(rejected(replace(small_fixture(), "[5500,1900]", "[2500,6500]")),
              ScenarioError::invalid_geometry);
    EXPECT_EQ(rejected(replace(small_fixture(), "[7900,6000,1300,3700]", "[2200,6100,500,1000]")),
              ScenarioError::invalid_geometry);
    EXPECT_EQ(rejected(replace(small_fixture(), "[7900,6000,1300,3700]", "[7900,6000,1300,5000]")),
              ScenarioError::invalid_geometry);
    EXPECT_EQ(rejected(replace(small_fixture(), "[6400,7600]", "[9000,7600]")),
              ScenarioError::invalid_geometry);
    EXPECT_EQ(
        rejected(replace(small_fixture(), "\"aisle\":\"east-aisle\"", "\"aisle\":\"missing\"")),
        ScenarioError::invalid_reference);
    EXPECT_EQ(rejected(replace(small_fixture(), "\"shelf\":3", "\"shelf\":4")),
              ScenarioError::invalid_reference);
    EXPECT_EQ(rejected(replace(small_fixture(), "[650,2100]", "[1300,2100]")),
              ScenarioError::invalid_geometry);
    EXPECT_EQ(
        rejected(replace(small_fixture(), "A00000000000000000000002", "A00000000000000000000001")),
        ScenarioError::duplicate_identifier);
}

} // namespace
