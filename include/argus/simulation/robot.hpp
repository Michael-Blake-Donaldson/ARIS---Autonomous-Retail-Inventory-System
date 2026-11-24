#pragma once

#include "argus/robot/control.hpp"
#include "argus/simulation/encoders.hpp"
#include "argus/simulation/warehouse.hpp"

#include <cstdint>

namespace argus::simulation {

struct MotionConfig {
    double max_linear_mps{1.2};
    double max_angular_radps{2.0};
    double linear_acceleration_mps2{0.8};
    double linear_braking_mps2{1.8};
    double angular_acceleration_radps2{3.0};
    double angular_braking_radps2{5.0};
    double track_m{0.36};
    double encoder_step_m{0.001};
};

struct BatteryConfig {
    std::uint64_t capacity_uj{72000000000ULL};
    std::uint64_t initial_uj{43200000000ULL};
    std::uint64_t low_threshold_uj{14400000000ULL};
    std::uint32_t idle_mw{3000};
    std::uint32_t drive_mw{24000};
    std::uint32_t sensor_mw{2000};
    std::uint32_t charge_mw{35000};
};

enum class RobotError { invalid_configuration, invalid_spawn };

struct RobotTruth {
    robot::Pose pose;
    robot::Twist applied;
};

class RobotSimulation final : public robot::MotionController {
  public:
    [[nodiscard]] static Result<RobotSimulation, RobotError>
    create(Warehouse warehouse, std::uint64_t tick = 0, MotionConfig motion = {},
           BatteryConfig battery = {}, EncoderEffects encoders = {});

    void step(robot::Command command) noexcept override;
    [[nodiscard]] robot::Telemetry telemetry() const noexcept override { return telemetry_; }

    // Explicit capability for simulation evaluators and the developer truth overlay.
    [[nodiscard]] RobotTruth debug_truth() const noexcept { return truth_; }

  private:
    RobotSimulation(Warehouse warehouse, std::uint64_t tick, world::PointMm spawn,
                    std::int32_t radius, MotionConfig motion, BatteryConfig battery,
                    EncoderEffects encoders, std::uint32_t seed);

    void update_odometry(robot::Twist motion) noexcept;
    void update_battery(std::uint64_t draw, bool stationary, bool power_limited) noexcept;
    [[nodiscard]] std::uint64_t energy_draw(robot::Twist motion, bool sensors_on) const noexcept;

    Warehouse warehouse_;
    std::int32_t radius_mm_;
    MotionConfig motion_;
    BatteryConfig battery_;
    Encoders encoders_;
    RobotTruth truth_;
    robot::Telemetry telemetry_;
    friend class SensorRig;
};

} // namespace argus::simulation
