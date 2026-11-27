#pragma once

#include "argus/core/random.hpp"
#include "argus/robot/control.hpp"

namespace argus::simulation {

struct EncoderEffects {
    double drift_fraction{};
    double noise_m{};
    double slip_probability{};
    double slip_loss_fraction{};
};

inline constexpr EncoderEffects warehouse_encoder_effects{0.003, 0.0006, 0.04, 0.12};

[[nodiscard]] bool valid_encoder_effects(EncoderEffects effects) noexcept;

// Consumes accepted simulation wheel travel and emits sampled encoder increments.
class Encoders {
    friend class RobotSimulation;

    Encoders(double track_m, double resolution_m, EncoderEffects effects, std::uint32_t seed);

    [[nodiscard]] robot::WheelReading sample(double left_m, double right_m) noexcept;

    double track_m_;
    double resolution_m_;
    EncoderEffects effects_;
    Random random_;
    double left_bias_;
    double right_bias_;
    double left_remainder_{};
    double right_remainder_{};
    double distance_error_m_{};
    double heading_error_rad_{};

    [[nodiscard]] double measure(double travel, double bias, double &remainder) noexcept;
};

} // namespace argus::simulation
