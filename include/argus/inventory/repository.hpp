#pragma once

#include "argus/core/types.hpp"
#include "argus/inventory/catalog.hpp"
#include "argus/robot/control.hpp"
#include "argus/sensors/observations.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>

struct sqlite3;

namespace argus::inventory {

using SessionId = Id<struct SessionTag>;

struct ScanSession {
    SessionId id;
    MissionId mission;
    RobotId robot;
    std::uint64_t started_ms;
    bool operator==(const ScanSession &) const = default;
};

struct ScanEvidence {
    Epc epc;
    std::uint64_t timestamp_ms;
    robot::Pose estimated_pose;
    AntennaId antenna;
    double confidence;
    double signal_dbm;
    std::vector<std::byte> raw;
};

struct StoredEvidence {
    std::uint64_t id;
    SessionId session;
    ScanEvidence scan;
};

struct ProductPage {
    Product product;
    std::vector<Unit> units;
    bool has_more{};
};

struct Assignment {
    Unit unit;
    AisleId aisle;
};

inline constexpr std::size_t page_size = 256;
inline constexpr std::size_t max_raw_evidence = 256;

// An owned connection for one caller thread. The database path is operator configuration.
class Repository {
  public:
    [[nodiscard]] static Result<Repository, Error> open(const std::filesystem::path &file);
    [[nodiscard]] Status insert_catalog(const Catalog &catalog);
    [[nodiscard]] Status update_product(const Product &product);
    [[nodiscard]] Status remove_product(const Dpci &id);
    [[nodiscard]] Result<ProductPage, Error> find(const Dpci &id,
                                                  const std::optional<Epc> &after = {});
    [[nodiscard]] Result<std::vector<ExpectedStock>, Error>
    expected_at(const AisleId &aisle, const std::optional<ExpectedStock> &after = {});
    [[nodiscard]] Result<Assignment, Error> assignment(const Epc &epc);
    [[nodiscard]] Result<std::vector<Assignment>, Error>
    assigned_at(const AisleId &aisle, const std::optional<Epc> &after = {});

    [[nodiscard]] Status start_session(ScanSession session);
    [[nodiscard]] Result<ScanSession, Error> session(SessionId id);
    [[nodiscard]] Status record(SessionId id, std::span<const ScanEvidence> observations);
    [[nodiscard]] Status record_rfid(SessionId id, const sensors::RfidScan &scan,
                                     const AntennaId &antenna);
    [[nodiscard]] Result<std::vector<StoredEvidence>, Error>
    observations(SessionId id, std::uint64_t after_id = 0);
    [[nodiscard]] Result<std::vector<StoredEvidence>, Error> observed(const Epc &epc,
                                                                      std::uint64_t after_id = 0);

  private:
    struct Close {
        void operator()(sqlite3 *db) const noexcept;
    };
    explicit Repository(std::unique_ptr<sqlite3, Close> db) noexcept;
    std::unique_ptr<sqlite3, Close> db_;
};

} // namespace argus::inventory
