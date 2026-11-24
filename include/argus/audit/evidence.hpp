#pragma once

#include "argus/inventory/catalog.hpp"
#include "argus/navigation/navigator.hpp"

#include <array>
#include <map>

namespace argus::audit {

using inventory::AisleId;
using inventory::Epc;
using navigation::Point;

struct Site {
    AisleId aisle;
    Point low;
    Point high;
    std::array<navigation::Destination, 2> views;
};

struct Footprint {
    Point origin;
    robot::Radians heading;
    Meters range;
    Meters position_error;
    robot::Radians heading_error;
};

struct ViewEvidence {
    std::size_t view;
    Footprint footprint;
    double peak_dbm;
};

struct TagEvidence {
    Epc epc;
    std::uint64_t first_ms;
    std::uint64_t last_ms;
    std::size_t read_count{};
    std::vector<ViewEvidence> views;
};

struct Attribution {
    std::optional<AisleId> aisle;
    double score{};
    bool supported{};
};

enum class Finding {
    supported,
    likely_misplaced,
    ambiguous,
    unscanned,
    insufficient_coverage,
    not_observed_after_two_views,
    unknown_epc
};

inline constexpr std::size_t max_sites = 8;
inline constexpr std::size_t max_tags = 1024;
inline constexpr std::size_t max_reads = 16384;
inline constexpr std::size_t min_coverage_frames = 8;

struct Visit {
    AisleId aisle;
    std::size_t pass;
    std::size_t frames{};
    std::string_view outcome{"not_attempted"};
};

[[nodiscard]] bool valid_sites(std::span<const Site> sites);
[[nodiscard]] std::string_view label(Finding finding) noexcept;
[[nodiscard]] std::string_view rationale(Finding finding) noexcept;

class Evidence {
  public:
    [[nodiscard]] inventory::Status ingest(const sensors::RfidScan &scan, Footprint footprint,
                                           std::size_t view);
    [[nodiscard]] const std::map<std::string, TagEvidence, std::less<>> &tags() const noexcept {
        return tags_;
    }
    [[nodiscard]] std::size_t read_count() const noexcept { return read_count_; }

  private:
    std::map<std::string, TagEvidence, std::less<>> tags_;
    std::size_t read_count_{};
    std::uint64_t last_tick_{};
};

[[nodiscard]] Attribution attribute(const TagEvidence &tag, std::span<const Site> sites);
[[nodiscard]] Finding classify(const std::optional<AisleId> &assigned, const TagEvidence *evidence,
                               const Attribution &attribution, std::span<const Visit> visits);

} // namespace argus::audit
