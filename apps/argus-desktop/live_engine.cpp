#include "live_engine.hpp"

#include "map_run.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace argus::desktop {
namespace {

constexpr std::size_t trace_limit = 1024;
constexpr std::size_t trace_batch = 128;
constexpr std::size_t route_limit = 4096;
constexpr std::size_t tag_limit = 1024;

mapping::Point position(robot::Pose pose) noexcept { return {pose.x.value(), pose.y.value()}; }

} // namespace

LiveEngine::LiveEngine(simulation::Warehouse warehouse, simulation::RobotSimulation robot,
                       simulation::SensorRig sensors, mapping::Mapper mapper, Layout layout)
    : warehouse_{std::move(warehouse)}, initial_{robot}, robot_{std::move(robot)},
      sensors_{std::move(sensors)}, mapper_{std::move(mapper)}, layout_{std::move(layout)} {
    trace_.reserve(trace_limit);
}

Result<LiveEngine, app::Failure> LiveEngine::create(simulation::Warehouse warehouse,
                                                    std::uint64_t start_tick) {
    auto declared = warehouse.snapshot(start_tick);
    auto mapper = mapping::Mapper::create(app::map_spec(declared));
    if (const auto *error = std::get_if<mapping::MapError>(&mapper)) {
        return app::Failure{*error};
    }
    auto robot = simulation::RobotSimulation::create(warehouse, start_tick, {}, {},
                                                     simulation::warehouse_encoder_effects);
    if (const auto *error = std::get_if<simulation::RobotError>(&robot)) {
        return app::Failure{*error};
    }
    auto sensors = simulation::SensorRig::create(declared.seed);
    if (const auto *error = std::get_if<simulation::SensorError>(&sensors)) {
        return app::Failure{*error};
    }
    return LiveEngine{std::move(warehouse),
                      std::get<simulation::RobotSimulation>(std::move(robot)),
                      std::get<simulation::SensorRig>(std::move(sensors)),
                      std::get<mapping::Mapper>(std::move(mapper)),
                      {declared.floor, declared.robot_radius_mm, std::move(declared.solids),
                       std::move(declared.zones)}};
}

void LiveEngine::restart() {
    robot_ = initial_;
    sensors_.reset();
    mapper_.reset();
    navigator_.reset();
    lidar_.reset();
    trace_.clear();
    route_.clear();
    epcs_.clear();
    reads_ = 0;
}

void LiveEngine::step(robot::Command manual, LiveAction action, bool armed) {
    const auto telemetry = robot_.telemetry();
    if (action == LiveAction::restart && !manual.deadman && !telemetry.emergency_latched) {
        restart();
        return;
    }
    if (action == LiveAction::pause || manual.deadman || !armed) {
        navigator_.reset();
    }
    if (action == LiveAction::explore && armed && !telemetry.emergency_latched && !manual.deadman) {
        navigator_ = navigation::Navigator::create(mapper_.grid(),
                                                   {.radius = Meters{layout_.radius_mm / 1000.0}});
    }
    if (action == LiveAction::emergency_stop || manual.emergency_stop) {
        navigator_.reset();
        manual = {.emergency_stop = true};
    } else if (action == LiveAction::reset_stop && !manual.deadman) {
        manual.reset_stop = true;
    }
    if (!armed && !manual.emergency_stop) {
        manual = {};
    }
    if (navigator_ && !manual.deadman && !manual.emergency_stop) {
        manual = navigator_->update(mapper_, telemetry, lidar_ ? &*lidar_ : nullptr);
    }
    robot_.step(manual);
    capture();
    route_.clear();
    if (navigator_) {
        const auto cells = navigator_->route();
        const auto stride =
            std::max(std::size_t{1}, (cells.size() + route_limit - 1) / route_limit);
        for (std::size_t index = 0; index < cells.size(); index += stride) {
            if (auto point = mapper_.grid().center(cells[index])) {
                route_.push_back(*point);
            }
        }
    }
}

void LiveEngine::capture() {
    sensors_.capture(robot_);
    while (auto reading = sensors_.pop()) {
        const auto accepted = mapper_.ingest(*reading);
        if (auto *scan = std::get_if<sensors::LidarScan>(&*reading);
            scan && accepted == mapping::Update::accepted) {
            lidar_ = std::move(*scan);
        } else if (auto *radio = std::get_if<sensors::RfidScan>(&*reading)) {
            reads_ = std::min(std::size_t{1000000000}, reads_ + radio->reads.size());
            for (const auto &read : radio->reads) {
                if (epcs_.size() < tag_limit) {
                    epcs_.insert(read.epc);
                }
            }
        }
    }
    const auto telemetry = robot_.telemetry();
    if (!telemetry.sensors_powered) {
        lidar_.reset();
    }
    const auto at = position(telemetry.estimated_pose);
    if (trace_.empty() ||
        std::hypot(at.x_m - trace_.back().x_m, at.y_m - trace_.back().y_m) > 0.025) {
        if (trace_.size() == trace_limit) {
            trace_.erase(trace_.begin(), trace_.begin() + trace_batch);
        }
        trace_.push_back(at);
    }
}

LiveFrame LiveEngine::frame() const noexcept {
    const auto progress = navigator_ ? navigator_->progress() : navigation::Progress{};
    return {robot_.telemetry(),
            mapper_.grid(),
            lidar_ ? &*lidar_ : nullptr,
            route_,
            trace_,
            reads_,
            epcs_.size(),
            navigator_ ? navigation::label(progress.reason == navigation::Reason::none
                                               ? navigation::Reason::none
                                               : progress.reason)
                       : "manual",
            navigator_ && progress.state != navigation::State::finished};
}

DiagnosticsFrame LiveEngine::diagnostics() const {
    return {robot_.debug_truth().pose, warehouse_.snapshot(robot_.telemetry().world_tick).actors};
}

} // namespace argus::desktop
