#include "application.hpp"
#include "argus/core/random.hpp"
#include "robot_run.hpp"
#include "sensor_output.hpp"
#include "warehouse_io.hpp"

#include <iomanip>
#include <iostream>
#include <optional>
#include <utility>

namespace {

void write_robot(const argus::robot::Telemetry &read) {
    const auto &pose = read.estimated_pose;
    std::cout << std::fixed << std::setprecision(6) << std::boolalpha
              << ",\"robot\":{\"odometry\":{\"xM\":" << pose.x.value()
              << ",\"yM\":" << pose.y.value() << ",\"headingRad\":" << pose.heading.value()
              << "},\"commandedMps\":" << read.commanded.linear.value()
              << ",\"commandedRadps\":" << read.commanded.angular.value()
              << ",\"measuredMps\":" << read.measured.linear.value()
              << ",\"measuredRadps\":" << read.measured.angular.value()
              << ",\"energyUj\":" << read.energy_uj << ",\"capacityUj\":" << read.capacity_uj
              << ",\"lowBattery\":" << read.low_battery << ",\"depleted\":" << read.depleted
              << ",\"docked\":" << read.docked << ",\"charging\":" << read.charging
              << ",\"emergencyLatched\":" << read.emergency_latched
              << ",\"collisionBlocked\":" << read.collision_blocked
              << ",\"worldHeld\":" << read.world_held << '}';
}

void write_snapshot(const argus::world::Snapshot &snapshot,
                    const argus::robot::Telemetry *robot = nullptr,
                    argus::simulation::SensorRig *sensors = nullptr) {
    std::cout << "{\"scenarioId\":" << snapshot.scenario_id.value() << ",\"seed\":" << snapshot.seed
              << ",\"tick\":" << snapshot.tick << ",\"sizeMm\":[" << snapshot.floor.max.x << ','
              << snapshot.floor.max.y << "],\"fixtureCount\":" << snapshot.solids.size()
              << ",\"zoneCount\":" << snapshot.zones.size() << ",\"actors\":[";
    const char *separator = "";
    for (const auto &actor : snapshot.actors) {
        const auto &bounds = actor.bounds;
        std::cout << separator << "{\"id\":\"" << actor.id << "\",\"boundsMm\":[" << bounds.min.x
                  << ',' << bounds.min.y << ',' << bounds.max.x - bounds.min.x << ','
                  << bounds.max.y - bounds.min.y << "]}";
        separator = ",";
    }
    std::cout << ']';
    if (robot != nullptr) {
        write_robot(*robot);
    }
    if (sensors != nullptr) {
        argus::app::write_sensor_report(std::cout, *sensors);
    }
    std::cout << "}\n";
}

argus::app::Status run_headless(const argus::app::Options &options) {
    using argus::app::WarehouseSource;
    using argus::simulation::RobotError;
    using argus::simulation::RobotSimulation;
    using argus::simulation::ScenarioError;
    using argus::simulation::SensorError;
    using argus::simulation::SensorRig;
    using argus::simulation::Warehouse;
    if (options.warehouse_source == WarehouseSource::export_fixture) {
        return argus::app::export_warehouse(options.fixture);
    }
    if (options.warehouse_source == WarehouseSource::none) {
        argus::Random random{options.seed};
        std::cout << "{\"seed\":" << options.seed << ",\"firstSample\":" << random.next() << "}\n";
    } else {
        const auto loaded = argus::app::open_warehouse(options);
        if (const auto *error = std::get_if<ScenarioError>(&loaded)) {
            return *error;
        }
        const auto &warehouse = std::get<Warehouse>(loaded);
        if (options.drive_steps == 0 && options.sensor_steps == 0) {
            write_snapshot(warehouse.snapshot(options.tick));
        } else {
            using argus::simulation::EncoderEffects;
            const auto effects = options.sensor_steps > 0
                                     ? argus::simulation::warehouse_encoder_effects
                                     : EncoderEffects{};
            auto created = RobotSimulation::create(warehouse, options.tick, {}, {}, effects);
            if (const auto *error = std::get_if<RobotError>(&created)) {
                return *error;
            }
            auto &robot = std::get<RobotSimulation>(created);
            std::optional<SensorRig> sensors;
            if (options.sensor_steps > 0) {
                auto result = SensorRig::create(warehouse.snapshot(options.tick).seed);
                if (const auto *error = std::get_if<SensorError>(&result)) {
                    return *error;
                }
                sensors.emplace(std::get<SensorRig>(std::move(result)));
                for (std::uint32_t step = 0; step < options.sensor_steps; ++step) {
                    robot.step(argus::app::probe_command);
                    sensors->capture(robot);
                }
            } else {
                argus::app::run_drive_probe(robot, options.drive_steps);
            }
            const auto telemetry = robot.telemetry();
            write_snapshot(warehouse.snapshot(telemetry.world_tick), &telemetry,
                           sensors ? &*sensors : nullptr);
        }
    }
    if (!std::cout.flush()) {
        return argus::app::Error::output_failure;
    }
    return std::monostate{};
}

} // namespace

int main(int argc, char **argv) {
    return argus::app::run_application(argc, argv, "argus-headless", run_headless);
}
