#pragma once

#include "argus/robot/control.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace argus::sensors {

enum class SensorId { wheel_encoder, lidar, rfid };
enum class Provenance { simulated };

struct Header {
    SensorId sensor;
    std::uint64_t sample_tick;
    std::uint64_t timestamp_ms;
    std::uint64_t world_tick;
    robot::Pose estimated_pose;
    Provenance provenance{Provenance::simulated};
};

struct WheelSample {
    Header header;
    robot::WheelReading reading;
    bool powered;
};

struct LidarScan {
    Header header;
    double first_angle_rad;
    double angle_step_rad;
    double range_error_m;
    std::vector<std::optional<double>> ranges_m;
    std::uint32_t return_count{};
};

struct RfidRead {
    std::string epc;
    double signal_dbm;
    bool operator==(const RfidRead &) const = default;
};

struct RfidScan {
    Header header;
    std::vector<RfidRead> reads;
    std::uint32_t truncated_reads{};
};

using Observation = std::variant<WheelSample, LidarScan, RfidScan>;

struct Counters {
    std::uint64_t produced{};
    std::uint64_t consumed{};
    std::uint64_t dropped{};
};

struct Throughput {
    std::array<Counters, 3> streams{};
    std::uint32_t pending{};
};

[[nodiscard]] constexpr std::size_t index(SensorId id) noexcept {
    return static_cast<std::size_t>(id);
}

} // namespace argus::sensors
