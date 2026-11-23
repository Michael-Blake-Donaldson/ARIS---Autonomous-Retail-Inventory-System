#include "argus/world/geometry.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace {

using namespace argus::world;

TEST(Geometry, DistinguishesContactFromInteriorOverlap) {
    const RectMm box{{0, 0}, {1000, 1000}};
    const RectMm flush{{1000, 400}, {2000, 600}};
    EXPECT_TRUE(overlaps(box, flush));
    EXPECT_FALSE(interiors_overlap(box, flush));
    EXPECT_TRUE(interiors_overlap(box, RectMm{{999, 400}, {2000, 600}}));
    EXPECT_TRUE(contains(box, RectMm{{0, 0}, {500, 1000}}));
}

TEST(Geometry, DiskUsesClosestPointAndTreatsContactAsCollision) {
    const RectMm box{{100, 100}, {200, 200}};
    EXPECT_TRUE(touches_disk(box, {70, 60}, 50));
    EXPECT_FALSE(touches_disk(box, {70, 60}, 49));
    EXPECT_TRUE(touches_disk(box, {150, 150}, 0));
    EXPECT_FALSE(touches_disk(box, {150, 150}, -1));
}

TEST(Geometry, HandlesExtremeQueryCoordinatesWithoutOverflow) {
    constexpr auto low = std::numeric_limits<std::int32_t>::min();
    constexpr auto high = std::numeric_limits<std::int32_t>::max();
    EXPECT_FALSE(touches_disk({{low, low}, {low + 1, low + 1}}, {high, high}, high));
    EXPECT_FALSE(touches_disk({{0, 0}, {0, 0}}, {high, high}, high));
}

} // namespace
