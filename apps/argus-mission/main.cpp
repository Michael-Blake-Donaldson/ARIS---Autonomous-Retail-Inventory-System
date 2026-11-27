#include "argus/mission/simulation.hpp"

#include <array>
#include <charconv>
#include <exception>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

using argus::MissionId;
using namespace argus::mission;
namespace inventory = argus::inventory;
using Json = nlohmann::json;

constexpr std::string_view usage =
    "Usage: argus-mission DATABASE submit ID KIND PRIORITY [DPCI_OR_AISLE] [LAST_AISLE]\n"
    "       argus-mission DATABASE run TICKS CATALOG_OR_DASH [normal|reserve] [RESUME_ID]\n"
    "       argus-mission DATABASE report|cancel|inspect-confirm ID\n"
    "       argus-mission DATABASE recover\n"
    "KIND: scan-all, find-item, dock, inspect. PRIORITY: routine, urgent, safety.\n";

std::string_view argument(const char *value, std::size_t max) noexcept {
    std::size_t size = 0;
    while (size <= max && value[size] != '\0') {
        ++size;
    }
    return {value, size};
}

std::optional<std::uint64_t> number(const char *input, std::size_t width) {
    const auto value = argument(input, width);
    std::uint64_t parsed = 0;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return ec == std::errc{} && end == value.data() + value.size() && value.size() <= width &&
                   parsed > 0
               ? std::optional{parsed}
               : std::nullopt;
}

template <typename T> T take(argus::Result<T, Error> result) {
    if (const auto *error = std::get_if<Error>(&result)) {
        throw *error;
    }
    return std::get<T>(std::move(result));
}

int print(const Json &value) {
    return (std::cout << value.dump(-1, ' ', true) << '\n').flush() ? 0 : 1;
}

Json postmortem(Store &store, const Record &record) {
    const auto &point = record.checkpoint;
    auto events = Json::array();
    for (const auto &event : take(store.events(record.intent.id))) {
        if (event.revision > record.revision) {
            break;
        }
        events.push_back({{"revision", event.revision},
                          {"from", label(event.from)},
                          {"to", label(event.to)},
                          {"cause", label(event.cause)},
                          {"elapsedMs", event.elapsed_ms}});
    }
    return {{"missionId", std::to_string(record.intent.id.value())},
            {"workflow", label(record.intent.workflow)},
            {"state", label(record.state)},
            {"cause", label(record.cause)},
            {"revision", record.revision},
            {"plannedDistanceMm", point.planned_mm},
            {"actualOdometryMm", point.actual_mm},
            {"durationMs", point.elapsed_ms},
            {"plannedViews", point.planned_scans},
            {"coveredViews", point.covered_scans},
            {"rawReads", point.raw_reads},
            {"exceptions", point.exceptions},
            {"events", std::move(events)}};
}

Intent intent(int argc, char **argv) {
    const auto id = number(argv[3], 19);
    if (!id) {
        throw Error::invalid;
    }
    const auto command = argument(argv[4], 9);
    std::optional<Workflow> workflow;
    for (const auto candidate :
         {Workflow::scan_all, Workflow::find_item, Workflow::dock, Workflow::inspect}) {
        if (label(candidate) == command) {
            workflow = candidate;
        }
    }
    const auto name = argument(argv[5], 7);
    const auto priority = name == "routine"  ? std::optional{Priority::routine}
                          : name == "urgent" ? std::optional{Priority::urgent}
                          : name == "safety" ? std::optional{Priority::safety}
                                             : std::nullopt;
    if (!workflow || !priority) {
        throw Error::invalid;
    }
    Intent requested{MissionId{*id}, *workflow, *priority, 100000};
    if (*workflow == Workflow::find_item && argc >= 7) {
        requested.product = inventory::Dpci{std::string{argument(argv[6], 11)}};
        if (argc == 8) {
            requested.aisle = inventory::AisleId{std::string{argument(argv[7], 32)}};
        }
    } else if (*workflow == Workflow::inspect && argc == 7) {
        requested.aisle = inventory::AisleId{std::string{argument(argv[6], 32)}};
    } else if (argc != 6) {
        throw Error::invalid;
    }
    if (!valid(requested)) {
        throw Error::invalid;
    }
    return requested;
}

int run_simulation(Store &store, int argc, char **argv) {
    const auto ticks = number(argv[3], 4);
    const auto path = argument(argv[4], 4096);
    const auto mode = argc >= 6 ? argument(argv[5], 7) : "normal";
    if (!ticks || *ticks > 2400 || path.empty() || path.size() > 4096 ||
        (mode != "normal" && mode != "reserve")) {
        throw Error::invalid;
    }
    DemoOptions options{static_cast<std::uint32_t>(*ticks), mode == "reserve"};
    if (argc == 7) {
        const auto id = number(argv[6], 19);
        if (!id) {
            throw Error::invalid;
        }
        options.resume = MissionId{*id};
    }
    std::optional<inventory::Repository> catalog;
    if (path != "-") {
        auto opened = inventory::Repository::open(std::filesystem::path{path});
        if (auto *repo = std::get_if<inventory::Repository>(&opened)) {
            catalog.emplace(std::move(*repo));
        } else {
            throw Error::storage;
        }
    }
    const auto result = take(run_demo(store, catalog ? &*catalog : nullptr, options));
    if (!result.record) {
        return print({{"status", "idle"}});
    }
    auto report = postmortem(store, *result.record);
    report["docked"] = result.docked;
    report["remainingEnergyUj"] = result.remaining_energy_uj;
    report["reserveBeforeLowBattery"] = result.reserve_before_low_battery;
    return print(report);
}

int run(int argc, char **argv) {
    if (argc == 2 && argument(argv[1], 6) == "--help") {
        return (std::cout << usage).flush() ? 0 : 1;
    }
    if (argc < 3 || argc > 8) {
        throw Error::invalid;
    }
    const auto path = argument(argv[1], 4096);
    if (path.empty() || path.size() > 4096) {
        throw Error::invalid;
    }
    const auto action = argument(argv[2], 15);
    if (action == "submit" && argc >= 6) {
        const auto request = intent(argc, argv);
        auto store = take(Store::open(std::filesystem::path{path}));
        return print(postmortem(store, take(store.submit(request))));
    }
    if ((action == "run" && argc >= 5 && argc <= 7) || (action == "recover" && argc == 3)) {
        auto store = take(Store::open(std::filesystem::path{path}));
        return action == "recover" ? print({{"pausedForRelocalization", take(store.recover())}})
                                   : run_simulation(store, argc, argv);
    }
    if (argc == 4 && (action == "report" || action == "cancel" || action == "inspect-confirm")) {
        const auto id = number(argv[3], 19);
        if (!id) {
            throw Error::invalid;
        }
        auto store = take(Store::open(std::filesystem::path{path}));
        const MissionId mission{*id};
        const auto result = action == "cancel"            ? store.cancel(mission)
                            : action == "inspect-confirm" ? store.confirm_inspection(mission)
                                                          : store.get(mission);
        return print(postmortem(store, take(result)));
    }
    throw Error::invalid;
}

} // namespace

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (Error error) {
        std::cerr << "argus-mission: " << label(error) << '\n';
        return error == Error::invalid ? 2 : 1;
    } catch (const std::exception &) {
        std::cerr << "argus-mission: storage\n";
        return 1;
    }
}
