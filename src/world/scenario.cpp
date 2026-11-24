#include "scenario.hpp"

#include "argus/core/random.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace argus::simulation::detail {
namespace {

using Json = nlohmann::json;
using namespace world;

constexpr std::size_t max_parse_events = 200000;
constexpr std::size_t max_depth = 8;
constexpr std::uint64_t max_scenario_id = 9007199254740991ULL;
constexpr std::size_t max_zones = 32;
constexpr std::size_t max_actors = 32;
constexpr std::size_t max_shelves = 16;

[[noreturn]] void reject(ScenarioError error) { throw error; }

Json parse(std::string_view text) {
    if (text.size() > scenario_byte_limit) {
        reject(ScenarioError::too_large);
    }
    // Reject embedded NUL explicitly, including a suffix after an otherwise valid document.
    if (text.find('\0') != std::string_view::npos) {
        reject(ScenarioError::malformed_json);
    }
    std::array<std::set<std::string>, max_depth + 1> keys;
    std::size_t events = 0;
    const auto callback = [&keys, &events](int depth, Json::parse_event_t event, Json &value) {
        if (depth < 0 || static_cast<std::size_t>(depth) >= max_depth ||
            ++events > max_parse_events) {
            reject(ScenarioError::too_large);
        }
        const auto level = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) {
            keys.at(level + 1).clear();
        }
        if ((event == Json::parse_event_t::key || event == Json::parse_event_t::value) &&
            value.is_string() && value.get_ref<const std::string &>().size() > 64) {
            reject(ScenarioError::invalid_schema);
        }
        if (event == Json::parse_event_t::key &&
            !keys.at(level).insert(value.get_ref<const std::string &>()).second) {
            reject(ScenarioError::duplicate_key);
        }
        return true;
    };
    return Json::parse(text, callback);
}

void fields(const Json &object, std::initializer_list<std::string_view> required) {
    if (!object.is_object() || object.size() != required.size() ||
        !std::ranges::all_of(
            required, [&object](std::string_view field) { return object.contains(field); })) {
        reject(ScenarioError::invalid_schema);
    }
}

void array_size(const Json &array, std::size_t min, std::size_t max) {
    if (!array.is_array() || array.size() < min || array.size() > max) {
        reject(ScenarioError::invalid_schema);
    }
}

std::string_view string(const Json &value) {
    if (!value.is_string()) {
        reject(ScenarioError::invalid_schema);
    }
    return value.get_ref<const std::string &>();
}

std::uint64_t number(const Json &value, std::uint64_t min, std::uint64_t max) {
    if (!value.is_number_integer() ||
        (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)) {
        reject(ScenarioError::invalid_schema);
    }
    const auto result = value.get<std::uint64_t>();
    if (result < min || result > max) {
        reject(ScenarioError::invalid_schema);
    }
    return result;
}

std::int32_t mm(const Json &value, std::uint64_t min = 0, std::uint64_t max = max_extent_mm) {
    return static_cast<std::int32_t>(number(value, min, max));
}

std::string identifier(const Json &value) {
    const auto id = string(value);
    const auto is_lower = [](char c) { return c >= 'a' && c <= 'z'; };
    if (id.empty() || id.size() > 32 || !is_lower(id.front()) ||
        !std::ranges::all_of(
            id, [is_lower](char c) { return is_lower(c) || (c >= '0' && c <= '9') || c == '-'; })) {
        reject(ScenarioError::invalid_schema);
    }
    return std::string{id};
}

void unique(std::set<std::string> &ids, const std::string &id) {
    if (!ids.insert(id).second) {
        reject(ScenarioError::duplicate_identifier);
    }
}

PointMm point(const Json &value) {
    array_size(value, 2, 2);
    return {mm(value.at(0)), mm(value.at(1))};
}

RectMm rectangle(const Json &value) {
    array_size(value, 4, 4);
    const PointMm start{mm(value.at(0)), mm(value.at(1))};
    return {start, {start.x + mm(value.at(2), 1), start.y + mm(value.at(3), 1)}};
}

template <typename Kind>
Kind kind(const Json &value, std::initializer_list<std::pair<std::string_view, Kind>> choices) {
    const auto name = string(value);
    for (const auto &[label, entry] : choices) {
        if (name == label) {
            return entry;
        }
    }
    reject(ScenarioError::invalid_schema);
}

void read_solids(Scenario &scenario, const Json &values, std::set<std::string> &ids) {
    array_size(values, 0, max_solids);
    scenario.solids.reserve(values.size());
    for (const auto &value : values) {
        const auto solid_kind =
            kind<SolidKind>(value.at("kind"), {{"wall", SolidKind::wall},
                                               {"rack", SolidKind::rack},
                                               {"obstruction", SolidKind::obstruction}});
        std::string aisle;
        std::vector<std::int32_t> heights;
        if (solid_kind == SolidKind::rack) {
            fields(value, {"id", "kind", "boundsMm", "aisle", "shelfHeightsMm"});
            aisle = identifier(value.at("aisle"));
            const auto &shelves = value.at("shelfHeightsMm");
            array_size(shelves, 1, max_shelves);
            heights.reserve(shelves.size());
            for (const auto &shelf : shelves) {
                const auto previous = heights.empty() ? 0 : heights.back();
                const auto height = mm(shelf, 1, 20000);
                if (height <= previous) {
                    reject(ScenarioError::invalid_geometry);
                }
                heights.push_back(height);
            }
        } else {
            fields(value, {"id", "kind", "boundsMm"});
        }
        auto id = identifier(value.at("id"));
        unique(ids, id);
        const auto bounds = rectangle(value.at("boundsMm"));
        if (!contains(scenario.floor, bounds) ||
            touches_disk(bounds, scenario.spawn, scenario.robot_radius_mm) ||
            std::ranges::any_of(scenario.solids, [bounds](const Solid &other) {
                return interiors_overlap(bounds, other.view.bounds);
            })) {
            reject(ScenarioError::invalid_geometry);
        }
        scenario.solids.push_back(
            {{std::move(id), solid_kind, bounds, std::move(heights)}, std::move(aisle)});
    }
}

void read_zones(Scenario &scenario, const Json &values, std::set<std::string> &ids) {
    array_size(values, 5, max_zones);
    scenario.zones.reserve(values.size());
    for (const auto &value : values) {
        fields(value, {"id", "kind", "boundsMm"});
        auto id = identifier(value.at("id"));
        unique(ids, id);
        const auto zone_kind = kind<ZoneKind>(value.at("kind"), {{"aisle", ZoneKind::aisle},
                                                                 {"doorway", ZoneKind::doorway},
                                                                 {"receiving", ZoneKind::receiving},
                                                                 {"dock", ZoneKind::dock},
                                                                 {"charging", ZoneKind::charging}});
        const auto bounds = rectangle(value.at("boundsMm"));
        const bool clear_zone = zone_kind == ZoneKind::doorway || zone_kind == ZoneKind::dock ||
                                zone_kind == ZoneKind::charging;
        if (!contains(scenario.floor, bounds) ||
            (clear_zone && std::ranges::any_of(scenario.solids, [bounds](const Solid &other) {
                 return interiors_overlap(bounds, other.view.bounds);
             }))) {
            reject(ScenarioError::invalid_geometry);
        }
        scenario.zones.push_back({std::move(id), zone_kind, bounds});
    }
    const auto count = [&scenario](ZoneKind type) {
        return std::ranges::count(scenario.zones, type, &ZoneView::kind);
    };
    if (count(ZoneKind::aisle) < 1 || count(ZoneKind::doorway) < 1 ||
        count(ZoneKind::receiving) != 1 || count(ZoneKind::dock) != 1 ||
        count(ZoneKind::charging) != 1) {
        reject(ScenarioError::invalid_schema);
    }
    const auto dock = std::ranges::find(scenario.zones, ZoneKind::dock, &ZoneView::kind);
    const auto charging = std::ranges::find(scenario.zones, ZoneKind::charging, &ZoneView::kind);
    if (!contains(dock->bounds, charging->bounds)) {
        reject(ScenarioError::invalid_geometry);
    }
    for (const auto &solid : scenario.solids) {
        if (solid.view.kind == SolidKind::rack &&
            !std::ranges::any_of(scenario.zones, [&solid](const ZoneView &zone) {
                return zone.kind == ZoneKind::aisle && zone.id == solid.aisle;
            })) {
            reject(ScenarioError::invalid_reference);
        }
    }
}

void read_actors(Scenario &scenario, const Json &values, std::set<std::string> &ids) {
    array_size(values, 0, max_actors);
    scenario.actors.reserve(values.size());
    Random random{scenario.seed};
    for (const auto &value : values) {
        const auto actor_kind = kind<ActorKind>(
            value.at("kind"), {{"worker", ActorKind::worker},
                               {"cart", ActorKind::cart},
                               {"u-boat", ActorKind::u_boat},
                               {"temporary-obstruction", ActorKind::temporary_obstruction}});
        std::optional<ActiveWindow> active_window;
        if (actor_kind == ActorKind::temporary_obstruction) {
            fields(value, {"id", "kind", "startMm", "endMm", "sizeMm", "legTicks", "activeTicks"});
            const auto &times = value.at("activeTicks");
            array_size(times, 2, 2);
            constexpr std::uint64_t tick_limit = 1000000;
            const auto from = number(times.at(0), 0, tick_limit - 1);
            active_window = ActiveWindow{from, number(times.at(1), from + 1, tick_limit)};
        } else {
            fields(value, {"id", "kind", "startMm", "endMm", "sizeMm", "legTicks"});
        }
        const auto leg = static_cast<std::uint32_t>(number(value.at("legTicks"), 1, 3600));
        Actor actor{identifier(value.at("id")), actor_kind,
                    point(value.at("startMm")), point(value.at("endMm")),
                    point(value.at("sizeMm")),  leg,
                    random.next() % (2U * leg), active_window};
        unique(ids, actor.id);
        const auto bounds = sweep(actor);
        if (actor.size.x == 0 || actor.size.y == 0 || actor.start == actor.end ||
            (actor.start.x != actor.end.x && actor.start.y != actor.end.y) ||
            !contains(scenario.floor, bounds) ||
            touches_disk(bounds, scenario.spawn, scenario.robot_radius_mm) ||
            std::ranges::any_of(
                scenario.solids,
                [bounds](const Solid &other) { return overlaps(bounds, other.view.bounds); }) ||
            std::ranges::any_of(scenario.actors, [bounds](const Actor &other) {
                return overlaps(bounds, sweep(other));
            })) {
            reject(ScenarioError::invalid_geometry);
        }
        scenario.actors.push_back(std::move(actor));
    }
}

void read_tags(Scenario &scenario, const Json &values) {
    array_size(values, 0, scenario_tag_limit);
    scenario.tags.reserve(values.size());
    std::set<std::string> ids;
    for (const auto &value : values) {
        std::int32_t heading_deg = 0;
        if (value.contains("headingDeg")) {
            fields(value, {"epc", "rack", "shelf", "offsetMm", "headingDeg"});
            heading_deg = mm(value.at("headingDeg"), 0, 359);
        } else {
            fields(value, {"epc", "rack", "shelf", "offsetMm"});
        }
        const auto epc = string(value.at("epc"));
        if (epc.size() != 24 || !std::ranges::all_of(epc, [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
            })) {
            reject(ScenarioError::invalid_schema);
        }
        const auto rack_id = identifier(value.at("rack"));
        const auto rack = std::ranges::find_if(scenario.solids, [&rack_id](const Solid &solid) {
            return solid.view.kind == SolidKind::rack && solid.view.id == rack_id;
        });
        const auto shelf = static_cast<std::size_t>(number(value.at("shelf"), 1, max_shelves));
        if (rack == scenario.solids.end() || shelf > rack->view.shelf_heights_mm.size()) {
            reject(ScenarioError::invalid_reference);
        }
        const auto offset = point(value.at("offsetMm"));
        const auto bounds = rack->view.bounds;
        const PointMm position{bounds.min.x + offset.x, bounds.min.y + offset.y};
        if (position.x >= bounds.max.x || position.y >= bounds.max.y) {
            reject(ScenarioError::invalid_geometry);
        }
        std::string owned_epc{epc};
        unique(ids, owned_epc);
        scenario.tags.push_back({std::move(owned_epc), position,
                                 rack->view.shelf_heights_mm.at(shelf - 1), heading_deg});
    }
}

} // namespace

Result<Scenario, ScenarioError> decode(std::string_view input) {
    try {
        const auto root = parse(input);
        if (string(root.at("schemaVersion")) != "1.0.0") {
            return ScenarioError::unsupported_version;
        }
        fields(root, {"schemaVersion", "scenarioId", "seed", "sizeMm", "robot", "fixtures", "zones",
                      "actors", "items"});
        const auto &robot = root.at("robot");
        fields(robot, {"spawnMm", "radiusMm"});
        const auto size = point(root.at("sizeMm"));
        Scenario scenario{
            ScenarioId{number(root.at("scenarioId"), 1, max_scenario_id)},
            static_cast<std::uint32_t>(
                number(root.at("seed"), 0, std::numeric_limits<std::uint32_t>::max())),
            {{0, 0}, size},
            point(robot.at("spawnMm")),
            mm(robot.at("radiusMm"), 1, max_robot_radius_mm),
            {},
            {},
            {},
            {},
        };
        if (!inside_floor(scenario.floor, scenario.spawn, scenario.robot_radius_mm)) {
            return ScenarioError::invalid_geometry;
        }
        std::set<std::string> ids;
        read_solids(scenario, root.at("fixtures"), ids);
        read_zones(scenario, root.at("zones"), ids);
        read_actors(scenario, root.at("actors"), ids);
        read_tags(scenario, root.at("items"));
        return scenario;
    } catch (ScenarioError error) {
        return error;
    } catch (const Json::parse_error &) {
        return ScenarioError::malformed_json;
    } catch (const Json::out_of_range &error) {
        // nlohmann reports a non-representable numeric token as out_of_range during parse.
        return error.id == 406 ? ScenarioError::malformed_json : ScenarioError::invalid_schema;
    } catch (const Json::type_error &) {
        return ScenarioError::invalid_schema;
    }
}

} // namespace argus::simulation::detail
