#include "map_run.hpp"

#include "argus/mapping/map_io.hpp"
#include "argus/mapping/mapper.hpp"
#include "argus/navigation/navigator.hpp"
#include "warehouse_io.hpp"

#include <cstdio>
#include <iomanip>
#include <iostream>
#include <utility>

namespace argus::app {
namespace {

Status report(const mapping::Grid &grid, const std::optional<mapping::Estimate> &estimate = {}) {
    const auto counts = grid.counts();
    const auto spec = grid.spec();
    std::cout << "{\"method\":\"occupancy mapping + odometry\",\"scenarioId\":"
              << spec.scenario.value()
              << ",\"frame\":\"warehouse-east-north\",\"cellMm\":" << spec.cell_mm
              << ",\"columns\":" << grid.columns() << ",\"rows\":" << grid.rows()
              << ",\"unknown\":" << counts.unknown << ",\"free\":" << counts.free
              << ",\"occupied\":" << counts.occupied;
    if (estimate) {
        std::cout << std::fixed << std::setprecision(6) << std::boolalpha
                  << ",\"estimate\":{\"sampleTick\":" << estimate->sample_tick
                  << ",\"xM\":" << estimate->pose.x.value()
                  << ",\"yM\":" << estimate->pose.y.value()
                  << ",\"headingRad\":" << estimate->pose.heading.value()
                  << ",\"distanceErrorM\":" << estimate->distance_error_m
                  << ",\"headingErrorRad\":" << estimate->heading_error_rad
                  << ",\"reliable\":" << estimate->reliable << '}';
    }
    std::cout << "}\n";
    return std::cout.flush() ? Status{std::monostate{}} : Status{Error::output_failure};
}

robot::Command survey(std::uint32_t step) {
    const bool turning = step % 100U >= 70U;
    return {.target = {MetersPerSecond{turning ? 0.0 : 0.45},
                       robot::RadiansPerSecond{turning ? 1.2 : 0.0}},
            .deadman = true};
}

Status explore(const Options &options, const world::Snapshot &scene, mapping::Mapper &mapper,
               simulation::RobotSimulation &robot, simulation::SensorRig &rig) {
    navigation::Settings settings{.radius = Meters{scene.robot_radius_mm / 1000.0}};
    if (options.dock_x_mm != 0) {
        settings.dock = navigation::Point{options.dock_x_mm / 1000.0, options.dock_y_mm / 1000.0};
    }
    auto navigator = navigation::Navigator::create(mapper.grid(), settings);
    if (!navigator) {
        return Error::invalid_dock;
    }
    robot::Command command;
    std::optional<sensors::LidarScan> scan;
    std::uint32_t steps = 0;
    do {
        robot.step(command);
        rig.capture(robot);
        while (auto observation = rig.pop()) {
            const auto result = mapper.ingest(*observation);
            if (auto *lidar = std::get_if<sensors::LidarScan>(&*observation);
                lidar && result == mapping::Update::accepted) {
                scan = std::move(*lidar);
            }
        }
        command = navigator->update(mapper, robot.telemetry(), scan ? &*scan : nullptr);
        ++steps;
    } while (steps < options.map_steps &&
             navigator->progress().state != navigation::State::finished);
    const auto progress = navigator->progress();
    const auto counts = mapper.grid().counts();
    std::cout << "{\"method\":\"frontier exploration\",\"state\":\""
              << navigation::label(progress.state) << "\",\"reason\":\""
              << (progress.state == navigation::State::finished ? navigation::label(progress.reason)
                                                                : "step_limit")
              << "\",\"lastDecision\":\"" << navigation::label(progress.reason)
              << "\",\"steps\":" << steps << ",\"plans\":" << progress.plans
              << ",\"yields\":" << progress.yields << ",\"movingYields\":" << progress.moving_yields
              << ",\"replans\":" << progress.replans << ",\"stuck\":" << progress.stuck
              << ",\"free\":" << counts.free << ",\"occupied\":" << counts.occupied
              << ",\"unknown\":" << counts.unknown << "}\n";
    return std::cout.flush() ? Status{std::monostate{}} : Status{Error::output_failure};
}

} // namespace

mapping::Spec map_spec(const world::Snapshot &scene, std::uint32_t cell_mm) {
    return {scene.scenario_id, static_cast<std::uint32_t>(scene.floor.max.x),
            static_cast<std::uint32_t>(scene.floor.max.y), cell_mm};
}

Status run_map(const Options &options) {
    using namespace simulation;
    using mapping::MapError;
    auto opened = open_warehouse(options);
    if (const auto *error = std::get_if<ScenarioError>(&opened)) {
        return *error;
    }
    const auto &warehouse = std::get<Warehouse>(opened);
    const auto scene = warehouse.snapshot(options.tick);
    const auto spec = map_spec(scene, options.cell_mm);
    if (options.map_mode == MapMode::load_map) {
        if (!use_binary_mode(stdin)) {
            return MapError::input_failure;
        }
        const auto imported = mapping::load(std::cin, spec);
        if (const auto *error = std::get_if<MapError>(&imported)) {
            return *error;
        }
        return report(std::get<mapping::Grid>(imported));
    }

    auto mapped = mapping::Mapper::create(spec);
    if (const auto *error = std::get_if<MapError>(&mapped)) {
        return *error;
    }
    auto created_robot =
        RobotSimulation::create(warehouse, options.tick, {}, {}, warehouse_encoder_effects);
    if (const auto *error = std::get_if<RobotError>(&created_robot)) {
        return *error;
    }
    auto sensors = SensorRig::create(scene.seed);
    if (const auto *error = std::get_if<SensorError>(&sensors)) {
        return *error;
    }
    auto &mapper = std::get<mapping::Mapper>(mapped);
    auto &robot = std::get<RobotSimulation>(created_robot);
    auto &rig = std::get<SensorRig>(sensors);
    if (options.map_mode == MapMode::explore) {
        return explore(options, scene, mapper, robot, rig);
    }
    for (std::uint32_t step = 0; step < options.map_steps; ++step) {
        robot.step(survey(step));
        rig.capture(robot);
        while (const auto observation = rig.pop()) {
            mapper.ingest(*observation);
        }
    }
    if (options.map_mode == MapMode::export_map) {
        if (!use_binary_mode(stdout)) {
            return MapError::output_failure;
        }
        const auto saved = mapping::save(mapper.grid(), std::cout);
        if (const auto *error = std::get_if<MapError>(&saved)) {
            return *error;
        }
        return std::monostate{};
    }
    return report(mapper.grid(), mapper.estimate());
}

} // namespace argus::app
