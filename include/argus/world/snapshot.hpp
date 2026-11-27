#pragma once

#include "argus/core/types.hpp"
#include "argus/world/geometry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace argus::world {

enum class SolidKind { wall, rack, obstruction };
enum class ZoneKind { aisle, doorway, receiving, dock, charging };
enum class ActorKind { worker, cart, u_boat, temporary_obstruction };

struct SolidView {
    std::string id;
    SolidKind kind;
    RectMm bounds;
    std::vector<std::int32_t> shelf_heights_mm;
};

struct ZoneView {
    std::string id;
    ZoneKind kind;
    RectMm bounds;
};

struct ActorView {
    std::string id;
    ActorKind kind;
    RectMm bounds;
    bool operator==(const ActorView &) const = default;
};

// An owned renderer snapshot. Changes to a copy cannot mutate the simulation.
struct Snapshot {
    ScenarioId scenario_id;
    std::uint32_t seed;
    std::uint64_t tick;
    RectMm floor;
    PointMm robot_spawn;
    std::int32_t robot_radius_mm;
    std::vector<SolidView> solids;
    std::vector<ZoneView> zones;
    std::vector<ActorView> actors;
};

} // namespace argus::world
