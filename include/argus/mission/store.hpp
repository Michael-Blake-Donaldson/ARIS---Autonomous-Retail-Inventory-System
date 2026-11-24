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

class Store {
  public:
    [[nodiscard]] static Result<Store, Error> open(const std::filesystem::path &path);
    [[nodiscard]] Result<Record, Error> submit(const Intent &intent);
    [[nodiscard]] Result<Record, Error> get(MissionId id);
    [[nodiscard]] Result<std::vector<Event>, Error> events(MissionId id);
    [[nodiscard]] Result<std::optional<Record>, Error> latest();
    [[nodiscard]] Result<std::optional<Record>, Error> dispatch();
    [[nodiscard]] Result<Record, Error> cancel(MissionId id);
    [[nodiscard]] Result<Record, Error> confirm_inspection(MissionId id);
    [[nodiscard]] Result<Record, Error> abort(Lease lease, Cause cause);
    [[nodiscard]] Result<Record, Error> resume(MissionId id, LocalizationProof proof);
    [[nodiscard]] Result<std::size_t, Error> recover();
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
