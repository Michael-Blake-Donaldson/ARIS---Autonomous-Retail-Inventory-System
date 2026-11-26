#include "live_engine.hpp"

#include "argus/audit/stockroom.hpp"
#include "map_run.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

namespace argus::desktop {
namespace {

constexpr std::size_t trace_limit = 1024;
constexpr std::size_t trace_batch = 128;
constexpr std::size_t route_limit = 4096;
constexpr std::size_t tag_limit = 1024;

mapping::Point position(robot::Pose pose) noexcept { return {pose.x.value(), pose.y.value()}; }

RunId new_run() {
    std::random_device entropy;
    const auto value = (static_cast<std::uint64_t>(entropy()) << 32U) | entropy();
    return RunId{1 + (value >> 2U)};
}

Result<simulation::RobotSimulation, simulation::RobotError>
make_robot(const simulation::Warehouse &warehouse, std::uint64_t start_tick,
           simulation::MotionConfig motion, simulation::EncoderEffects effects,
           const std::optional<FleetAttachment> &fleet) {
    if (fleet) {
        return simulation::RobotSimulation::create_at(warehouse, audit::stockroom_berth(fleet->id),
                                                      motion, {}, effects);
    }
    return simulation::RobotSimulation::create(warehouse, start_tick, motion, {}, effects);
}

} // namespace

ObservedFrame::ObservedFrame(const LiveFrame &frame)
    : telemetry{frame.telemetry}, grid{frame.grid},
      lidar{frame.lidar ? std::optional{*frame.lidar} : std::nullopt},
      route(frame.route.begin(), frame.route.end()), trace(frame.trace.begin(), frame.trace.end()),
      reads{frame.rfid_reads}, unique{frame.unique_epcs}, estimate{frame.estimate},
      navigation{frame.navigation}, run{frame.run}, archived{frame.archived},
      map_revision{frame.map_revision}, traffic_yield{frame.traffic_yield},
      returning_to_berth{frame.returning_to_berth} {}

LiveFrame ObservedFrame::view() const noexcept {
    const auto *scan = lidar ? &*lidar : nullptr;
    return {telemetry,
            grid,
            scan,
            route,
            trace,
            reads,
            unique,
            map_status(telemetry, scan, estimate),
            estimate,
            navigation,
            run,
            archived,
            map_revision,
            traffic_yield,
            returning_to_berth};
}

LiveEngine::LiveEngine(simulation::Warehouse warehouse, simulation::RobotSimulation robot,
                       simulation::SensorRig sensors, mapping::Mapper mapper, Layout layout,
                       std::optional<FleetAttachment> fleet)
    : warehouse_{std::move(warehouse)}, initial_{robot}, robot_{std::move(robot)},
      sensors_{std::move(sensors)}, mapper_{std::move(mapper)}, layout_{std::move(layout)},
      id_{new_run()}, fleet_{std::move(fleet)} {
    trace_.reserve(trace_limit);
}

Result<LiveEngine, app::Failure> LiveEngine::create(simulation::Warehouse warehouse,
                                                    std::uint64_t start_tick, bool shared_demo,
                                                    std::optional<FleetAttachment> fleet) {
    auto declared = warehouse.snapshot(start_tick);
    auto mapper = mapping::Mapper::create(app::map_spec(declared));
    if (const auto *error = std::get_if<mapping::MapError>(&mapper)) {
        return app::Failure{*error};
    }
    simulation::MotionConfig motion;
    if (shared_demo) {
        motion.encoder_step_m = declared.scenario_id == audit::stockroom_scenario
                                    ? audit::stockroom_encoder_step_m
                                    : 0.0001;
    }
    const auto effects =
        shared_demo ? simulation::EncoderEffects{} : simulation::warehouse_encoder_effects;
    auto robot = make_robot(warehouse, start_tick, motion, effects, fleet);
    if (const auto *error = std::get_if<simulation::RobotError>(&robot)) {
        return app::Failure{*error};
    }
    auto config = declared.scenario_id == audit::stockroom_scenario
                      ? audit::stockroom_sensor_config()
                      : simulation::SensorConfig{};
    if (shared_demo) {
        config.rfid.range_m = 3.5;
    }
    auto sensors = simulation::SensorRig::create(
        fleet ? audit::stockroom_berth(fleet->id).seed : declared.seed, config);
    if (const auto *error = std::get_if<simulation::SensorError>(&sensors)) {
        return app::Failure{*error};
    }
    return LiveEngine{std::move(warehouse),
                      std::get<simulation::RobotSimulation>(std::move(robot)),
                      std::get<simulation::SensorRig>(std::move(sensors)),
                      std::get<mapping::Mapper>(std::move(mapper)),
                      {declared.scenario_id, declared.floor, declared.robot_radius_mm,
                       std::move(declared.solids), std::move(declared.zones)},
                      std::move(fleet)};
}

bool LiveEngine::advance(robot::Command command) {
    if (fleet_) {
        return fleet_->clock->step(fleet_->id, robot_, command);
    }
    robot_.step(command);
    return true;
}

void LiveEngine::restart() {
    id_ = new_run();
    robot_ = initial_;
    sensors_.reset();
    mapper_.reset();
    navigator_.reset();
    job_progress_.reset();
    lidar_.reset();
    trace_.clear();
    route_.clear();
    epcs_.clear();
    reads_ = 0;
}

bool LiveEngine::step(robot::Command manual, LiveAction action, bool armed, bool lidar_dropout) {
    const auto telemetry = robot_.telemetry();
    if (!fleet_ && action == LiveAction::restart && !manual.deadman &&
        !telemetry.emergency_latched) {
        restart();
        return true;
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
    if (!advance(manual)) {
        return false;
    }
    capture(lidar_dropout);
    update_route(navigator_ ? &*navigator_ : nullptr);
    return true;
}

void LiveEngine::update_route(const navigation::Navigator *navigator) {
    route_.clear();
    if (navigator) {
        const auto cells = navigator->route();
        const auto stride =
            std::max(std::size_t{1}, (cells.size() + route_limit - 1) / route_limit);
        for (std::size_t index = 0; index < cells.size(); index += stride) {
            if (auto point = mapper_.grid().center(cells[index])) {
                route_.push_back(*point);
            }
        }
    }
}

void LiveEngine::capture(bool lidar_dropout) {
    sensors_.capture(robot_);
    while (auto reading = sensors_.pop()) {
        if (lidar_dropout && std::holds_alternative<sensors::LidarScan>(*reading)) {
            continue;
        }
        const auto accepted = mapper_.ingest(*reading);
        if (auto *scan = std::get_if<sensors::LidarScan>(&*reading);
            scan && accepted == mapping::Update::accepted) {
            lidar_ = std::move(*scan);
        } else if (auto *radio = std::get_if<sensors::RfidScan>(&*reading)) {
            count_reads(*radio);
        }
    }
    const auto telemetry = robot_.telemetry();
    if (!telemetry.sensors_powered) {
        lidar_.reset();
    }
    update_trace();
}

void LiveEngine::count_reads(const sensors::RfidScan &radio) {
    reads_ = std::min(std::size_t{1000000000}, reads_ + radio.reads.size());
    for (const auto &read : radio.reads) {
        if (epcs_.size() < tag_limit) {
            epcs_.insert(read.epc);
        }
    }
}

void LiveEngine::update_trace() {
    const auto at = position(robot_.telemetry().estimated_pose);
    if (trace_.empty() ||
        std::hypot(at.x_m - trace_.back().x_m, at.y_m - trace_.back().y_m) > 0.025) {
        if (trace_.size() == trace_limit) {
            trace_.erase(trace_.begin(), trace_.begin() + trace_batch);
        }
        trace_.push_back(at);
    }
}

simulation::ObservedRun LiveEngine::borrow_run(
    std::function<bool(const navigation::Navigator &, const sensors::RfidScan *)> observer,
    std::function<bool()> should_continue) {
    navigator_.reset();
    return {robot_,
            sensors_,
            mapper_,
            lidar_,
            std::move(observer),
            Meters{layout_.radius_mm / 1000.0},
            [this](robot::Command command) { return advance(command); },
            std::move(should_continue)};
}

void LiveEngine::observe_job(const navigation::Navigator &navigator,
                             const sensors::RfidScan *scan) {
    if (scan) {
        count_reads(*scan);
    }
    update_trace();
    job_progress_ = navigator.progress();
    update_route(&navigator);
}

void LiveEngine::finish_job(bool emergency) {
    job_progress_.reset();
    navigator_.reset();
    route_.clear();
    traffic_yield_ = false;
    returning_to_berth_ = false;
    static_cast<void>(advance({.emergency_stop = emergency}));
}

LiveFrame LiveEngine::frame() const noexcept {
    const auto telemetry = robot_.telemetry();
    const auto *lidar = lidar_ ? &*lidar_ : nullptr;
    return {telemetry,
            mapper_.grid(),
            lidar,
            route_,
            trace_,
            reads_,
            epcs_.size(),
            map_status(telemetry, lidar, mapper_.estimate()),
            mapper_.estimate(),
            navigator_ ? std::optional{navigator_->progress()} : job_progress_,
            id_,
            false,
            mapper_.revision(),
            traffic_yield_,
            returning_to_berth_};
}

DiagnosticsFrame LiveEngine::diagnostics() const {
    return {robot_.debug_truth().pose, warehouse_.snapshot(robot_.telemetry().world_tick).actors};
}

} // namespace argus::desktop
