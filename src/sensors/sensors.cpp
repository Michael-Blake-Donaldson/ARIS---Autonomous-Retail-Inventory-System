#include "argus/simulation/sensors.hpp"

#include "argus/simulation/robot.hpp"
#include "model.hpp"

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

Result<SensorRig, SensorError> SensorRig::create(std::uint32_t seed, SensorConfig config) {
    if (!valid(config)) {
        return SensorError::invalid_configuration;
    }
    return SensorRig{seed, config};
}

SensorRig::SensorRig(std::uint32_t seed, SensorConfig config)
    : config_{config}, lidar_random_{seed ^ 0x5DA33D51U}, rfid_random_{seed ^ 0x2C859AC3U} {}

bool SensorRig::capture(const RobotSimulation &robot) {
    const auto telemetry = robot.telemetry();
    const auto tick = telemetry.sample_tick;
    if (tick <= last_sample_) {
        return false;
    }
    if (tick != last_sample_ + 1 || !telemetry.sensors_powered) {
        dwell_.clear();
    }
    last_sample_ = tick;

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
    if (tick % config_.lidar.period_ticks == 0) {
        publish(detail::scan_lidar(world, robot.truth_.pose, header(sensors::SensorId::lidar),
                                   config_.lidar, lidar_random_));
    }
    if (tick % config_.rfid.period_ticks == 0) {
        publish(detail::scan_rfid(world, robot.truth_.pose, header(sensors::SensorId::rfid),
                                  config_.rfid, dwell_, rfid_random_));
    }
    return true;
}

void SensorRig::publish(sensors::Observation observation) {
    const auto id = sensors::index(id_of(observation));
    // Append first so a failed allocation leaves existing samples and counters unchanged.
    pending_.push_back(std::move(observation));
    increment(stats_.streams.at(id).produced);
    if (pending_.size() > config_.capacity) {
        increment(stats_.streams.at(sensors::index(id_of(pending_.front()))).dropped);
        pending_.pop_front();
    }
}

std::optional<sensors::Observation> SensorRig::pop() {
    if (pending_.empty()) {
        return std::nullopt;
    }
    auto sample = std::move(pending_.front());
    pending_.pop_front();
    increment(stats_.streams.at(sensors::index(id_of(sample))).consumed);
    return sample;
}

sensors::Throughput SensorRig::throughput() const noexcept {
    auto result = stats_;
    result.pending = static_cast<std::uint32_t>(pending_.size());
    return result;
}

} // namespace argus::simulation
