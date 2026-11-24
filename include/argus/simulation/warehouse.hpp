#pragma once

#include "argus/core/result.hpp"
#include "argus/world/snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string_view>

namespace argus::simulation {

enum class ScenarioError {
    too_large,
    malformed_json,
    duplicate_key,
    unsupported_version,
    invalid_schema,
    invalid_geometry,
    invalid_reference,
    duplicate_identifier,
    input_failure,
    unknown_fixture,
};

inline constexpr std::size_t scenario_byte_limit = 2 * 1024 * 1024;
inline constexpr std::size_t scenario_tag_limit = 10000;
inline constexpr std::int32_t max_robot_radius_mm = 5000;

class Warehouse {
  public:
    [[nodiscard]] static Result<Warehouse, ScenarioError> from_json(std::string_view text);
    [[nodiscard]] static Result<Warehouse, ScenarioError> from_stream(std::istream &input);

    [[nodiscard]] world::Snapshot snapshot(std::uint64_t tick) const;

    // Invalid query radii and disks touching the warehouse boundary are treated as blocked.
    [[nodiscard]] bool collides(world::PointMm center, std::int32_t radius_mm,
                                std::uint64_t tick) const noexcept;

    // Tests the disk's entire segment as dynamic actors advance from tick to the next tick.
    [[nodiscard]] bool sweep_collides(world::PrecisePointMm from, world::PrecisePointMm to,
                                      double radius_mm, std::uint64_t tick) const noexcept;
    [[nodiscard]] bool charging_contact(world::PrecisePointMm center,
                                        std::int32_t radius_mm) const noexcept;

  private:
    struct State;

    explicit Warehouse(std::shared_ptr<const State> state);

    std::shared_ptr<const State> state_;
    friend class SensorQuery;
};

[[nodiscard]] std::string_view scenario_error_code(ScenarioError error) noexcept;

} // namespace argus::simulation
