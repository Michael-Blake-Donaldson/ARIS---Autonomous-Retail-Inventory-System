#include "argus/simulation/fixtures.hpp"

#include "warehouse_fixtures.hpp"

namespace argus::simulation {

Result<std::string_view, ScenarioError> fixture_json(std::string_view name) noexcept {
    if (name == "small-backroom") {
        return detail::small_backroom_json.substr(1);
    }
    if (name == "cluttered-receiving") {
        return detail::cluttered_receiving_json.substr(1);
    }
    if (name == "broad-stockroom") {
        return detail::broad_stockroom_json.substr(1);
    }
    return ScenarioError::unknown_fixture;
}

} // namespace argus::simulation
