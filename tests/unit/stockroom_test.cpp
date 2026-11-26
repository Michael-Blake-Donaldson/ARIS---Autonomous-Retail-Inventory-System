#include "argus/audit/stockroom.hpp"
#include "argus/simulation/observed_run.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>

namespace {

using namespace argus;
using namespace argus::audit;

class CatalogDirectory {
  public:
    CatalogDirectory()
        : root{std::filesystem::temp_directory_path() /
               ("argus-stockroom-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))} {
        if (!std::filesystem::create_directory(root)) {
            throw std::runtime_error("Cannot create a stockroom test directory");
        }
    }
    CatalogDirectory(const CatalogDirectory &) = delete;
    CatalogDirectory &operator=(const CatalogDirectory &) = delete;
    ~CatalogDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    const std::filesystem::path root;
};

TEST(Stockroom, DividedShelfContributesTenLeavesInsteadOfAParent) {
    const auto locations = stockroom_locations();
    ASSERT_EQ(locations.size(), 21U);
    EXPECT_EQ(std::ranges::count(locations, std::size_t{1}, &StockLocation::aisle), 12);
    EXPECT_EQ(std::ranges::count_if(locations,
                                    [](const auto &location) {
                                        return location.aisle == 1 && location.shelf == 6 &&
                                               location.waco != 0;
                                    }),
              10);
    EXPECT_EQ(std::ranges::find_if(
                  locations, [](const auto &location) { return location.id.value == "A20J06"; }),
              locations.end());
    EXPECT_EQ(inventory::validate(stockroom_catalog()).index(), 0U);
    for (std::size_t index = 0; index < stockroom_robot_count; ++index) {
        const auto warehouse = stockroom_world(RobotId{index + 1});
        const auto *world = std::get_if<simulation::Warehouse>(&warehouse);
        ASSERT_NE(world, nullptr);
        const auto layout = world->snapshot(0);
        EXPECT_EQ(layout.scenario_id, stockroom_scenario);
        EXPECT_EQ(layout.solids.size(), stockroom_aisles.size());
    }
}

TEST(Stockroom, AdjacentUncertainOrRepeatedViewDoesNotConfirmInventory) {
    const std::string present = "B00000000000000000000101";
    const std::string neighbor = "B00000000000000000000102";
    const inventory::SessionId session{1};
    const LocationView first{{4.0, 5.0}, {present, neighbor}, true};
    LocationView second{{4.0, 5.8}, {present}, true};
    const auto confirmed = std::get<LocationSurvey>(attribute_location(session, first, second));
    ASSERT_EQ(confirmed.observed, (std::vector<inventory::Epc>{{present}}));
    EXPECT_EQ(confirmed.unresolved, (std::vector<inventory::Epc>{{neighbor}}));
    second.confined = false;
    auto uncertain = std::get<LocationSurvey>(attribute_location(session, first, second));
    EXPECT_TRUE(uncertain.observed.empty());
    EXPECT_EQ(uncertain.unresolved.size(), 2U);
    second.confined = true;
    second.origin = {4.0, 5.1};
    const auto repeated = std::get<LocationSurvey>(attribute_location(session, first, second));
    EXPECT_TRUE(repeated.observed.empty());
    EXPECT_EQ(repeated.unresolved.size(), 2U);
}

TEST(Stockroom, FarAisleCanBeAuditedUsingObservedCrossAisleRoute) {
    CatalogDirectory directory;
    auto repository =
        std::get<inventory::Repository>(inventory::Repository::open(directory.root / "catalog.db"));
    ASSERT_EQ(repository.insert_catalog(stockroom_catalog()).index(), 0U);
    const RobotId robot_id{2};
    const auto warehouse = std::get<simulation::Warehouse>(stockroom_world(robot_id));
    simulation::MotionConfig motion;
    motion.encoder_step_m = stockroom_encoder_step_m;
    auto robot = std::get<simulation::RobotSimulation>(
        simulation::RobotSimulation::create(warehouse, 0, motion, {}, {}));
    const auto config = stockroom_sensor_config();
    auto sensors = std::get<simulation::SensorRig>(
        simulation::SensorRig::create(warehouse.snapshot(0).seed, config));
    auto mapper =
        std::get<mapping::Mapper>(mapping::Mapper::create({stockroom_scenario, 24000, 10600, 250}));
    std::optional<sensors::LidarScan> lidar;
    navigation::Reason last{};
    simulation::ObservedRun run{robot,
                                sensors,
                                mapper,
                                lidar,
                                [&](const auto &navigator, const auto *) {
                                    last = navigator.progress().reason;
                                    return true;
                                },
                                Meters{0.30}};
    const auto list = stockroom_locations();
    const auto target = std::ranges::find_if(
        list, [](const auto &location) { return location.id.value == "A40J07"; });
    ASSERT_NE(target, list.end());
    auto result = survey_location(repository, *target, inventory::SessionId{1}, robot_id, run);
    const auto *survey = std::get_if<LocationSurvey>(&result);
    ASSERT_NE(survey, nullptr) << "sample " << robot.telemetry().sample_tick << ", last decision "
                               << navigation::label(last);
    EXPECT_EQ(survey->observed.size() + survey->unresolved.size(), 1U);
}

} // namespace
