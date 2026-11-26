#pragma once

#include "argus/mission/model.hpp"

#include <filesystem>
#include <memory>
#include <vector>

struct sqlite3;

namespace argus::mission {

struct Cycle {
    Record record;
    Motion motion;
    Cause cause;
};

struct LocalizationProof {
    std::uint64_t observation_tick;
    double position_error_m;
    double heading_error_rad;
    bool reliable;
};

inline constexpr std::size_t archived_map_limit = 128 * 1024;
inline constexpr std::size_t archived_report_limit = 512 * 1024;

struct SavedRun {
    RunId id;
    ScenarioId scenario;
    std::uint64_t sample_tick;
    std::uint64_t mission;
    std::uint64_t search_session;
    std::uint64_t audit_session;
    std::string map;
    std::string report;
};

inline constexpr std::size_t shelf_history_limit = 20;
inline constexpr std::size_t shelf_report_limit = 128 * 1024;

struct ShelfAuditKey {
    RobotId robot;
    RunId run;
    std::uint64_t session;
    bool operator==(const ShelfAuditKey &) const = default;
};

struct ShelfAudit {
    inventory::BinId location;
    RobotId robot;
    RunId run;
    std::uint64_t session;
    std::uint64_t completed_ms;
    std::string report;
    std::optional<ShelfAuditKey> recheck_of{};
    std::optional<std::uint64_t> reviewed_ms{};
    [[nodiscard]] ShelfAuditKey key() const noexcept { return {robot, run, session}; }
};

[[nodiscard]] bool valid_shelf_location(const inventory::BinId &location) noexcept;

class Store {
  public:
    [[nodiscard]] static Result<Store, Error> open(const std::filesystem::path &path);
    [[nodiscard]] Result<Record, Error> submit(const Intent &intent);
    [[nodiscard]] Result<Record, Error> get(MissionId id);
    [[nodiscard]] Result<std::vector<Event>, Error> events(MissionId id);
    [[nodiscard]] Result<std::optional<Record>, Error> latest();
    [[nodiscard]] Result<std::uint64_t, Error> highest_id();
    [[nodiscard]] Result<std::monostate, Error> save_run(const SavedRun &run);
    [[nodiscard]] Result<std::optional<SavedRun>, Error> saved_run();
    [[nodiscard]] Result<std::monostate, Error> save_shelf_audit(const ShelfAudit &audit);
    [[nodiscard]] Result<std::uint64_t, Error> review_shelf_audit(const inventory::BinId &location,
                                                                  ShelfAuditKey key,
                                                                  std::uint64_t reviewed_ms);
    [[nodiscard]] Result<std::vector<ShelfAudit>, Error>
    shelf_history(const inventory::BinId &location);
    [[nodiscard]] Result<std::optional<Record>, Error>
    dispatch(std::optional<MissionId> expected = {});
    [[nodiscard]] Result<Record, Error> cancel(MissionId id);
    [[nodiscard]] Result<Record, Error> confirm_inspection(MissionId id);
    [[nodiscard]] Result<Record, Error> abort(Lease lease, Cause cause);
    [[nodiscard]] Result<Record, Error> resume(MissionId id, LocalizationProof proof);
    [[nodiscard]] Result<std::size_t, Error> recover(std::optional<MissionId> expected = {});
    [[nodiscard]] Result<Record, Error> checkpoint(Lease lease, const Checkpoint &checkpoint);
    [[nodiscard]] Result<Cycle, Error> cycle(Lease lease, const Readiness &readiness);

  private:
    struct Close {
        void operator()(sqlite3 *db) const noexcept;
    };
    explicit Store(std::unique_ptr<sqlite3, Close> database) noexcept;
    std::unique_ptr<sqlite3, Close> database_;
};

} // namespace argus::mission
