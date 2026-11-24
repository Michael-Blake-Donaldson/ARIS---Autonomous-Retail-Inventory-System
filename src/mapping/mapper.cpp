#include "argus/mapping/mapper.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace argus::mapping {
namespace {

constexpr double turn = 2.0 * std::numbers::pi;

bool valid_pose(robot::Pose pose) noexcept {
    return std::abs(pose.x.value()) <= 1000000.0 && std::abs(pose.y.value()) <= 1000000.0 &&
           std::abs(pose.heading.value()) <= std::numbers::pi;
}

bool valid_error(double value) noexcept { return value >= 0.0 && value <= 1000000.0; }

bool valid_header(const sensors::Header &header, sensors::SensorId expected) noexcept {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    const auto stamp =
        header.sample_tick <= max / robot::step_ms ? header.sample_tick * robot::step_ms : max;
    return header.sensor == expected && header.provenance == sensors::Provenance::simulated &&
           header.sample_tick > 0 && header.timestamp_ms == stamp;
}

Point point(robot::Pose pose) noexcept { return {pose.x.value(), pose.y.value()}; }

} // namespace

Mapper::Mapper(Grid grid, Limits limits) : grid_{std::move(grid)}, limits_{limits} {}

Result<Mapper, MapError> Mapper::create(Spec spec, Limits limits) {
    if (!valid_error(limits.distance_error_m) || limits.distance_error_m == 0.0 ||
        !valid_error(limits.heading_error_rad) || limits.heading_error_rad == 0.0) {
        return MapError::invalid_spec;
    }
    auto grid = Grid::create(spec);
    if (const auto *err = std::get_if<MapError>(&grid)) {
        return *err;
    }
    return Mapper{std::get<Grid>(std::move(grid)), limits};
}

Update Mapper::ingest(const sensors::Observation &observation) noexcept {
    if (const auto *wheel = std::get_if<sensors::WheelSample>(&observation)) {
        return ingest_wheel(*wheel);
    }
    if (const auto *lidar = std::get_if<sensors::LidarScan>(&observation)) {
        return ingest_lidar(*lidar);
    }
    return Update::unrelated;
}

Update Mapper::ingest_wheel(const sensors::WheelSample &sample) noexcept {
    const auto &header = sample.header;
    const auto &reading = sample.reading;
    if (!valid_header(header, sensors::SensorId::wheel_encoder) ||
        !valid_pose(header.estimated_pose) || !valid_error(reading.distance_error_m) ||
        !valid_error(reading.heading_error_rad) ||
        !(std::abs(reading.left_delta.value()) <= 2.0 &&
          std::abs(reading.right_delta.value()) <= 2.0) ||
        (wheel_ && (header.sample_tick <= wheel_->header.sample_tick ||
                    header.world_tick < wheel_->header.world_tick ||
                    reading.distance_error_m < wheel_->reading.distance_error_m ||
                    reading.heading_error_rad < wheel_->reading.heading_error_rad))) {
        return Update::invalid;
    }
    wheel_ = sample;
    auto estimate = Estimate{header.estimated_pose, reading.distance_error_m,
                             reading.heading_error_rad, header.sample_tick, false};
    if (anchor_) {
        const auto offset = anchor_->reference.heading.value() - anchor_->measured.heading.value();
        const auto cos_offset = std::cos(offset);
        const auto sin_offset = std::sin(offset);
        const auto dx = header.estimated_pose.x.value() - anchor_->measured.x.value();
        const auto dy = header.estimated_pose.y.value() - anchor_->measured.y.value();
        estimate.pose = {
            Meters{anchor_->reference.x.value() + dx * cos_offset - dy * sin_offset},
            Meters{anchor_->reference.y.value() + dx * sin_offset + dy * cos_offset},
            robot::Radians{std::remainder(header.estimated_pose.heading.value() + offset, turn)}};
        estimate.distance_error_m +=
            anchor_->reference_distance_error - anchor_->measured_distance_error;
        estimate.heading_error_rad +=
            anchor_->reference_heading_error - anchor_->measured_heading_error;
    }
    estimate.reliable = sample.powered && grid_.locate(point(estimate.pose)).has_value() &&
                        estimate.distance_error_m < limits_.distance_error_m &&
                        estimate.heading_error_rad < limits_.heading_error_rad;
    estimate_ = estimate;
    return estimate.reliable ? Update::accepted : Update::suspended;
}

Update Mapper::ingest_lidar(const sensors::LidarScan &scan) noexcept {
    const auto &header = scan.header;
    if (!valid_header(header, sensors::SensorId::lidar) || header.sample_tick <= last_scan_ ||
        scan.ranges_m.empty() || scan.ranges_m.size() > 720 ||
        !(std::abs(scan.first_angle_rad) <= std::numbers::pi && scan.angle_step_rad > 0.0 &&
          scan.angle_step_rad * static_cast<double>(scan.ranges_m.size()) <= turn + 1e-9 &&
          scan.range_error_m >= 0.0 && scan.range_error_m <= 0.5)) {
        return Update::invalid;
    }
    if (!wheel_ || header.sample_tick != wheel_->header.sample_tick ||
        header.world_tick != wheel_->header.world_tick) {
        return Update::missing_pose;
    }
    std::uint32_t returns{};
    for (const auto &range : scan.ranges_m) {
        if (range) {
            if (!(*range >= 0.0 && *range <= 50.0)) {
                return Update::invalid;
            }
            ++returns;
        }
    }
    if (returns != scan.return_count) {
        return Update::invalid;
    }
    last_scan_ = header.sample_tick;
    if (!estimate_ || !estimate_->reliable) {
        return Update::suspended;
    }

    const auto origin = point(estimate_->pose);
    auto angle = estimate_->pose.heading.value() + scan.first_angle_rad;
    for (const auto &range : scan.ranges_m) {
        if (range) {
            grid_.observe_hit(origin, {origin.x_m + *range * std::cos(angle),
                                       origin.y_m + *range * std::sin(angle)});
        }
        angle += scan.angle_step_rad;
    }
    return Update::accepted;
}

bool Mapper::reanchor(robot::Pose pose, double distance_error_m,
                      double heading_error_rad) noexcept {
    if (!wheel_ || !wheel_->powered || wheel_->reading.distance_error_m >= 1000000.0 ||
        wheel_->reading.heading_error_rad >= 1000000.0 || !valid_pose(pose) ||
        !grid_.locate(point(pose)) || !valid_error(distance_error_m) ||
        !valid_error(heading_error_rad) || distance_error_m >= limits_.distance_error_m ||
        heading_error_rad >= limits_.heading_error_rad) {
        return false;
    }
    anchor_ = Anchor{wheel_->header.estimated_pose,
                     pose,
                     wheel_->reading.distance_error_m,
                     wheel_->reading.heading_error_rad,
                     distance_error_m,
                     heading_error_rad};
    estimate_ =
        Estimate{pose, distance_error_m, heading_error_rad, wheel_->header.sample_tick, true};
    last_scan_ = wheel_->header.sample_tick;
    grid_.clear();
    return true;
}

void Mapper::reset() noexcept {
    grid_.clear();
    wheel_.reset();
    estimate_.reset();
    anchor_.reset();
    last_scan_ = 0;
}

} // namespace argus::mapping
