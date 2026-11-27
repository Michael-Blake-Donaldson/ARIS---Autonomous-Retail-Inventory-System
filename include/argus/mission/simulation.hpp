#pragma once

#include "argus/inventory/repository.hpp"
#include "argus/mission/store.hpp"

namespace argus::simulation {
struct ObservedRun;
}

namespace argus::mission {

struct DemoOptions {
    std::uint32_t ticks{1600};
    bool reserve_test{};
    std::optional<MissionId> resume{};
    std::optional<MissionId> expected{};
};

struct DemoResult {
    std::optional<Record> record;
    std::uint64_t remaining_energy_uj{};
    bool docked{};
    bool reserve_before_low_battery{};
};

[[nodiscard]] Result<DemoResult, Error> run_demo(Store &store, inventory::Repository *catalog,
                                                 DemoOptions options = {});
[[nodiscard]] Result<DemoResult, Error> run_observed(Store &store, inventory::Repository *catalog,
                                                     simulation::ObservedRun &run,
                                                     DemoOptions options = {});

} // namespace argus::mission
