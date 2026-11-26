#pragma once

#include "argus/audit/mission.hpp"

namespace argus::desktop {

struct ShelfTag {
    inventory::Epc epc;
    bool observed_here;
};

struct ShelfProduct {
    inventory::Product product;
    std::uint64_t expected;
    std::size_t observed;
    std::vector<ShelfTag> tags;
    [[nodiscard]] bool discrepancy() const noexcept { return expected != observed; }
};

struct ShelfReport {
    std::vector<ShelfProduct> products;
    std::vector<inventory::Epc> unidentified;
    std::vector<inventory::Epc> unresolved;
    [[nodiscard]] bool needs_review() const noexcept;
};

[[nodiscard]] std::optional<ShelfReport>
make_shelf_report(const audit::Report &audit, std::span<const inventory::Product> products);
[[nodiscard]] std::string encode_shelf_report(const ShelfReport &report);
[[nodiscard]] std::optional<ShelfReport> decode_shelf_report(std::string_view bytes);

} // namespace argus::desktop
