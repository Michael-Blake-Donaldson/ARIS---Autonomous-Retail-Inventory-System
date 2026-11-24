#include "argus/audit/evidence.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>

namespace argus::audit {
namespace {

constexpr double pi = std::numbers::pi;
constexpr double full_turn = 2.0 * pi;

bool valid_point(Point point) noexcept {
    return std::abs(point.x_m) <= 1000.0 && std::abs(point.y_m) <= 1000.0;
}

bool valid_footprint(const Footprint &field) noexcept {
    return valid_point(field.origin) && std::abs(field.heading.value()) <= pi &&
           field.range.value() > 0 && field.range.value() <= 12.0 &&
           field.position_error.value() >= 0 && field.position_error.value() <= 1000000.0 &&
           field.heading_error.value() >= 0 && field.heading_error.value() <= 1000000.0;
}

bool contains(const Site &site, Point point) noexcept {
    return point.x_m >= site.low.x_m && point.x_m <= site.high.x_m && point.y_m >= site.low.y_m &&
           point.y_m <= site.high.y_m;
}

double support(const Site &site, const ViewEvidence &view) {
    const auto &field = view.footprint;
    const double spread = 0.75 + std::min(0.50, field.heading_error.value() * 0.12);
    const double quality =
        1.0 / (1.0 + field.position_error.value() * 0.35 + field.heading_error.value() * 0.045);
    const double signal = std::clamp((view.peak_dbm + 95.0) / 40.0, 0.15, 1.0);
    double covered = 0;
    double total = 0;
    for (int ray = -4; ray <= 4; ++ray) {
        const double angle = field.heading.value() + spread * ray / 4.0;
        const double weight = 1.0 - std::abs(static_cast<double>(ray)) / 8.0;
        for (int step = 1; step <= 8; ++step) {
            const double distance = field.range.value() * step / 8.0;
            const Point sample{field.origin.x_m + distance * std::cos(angle),
                               field.origin.y_m + distance * std::sin(angle)};
            total += weight;
            if (contains(site, sample)) {
                covered += weight;
            }
        }
    }
    return covered / total * quality * signal;
}

bool independent(const Footprint &a, const Footprint &b) noexcept {
    return std::hypot(a.origin.x_m - b.origin.x_m, a.origin.y_m - b.origin.y_m) >= 0.40 ||
           std::abs(std::remainder(a.heading.value() - b.heading.value(), full_turn)) >= 0.45;
}

} // namespace

bool valid_sites(std::span<const Site> sites) {
    if (sites.empty() || sites.size() > max_sites) {
        return false;
    }
    std::set<std::string_view> aisles;
    for (const auto &site : sites) {
        if (!inventory::valid_code(site.aisle.value) || !aisles.insert(site.aisle.value).second ||
            !valid_point(site.low) || !valid_point(site.high) ||
            !(site.low.x_m < site.high.x_m && site.low.y_m < site.high.y_m)) {
            return false;
        }
        for (const auto &view : site.views) {
            if (!valid_point(view.point) || !(std::abs(view.heading.value()) <= pi)) {
                return false;
            }
        }
        if (std::abs(std::remainder(site.views[0].heading.value() - site.views[1].heading.value(),
                                    full_turn)) < 0.45) {
            return false;
        }
    }
    return true;
}

inventory::Status Evidence::ingest(const sensors::RfidScan &scan, Footprint footprint,
                                   std::size_t view) {
    using inventory::Error;
    if (scan.reads.size() > 256 || scan.reads.size() > max_reads - read_count_) {
        return Error::too_large;
    }
    const auto &header = scan.header;
    if (view >= max_sites * 2 || !valid_footprint(footprint) ||
        header.sensor != sensors::SensorId::rfid ||
        header.provenance != sensors::Provenance::simulated || header.sample_tick <= last_tick_ ||
        header.sample_tick > std::numeric_limits<std::uint64_t>::max() / robot::step_ms ||
        header.timestamp_ms != header.sample_tick * robot::step_ms) {
        return Error::invalid_data;
    }
    std::set<std::string_view> incoming;
    for (const auto &read : scan.reads) {
        if (read.epc.size() != 24 || !inventory::valid(Epc{read.epc}) ||
            !(read.signal_dbm >= -160.0 && read.signal_dbm <= 20.0)) {
            return Error::invalid_data;
        }
        if (!tags_.contains(read.epc)) {
            incoming.insert(read.epc);
        }
    }
    if (incoming.size() > max_tags - tags_.size()) {
        return Error::too_large;
    }

    for (const auto &read : scan.reads) {
        auto &tag =
            tags_
                .try_emplace(
                    read.epc,
                    TagEvidence{Epc{read.epc}, header.timestamp_ms, header.timestamp_ms, 0, {}})
                .first->second;
        tag.last_ms = header.timestamp_ms;
        ++tag.read_count;
        auto selected = std::ranges::find(tag.views, view, &ViewEvidence::view);
        if (selected == tag.views.end()) {
            tag.views.push_back({view, footprint, read.signal_dbm});
        } else if (read.signal_dbm > selected->peak_dbm) {
            *selected = {view, footprint, read.signal_dbm};
        }
    }
    read_count_ += scan.reads.size();
    last_tick_ = header.sample_tick;
    return std::monostate{};
}

Attribution attribute(const TagEvidence &tag, std::span<const Site> sites) {
    if (sites.empty() || sites.size() > max_sites || tag.views.size() > max_sites * 2) {
        return {};
    }
    Attribution result;
    double best = 0;
    double second = 0;
    bool diverse = false;
    for (const auto &site : sites) {
        double sum = 0;
        bool distinct = false;
        const Footprint *first = nullptr;
        for (const auto &view : tag.views) {
            const double weight = support(site, view);
            sum += weight;
            if (weight >= 0.08) {
                if (!first) {
                    first = &view.footprint;
                } else if (independent(*first, view.footprint)) {
                    distinct = true;
                }
            }
        }
        if (sum > best) {
            second = best;
            best = sum;
            result.aisle = site.aisle;
            diverse = distinct;
        } else {
            second = std::max(second, sum);
        }
    }
    if (best > 0) {
        result.score = (best - second) / (best + second) * std::min(1.0, best / 0.35);
        result.supported = diverse && result.score >= 0.58;
    }
    return result;
}

Finding classify(const std::optional<AisleId> &assigned, const TagEvidence *evidence,
                 const Attribution &attribution, std::span<const Visit> visits) {
    if (!assigned) {
        return Finding::unknown_epc;
    }
    if (evidence) {
        if (!attribution.supported || !attribution.aisle) {
            return Finding::ambiguous;
        }
        return *attribution.aisle == *assigned ? Finding::supported : Finding::likely_misplaced;
    }
    std::uint32_t coverage = 0;
    for (const auto &visit : visits) {
        if (visit.aisle == *assigned && visit.frames >= min_coverage_frames && visit.pass < 2) {
            coverage |= 1U << visit.pass;
        }
    }
    if (coverage == 0) {
        return Finding::unscanned;
    }
    return coverage == 3 ? Finding::not_observed_after_two_views : Finding::insufficient_coverage;
}

std::string_view label(Finding finding) noexcept {
    switch (finding) {
    case Finding::supported:
        return "observed_in_assigned_aisle";
    case Finding::likely_misplaced:
        return "likely_misplaced";
    case Finding::ambiguous:
        return "ambiguous_aisle";
    case Finding::unscanned:
        return "unscanned";
    case Finding::insufficient_coverage:
        return "insufficient_coverage";
    case Finding::not_observed_after_two_views:
        return "not_observed_after_two_views";
    case Finding::unknown_epc:
        return "unknown_epc";
    }
    return "unknown";
}

std::string_view rationale(Finding finding) noexcept {
    switch (finding) {
    case Finding::supported:
        return "Independent views favor the assigned aisle";
    case Finding::likely_misplaced:
        return "Independent views favor another aisle";
    case Finding::ambiguous:
        return "Read evidence lacks distinct views or aisle separation";
    case Finding::unscanned:
        return "No qualified scan view reached the assigned aisle";
    case Finding::insufficient_coverage:
        return "One qualified view produced no matching read";
    case Finding::not_observed_after_two_views:
        return "Two qualified views produced no matching read";
    case Finding::unknown_epc:
        return "Observed EPC has no catalog assignment";
    }
    return {};
}

} // namespace argus::audit
