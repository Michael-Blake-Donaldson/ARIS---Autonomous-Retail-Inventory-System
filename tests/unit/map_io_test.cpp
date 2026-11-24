#include "argus/mapping/map_io.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>

namespace {

using namespace argus;
using namespace argus::mapping;

constexpr Spec spec{ScenarioId{5}, 4130, 5220, 250};

std::string saved() {
    auto grid = std::get<Grid>(Grid::create(spec));
    grid.observe_hit({0.26, 0.26}, {1.5, 0.26});
    std::ostringstream out;
    EXPECT_TRUE(std::holds_alternative<std::monostate>(save(grid, out)));
    return out.str();
}

MapError rejects(std::string bytes, Spec expected = spec) {
    std::istringstream input(std::move(bytes));
    return std::get<MapError>(load(input, expected));
}

TEST(MapPersistence, RoundTripsEvidenceAndFrameMetadataExactly) {
    const auto original = saved();
    std::istringstream input(original);
    input.exceptions(std::ios::badbit | std::ios::failbit | std::ios::eofbit);
    const auto loaded = load(input, spec);
    ASSERT_TRUE(std::holds_alternative<Grid>(loaded));
    const auto &grid = std::get<Grid>(loaded);
    EXPECT_EQ(grid.spec(), spec);
    EXPECT_EQ(grid.occupancy({6, 1}), Occupancy::occupied);
    EXPECT_EQ(grid.occupancy({1, 1}), Occupancy::free);
    EXPECT_EQ(grid.occupancy({0, 0}), Occupancy::unknown);
    std::ostringstream written;
    EXPECT_TRUE(std::holds_alternative<std::monostate>(save(grid, written)));
    EXPECT_EQ(written.str(), original);
}

TEST(MapPersistence, RejectsIncompatibleVersionFrameScenarioAndResolution) {
    const auto original = saved();
    auto edited = original;
    edited[8] = 2;
    EXPECT_EQ(rejects(edited), MapError::incompatible);
    edited = original;
    edited[12] = 2;
    EXPECT_EQ(rejects(edited), MapError::incompatible);
    auto mismatch = spec;
    mismatch.scenario = ScenarioId{6};
    EXPECT_EQ(rejects(original, mismatch), MapError::incompatible);
    mismatch = spec;
    mismatch.cell_mm = 500;
    EXPECT_EQ(rejects(original, mismatch), MapError::incompatible);
}

TEST(MapPersistence, RejectsTruncationCorruptionTrailingBytesAndBadStreams) {
    const auto original = saved();
    EXPECT_EQ(rejects(original.substr(0, 9)), MapError::malformed);
    EXPECT_EQ(rejects(original.substr(0, original.size() - 1)), MapError::malformed);
    EXPECT_EQ(rejects(original + std::string(2000000, 'x')), MapError::malformed);
    auto changed = original;
    changed[37] = 0;
    EXPECT_EQ(rejects(changed), MapError::malformed);
    std::istringstream input(original);
    input.setstate(std::ios::badbit);
    EXPECT_EQ(std::get<MapError>(load(input, spec)), MapError::input_failure);
    std::ostringstream out;
    out.setstate(std::ios::badbit);
    const auto grid = std::get<Grid>(Grid::create(spec));
    EXPECT_EQ(std::get<MapError>(save(grid, out)), MapError::output_failure);
}

} // namespace
