#pragma once

#include "argus/core/random.hpp"
#include "argus/core/result.hpp"
#include "argus/sensors/observations.hpp"

#include <deque>
#include <map>
#include <numbers>

namespace argus::simulation {

class RobotSimulation;

struct LidarConfig {
    std::uint32_t beams{180};
    double field_of_view_rad{2.0 * std::numbers::pi};
    double range_m{12.0};
    double range_noise_m{0.0125};
    double missing_probability{0.025};
    std::uint32_t period_ticks{2};
};

struct RfidConfig {
    double antenna_forward_m{0.18};
    double antenna_left_m{};
    double antenna_height_m{0.8};
    double antenna_yaw_rad{0.2};
    double field_of_view_rad{std::numbers::pi};
    double range_m{8.0};
    double detection_probability{0.9};
    double obstacle_transmission{0.45};
    std::uint32_t period_ticks{2};
    std::uint32_t dwell_scans{2};
    std::uint32_t max_reads{64};
};

struct SensorConfig {
    LidarConfig lidar{};
    RfidConfig rfid{};
    std::uint32_t capacity{24};
};

enum class SensorError { invalid_configuration };

// Own one rig per run. Simulation publishes and reads on the same thread.
class SensorRig {
  public:
    [[nodiscard]] static Result<SensorRig, SensorError> create(std::uint32_t seed,
                                                               SensorConfig config = {});

    // Publish at most once per robot step. Gaps invalidate RFID dwell.
    bool capture(const RobotSimulation &robot);
    [[nodiscard]] std::optional<sensors::Observation> pop();
    [[nodiscard]] sensors::Throughput throughput() const noexcept;

  private:
    SensorRig(std::uint32_t seed, SensorConfig config);
    void publish(sensors::Observation observation);

    SensorConfig config_;
    Random lidar_random_;
    Random rfid_random_;
    std::uint64_t last_sample_{};
    std::map<std::string, std::uint32_t> dwell_;
    std::deque<sensors::Observation> pending_;
    sensors::Throughput stats_;
};

} // namespace argus::simulation
