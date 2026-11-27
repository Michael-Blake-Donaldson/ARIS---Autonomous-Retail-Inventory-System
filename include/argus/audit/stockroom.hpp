#pragma once

#include "argus/audit/mission.hpp"
#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"
#include "argus/simulation/warehouse.hpp"

#include <set>

namespace argus::audit {

inline constexpr ScenarioId stockroom_scenario{802};
inline constexpr std::size_t stockroom_robot_count = 2;
inline constexpr double stockroom_encoder_step_m = 0.00002;
inline constexpr std::array<std::string_view, 4> stockroom_aisles{"A10", "A20", "A30", "A40"};

struct StockLocation {
    inventory::BinId id;
    std::size_t aisle;
    unsigned shelf;
    unsigned waco;
    Point center;
    Meters height;
    Meters half_width;
};

struct LocationSurvey {
    inventory::SessionId session;
    std::vector<inventory::Epc> observed;
    std::vector<inventory::Epc> unresolved;
};

struct LocationView {
    Point origin;
    std::set<std::string, std::less<>> reads;
    bool confined{};
};

[[nodiscard]] Result<LocationSurvey, inventory::Error>
attribute_location(inventory::SessionId session, const LocationView &first,
                   const LocationView &second);

[[nodiscard]] std::span<const StockLocation> stockroom_locations();
[[nodiscard]] const inventory::Catalog &stockroom_catalog();
[[nodiscard]] Result<simulation::Warehouse, simulation::ScenarioError>
stockroom_world(RobotId robot);
[[nodiscard]] Result<simulation::Warehouse, simulation::ScenarioError> shared_stockroom_world();
[[nodiscard]] simulation::RobotStart stockroom_berth(RobotId robot);
[[nodiscard]] bool return_to_berth(simulation::ObservedRun &run, RobotId robot);
[[nodiscard]] simulation::SensorConfig stockroom_sensor_config() noexcept;
[[nodiscard]] Result<LocationSurvey, inventory::Error>
survey_location(inventory::Repository &repository, const StockLocation &location,
                inventory::SessionId session, RobotId robot, simulation::ObservedRun &run);

} // namespace argus::audit
