#pragma once

#include "argus/core/types.hpp"
#include "argus/inventory/catalog.hpp"

#include <optional>

namespace argus::mission {

enum class State {
    draft,
    queued,
    planning,
    executing,
    paused,
    blocked,
    returning,
    completed,
    failed,
    cancelled
};
enum class Workflow { scan_all, find_item, dock, inspect };
enum class Priority { routine, urgent, safety };
enum class Action {
    enqueue,
    dispatch,
    begin,
    pause,
    obstruct,
    retry,
    return_home,
    finish,
    fail,
    cancel,
    recover,
    resume,
    charge
};
enum class Cause {
    submitted,
    queued,
    scheduled,
    ready,
    operator_request,
    obstruction,
    cleared,
    reserve,
    sensor_degraded,
    localization_lost,
    storage_error,
    timeout,
    preempted,
    restart_relocalize,
    arrival,
    work_finished,
    return_unavailable,
    inspected,
    work_unavailable
};
enum class Error { invalid, conflict, full, not_found, stale, illegal_transition, busy, storage };
enum class Motion { brake, work, return_to_dock };

struct Intent {
    MissionId id;
    Workflow workflow;
    Priority priority;
    std::uint64_t limit_ms;
    std::optional<inventory::Dpci> product{};
    std::optional<inventory::AisleId> aisle{};
    bool operator==(const Intent &) const = default;
};

struct Checkpoint {
    std::uint64_t sample{};
    std::uint64_t elapsed_ms{};
    std::uint64_t planned_mm{};
    std::uint64_t actual_mm{};
    std::uint64_t planned_scans{};
    std::uint64_t covered_scans{};
    std::uint64_t raw_reads{};
    std::uint64_t exceptions{};
    std::uint64_t covered_mask{};
};

struct Lease {
    MissionId id;
    std::uint64_t revision;
};

struct Record {
    Intent intent;
    State state;
    Cause cause;
    std::uint64_t revision;
    Checkpoint checkpoint{};
    [[nodiscard]] Lease lease() const noexcept { return {intent.id, revision}; }
};

struct Event {
    std::uint64_t revision;
    State from;
    State to;
    Cause cause;
    std::uint64_t elapsed_ms;
};

struct Readiness {
    std::uint64_t energy_uj{};
    std::optional<std::uint64_t> return_energy_uj{};
    std::uint64_t next_step_energy_uj{};
    bool localized{};
    bool sensors_ok{};
    bool obstructed{};
    bool low_battery{};
    bool docked{};
    bool work_finished{};
    bool inspected{};
    bool storage_ok{true};
};

struct Decision {
    Motion motion{Motion::brake};
    std::optional<Action> action{};
    Cause cause{Cause::ready};
};

inline constexpr std::uint64_t reserve_uj = 250000000;
inline constexpr std::size_t max_pending = 16;
inline constexpr std::size_t max_records = 4096;
inline constexpr std::uint64_t max_revision = 255;

[[nodiscard]] bool valid(const Intent &intent) noexcept;
[[nodiscard]] bool valid(const Checkpoint &checkpoint) noexcept;
[[nodiscard]] bool terminal(State state) noexcept;
[[nodiscard]] std::optional<State> next(State state, Action action) noexcept;
[[nodiscard]] Decision decide(const Record &record, const Readiness &ready) noexcept;
[[nodiscard]] std::string_view label(State state) noexcept;
[[nodiscard]] std::string_view label(Workflow kind) noexcept;
[[nodiscard]] std::string_view label(Cause cause) noexcept;
[[nodiscard]] std::string_view label(Error error) noexcept;

} // namespace argus::mission
