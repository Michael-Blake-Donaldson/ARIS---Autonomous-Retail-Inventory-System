#include "argus/audit/stockroom.hpp"

#include "argus/simulation/observed_run.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

#include <nlohmann/json.hpp>

namespace argus::audit {
namespace {

using inventory::Error;
using inventory::SessionId;
using Json = nlohmann::json;

constexpr std::array rack_x{2200, 7800, 13400, 19000};
constexpr std::array heights{800, 1250, 1700};
constexpr int width_mm = 1000;
constexpr int length_mm = 3400;
constexpr int bottom_mm = 4800;
constexpr unsigned waco_count = 10;
constexpr double mm = 1000.0;
constexpr auto pi = std::numbers::pi;

std::string code(std::size_t aisle, unsigned shelf, unsigned waco) {
    auto id = std::string{stockroom_aisles[aisle]} + "J0" + std::to_string(shelf);
    if (waco != 0) {
        id += waco < 10 ? "-W0" : "-W";
        id += std::to_string(waco);
    }
    return id;
}

std::vector<StockLocation> locations() {
    std::vector<StockLocation> result;
    result.reserve(21);
    for (std::size_t aisle = 0; aisle < rack_x.size(); ++aisle) {
        for (unsigned level = 0; level < heights.size(); ++level) {
            const auto add = [&](unsigned waco, double y, double half) {
                const auto shelf = level + 5;
                result.push_back({{code(aisle, shelf, waco)},
                                  aisle,
                                  shelf,
                                  waco,
                                  {(rack_x[aisle] + 820) / mm, y / mm},
                                  Meters{heights[level] / mm},
                                  Meters{half / mm}});
            };
            if (aisle == 1 && level == 1) {
                for (unsigned waco = 1; waco <= waco_count; ++waco) {
                    constexpr auto pitch = length_mm / waco_count;
                    add(waco, bottom_mm + pitch * (static_cast<double>(waco) - 0.5), pitch / 2.0);
                }
            } else {
                add(0, bottom_mm + length_mm / 2.0, length_mm / 2.0);
            }
        }
    }
    return result;
}

inventory::Epc demo_epc(std::size_t index) {
    const auto tail = std::to_string(index + 101);
    return {"B" + std::string(23 - tail.size(), '0') + tail};
}

std::string world_aisle(std::size_t index) { return "aisle-" + std::to_string(index + 1); }

struct FocusReset {
    simulation::SensorRig &rig;
    ~FocusReset() { static_cast<void>(rig.focus_rfid(std::nullopt)); }
};

bool navigate_to(simulation::ObservedRun &run, navigation::Navigator &navigator,
                 navigation::Destination goal) {
    if (!navigator.set_destination(goal)) {
        return false;
    }
    robot::Command command;
    for (unsigned tick = 0; tick < 1450; ++tick) {
        static_cast<void>(simulation::step_observed(run, command, navigator));
        command =
            navigator.update(run.mapper, run.robot.telemetry(), run.lidar ? &*run.lidar : nullptr);
        const auto progress = navigator.progress();
        if (progress.state == navigation::State::finished) {
            return progress.reason == navigation::Reason::destination_reached;
        }
    }
    return false;
}

bool approach(simulation::ObservedRun &run, navigation::Navigator &navigator, Point target) {
    const auto pose = run.robot.telemetry().estimated_pose;
    double x = pose.x.value();
    constexpr double cross_aisle_y = 3.125;
    constexpr double max_leg = 2.0;
    if (std::abs(target.x_m - x) > 4.0) {
        const auto heading = robot::Radians{target.x_m > x ? 0.0 : pi};
        if (std::abs(pose.y.value() - cross_aisle_y) > 0.25 &&
            !navigate_to(run, navigator, {{x, cross_aisle_y}, heading})) {
            return false;
        }
        // Each leg is short enough to plan with current cross-aisle observations.
        for (unsigned leg = 0; std::abs(target.x_m - x) > 0.25 && leg < 12; ++leg) {
            x += std::clamp(target.x_m - x, -max_leg, max_leg);
            if (!navigate_to(run, navigator, {{x, cross_aisle_y}, heading})) {
                return false;
            }
        }
    }
    return navigate_to(run, navigator, {target, robot::Radians{pi}});
}
Result<LocationView, Error> read_pass(inventory::Repository &repository,
                                      const StockLocation &location, SessionId session,
                                      simulation::ObservedRun &run,
                                      navigation::Navigator &navigator, double offset) {
    static_cast<void>(run.sensors.focus_rfid(std::nullopt));
    if (!approach(run, navigator, {location.center.x_m + 1.15, location.center.y_m + offset})) {
        return Error::invalid_data;
    }
    const auto estimate = run.mapper.estimate();
    if (navigator.progress().reason != navigation::Reason::destination_reached || !estimate ||
        !estimate->reliable) {
        return Error::invalid_data;
    }
    const auto &pose = estimate->pose;
    const double east = location.center.x_m - pose.x.value();
    const double aim_y = location.center.y_m + (location.waco == 0 ? offset / 3.8 : 0.0);
    const double north = aim_y - pose.y.value();
    const double yaw = pose.heading.value();
    const double cs = std::cos(yaw);
    const double sn = std::sin(yaw);
    const auto width = location.waco != 0 ? 0.095 : 1.60;
    constexpr double depth = 0.20;
    const simulation::RfidFocus focus{Meters{east * cs + north * sn},
                                      Meters{north * cs - east * sn},
                                      location.height,
                                      Meters{depth},
                                      Meters{width},
                                      Meters{0.14}};
    if (!run.sensors.focus_rfid(focus)) {
        return Error::invalid_data;
    }
    // Conservative lateral envelope at the head, including the pose error and aim rotation.
    const auto confined = [&](const mapping::Estimate &sample) {
        const auto angle = sample.pose.heading.value();
        const auto sine = std::sin(angle);
        const auto cosine = std::cos(angle);
        const auto center =
            sample.pose.y.value() + focus.forward.value() * sine + focus.left.value() * cosine;
        const double lever = std::hypot(east, north) + std::hypot(depth, width);
        const double error = sample.distance_error_m +
                             2.0 * lever * std::sin(std::min(pi, sample.heading_error_rad) / 2);
        if (location.waco == 0) {
            // Whole shelves have no slots on their level. Neighboring racks are 5.6 m apart.
            const auto x =
                sample.pose.x.value() + focus.forward.value() * cosine - focus.left.value() * sine;
            return std::abs(x - location.center.x_m) + depth * std::abs(cosine) +
                       width * std::abs(sine) + error <
                   2.4;
        }
        return std::abs(center - location.center.y_m) + width * std::abs(cosine) +
                   depth * std::abs(sine) + error <
               location.half_width.value();
    };
    LocationView pass{{pose.x.value(), pose.y.value()}, {}, confined(*estimate)};
    unsigned frames = 0;
    for (unsigned tick = 0; tick < 64 && frames < 20; ++tick) {
        const auto scan = simulation::step_observed(run, {}, navigator);
        const auto &current = run.mapper.estimate();
        if (!scan || !current || !current->reliable ||
            current->sample_tick != scan->header.sample_tick || scan->truncated_reads != 0) {
            continue;
        }
        pass.confined = pass.confined && confined(*current);
        if (const auto saved = repository.record_rfid(
                session, *scan, inventory::AntennaId{offset < 0 ? "FOCUS-A" : "FOCUS-B"});
            std::holds_alternative<Error>(saved)) {
            return std::get<Error>(saved);
        }
        ++frames;
        for (const auto &read : scan->reads) {
            pass.reads.insert(read.epc);
        }
        if (pass.reads.size() > max_tags) {
            return Error::too_large;
        }
    }
    if (frames < min_coverage_frames) {
        return Error::invalid_data;
    }
    return pass;
}

} // namespace

std::span<const StockLocation> stockroom_locations() {
    static const auto value = locations();
    return value;
}

simulation::SensorConfig stockroom_sensor_config() noexcept {
    simulation::SensorConfig config;
    config.lidar.beams = 360;
    config.lidar.range_m = 30.0;
    config.rfid.range_m = 3.5;
    return config;
}

const inventory::Catalog &stockroom_catalog() {
    static const auto catalog = [] {
        inventory::Catalog value;
        value.products = {
            {{"990-01-0001"}, "North Loop, cotton tee", inventory::Category::clothing},
            {{"990-02-0001"}, "Juniper canvas shoe", inventory::Category::shoes},
            {{"990-03-0001"}, "Orbit pocket radio", inventory::Category::electronics},
        };
        std::size_t index = 0;
        for (const auto &location : stockroom_locations()) {
            const auto &product =
                value.products[location.aisle == 0 && location.shelf == 5   ? 0
                               : location.aisle == 1 && location.shelf == 5 ? 2
                                                                            : index % 3];
            value.bins.push_back({location.id, {std::string{stockroom_aisles[location.aisle]}}});
            value.units.push_back({demo_epc(index++), product.id, location.id});
            value.stock.push_back({product.id, location.id, 1});
            if (location.id.value == "A10J05") {
                value.stock.back().quantity = 4;
                value.stock.push_back({value.products[1].id, location.id, 2});
            } else if (location.id.value == "A20J05") {
                value.stock.back().quantity = 6;
            }
        }
        return value;
    }();
    return catalog;
}

Result<simulation::Warehouse, simulation::ScenarioError> stockroom_world(RobotId robot) {
    if (robot.value() < 1 || robot.value() > stockroom_robot_count) {
        return simulation::ScenarioError::invalid_schema;
    }
    Json racks = Json::array();
    Json zones = Json::array();
    Json items = Json::array();
    for (std::size_t i = 0; i < rack_x.size(); ++i) {
        const auto aisle = world_aisle(i);
        racks.push_back({{"id", "rack-" + aisle},
                         {"kind", "rack"},
                         {"boundsMm", {rack_x[i], bottom_mm, width_mm, length_mm}},
                         {"aisle", aisle},
                         {"shelfHeightsMm", heights}});
        zones.push_back({{"id", aisle},
                         {"kind", "aisle"},
                         {"boundsMm", {rack_x[i] + width_mm, bottom_mm, 2200, length_mm}}});
    }
    zones.push_back(
        {{"id", "receiving"}, {"kind", "receiving"}, {"boundsMm", {800, 800, 2500, 1600}}});
    zones.push_back({{"id", "door"}, {"kind", "doorway"}, {"boundsMm", {5100, 800, 1800, 1500}}});
    zones.push_back(
        {{"id", "service-dock"}, {"kind", "dock"}, {"boundsMm", {20100, 800, 2200, 1600}}});
    zones.push_back(
        {{"id", "charge"}, {"kind", "charging"}, {"boundsMm", {20500, 1050, 1000, 1000}}});
    std::size_t index = 0;
    for (const auto &location : stockroom_locations()) {
        const auto y = static_cast<int>(std::lround(location.center.y_m * mm)) - bottom_mm;
        items.push_back({{"epc", demo_epc(index++).value},
                         {"rack", "rack-" + world_aisle(location.aisle)},
                         {"shelf", location.shelf - 4},
                         {"offsetMm", {820, y}}});
    }
    const auto world =
        Json{{"schemaVersion", "1.0.0"},
             {"scenarioId", stockroom_scenario.value()},
             {"seed", 5437 + robot.value()},
             {"sizeMm", {24000, 10600}},
             {"robot", {{"spawnMm", {4125 + (robot.value() - 1) * 5600, 3125}}, {"radiusMm", 300}}},
             {"fixtures", std::move(racks)},
             {"zones", std::move(zones)},
             {"actors", Json::array()},
             {"items", std::move(items)}};
    return simulation::Warehouse::from_json(world.dump());
}

Result<LocationSurvey, Error> survey_location(inventory::Repository &repository,
                                              const StockLocation &location, SessionId session,
                                              RobotId robot, simulation::ObservedRun &run) {
    const auto locations = stockroom_locations();
    const auto chosen = std::ranges::find(locations, location.id, &StockLocation::id);
    if (chosen == locations.end() || run.mapper.grid().spec().scenario != stockroom_scenario ||
        robot.value() == 0 || robot.value() > stockroom_robot_count) {
        return Error::invalid_data;
    }
    auto navigator = navigation::Navigator::create(run.mapper.grid(), {.radius = run.radius});
    const auto tick = run.robot.telemetry().sample_tick;
    if (!navigator || tick > static_cast<std::uint64_t>(INT64_MAX) / robot::step_ms) {
        return Error::invalid_data;
    }
    const auto started = repository.start_session(
        {session, MissionId{session.value()}, robot, tick * robot::step_ms});
    if (const auto *error = std::get_if<Error>(&started)) {
        return *error;
    }
    const FocusReset reset{run.sensors};
    auto first = read_pass(repository, *chosen, session, run, *navigator, -0.38);
    if (const auto *error = std::get_if<Error>(&first)) {
        return *error;
    }
    auto second = read_pass(repository, *chosen, session, run, *navigator, 0.38);
    if (const auto *error = std::get_if<Error>(&second)) {
        return *error;
    }
    return attribute_location(session, std::get<LocationView>(first),
                              std::get<LocationView>(second));
}

Result<LocationSurvey, Error> attribute_location(SessionId session, const LocationView &a,
                                                 const LocationView &b) {
    if (session.value() == 0 || a.reads.size() > max_tags || b.reads.size() > max_tags ||
        !std::isfinite(a.origin.x_m) || !std::isfinite(a.origin.y_m) ||
        !std::isfinite(b.origin.x_m) || !std::isfinite(b.origin.y_m)) {
        return Error::invalid_data;
    }
    auto tags = a.reads;
    tags.insert(b.reads.begin(), b.reads.end());
    if (tags.size() > max_tags) {
        return Error::too_large;
    }
    LocationSurvey evidence{session, {}, {}};
    const bool separated =
        a.confined && b.confined &&
        std::hypot(a.origin.x_m - b.origin.x_m, a.origin.y_m - b.origin.y_m) >= 0.4;
    for (const auto &tag : tags) {
        if (!inventory::valid(inventory::Epc{tag})) {
            return Error::invalid_data;
        }
        auto &out = separated && a.reads.contains(tag) && b.reads.contains(tag)
                        ? evidence.observed
                        : evidence.unresolved;
        out.push_back({tag});
    }
    return evidence;
}

} // namespace argus::audit
