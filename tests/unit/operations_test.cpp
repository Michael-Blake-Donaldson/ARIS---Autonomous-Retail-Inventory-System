#include "fleet_report.hpp"
#include "occupancy_mesh.hpp"
#include "operations_model.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

namespace {

using namespace argus::desktop;

TEST(OperationsModel, SearchToleratesWhitespaceAndRecoversAfterWrongIds) {
    using namespace argus;
    EXPECT_TRUE(trim_search(" \t ").empty());
    EXPECT_FALSE(inventory::valid(inventory::Dpci{std::string{trim_search("990-0X-0001")}}));
    EXPECT_TRUE(inventory::valid(inventory::Dpci{std::string{trim_search(" 990-01-0001 ")}}));
    EXPECT_FALSE(matches_location_search("A20J06-W01", " A20J06-W00 "));
    EXPECT_TRUE(matches_location_search("A20J06-W01", " a20j06-w01 "));
    EXPECT_TRUE(matches_location_search("A20J06-W01", " "));
}

TEST(ShelfReport, KeepsZeroReadsAndOrdersProductsWithValidatedTags) {
    using namespace argus;
    const inventory::Product shoe{{"990-02-0001"}, "Canvas shoe", inventory::Category::shoes};
    const inventory::Product tee{{"990-01-0001"}, "Cotton tee", inventory::Category::clothing};
    const inventory::Epc epc{"A00000000000000000000001"};
    const inventory::AisleId aisle{"SOFTLINES"};
    const inventory::Assignment assigned{{epc, tee.id, {"SOFT-A1"}}, aisle};
    audit::Report report{inventory::SessionId{1},
                         audit::AuditAisle{aisle},
                         {},
                         {{epc, assigned, audit::Finding::supported, {aisle, 1.0, true}, {}}},
                         {{shoe.id, 2, 0, 0}, {tee.id, 1, 1, 1}}};
    const std::array products{shoe, tee};
    const auto shelf = make_shelf_report(report, products);
    ASSERT_TRUE(shelf);
    const auto restored = decode_shelf_report(encode_shelf_report(*shelf));
    ASSERT_TRUE(restored);
    ASSERT_EQ(restored->products.size(), 2U);
    EXPECT_EQ(restored->products[0].product, tee);
    EXPECT_EQ(restored->products[0].tags.at(0).epc, epc);
    EXPECT_TRUE(restored->products[0].tags.at(0).observed_here);
    EXPECT_EQ(restored->products[1].product, shoe);
    EXPECT_EQ(restored->products[1].observed, 0U);
    EXPECT_TRUE(restored->needs_review());
}

TEST(ShelfReport, RejectsMalformedOrInconsistentPersistedProductData) {
    EXPECT_FALSE(decode_shelf_report(R"({"version":99,"products":[],"unidentified":[]})"));
    EXPECT_FALSE(decode_shelf_report(
        R"({"version":1,"products":[["990-01-0001","Tee",0,1,1,[]]],"unidentified":[]})"));
    EXPECT_FALSE(decode_shelf_report(
        R"({"version":1,"products":[["990-01-0001","Tee",0,1,1,[["A00000000000000000000001",true]]]],"unidentified":["A00000000000000000000001"]})"));
    EXPECT_FALSE(decode_shelf_report(std::string(131073, 'a')));
}

TEST(ShelfReport, ComparesAddedRemovedAndSharedProductsByDpci) {
    using namespace argus;
    const inventory::Product tee{{"990-01-0001"}, "Tee", inventory::Category::clothing};
    const inventory::Product shoe{{"990-02-0001"}, "Canvas shoe", inventory::Category::shoes};
    const inventory::Product radio{
        {"990-03-0001"}, "Pocket radio", inventory::Category::electronics};
    const ShelfReport source{{{tee, 2, 0, {}}, {shoe, 4, 1, {}}}, {}, {}};
    const ShelfReport current{{{tee, 2, 1, {}}, {radio, 1, 1, {}}}, {}, {}};
    const auto comparison = compare_shelf_reports(source, current);
    ASSERT_EQ(comparison.size(), 3U);
    EXPECT_EQ(comparison[0].source, &source.products[0]);
    EXPECT_EQ(comparison[0].current, &current.products[0]);
    EXPECT_EQ(comparison[1].source, &source.products[1]);
    EXPECT_EQ(comparison[1].current, nullptr);
    EXPECT_EQ(comparison[2].source, nullptr);
    EXPECT_EQ(comparison[2].current, &current.products[1]);
}

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

TEST(OccupancyMesh, ReusesAnObservedRevisionAcrossCopiesAndMatchesUncachedGeometry) {
    using namespace argus;
    auto grid = std::get<mapping::Grid>(mapping::Grid::create({ScenarioId{4}, 750, 400, 250}));
    constexpr std::array<std::int8_t, 6> first{6, -2, 0, -2, 0, 6};
    ASSERT_TRUE(grid.restore(first));
    OccupancyMesh cached;
    OccupancyMesh reference;
    cached.update(grid, {RunId{1}, 1, false});
    reference.rebuild(grid);
    ASSERT_EQ(cached.vertices().size(), 16U);
    EXPECT_TRUE(std::ranges::equal(cached.vertices(), reference.vertices()));
    const auto worker_snapshot = grid;
    cached.update(worker_snapshot, {RunId{1}, 1, false});
    EXPECT_EQ(cached.builds(), 1U);

    grid.clear();
    cached.update(grid, {RunId{1}, 4, false});
    reference.rebuild(grid);
    EXPECT_EQ(cached.builds(), 2U);
    EXPECT_TRUE(std::ranges::equal(cached.vertices(), reference.vertices()));
    EXPECT_TRUE(cached.vertices().empty());
}

TEST(OccupancyMesh, SeparatesArchivesRunsAndMapSpecifications) {
    using namespace argus;
    constexpr mapping::Spec spec{ScenarioId{4}, 250, 250, 250};
    auto live = std::get<mapping::Grid>(mapping::Grid::create(spec));
    auto saved = live;
    constexpr std::array<std::int8_t, 1> occupied{6};
    ASSERT_TRUE(saved.restore(occupied));
    OccupancyMesh mesh;
    mesh.update(live, {RunId{1}, 1, false});
    EXPECT_TRUE(mesh.vertices().empty());
    mesh.update(saved, {RunId{1}, 1, true});
    EXPECT_EQ(mesh.vertices().size(), 4U);
    mesh.update(live, {RunId{1}, 1, false});
    EXPECT_TRUE(mesh.vertices().empty());
    mesh.update(saved, {RunId{2}, 1, false});
    EXPECT_EQ(mesh.vertices().size(), 4U);
    auto other = std::get<mapping::Grid>(mapping::Grid::create({ScenarioId{5}, 250, 250, 250}));
    mesh.update(other, {RunId{2}, 1, false});
    EXPECT_TRUE(mesh.vertices().empty());
    EXPECT_EQ(mesh.builds(), 5U);
}

} // namespace
