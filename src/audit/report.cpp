#include "argus/audit/mission.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <ostream>

namespace argus::audit {
namespace {

using Json = nlohmann::json;

std::string case_id(const Report &report, const Row &row) {
    return std::to_string(report.session.value()) + "-" + row.epc.value;
}

Json footprints(const TagEvidence &tag) {
    auto result = Json::array();
    for (const auto &view : tag.views) {
        const auto &field = view.footprint;
        result.push_back({{"view", view.view},
                          {"xM", field.origin.x_m},
                          {"yM", field.origin.y_m},
                          {"headingRad", field.heading.value()},
                          {"rangeM", field.range.value()},
                          {"positionErrorM", field.position_error.value()},
                          {"headingErrorRad", field.heading_error.value()},
                          {"peakDbm", view.peak_dbm}});
    }
    return result;
}

Json evidence_json(const std::optional<TagEvidence> &tag) {
    if (!tag) {
        return nullptr;
    }
    return {{"rawReads", tag->read_count},
            {"firstMs", tag->first_ms},
            {"lastMs", tag->last_ms},
            {"footprints", footprints(*tag)}};
}

void write_json(std::ostream &out, const Report &report) {
    auto rows = Json::array();
    for (const auto &row : report.rows) {
        rows.push_back(
            {{"epc", row.epc.value},
             {"dpci", row.assignment ? Json(row.assignment->unit.product.value) : Json(nullptr)},
             {"assignedAisle", row.assignment ? Json(row.assignment->aisle.value) : Json(nullptr)},
             {"candidateAisle",
              row.attribution.aisle ? Json(row.attribution.aisle->value) : Json(nullptr)},
             {"score", row.attribution.score},
             {"aisleSupported", row.attribution.supported},
             {"finding", label(row.finding)},
             {"rationale", rationale(row.finding)},
             {"review", row.finding == Finding::supported ? Json(nullptr) : Json("pending")},
             {"caseId", case_id(report, row)},
             {"evidence", evidence_json(row.evidence)}});
    }
    auto visits = Json::array();
    for (const auto &visit : report.visits) {
        visits.push_back({{"aisle", visit.aisle.value},
                          {"pass", visit.pass + 1},
                          {"qualifiedFrames", visit.frames},
                          {"outcome", visit.outcome}});
    }
    auto quantities = Json::array();
    for (const auto &stock : report.quantities) {
        quantities.push_back(
            {{"dpci", stock.product.value},
             {"expectedQuantity", stock.expected},
             {"catalogAssignedEpcs", stock.catalog_assigned},
             {"uniqueObservedInAisle", stock.uniquely_observed_in_aisle},
             {"reviewRequired", stock.uniquely_observed_in_aisle != stock.expected}});
    }
    Json subject;
    if (const auto *request = std::get_if<FindItem>(&report.request)) {
        subject = {{"mode", "find-item"},
                   {"dpci", request->product.value},
                   {"assignedEpcs", report.rows.size()},
                   {"status", report.rows.empty() ? "no_assigned_epcs" : "investigated"},
                   {"lastKnownAisle",
                    request->last_known ? Json(request->last_known->value) : Json(nullptr)}};
    } else {
        subject = {{"mode", "audit"}, {"aisle", std::get<AuditAisle>(report.request).aisle.value}};
    }
    const auto observed_in_scope =
        std::ranges::count_if(report.rows, [](const Row &row) { return row.evidence.has_value(); });
    const Json output{{"session", report.session.value()},
                      {"request", std::move(subject)},
                      {"method", "heuristic_rfid_aisle_attribution"},
                      {"missionRawReads", report.raw_reads},
                      {"missionUniqueObserved", report.unique_observed},
                      {"reportedUniqueObserved", observed_in_scope},
                      {"firstPassResolved", report.first_pass_resolved},
                      {"resolvedAfterSearch", report.resolved},
                      {"simulatedTicks", report.motion_ticks},
                      {"visits", std::move(visits)},
                      {"quantities", std::move(quantities)},
                      {"items", std::move(rows)}};
    out << output.dump(-1, ' ', true) << '\n';
}

void write_csv(std::ostream &out, const Report &report) {
    out << "case_id,epc,dpci,assigned_aisle,candidate_aisle,aisle_supported,score,finding,"
           "rationale,raw_reads,first_ms,last_ms,evidence_json,review_status,review_disposition,"
           "review_notes\n";
    for (const auto &row : report.rows) {
        if (row.finding == Finding::supported) {
            continue;
        }
        const std::array<std::string, 16> fields{
            case_id(report, row),
            row.epc.value,
            row.assignment ? row.assignment->unit.product.value : "",
            row.assignment ? row.assignment->aisle.value : "",
            row.attribution.aisle ? row.attribution.aisle->value : "",
            row.attribution.supported ? "true" : "false",
            std::to_string(row.attribution.score),
            std::string(label(row.finding)),
            std::string(rationale(row.finding)),
            row.evidence ? std::to_string(row.evidence->read_count) : "0",
            row.evidence ? std::to_string(row.evidence->first_ms) : "",
            row.evidence ? std::to_string(row.evidence->last_ms) : "",
            row.evidence ? evidence_json(row.evidence).dump(-1, ' ', true) : "",
            "pending",
            "",
            ""};
        const char *separator = "";
        for (const auto &field : fields) {
            out << separator << csv_field(field);
            separator = ",";
        }
        out << '\n';
    }
}

} // namespace

std::string csv_field(std::string_view value) {
    std::string result{"\""};
    const auto first = std::ranges::find_if(value, [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte > 0x20 && byte != 0x7f;
    });
    if (first != value.end() && std::string_view{"=+-@"}.find(*first) != std::string_view::npos) {
        result.push_back('\'');
    }
    for (char c : value) {
        if (c == '"') {
            result.push_back('"');
        }
        const auto byte = static_cast<unsigned char>(c);
        result.push_back(byte < 0x20 || byte == 0x7f ? ' ' : c);
    }
    result.push_back('"');
    return result;
}

inventory::Status write_report(std::ostream &out, const Report &report, Export format) {
    if (format == Export::json) {
        write_json(out, report);
    } else if (format == Export::csv) {
        write_csv(out, report);
    } else {
        return inventory::Error::invalid_data;
    }
    return out.flush() ? inventory::Status{std::monostate{}}
                       : inventory::Status{inventory::Error::input_failure};
}

} // namespace argus::audit
