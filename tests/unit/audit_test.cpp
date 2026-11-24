#include "argus/audit/mission.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace {

using namespace argus;
using namespace argus::audit;

const Epc tag{"A00000000000000000000001"};
const AisleId west{"WEST"};
const AisleId east{"EAST"};

sensors::RfidScan scan(std::uint64_t tick, std::vector<sensors::RfidRead> reads) {
    return {{sensors::SensorId::rfid, tick, tick * robot::step_ms, tick, {}}, std::move(reads), 0};
}

Footprint west_view(double y, double heading) {
    return {{1.0, y}, robot::Radians{heading}, Meters{3.5}, Meters{0.05}, robot::Radians{0.05}};
}

std::array<Site, 2> aisle_regions() {
    return {{{west,
              {1.0, 0.0},
              {4.0, 4.0},
              {{{{1.0, 1.0}, robot::Radians{0.4}}, {{1.0, 2.0}, robot::Radians{-0.4}}}}},
             {east,
              {6.0, 0.0},
              {10.0, 4.0},
              {{{{6.0, 1.0}, robot::Radians{0.4}}, {{6.0, 2.0}, robot::Radians{-0.4}}}}}}};
}

TEST(RfidAudit, RepeatedReadsCountAsOneTagAndOneView) {
    Evidence evidence;
    const auto first = scan(2, {{tag.value, -60.0}, {tag.value, -55.0}});
    ASSERT_EQ(evidence.ingest(first, west_view(1.5, 0.0), 0), inventory::Status{std::monostate{}});
    ASSERT_EQ(evidence.ingest(scan(4, {{tag.value, -58.0}}), west_view(1.5, 0.0), 0),
              inventory::Status{std::monostate{}});
    EXPECT_EQ(evidence.ingest(first, west_view(1.5, 0.0), 0),
              inventory::Status{inventory::Error::invalid_data});
    EXPECT_EQ(
        evidence.ingest(scan(6, {{tag.value, -56.0}, {"bad-tag", -30.0}}), west_view(1.5, 0.0), 0),
        inventory::Status{inventory::Error::invalid_data});
    ASSERT_EQ(evidence.tags().size(), 1U);
    const auto &reading = evidence.tags().at(tag.value);
    EXPECT_EQ(reading.read_count, 3U);
    EXPECT_EQ(reading.first_ms, 100U);
    EXPECT_EQ(reading.last_ms, 200U);
    ASSERT_EQ(reading.views.size(), 1U);
    EXPECT_DOUBLE_EQ(reading.views.front().peak_dbm, -55.0);
    EXPECT_FALSE(attribute(reading, aisle_regions()).supported);
}

TEST(RfidAudit, CompetingAislesStayAmbiguousAndSecondAngleAddsSupport) {
    auto sites = aisle_regions();
    EXPECT_TRUE(valid_sites(sites));
    Evidence evidence;
    ASSERT_EQ(evidence.ingest(scan(2, {{tag.value, -50.0}}), west_view(1.4, 0.25), 0),
              inventory::Status{std::monostate{}});
    ASSERT_EQ(evidence.ingest(scan(4, {{tag.value, -51.0}}), west_view(2.3, -0.25), 1),
              inventory::Status{std::monostate{}});
    const auto &reading = evidence.tags().at(tag.value);
    const auto strong = attribute(reading, sites);
    ASSERT_TRUE(strong.supported);
    EXPECT_EQ(strong.aisle, west);
    EXPECT_EQ(classify(east, &reading, strong, {}), Finding::likely_misplaced);

    sites[1].low = sites[0].low;
    sites[1].high = sites[0].high;
    const auto ambiguous = attribute(reading, sites);
    EXPECT_FALSE(ambiguous.supported);
    EXPECT_DOUBLE_EQ(ambiguous.score, 0.0);
    EXPECT_EQ(classify(west, &reading, ambiguous, {}), Finding::ambiguous);
}

TEST(RfidAudit, SilenceDistinguishesCoverageWithoutConfirmingLoss) {
    const std::array<Visit, 3> visits{{{west, 0, min_coverage_frames, "scanned"},
                                       {west, 0, min_coverage_frames, "scanned"},
                                       {west, 1, min_coverage_frames, "scanned"}}};
    EXPECT_EQ(classify(east, nullptr, {}, visits), Finding::unscanned);
    EXPECT_EQ(classify(west, nullptr, {}, std::span{visits}.first(2)),
              Finding::insufficient_coverage);
    EXPECT_EQ(classify(west, nullptr, {}, visits), Finding::not_observed_after_two_views);
    EXPECT_EQ(classify(std::nullopt, nullptr, {}, visits), Finding::unknown_epc);
}

TEST(RfidAudit, EscapesSpreadsheetFormulasAndJsonControlCharacters) {
    EXPECT_EQ(csv_field("value,\"quoted\"\nline"), "\"value,\"\"quoted\"\" line\"");
    EXPECT_EQ(csv_field("  =SUM(A1:A2)"), "\"'  =SUM(A1:A2)\"");
    EXPECT_EQ(csv_field("\v@HYPERLINK(1)"), "\"' @HYPERLINK(1)\"");
    EXPECT_EQ(csv_field("+17"), "\"'+17\"");
    EXPECT_EQ(csv_field("-1"), "\"'-1\"");
    Report report{inventory::SessionId{7}, AuditAisle{AisleId{"SAMPLE"}}, {}, {}, {}};
    report.rows.push_back({Epc{"=x,\"\n"}, {}, Finding::unknown_epc, {}, {}});
    std::ostringstream json;
    EXPECT_EQ(write_report(json, report, Export::json), inventory::Status{std::monostate{}});
    EXPECT_NE(json.str().find("=x,\\\"\\n"), std::string::npos);
    std::ostringstream csv;
    EXPECT_EQ(write_report(csv, report, Export::csv), inventory::Status{std::monostate{}});
    EXPECT_NE(csv.str().find("\"'=x,\"\" \""), std::string::npos);
    EXPECT_NE(csv.str().find("\"pending\",\"\",\"\""), std::string::npos);
}

TEST(RfidAudit, RejectsReportsOverExportLimitsWithoutWritingAPrefix) {
    Report report{inventory::SessionId{7}, AuditAisle{AisleId{"SAMPLE"}}, {}, {}, {}};
    report.visits.resize(2 * max_sites + 1);
    std::ostringstream output;
    EXPECT_EQ(write_report(output, report, Export::json),
              inventory::Status{inventory::Error::too_large});
    EXPECT_EQ(write_report(output, report, Export::csv),
              inventory::Status{inventory::Error::too_large});
    EXPECT_TRUE(output.str().empty());
}

} // namespace
