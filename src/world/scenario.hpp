#pragma once

#include "argus/simulation/sensor_query.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace argus::simulation::detail {

inline constexpr std::size_t max_solids = 128;
inline constexpr std::int32_t max_extent_mm = 100000;

struct Solid {
    world::SolidView view;
    std::string aisle;
};

struct Actor {
    std::string id;
    world::ActorKind kind;
    world::PointMm start;
    world::PointMm end;
    world::PointMm size;
    std::uint32_t leg_ticks;
    std::uint32_t phase;
};

struct Scenario {
    ScenarioId id;
    std::uint32_t seed;
    world::RectMm floor;
    world::PointMm spawn;
    std::int32_t robot_radius_mm;
    std::vector<Solid> solids;
    std::vector<world::ZoneView> zones;
    std::vector<Actor> actors;
    std::vector<TagTruth> tags;
};

[[nodiscard]] Result<Scenario, ScenarioError> decode(std::string_view input);
[[nodiscard]] world::RectMm sweep(const Actor &actor) noexcept;
[[nodiscard]] world::RectMm actor_bounds(const Actor &actor, std::uint64_t tick) noexcept;
[[nodiscard]] bool inside_floor(world::RectMm floor, world::PointMm center,
                                std::int32_t radius_mm) noexcept;

} // namespace argus::simulation::detail
