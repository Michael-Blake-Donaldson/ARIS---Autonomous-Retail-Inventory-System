#include "options.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <variant>

namespace {

using argus::app::Action;
using argus::app::Error;
using argus::app::Options;
using argus::app::parse_options;

TEST(Options, UsesDeterministicDefaults) {
    const auto options = std::get<Options>(parse_options({}));
    EXPECT_EQ(options.action, Action::run);
    EXPECT_EQ(options.seed, 5489U);
}

TEST(Options, AcceptsHelpAndVersionAsStandaloneActions) {
    EXPECT_EQ(std::get<Options>(parse_options(std::array{"--help"})).action, Action::help);
    EXPECT_EQ(std::get<Options>(parse_options(std::array{"--version"})).action, Action::version);
}

TEST(Options, AcceptsSeedBoundaries) {
    EXPECT_EQ(std::get<Options>(parse_options(std::array{"--seed", "0"})).seed, 0U);
    EXPECT_EQ(std::get<Options>(parse_options(std::array{"--seed", "4294967295"})).seed,
              std::numeric_limits<std::uint32_t>::max());
}

TEST(Options, RejectsMalformedOrOutOfRangeSeeds) {
    for (const auto *value :
         {"", "-1", "+1", " 1", "1 ", "1a", "1\n", "4294967296", "00000000000", "0x10"}) {
        EXPECT_EQ(std::get<Error>(parse_options(std::array{"--seed", value})), Error::invalid_seed);
    }
}

TEST(Options, RejectsMissingUnknownAndExtraArguments) {
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--seed"})), Error::invalid_arguments);
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--unknown"})), Error::invalid_arguments);
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--version-more"})),
              Error::invalid_arguments);
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--seed-more", "1"})),
              Error::invalid_arguments);
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--version", "--help"})),
              Error::invalid_arguments);
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--seed", "1", "2"})),
              Error::invalid_arguments);
}

} // namespace
