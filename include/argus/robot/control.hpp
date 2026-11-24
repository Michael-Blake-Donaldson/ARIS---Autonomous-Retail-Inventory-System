#pragma once

#include "argus/core/types.hpp"

#include <cstdint>

namespace argus::robot {

using Radians = Quantity<struct RadianTag>;
using RadiansPerSecond = Quantity<struct AngularVelocityTag>;

struct Pose {
    Meters x{0.0};
    Meters y{0.0};
    Radians heading{0.0};
};

struct Twist {
    MetersPerSecond linear{0.0};
    RadiansPerSecond angular{0.0};
};

struct Command {
    Twist target{};
    bool deadman{};
    bool emergency_stop{};
    bool reset_stop{};
    bool sensors_on{true};
};

struct WheelReading {
    Meters left_delta{0.0};
    Meters right_delta{0.0};
    double distance_error_m{};
    double heading_error_rad{};
    bool operator==(const WheelReading &) const = default;
};

struct Telemetry {
    std::uint64_t world_tick{};
    std::uint64_t sample_tick{};
    Pose estimated_pose{};
    Twist commanded{};
    Twist measured{};
    WheelReading wheels{};
    std::uint64_t energy_uj{};
    std::uint64_t capacity_uj{};
    bool low_battery{};
    bool depleted{};
    bool docked{};
    bool charging{};
    bool emergency_latched{};
    bool invalid_command{};
    bool collision_blocked{};
    bool world_held{};
    bool sensors_powered{};
};

// The controller exposes commanded and estimated motion only.
class MotionController {
  public:
    virtual ~MotionController() = default;
    virtual void step(Command command) noexcept = 0;
    [[nodiscard]] virtual Telemetry telemetry() const noexcept = 0;
};

inline constexpr std::uint64_t step_ms = 50;

} // namespace argus::robot
