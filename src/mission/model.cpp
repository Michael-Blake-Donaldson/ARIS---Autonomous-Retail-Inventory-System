#include "argus/mission/model.hpp"

#include <array>
#include <bit>
#include <limits>

namespace argus::mission {
namespace {

template <typename Enum, std::size_t Size>
std::string_view name(Enum value, const std::array<std::string_view, Size> &names) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < names.size() ? names[index] : "unknown";
}

struct Edge {
    State from;
    Action action;
    State to;
};

constexpr std::array edges{Edge{State::draft, Action::enqueue, State::queued},
                           Edge{State::draft, Action::cancel, State::cancelled},
                           Edge{State::queued, Action::dispatch, State::planning},
                           Edge{State::queued, Action::cancel, State::cancelled},
                           Edge{State::planning, Action::begin, State::executing},
                           Edge{State::planning, Action::return_home, State::returning},
                           Edge{State::planning, Action::pause, State::paused},
                           Edge{State::planning, Action::obstruct, State::blocked},
                           Edge{State::planning, Action::fail, State::failed},
                           Edge{State::planning, Action::cancel, State::cancelled},
                           Edge{State::planning, Action::recover, State::paused},
                           Edge{State::executing, Action::pause, State::paused},
                           Edge{State::executing, Action::obstruct, State::blocked},
                           Edge{State::executing, Action::return_home, State::returning},
                           Edge{State::executing, Action::finish, State::completed},
                           Edge{State::executing, Action::fail, State::failed},
                           Edge{State::executing, Action::cancel, State::cancelled},
                           Edge{State::executing, Action::recover, State::paused},
                           Edge{State::paused, Action::resume, State::queued},
                           Edge{State::paused, Action::cancel, State::cancelled},
                           Edge{State::paused, Action::fail, State::failed},
                           Edge{State::blocked, Action::retry, State::planning},
                           Edge{State::blocked, Action::return_home, State::returning},
                           Edge{State::blocked, Action::pause, State::paused},
                           Edge{State::blocked, Action::recover, State::paused},
                           Edge{State::blocked, Action::fail, State::failed},
                           Edge{State::blocked, Action::cancel, State::cancelled},
                           Edge{State::returning, Action::charge, State::paused},
                           Edge{State::returning, Action::finish, State::completed},
                           Edge{State::returning, Action::pause, State::paused},
                           Edge{State::returning, Action::recover, State::paused},
                           Edge{State::returning, Action::fail, State::failed},
                           Edge{State::returning, Action::cancel, State::cancelled}};

Decision change(Action action, Cause cause) noexcept { return {Motion::brake, action, cause}; }

} // namespace

bool valid(const Intent &intent) noexcept {
    if (intent.id.value() == 0 ||
        intent.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        intent.limit_ms < 50 || intent.limit_ms > 3600000 || label(intent.workflow) == "unknown" ||
        static_cast<unsigned>(intent.priority) > static_cast<unsigned>(Priority::safety)) {
        return false;
    }
    if (intent.workflow == Workflow::dock) {
        return intent.priority == Priority::safety && !intent.aisle && !intent.product;
    }
    if (intent.priority == Priority::safety ||
        (intent.aisle && !inventory::valid_code(intent.aisle->value))) {
        return false;
    }
    switch (intent.workflow) {
    case Workflow::find_item:
        return intent.product && inventory::valid(*intent.product);
    case Workflow::inspect:
        return intent.aisle && !intent.product;
    case Workflow::scan_all:
        return !intent.product && !intent.aisle;
    case Workflow::dock:
        break;
    }
    return false;
}

bool valid(const Checkpoint &point) noexcept {
    return point.sample <= 1000000 && point.elapsed_ms <= 7200000 && point.planned_mm <= 10000000 &&
           point.actual_mm <= 10000000 && point.planned_scans <= 16 &&
           point.covered_scans <= point.planned_scans && point.raw_reads <= 1000000 &&
           point.exceptions <= 100000 && point.covered_mask <= 0xffff &&
           point.covered_scans == static_cast<std::uint64_t>(std::popcount(point.covered_mask));
}

bool terminal(State state) noexcept {
    return state == State::completed || state == State::failed || state == State::cancelled;
}

std::optional<State> next(State state, Action action) noexcept {
    for (const auto &edge : edges) {
        if (edge.from == state && edge.action == action) {
            return edge.to;
        }
    }
    return std::nullopt;
}

Decision decide(const Record &record, const Readiness &ready) noexcept {
    const auto state = record.state;
    if (state != State::planning && state != State::executing && state != State::blocked &&
        state != State::returning) {
        return {};
    }
    if (!ready.storage_ok) {
        return change(Action::pause, Cause::storage_error);
    }
    if (record.checkpoint.elapsed_ms >= record.intent.limit_ms) {
        return change(Action::fail, Cause::timeout);
    }
    if (!ready.localized || !ready.sensors_ok) {
        const auto cause = ready.localized ? Cause::sensor_degraded : Cause::localization_lost;
        return state == State::planning ? Decision{Motion::brake, {}, cause}
                                        : change(Action::pause, cause);
    }
    if (state == State::returning) {
        if (ready.docked) {
            return record.intent.workflow == Workflow::dock
                       ? change(Action::finish, Cause::arrival)
                       : change(Action::charge, Cause::arrival);
        }
        if (!ready.return_energy_uj || ready.energy_uj < *ready.return_energy_uj) {
            return {Motion::brake, {}, Cause::return_unavailable};
        }
        return {ready.obstructed ? Motion::brake : Motion::return_to_dock,
                {},
                ready.obstructed ? Cause::obstruction : Cause::reserve};
    }
    if (!ready.return_energy_uj) {
        return state == State::executing ? change(Action::obstruct, Cause::return_unavailable)
                                         : Decision{Motion::brake, {}, Cause::return_unavailable};
    }
    const auto after_return =
        ready.energy_uj > *ready.return_energy_uj ? ready.energy_uj - *ready.return_energy_uj : 0;
    const bool reserve_hit =
        after_return <= reserve_uj || ready.next_step_energy_uj >= after_return - reserve_uj;
    if (ready.low_battery || reserve_hit || record.intent.workflow == Workflow::dock) {
        return change(Action::return_home, Cause::reserve);
    }
    if (ready.obstructed) {
        return state == State::blocked ? Decision{Motion::brake, {}, Cause::obstruction}
                                       : change(Action::obstruct, Cause::obstruction);
    }
    if (state == State::blocked) {
        return change(Action::retry, Cause::cleared);
    }
    if (state == State::planning) {
        return change(Action::begin, Cause::ready);
    }
    if (record.intent.workflow == Workflow::inspect) {
        return ready.inspected ? change(Action::finish, Cause::inspected) : Decision{};
    }
    if (ready.work_finished && record.checkpoint.planned_scans > 0 &&
        record.checkpoint.covered_scans == record.checkpoint.planned_scans) {
        return change(Action::finish, Cause::work_finished);
    }
    return {Motion::work, {}, Cause::ready};
}

std::string_view label(State state) noexcept {
    constexpr std::array<std::string_view, 10> names{"draft",  "queued",   "planning",  "executing",
                                                     "paused", "blocked",  "returning", "completed",
                                                     "failed", "cancelled"};
    return name(state, names);
}
std::string_view label(Workflow kind) noexcept {
    constexpr std::array<std::string_view, 4> names{"scan-all", "find-item", "dock", "inspect"};
    return name(kind, names);
}
std::string_view label(Cause cause) noexcept {
    constexpr std::array<std::string_view, 19> names{"submitted",
                                                     "queued",
                                                     "scheduled",
                                                     "ready",
                                                     "operator_request",
                                                     "obstruction",
                                                     "cleared",
                                                     "reserve",
                                                     "sensor_degraded",
                                                     "localization_lost",
                                                     "storage_error",
                                                     "timeout",
                                                     "preempted",
                                                     "restart_relocalize",
                                                     "arrival",
                                                     "work_finished",
                                                     "return_unavailable",
                                                     "inspected",
                                                     "work_unavailable"};
    return name(cause, names);
}
std::string_view label(Error error) noexcept {
    constexpr std::array<std::string_view, 8> names{"invalid",   "conflict", "full",
                                                    "not_found", "stale",    "illegal_transition",
                                                    "busy",      "storage"};
    return name(error, names);
}

} // namespace argus::mission
