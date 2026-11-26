#include "argus/simulation/sensors.hpp"

#include "argus/simulation/robot.hpp"
#include "model.hpp"

#include <deque>
#include <limits>
#include <utility>

namespace argus::simulation {
namespace {

constexpr auto max_counter = std::numeric_limits<std::uint64_t>::max();
constexpr double full_circle = 2.0 * std::numbers::pi;

bool valid(const SensorConfig &config) noexcept {
    const auto in = [](double value, double lower, double upper) {
        return value >= lower && value <= upper;
    };
    const auto &lidar = config.lidar;
    const auto &rfid = config.rfid;
    return config.capacity >= 1 && config.capacity <= 64 && lidar.beams >= 1 &&
           lidar.beams <= 720 && in(lidar.field_of_view_rad, 0.001, full_circle) &&
           in(lidar.range_m, 0.1, 50.0) && in(lidar.range_noise_m, 0.0, 0.5) &&
           in(lidar.missing_probability, 0.0, 1.0) && lidar.period_ticks >= 1 &&
           lidar.period_ticks <= 120 && in(rfid.antenna_forward_m, -2.0, 2.0) &&
           in(rfid.antenna_left_m, -2.0, 2.0) && in(rfid.antenna_height_m, 0.0, 5.0) &&
           in(rfid.antenna_yaw_rad, -std::numbers::pi, std::numbers::pi) &&
           in(rfid.field_of_view_rad, 0.001, full_circle) && in(rfid.range_m, 0.1, 25.0) &&
           in(rfid.detection_probability, 0.0, 1.0) && in(rfid.obstacle_transmission, 0.01, 1.0) &&
           rfid.period_ticks >= 1 && rfid.period_ticks <= 120 && rfid.dwell_scans >= 1 &&
           rfid.dwell_scans <= 60 && rfid.max_reads >= 1 && rfid.max_reads <= 128;
}

void increment(std::uint64_t &value) noexcept {
    if (value < max_counter) {
        ++value;
    }
}

sensors::SensorId id_of(const sensors::Observation &sample) {
    return std::visit([](const auto &value) { return value.header.sensor; }, sample);
}

} // namespace

struct SensorRig::State {
    State(std::uint32_t run_seed, SensorConfig settings)
        : config{settings}, seed{run_seed}, lidar_random{seed ^ 0x5DA33D51U},
          rfid_random{seed ^ 0x2C859AC3U} {}

    SensorConfig config;
    std::uint32_t seed;
    Random lidar_random;
    Random rfid_random;
    std::uint64_t last_sample{};
    detail::RfidDwell dwell;
    std::optional<RfidFocus> focus;
    std::deque<sensors::Observation> pending;
    sensors::Throughput stats;
};

Result<SensorRig, SensorError> SensorRig::create(std::uint32_t seed, SensorConfig config) {
    if (!valid(config)) {
        return SensorError::invalid_configuration;
    }
    return SensorRig{seed, config};
}

SensorRig::SensorRig(std::uint32_t seed, SensorConfig config)
    : state_{std::make_unique<State>(seed, config)} {}

SensorRig::SensorRig(SensorRig &&) noexcept = default;
SensorRig &SensorRig::operator=(SensorRig &&) noexcept = default;
SensorRig::~SensorRig() = default;

void SensorRig::reset() { state_ = std::make_unique<State>(state_->seed, state_->config); }

bool SensorRig::focus_rfid(std::optional<RfidFocus> focus) noexcept {
    const auto in = [](Meters length, double low, double high) {
        return length.value() >= low && length.value() <= high;
    };
    if (focus && (!in(focus->forward, 0.0, 2.5) || !in(focus->left, -2.0, 2.0) ||
                  !in(focus->height, 0.2, 2.5) || !in(focus->half_depth, 0.04, 0.60) ||
                  !in(focus->half_width, 0.04, 1.6) || !in(focus->half_height, 0.04, 0.25))) {
        return false;
    }
    if (state_->focus != focus) {
        state_->dwell.clear();
        state_->focus = focus;
    }
    return true;
}

bool SensorRig::capture(const RobotSimulation &robot) {
    auto &state = *state_;
    const auto telemetry = robot.telemetry();
    const auto tick = telemetry.sample_tick;
    if (tick <= state.last_sample) {
        return false;
    }
    if (tick != state.last_sample + 1 || !telemetry.sensors_powered) {
        state.dwell.clear();
    }
    state.last_sample = tick;

    const auto timestamp =
        tick <= max_counter / argus::robot::step_ms ? tick * argus::robot::step_ms : max_counter;
    const auto header = [&](sensors::SensorId id) {
        return sensors::Header{id, tick, timestamp, telemetry.world_tick, telemetry.estimated_pose};
    };
    publish(sensors::WheelSample{header(sensors::SensorId::wheel_encoder), telemetry.wheels,
                                 !telemetry.depleted});
    if (!telemetry.sensors_powered) {
        return true;
    }
    const SensorQuery world{robot.warehouse_};
    if (tick % state.config.lidar.period_ticks == 0) {
        publish(detail::scan_lidar(world, robot.truth_.pose, header(sensors::SensorId::lidar),
                                   state.config.lidar, state.lidar_random));
    }
    if (tick % state.config.rfid.period_ticks == 0) {
        publish(detail::scan_rfid(world, robot.truth_.pose, header(sensors::SensorId::rfid),
                                  state.config.rfid, state.dwell, state.rfid_random, state.focus));
    }
    return true;
}

void SensorRig::publish(sensors::Observation observation) {
    auto &state = *state_;
    const auto id = sensors::index(id_of(observation));
    // Append first so a failed allocation leaves existing samples and counters unchanged.
    state.pending.push_back(std::move(observation));
    increment(state.stats.streams.at(id).produced);
    if (state.pending.size() > state.config.capacity) {
        increment(state.stats.streams.at(sensors::index(id_of(state.pending.front()))).dropped);
        state.pending.pop_front();
    }
}

std::optional<sensors::Observation> SensorRig::pop() {
    auto &state = *state_;
    if (state.pending.empty()) {
        return std::nullopt;
    }
    auto sample = std::move(state.pending.front());
    state.pending.pop_front();
    increment(state.stats.streams.at(sensors::index(id_of(sample))).consumed);
    return sample;
}

sensors::Throughput SensorRig::throughput() const noexcept {
    auto result = state_->stats;
    result.pending = static_cast<std::uint32_t>(state_->pending.size());
    return result;
}

} // namespace argus::simulation
