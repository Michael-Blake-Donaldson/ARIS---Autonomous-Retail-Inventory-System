#include "application.hpp"
#include "argus/core/random.hpp"
#include "warehouse_io.hpp"

#include <iostream>

namespace {

void write_snapshot(const argus::world::Snapshot &snapshot) {
    std::cout << "{\"scenarioId\":" << snapshot.scenario_id.value() << ",\"seed\":" << snapshot.seed
              << ",\"tick\":" << snapshot.tick << ",\"sizeMm\":[" << snapshot.floor.max.x << ','
              << snapshot.floor.max.y << "],\"fixtureCount\":" << snapshot.solids.size()
              << ",\"zoneCount\":" << snapshot.zones.size() << ",\"actors\":[";
    const char *separator = "";
    for (const auto &actor : snapshot.actors) {
        const auto &bounds = actor.bounds;
        std::cout << separator << "{\"id\":\"" << actor.id << "\",\"boundsMm\":[" << bounds.min.x
                  << ',' << bounds.min.y << ',' << bounds.max.x - bounds.min.x << ','
                  << bounds.max.y - bounds.min.y << "]}";
        separator = ",";
    }
    std::cout << "]}\n";
}

argus::app::Status run_headless(const argus::app::Options &options) {
    using argus::app::WarehouseSource;
    using argus::simulation::ScenarioError;
    using argus::simulation::Warehouse;
    if (options.warehouse_source == WarehouseSource::export_fixture) {
        return argus::app::export_warehouse(options.fixture);
    }
    if (options.warehouse_source == WarehouseSource::none) {
        argus::Random random{options.seed};
        std::cout << "{\"seed\":" << options.seed << ",\"firstSample\":" << random.next() << "}\n";
    } else {
        const auto loaded = argus::app::open_warehouse(options);
        if (const auto *error = std::get_if<ScenarioError>(&loaded)) {
            return *error;
        }
        write_snapshot(std::get<Warehouse>(loaded).snapshot(options.tick));
    }
    if (!std::cout.flush()) {
        return argus::app::Error::output_failure;
    }
    return std::monostate{};
}

} // namespace

int main(int argc, char **argv) {
    return argus::app::run_application(argc, argv, "argus-headless", run_headless);
}
