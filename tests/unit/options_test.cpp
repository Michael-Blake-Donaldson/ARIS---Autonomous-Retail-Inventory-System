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

TEST(Options, AcceptsWarehouseFixtureStdinAndExport) {
    using argus::app::WarehouseSource;
    const auto fixture = std::get<Options>(
        parse_options(std::array{"--warehouse", "small-backroom", "--tick", "5"}));
    EXPECT_EQ(fixture.fixture, "small-backroom");
    EXPECT_EQ(fixture.tick, 5U);
    EXPECT_EQ(fixture.warehouse_source, WarehouseSource::fixture);
    const auto input = std::get<Options>(
        parse_options(std::array{"--scenario-stdin", "--tick", "18446744073709551615"}));
    EXPECT_EQ(input.tick, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(input.warehouse_source, WarehouseSource::standard_input);
    const auto output =
        std::get<Options>(parse_options(std::array{"--export-warehouse", "broad-stockroom"}));
    EXPECT_EQ(output.fixture, "broad-stockroom");
    EXPECT_EQ(output.warehouse_source, WarehouseSource::export_fixture);
}

TEST(Options, RejectsInvalidWarehouseFlagsAndTicks) {
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--warehouse", "../small-backroom"})),
              Error::invalid_arguments);
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--warehouse", "C:fixture"})),
              Error::invalid_arguments);
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--warehouse"})), Error::invalid_arguments);
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--scenario-stdin", "--tick", "-1"})),
              Error::invalid_tick);
    EXPECT_EQ(std::get<Error>(
                  parse_options(std::array{"--scenario-stdin", "--tick", "18446744073709551616"})),
              Error::invalid_tick);
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--scenario-stdin", "--tick"})),
              Error::invalid_arguments);
    EXPECT_EQ(std::get<Error>(
                  parse_options(std::array{"--export-warehouse", "small-backroom", "--tick", "2"})),
              Error::invalid_arguments);
}

TEST(Options, BoundsDriveProbeAndRejectsRepeatedOptions) {
    const auto options = std::get<Options>(parse_options(
        std::array{"--warehouse", "small-backroom", "--drive-steps", "2000", "--tick", "9"}));
    EXPECT_EQ(options.drive_steps, 2000U);
    EXPECT_EQ(options.tick, 9U);
    for (const char *value : {"0", "2001", "4294967296", "-1"}) {
        EXPECT_EQ(
            std::get<Error>(parse_options(std::array{"--scenario-stdin", "--drive-steps", value})),
            Error::invalid_drive_steps);
    }
    EXPECT_EQ(std::get<Error>(
                  parse_options(std::array{"--scenario-stdin", "--tick", "0", "--tick", "1"})),
              Error::invalid_arguments);
}

TEST(Options, ChoosesOneBoundedSensorOrDriveProbe) {
    const auto selected = std::get<Options>(parse_options(
        std::array{"--warehouse", "small-backroom", "--sensor-steps", "2000", "--tick", "4"}));
    EXPECT_EQ(selected.sensor_steps, 2000U);
    EXPECT_EQ(selected.drive_steps, 0U);
    EXPECT_EQ(selected.tick, 4U);
    EXPECT_EQ(std::get<Error>(parse_options(std::array{"--scenario-stdin", "--sensor-steps", "0"})),
              Error::invalid_sensor_steps);
    EXPECT_EQ(std::get<Error>(parse_options(
                  std::array{"--scenario-stdin", "--sensor-steps", "5", "--drive-steps", "5"})),
              Error::invalid_arguments);
}

} // namespace
