#include "argus/core/random.hpp"
#include "argus/core/types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <type_traits>

namespace {

static_assert(!std::is_convertible_v<std::uint64_t, argus::ScenarioId>);
static_assert(!std::is_constructible_v<argus::RobotId, argus::ScenarioId>);
static_assert(!std::is_convertible_v<double, argus::Meters>);
static_assert(!std::is_constructible_v<argus::Seconds, argus::Meters>);
static_assert(!std::is_constructible_v<argus::MetersPerSecond, argus::Meters>);
static_assert(argus::ScenarioId{42}.value() == 42);
static_assert(argus::Meters{1.25}.value() == 1.25);
static_assert(argus::Seconds{0.5}.value() == 0.5);
static_assert(argus::MetersPerSecond{2.0}.value() == 2.0);

TEST(Random, MatchesMt19937ReferenceSequence) {
    constexpr std::array<std::uint32_t, 5> expected = {3499211612U, 581869302U, 3890346734U,
                                                       3586334585U, 545404204U};
    argus::Random random{5489U};
    for (const auto sample : expected) {
        EXPECT_EQ(random.next(), sample);
    }
}

TEST(Random, OwnsAnIndependentSequencePerInstance) {
    argus::Random first{17U};
    argus::Random second{17U};
    argus::Random different{18U};

    const auto first_sample = first.next();
    const auto second_sample = first.next();
    EXPECT_NE(first_sample, different.next());
    EXPECT_EQ(second.next(), first_sample);
    EXPECT_EQ(second.next(), second_sample);
}

} // namespace
