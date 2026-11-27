#include "fleet_report.hpp"

#include "argus/mission/store.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>

namespace argus::desktop {
namespace {

using Json = nlohmann::json;
constexpr auto max_products = audit::max_tags;
constexpr auto max_total_epcs = 2 * audit::max_tags;
constexpr std::uint64_t expected_limit =
    static_cast<std::uint64_t>(inventory::max_expected_quantity) * audit::max_tags;

} // namespace

bool ShelfReport::needs_review() const noexcept {
    return !unidentified.empty() || !unresolved.empty() ||
           std::ranges::any_of(products, &ShelfProduct::discrepancy);
}

std::vector<ShelfComparisonRow> compare_shelf_reports(const ShelfReport &source,
                                                      const ShelfReport &current) {
    const auto &before = source.products;
    const auto &after = current.products;
    std::vector<ShelfComparisonRow> rows;
    rows.reserve(before.size() + after.size());
    auto left = before.begin();
    auto right = after.begin();
    while (left != before.end() || right != after.end()) {
        if (right == after.end() ||
            (left != before.end() && left->product.id.value < right->product.id.value)) {
            rows.push_back({&*left++, nullptr});
        } else if (left == before.end() || right->product.id.value < left->product.id.value) {
            rows.push_back({nullptr, &*right++});
        } else {
            rows.push_back({&*left++, &*right++});
        }
    }
    return rows;
}

std::optional<ShelfReport> make_shelf_report(const audit::Report &report,
                                             std::span<const inventory::Product> products) {
    const auto *scope = std::get_if<audit::AuditAisle>(&report.request);
    if (!scope || report.quantities.size() > max_products ||
        report.quantities.size() != products.size() || report.rows.size() > max_total_epcs) {
        return std::nullopt;
    }
    ShelfReport shelf;
    std::map<std::string_view, std::size_t, std::less<>> index;
    for (std::size_t i = 0; i < products.size(); ++i) {
        const auto &quantity = report.quantities[i];
        if (products[i].id != quantity.product || !inventory::valid(products[i]) ||
            !index.emplace(products[i].id.value, i).second) {
            return std::nullopt;
        }
        shelf.products.push_back(
            {products[i], quantity.expected, quantity.uniquely_observed_in_aisle, {}});
    }
    for (const auto &row : report.rows) {
        const bool observed = row.attribution.supported && row.attribution.aisle == scope->aisle;
        if (!row.assignment) {
            if (observed || (row.evidence && !row.attribution.supported)) {
                shelf.unidentified.push_back(row.epc);
            }
        } else if (observed || row.assignment->aisle == scope->aisle) {
            const auto product = index.find(row.assignment->unit.product.value);
            if (product == index.end()) {
                return std::nullopt;
            }
            shelf.products[product->second].tags.push_back({row.epc, observed});
        }
    }
    std::ranges::sort(shelf.products, {},
                      [](const auto &row) -> const std::string & { return row.product.id.value; });
    return shelf;
}

std::string encode_shelf_report(const ShelfReport &report) {
    Json products = Json::array();
    for (const auto &row : report.products) {
        Json tags = Json::array();
        for (const auto &tag : row.tags) {
            tags.push_back({tag.epc.value, tag.observed_here});
        }
        products.push_back({row.product.id.value, row.product.name,
                            static_cast<unsigned>(row.product.category), row.expected, row.observed,
                            std::move(tags)});
    }
    Json unidentified = Json::array();
    for (const auto &epc : report.unidentified) {
        unidentified.push_back(epc.value);
    }
    Json unresolved = Json::array();
    for (const auto &epc : report.unresolved) {
        unresolved.push_back(epc.value);
    }
    return Json{{"version", 2},
                {"products", std::move(products)},
                {"unidentified", std::move(unidentified)},
                {"unresolved", std::move(unresolved)}}
        .dump();
}

std::optional<ShelfReport> decode_shelf_report(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > mission::shelf_report_limit ||
        bytes.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }
    try {
        std::size_t events = 0;
        const auto root = Json::parse(bytes, [&](int depth, Json::parse_event_t, Json &) {
            if (depth > 8 || ++events > 20000) {
                throw inventory::Error::too_large;
            }
            return true;
        });
        if (!root.is_object()) {
            return std::nullopt;
        }
        const auto &version = root.at("version");
        if (!((version == 1 && root.size() == 3) || (version == 2 && root.size() == 4))) {
            return std::nullopt;
        }
        const auto &products = root.at("products");
        const auto &unknown = root.at("unidentified");
        if (!products.is_array() || products.size() > max_products || !unknown.is_array() ||
            unknown.size() > max_total_epcs) {
            return std::nullopt;
        }
        ShelfReport report;
        std::set<std::string, std::less<>> epcs;
        std::set<std::string, std::less<>> confirmed;
        for (const auto &entry : products) {
            if (!entry.is_array() || entry.size() != 6 || !entry[2].is_number_unsigned() ||
                entry[2] > static_cast<unsigned>(inventory::Category::home_goods) ||
                !entry[3].is_number_unsigned() || entry[3] > expected_limit ||
                !entry[4].is_number_unsigned() || entry[4] > audit::max_tags ||
                !entry[5].is_array() || entry[5].size() > max_total_epcs - epcs.size()) {
                return std::nullopt;
            }
            inventory::Product product{{entry[0].get<std::string>()},
                                       entry[1].get<std::string>(),
                                       static_cast<inventory::Category>(entry[2].get<unsigned>())};
            if (!inventory::valid(product) ||
                (!report.products.empty() &&
                 product.id.value <= report.products.back().product.id.value)) {
                return std::nullopt;
            }
            ShelfProduct row{
                std::move(product), entry[3].get<std::uint64_t>(), entry[4].get<std::size_t>(), {}};
            for (const auto &tag : entry[5]) {
                if (!tag.is_array() || tag.size() != 2 || !tag[1].is_boolean()) {
                    return std::nullopt;
                }
                inventory::Epc epc{tag[0].get<std::string>()};
                if (!inventory::valid(epc) || !epcs.insert(epc.value).second) {
                    return std::nullopt;
                }
                if (tag[1].get<bool>()) {
                    confirmed.insert(epc.value);
                }
                row.tags.push_back({std::move(epc), tag[1].get<bool>()});
            }
            if (row.observed != static_cast<std::size_t>(
                                    std::ranges::count_if(row.tags, &ShelfTag::observed_here))) {
                return std::nullopt;
            }
            report.products.push_back(std::move(row));
        }
        if (unknown.size() > max_total_epcs - epcs.size()) {
            return std::nullopt;
        }
        for (const auto &item : unknown) {
            inventory::Epc epc{item.get<std::string>()};
            if (!inventory::valid(epc) || !epcs.insert(epc.value).second) {
                return std::nullopt;
            }
            confirmed.insert(epc.value);
            report.unidentified.push_back(std::move(epc));
        }
        if (version == 2) {
            const auto &pending = root.at("unresolved");
            if (!pending.is_array() || pending.size() > max_total_epcs) {
                return std::nullopt;
            }
            std::set<std::string, std::less<>> unique;
            for (const auto &item : pending) {
                inventory::Epc epc{item.get<std::string>()};
                if (!inventory::valid(epc) || confirmed.contains(epc.value) ||
                    !unique.insert(epc.value).second) {
                    return std::nullopt;
                }
                report.unresolved.push_back(std::move(epc));
            }
        }
        return report;
    } catch (const Json::exception &) {
        return std::nullopt;
    } catch (inventory::Error) {
        return std::nullopt;
    }
}

} // namespace argus::desktop
