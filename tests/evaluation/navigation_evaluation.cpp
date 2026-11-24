#include "argus/navigation/navigator.hpp"
#include "argus/simulation/fixtures.hpp"
#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <queue>
#include <utility>

namespace {

using namespace argus;

struct Metrics {
    double reachable_coverage{};
    std::uint32_t ticks{};
    std::uint32_t contacts{};
    std::uint32_t blocked_moves{};
    std::uint32_t obstacle_stops{};
    std::uint32_t false_stops{};
    double distance_m{};
    navigation::Progress progress;
};

world::PointMm mm(mapping::Point point) {
    return {static_cast<std::int32_t>(std::lround(point.x_m * 1000.0)),
            static_cast<std::int32_t>(std::lround(point.y_m * 1000.0))};
}

double reachable_coverage(const world::Snapshot &truth, const mapping::Grid &grid) {
    const auto passable = [&](mapping::Cell cell) {
        const auto center = grid.center(cell);
        if (!center) {
            return false;
        }
        const auto point = mm(*center);
        const auto radius = truth.robot_radius_mm;
        return point.x - radius > truth.floor.min.x && point.y - radius > truth.floor.min.y &&
               point.x + radius < truth.floor.max.x && point.y + radius < truth.floor.max.y &&
               !std::ranges::any_of(truth.solids, [&](const world::SolidView &solid) {
                   return world::touches_disk(solid.bounds, point, radius);
               });
    };
    const auto launch = grid.locate({truth.robot_spawn.x / 1000.0, truth.robot_spawn.y / 1000.0});
    if (!launch || !passable(*launch)) {
        return 0.0;
    }
    std::queue<mapping::Cell> pending;
    std::vector<bool> visited(grid.evidence().size());
    const auto index = [&](mapping::Cell at) { return at.y * grid.columns() + at.x; };
    pending.push(*launch);
    visited.at(index(*launch)) = true;
    std::uint32_t total = 0;
    std::uint32_t observed = 0;
    while (!pending.empty()) {
        const auto at = pending.front();
        pending.pop();
        ++total;
        observed += static_cast<std::uint32_t>(grid.occupancy(at) != mapping::Occupancy::unknown);
        constexpr std::array<std::pair<int, int>, 4> offsets{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        for (const auto &[dx, dy] : offsets) {
            const auto x = static_cast<int>(at.x) + dx;
            const auto y = static_cast<int>(at.y) + dy;
            if (x < 0 || y < 0) {
                continue;
            }
            const mapping::Cell next{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)};
            if (passable(next) && !visited.at(index(next))) {
                const auto center = grid.center(at).value();
                const auto end = grid.center(next).value();
                if (std::ranges::none_of(truth.solids, [&](const world::SolidView &solid) {
                        return world::touches_swept_disk(
                            solid.bounds, {center.x_m * 1000, center.y_m * 1000},
                            {end.x_m * 1000, end.y_m * 1000}, truth.robot_radius_mm);
                    })) {
                    pending.push(next);
                    visited.at(index(next)) = true;
                }
            }
        }
    }
    return static_cast<double>(observed) / total;
}

bool obstacle_in_horizon(const simulation::Warehouse &warehouse, const robot::Pose &pose,
                         std::int32_t radius, std::uint64_t tick) {
    const world::PrecisePointMm from{pose.x.value() * 1000, pose.y.value() * 1000};
    const world::PrecisePointMm to{from.x + 650 * std::cos(pose.heading.value()),
                                   from.y + 650 * std::sin(pose.heading.value())};
    for (std::uint64_t offset = 0; offset <= 12; ++offset) {
        if (warehouse.sweep_collides(from, to, radius + 100, tick + offset)) {
            return true;
        }
    }
    return false;
}

Metrics run(const simulation::Warehouse &warehouse, std::optional<navigation::Point> dock,
            std::span<const navigation::NoGo> zones) {
    const auto scene = warehouse.snapshot(0);
    simulation::BatteryConfig battery;
    if (dock) {
        battery.low_threshold_uj = battery.initial_uj;
    }
    auto robot = std::get<simulation::RobotSimulation>(simulation::RobotSimulation::create(
        warehouse, 0, {}, battery, simulation::warehouse_encoder_effects));
    auto rig = std::get<simulation::SensorRig>(simulation::SensorRig::create(scene.seed));
    auto mapper = std::get<mapping::Mapper>(
        mapping::Mapper::create({scene.scenario_id, static_cast<std::uint32_t>(scene.floor.max.x),
                                 static_cast<std::uint32_t>(scene.floor.max.y), 250}));
    auto navigator =
        navigation::Navigator::create(
            mapper.grid(), {.radius = Meters{scene.robot_radius_mm / 1000.0}, .dock = dock}, zones)
            .value();

    Metrics result;
    robot::Command command;
    std::optional<sensors::LidarScan> scan;
    bool previously_blocked = false;
    while (result.ticks < 2000 && result.progress.state != navigation::State::finished) {
        robot.step(command);
        rig.capture(robot);
        while (auto reading = rig.pop()) {
            const auto accepted = mapper.ingest(*reading);
            if (auto *lidar = std::get_if<sensors::LidarScan>(&*reading);
                lidar && accepted == mapping::Update::accepted) {
                scan = std::move(*lidar);
            }
        }
        const auto telemetry = robot.telemetry();
        command = navigator.update(mapper, telemetry, scan ? &*scan : nullptr);
        const auto truth = robot.debug_truth();
        const auto pose = truth.pose;
        result.distance_m += std::abs(truth.applied.linear.value()) * (robot::step_ms / 1000.0);
        result.progress = navigator.progress();
        const bool blocked = result.progress.reason == navigation::Reason::obstacle;
        if (blocked && !previously_blocked) {
            ++result.obstacle_stops;
            if (!obstacle_in_horizon(warehouse, pose, scene.robot_radius_mm,
                                     telemetry.world_tick)) {
                ++result.false_stops;
            }
        }
        previously_blocked = blocked;
        result.contacts += static_cast<std::uint32_t>(warehouse.collides(
            mm({pose.x.value(), pose.y.value()}), scene.robot_radius_mm - 1, telemetry.world_tick));
        result.blocked_moves += static_cast<std::uint32_t>(telemetry.collision_blocked);
        ++result.ticks;
    }
    result.reachable_coverage = reachable_coverage(scene, mapper.grid());
    return result;
}

Metrics evaluate(std::string_view fixture, std::string_view json,
                 std::optional<navigation::Point> dock = {},
                 std::span<const navigation::NoGo> zones = {}) {
    const auto warehouse = std::get<simulation::Warehouse>(simulation::Warehouse::from_json(json));
    const auto result = run(warehouse, dock, zones);
    std::cout << fixture << " reachable_coverage=" << result.reachable_coverage
              << " seconds=" << result.ticks / 20.0 << " distance_m=" << result.distance_m
              << " contacts=" << result.contacts << " blocked_moves=" << result.blocked_moves
              << " obstacle_stops=" << result.obstacle_stops
              << " false_stops=" << result.false_stops
              << " moving_yields=" << result.progress.moving_yields
              << " replans=" << result.progress.replans << " stuck=" << result.progress.stuck
              << " state=" << navigation::label(result.progress.state)
              << " last_reason=" << navigation::label(result.progress.reason) << '\n';
    return result;
}

Metrics evaluate(std::string_view fixture) {
    return evaluate(fixture, std::get<std::string_view>(simulation::fixture_json(fixture)));
}

} // namespace

int main() {
    const auto small = evaluate("small-backroom");
    const auto cluttered = evaluate("cluttered-receiving");
    const auto broad = evaluate("broad-stockroom");
    constexpr auto stockroom = R"({
        "schemaVersion":"1.0.0", "scenarioId":607, "seed":507, "sizeMm":[4000,3000],
        "robot":{"spawnMm":[2125,1625],"radiusMm":200},
        "fixtures":[], "actors":[], "items":[],
        "zones":[{"id":"aisle","kind":"aisle","boundsMm":[300,1000,1500,1750]},
                 {"id":"door","kind":"doorway","boundsMm":[1500,1250,750,700]},
                 {"id":"receiving","kind":"receiving","boundsMm":[300,300,1800,1300]},
                 {"id":"dock","kind":"dock","boundsMm":[2500,500,1300,1300]},
                 {"id":"charge","kind":"charging","boundsMm":[2700,700,900,900]}]
    })";
    const auto complete = evaluate("compact-stockroom", stockroom);
    constexpr auto crossing = R"({
        "schemaVersion":"1.0.0", "scenarioId":606, "seed":507, "sizeMm":[8000,5000],
        "robot":{"spawnMm":[2125,2125],"radiusMm":200},
        "fixtures":[],
        "zones":[{"id":"aisle","kind":"aisle","boundsMm":[800,1000,3900,3600]},
                 {"id":"door","kind":"doorway","boundsMm":[2500,1000,1000,800]},
                 {"id":"receiving","kind":"receiving","boundsMm":[600,1000,1800,2400]},
                 {"id":"dock","kind":"dock","boundsMm":[4800,1400,1600,1500]},
                 {"id":"charge","kind":"charging","boundsMm":[5000,1600,1000,1050]}],
        "actors":[{"id":"cart","kind":"cart","startMm":[3375,550],"endMm":[3375,4450],
                   "sizeMm":[500,400],"legTicks":155}],
        "items":[]
    })";
    const auto cart = evaluate("crossing-cart", crossing, navigation::Point{5.625, 2.125});
    const std::array<navigation::NoGo, 1> barrier{{{{3.1, 0.0}, {3.2, 5.0}}}};
    const auto inaccessible =
        evaluate("no-go-dock", crossing, navigation::Point{5.625, 2.125}, barrier);
    return small.reachable_coverage >= 0.75 && cluttered.reachable_coverage >= 0.50 &&
                   small.contacts == 0 && cluttered.contacts == 0 && broad.contacts == 0 &&
                   small.blocked_moves == 0 && cluttered.blocked_moves == 0 &&
                   broad.blocked_moves == 0 && cart.contacts == 0 && cart.blocked_moves == 0 &&
                   complete.reachable_coverage == 1.0 && complete.contacts == 0 &&
                   complete.blocked_moves == 0 &&
                   complete.progress.reason == navigation::Reason::no_frontiers &&
                   cart.progress.moving_yields > 0 && cart.progress.replans > 0 &&
                   cart.progress.reason == navigation::Reason::docked &&
                   inaccessible.progress.reason == navigation::Reason::dock_unreachable &&
                   inaccessible.distance_m == 0.0
               ? 0
               : 1;
}
