#pragma once

#include <compare>
#include <cstdint>

namespace argus {

template <typename Tag> class Id {
  public:
    explicit constexpr Id(std::uint64_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    auto operator<=>(const Id &) const = default;

  private:
    std::uint64_t value_;
};

using ScenarioId = Id<struct ScenarioTag>;
using RobotId = Id<struct RobotTag>;
using MissionId = Id<struct MissionTag>;
using RunId = Id<struct RunTag>;

// These wrappers preserve dimensions. Domain-specific range checks belong at input boundaries.
template <typename Unit> class Quantity {
  public:
    explicit constexpr Quantity(double value) noexcept : value_{value} {}

    [[nodiscard]] constexpr double value() const noexcept { return value_; }
    auto operator<=>(const Quantity &) const = default;

  private:
    double value_;
};

using Meters = Quantity<struct MeterTag>;
using Seconds = Quantity<struct SecondTag>;
using MetersPerSecond = Quantity<struct MeterPerSecondTag>;

} // namespace argus
