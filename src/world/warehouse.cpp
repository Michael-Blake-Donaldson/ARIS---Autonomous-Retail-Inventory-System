#include "argus/simulation/warehouse.hpp"

#include "argus/simulation/sensor_query.hpp"
#include "scenario.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <ios>
#include <istream>
#include <string>
#include <utility>
#include <variant>

namespace argus::simulation {
namespace {

using namespace world;
using detail::actor_bounds;
using detail::Scenario;

class StaticGrid {
  public:
    explicit StaticGrid(const Scenario &scenario) : floor_{scenario.floor} {
        for (std::size_t index = 0; index < scenario.solids.size(); ++index) {
            for_cells(scenario.solids.at(index).view.bounds,
                      [this, index](std::size_t cell) { cells_.at(cell).set(index); });
        }
    }

    [[nodiscard]] std::bitset<detail::max_solids> candidates(RectMm region) const noexcept {
        std::bitset<detail::max_solids> result;
        for_cells(region, [this, &result](std::size_t cell) { result |= cells_.at(cell); });
        return result;
    }

  private:
    static constexpr std::int32_t cell_mm = 4000;
    static constexpr std::int32_t row_size = detail::max_extent_mm / cell_mm + 1;

    template <typename Visit> void for_cells(RectMm region, Visit visit) const noexcept {
        const auto left = std::max(region.min.x, floor_.min.x);
        const auto right = std::min(region.max.x, floor_.max.x);
        const auto bottom = std::max(region.min.y, floor_.min.y);
        const auto top = std::min(region.max.y, floor_.max.y);
        if (left > right || bottom > top) {
            return;
        }
        for (auto y = bottom / cell_mm; y <= top / cell_mm; ++y) {
            for (auto x = left / cell_mm; x <= right / cell_mm; ++x) {
                visit(static_cast<std::size_t>(y * row_size + x));
            }
        }
    }

    RectMm floor_;
    std::array<std::bitset<detail::max_solids>, row_size * row_size> cells_{};
};

} // namespace

struct Warehouse::State {
    explicit State(Scenario data) : scenario{std::move(data)}, grid{scenario} {}
    Scenario scenario;
    StaticGrid grid;
};

Warehouse::Warehouse(std::shared_ptr<const State> state) : state_{std::move(state)} {}

Result<Warehouse, ScenarioError> Warehouse::from_json(std::string_view text) {
    auto parsed = detail::decode(text);
    if (const auto *error = std::get_if<ScenarioError>(&parsed)) {
        return *error;
    }
    return Warehouse{std::make_shared<const State>(std::get<Scenario>(std::move(parsed)))};
}

Result<Warehouse, ScenarioError> Warehouse::from_stream(std::istream &input) {
    std::string text(scenario_byte_limit + 1, '\0');
    try {
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
    } catch (const std::ios_base::failure &) {
        if (input.bad() || !input.eof()) {
            return ScenarioError::input_failure;
        }
    }
    if (input.bad() || (input.fail() && !input.eof())) {
        return ScenarioError::input_failure;
    }
    text.resize(static_cast<std::size_t>(input.gcount()));
    return from_json(text);
}

Snapshot Warehouse::snapshot(std::uint64_t tick) const {
    const auto &scenario = state_->scenario;
    Snapshot view{
        scenario.id, scenario.seed,  tick, scenario.floor, scenario.spawn, scenario.robot_radius_mm,
        {},          scenario.zones, {}};
    view.solids.reserve(scenario.solids.size());
    for (const auto &solid : scenario.solids) {
        view.solids.push_back(solid.view);
    }
    view.actors.reserve(scenario.actors.size());
    for (const auto &actor : scenario.actors) {
        if (detail::active_at(actor, tick)) {
            view.actors.push_back({actor.id, actor.kind, actor_bounds(actor, tick)});
        }
    }
    return view;
}

bool Warehouse::collides(PointMm center, std::int32_t radius, std::uint64_t tick) const noexcept {
    const auto &scenario = state_->scenario;
    if (!detail::inside_floor(scenario.floor, center, radius)) {
        return true;
    }
    const RectMm query{{center.x - radius, center.y - radius},
                       {center.x + radius, center.y + radius}};
    const auto candidates = state_->grid.candidates(query);
    for (std::size_t index = 0; index < scenario.solids.size(); ++index) {
        if (candidates.test(index) &&
            touches_disk(scenario.solids.at(index).view.bounds, center, radius)) {
            return true;
        }
    }
    return std::ranges::any_of(scenario.actors, [center, radius, tick](const detail::Actor &actor) {
        return detail::active_at(actor, tick) &&
               touches_disk(actor_bounds(actor, tick), center, radius);
    });
}

SensorQuery::SensorQuery(Warehouse warehouse) : warehouse_{std::move(warehouse)} {}

std::vector<RectMm> SensorQuery::obstacles(RectMm region, std::uint64_t tick) const {
    std::vector<RectMm> result;
    if (region.min.x > region.max.x || region.min.y > region.max.y) {
        return result;
    }
    const auto &state = *warehouse_.state_;
    const auto candidates = state.grid.candidates(region);
    for (std::size_t index = 0; index < state.scenario.solids.size(); ++index) {
        const auto bounds = state.scenario.solids.at(index).view.bounds;
        if (candidates.test(index) && overlaps(region, bounds)) {
            result.push_back(bounds);
        }
    }
    for (const auto &actor : state.scenario.actors) {
        const auto bounds = actor_bounds(actor, tick);
        if (detail::active_at(actor, tick) && overlaps(region, bounds)) {
            result.push_back(bounds);
        }
    }
    return result;
}

std::vector<TagTruth> SensorQuery::tags(RectMm region) const {
    std::vector<TagTruth> result;
    for (const auto &tag : warehouse_.state_->scenario.tags) {
        if (contains(region, {tag.position, tag.position})) {
            result.push_back(tag);
        }
    }
    return result;
}

std::string_view scenario_error_code(ScenarioError error) noexcept {
    switch (error) {
    case ScenarioError::too_large:
        return "scenario_too_large";
    case ScenarioError::malformed_json:
        return "malformed_scenario_json";
    case ScenarioError::duplicate_key:
        return "duplicate_scenario_key";
    case ScenarioError::unsupported_version:
        return "unsupported_scenario_version";
    case ScenarioError::invalid_schema:
        return "invalid_scenario_schema";
    case ScenarioError::invalid_geometry:
        return "invalid_scenario_geometry";
    case ScenarioError::invalid_reference:
        return "invalid_scenario_reference";
    case ScenarioError::duplicate_identifier:
        return "duplicate_scenario_identifier";
    case ScenarioError::input_failure:
        return "scenario_input_failure";
    case ScenarioError::unknown_fixture:
        return "unknown_warehouse_fixture";
    }
    return "unknown_scenario_error";
}

} // namespace argus::simulation
