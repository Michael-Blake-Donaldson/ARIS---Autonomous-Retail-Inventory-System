#pragma once

#include "argus/audit/evidence.hpp"
#include "argus/inventory/repository.hpp"

namespace argus::audit {

struct FindItem {
    inventory::Dpci product;
    std::optional<AisleId> last_known;
};

struct AuditAisle {
    AisleId aisle;
};

using Request = std::variant<FindItem, AuditAisle>;
enum class Export { json, csv };

struct Row {
    Epc epc;
    std::optional<inventory::Assignment> assignment;
    Finding finding;
    Attribution attribution;
    std::optional<TagEvidence> evidence;
};

struct Quantity {
    inventory::Dpci product;
    std::uint64_t expected{};
    std::size_t catalog_assigned{};
    std::size_t uniquely_observed_in_aisle{};
};

struct Report {
    inventory::SessionId session;
    Request request;
    std::vector<Visit> visits;
    std::vector<Row> rows;
    std::vector<Quantity> quantities;
    std::size_t raw_reads{};
    std::size_t unique_observed{};
    std::size_t first_pass_resolved{};
    std::size_t resolved{};
    std::uint32_t motion_ticks{};
};

// Uses a seeded simulation and an independently authored set of operator viewpoints.
[[nodiscard]] std::span<const Site> demo_sites() noexcept;
[[nodiscard]] Result<Report, inventory::Error> run_demo(inventory::Repository &repository,
                                                        const Request &request,
                                                        inventory::SessionId session,
                                                        bool enable_second_pass = true);

[[nodiscard]] std::string csv_field(std::string_view value);
[[nodiscard]] inventory::Status write_report(std::ostream &out, const Report &report,
                                             Export format);

} // namespace argus::audit
