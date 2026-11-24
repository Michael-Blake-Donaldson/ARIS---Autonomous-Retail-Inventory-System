#include "argus/simulation/encoders.hpp"

#include <algorithm>
#include <cmath>

namespace argus::simulation {

bool valid_encoder_effects(EncoderEffects effects) noexcept {
    const auto within = [](double value, double limit) { return value >= 0.0 && value <= limit; };
    return within(effects.drift_fraction, 0.1) && within(effects.noise_m, 0.02) &&
           within(effects.slip_probability, 1.0) && within(effects.slip_loss_fraction, 0.7);
}

Encoders::Encoders(double track, double resolution, EncoderEffects effects, std::uint32_t seed)
    : track_m_{track}, resolution_m_{resolution}, effects_{effects}, random_{seed ^ 0x9D7A2B11U},
      left_bias_{(2.0 * random_.unit_interval() - 1.0) * effects.drift_fraction},
      right_bias_{(2.0 * random_.unit_interval() - 1.0) * effects.drift_fraction} {}

double Encoders::measure(double travel, double bias, double &remainder) noexcept {
    if (travel != 0.0) {
        const auto slip = random_.unit_interval() < effects_.slip_probability
                              ? 1.0 - effects_.slip_loss_fraction
                              : 1.0;
        const auto jitter = std::min(effects_.noise_m, std::abs(travel) * 0.1) *
                            (2.0 * random_.unit_interval() - 1.0);
        travel = travel * (1.0 + bias) * slip + jitter;
    }
    const auto available = travel + remainder;
    const auto reported = std::trunc(available / resolution_m_) * resolution_m_;
    remainder = available - reported;
    return reported;
}

robot::WheelReading Encoders::sample(double left_m, double right_m) noexcept {
    const auto left = measure(left_m, left_bias_, left_remainder_);
    const auto right = measure(right_m, right_bias_, right_remainder_);
    // Use the published pulses and known model limits to budget error without exposing true travel.
    const double slip_bound = effects_.slip_probability > 0.0 ? effects_.slip_loss_fraction : 0.0;
    const auto gain_floor = (1.0 - effects_.drift_fraction) * (1.0 - slip_bound);
    const auto bound = [this, slip_bound, gain_floor](double published) {
        const auto pulse_error = 2.0 * resolution_m_;
        const auto possible_travel =
            (std::abs(published) + pulse_error + effects_.noise_m) / gain_floor;
        return pulse_error + effects_.noise_m +
               possible_travel * (effects_.drift_fraction + slip_bound);
    };
    const auto wheel_error = bound(left) + bound(right);
    constexpr double reporting_cap = 1000000.0;
    distance_error_m_ = std::min(reporting_cap, distance_error_m_ + wheel_error / 2.0);
    heading_error_rad_ = std::min(reporting_cap, heading_error_rad_ + wheel_error / track_m_);
    return {Meters{left}, Meters{right}, distance_error_m_, heading_error_rad_};
}

} // namespace argus::simulation
