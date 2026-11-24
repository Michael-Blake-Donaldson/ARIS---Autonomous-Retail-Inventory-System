#include "argus/mapping/mapper.hpp"
#include "argus/simulation/fixtures.hpp"
#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <variant>

namespace {

using namespace argus;

struct Metrics {
    double coverage;
    double occupied_iou;
    double max_position_m;
    double max_heading_rad;
};

bool occupied_truth(const world::Snapshot &truth, const mapping::Grid &grid, mapping::Cell cell) {
    const auto center = grid.center(cell).value();
    const auto x = center.x_m * 1000.0;
    const auto y = center.y_m * 1000.0;
    const bool perimeter =
        cell.x == 0 || cell.y == 0 || cell.x + 1 == grid.columns() || cell.y + 1 == grid.rows();
    return perimeter || std::ranges::any_of(truth.solids, [x, y](const world::SolidView &solid) {
               return solid.bounds.min.x <= x && solid.bounds.max.x >= x &&
                      solid.bounds.min.y <= y && solid.bounds.max.y >= y;
           });
}

Metrics evaluate(const simulation::Warehouse &warehouse, const world::Snapshot &truth,
                 simulation::EncoderEffects effects) {
    const mapping::Spec spec{truth.scenario_id, static_cast<std::uint32_t>(truth.floor.max.x),
                             static_cast<std::uint32_t>(truth.floor.max.y), 250};
    auto mapper = std::get<mapping::Mapper>(mapping::Mapper::create(spec));
    auto robot = std::get<simulation::RobotSimulation>(
        simulation::RobotSimulation::create(warehouse, 0, {}, {}, effects));
    auto sensor_config = simulation::SensorConfig{};
    sensor_config.lidar.range_noise_m = 0.0;
    sensor_config.lidar.missing_probability = 0.0;
    auto rig =
        std::get<simulation::SensorRig>(simulation::SensorRig::create(truth.seed, sensor_config));
    double max_position = 0.0;
    double max_heading = 0.0;
    for (int step = 0; step < 120; ++step) {
        const bool turn = step >= 65 && step < 100;
        robot.step({.target = {MetersPerSecond{turn ? 0.0 : 0.65},
                               robot::RadiansPerSecond{turn ? 1.1 : 0.0}},
                    .deadman = true});
        rig.capture(robot);
        while (const auto reading = rig.pop()) {
            mapper.ingest(*reading);
        }
        const auto &estimate = mapper.estimate().value().pose;
        const auto actual = robot.debug_truth().pose;
        max_position = std::max(max_position, std::hypot(estimate.x.value() - actual.x.value(),
                                                         estimate.y.value() - actual.y.value()));
        max_heading = std::max(
            max_heading, std::abs(std::remainder(estimate.heading.value() - actual.heading.value(),
                                                 2.0 * std::numbers::pi)));
    }

    const auto &grid = mapper.grid();
    const auto counts = grid.counts();
    std::uint32_t intersection{};
    std::uint32_t union_count{};
    for (std::uint32_t y = 0; y < grid.rows(); ++y) {
        for (std::uint32_t x = 0; x < grid.columns(); ++x) {
            const mapping::Cell cell{x, y};
            const bool expected = occupied_truth(truth, grid, cell);
            const bool predicted = grid.occupancy(cell) == mapping::Occupancy::occupied;
            intersection += static_cast<std::uint32_t>(expected && predicted);
            union_count += static_cast<std::uint32_t>(expected || predicted);
        }
    }
    return {static_cast<double>(counts.free + counts.occupied) /
                static_cast<double>(grid.evidence().size()),
            static_cast<double>(intersection) / union_count, max_position, max_heading};
}

void print(const char *case_name, Metrics result) {
    std::cout << case_name << " coverage=" << result.coverage
              << " occupied_iou=" << result.occupied_iou
              << " max_position_m=" << result.max_position_m
              << " max_heading_rad=" << result.max_heading_rad << '\n';
}

} // namespace

int main() {
    const auto fixture =
        std::get<std::string_view>(argus::simulation::fixture_json("small-backroom"));
    const auto warehouse =
        std::get<argus::simulation::Warehouse>(argus::simulation::Warehouse::from_json(fixture));
    const auto truth = warehouse.snapshot(0);
    const auto ideal = evaluate(warehouse, truth, {});
    const auto noisy = evaluate(warehouse, truth, argus::simulation::warehouse_encoder_effects);
    std::cout << std::fixed << std::setprecision(6);
    print("quantized", ideal);
    print("noisy", noisy);
    return ideal.coverage > 0.25 && ideal.occupied_iou > 0.10 && noisy.coverage > 0.25 &&
                   noisy.occupied_iou > 0.10 && noisy.max_position_m > ideal.max_position_m + 0.01
               ? 0
               : 1;
}
