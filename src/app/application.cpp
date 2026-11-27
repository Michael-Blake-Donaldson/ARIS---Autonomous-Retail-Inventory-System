#include "application.hpp"

#include "map_run.hpp"
#include "version.hpp"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <variant>

namespace argus::app {
namespace {

constexpr int usage_error = 2;

std::string_view error_code(Error error) noexcept {
    switch (error) {
    case Error::invalid_arguments:
        return "invalid_arguments";
    case Error::invalid_seed:
        return "invalid_seed";
    case Error::invalid_tick:
        return "invalid_tick";
    case Error::invalid_drive_steps:
        return "invalid_drive_steps";
    case Error::invalid_sensor_steps:
        return "invalid_sensor_steps";
    case Error::invalid_map_steps:
        return "invalid_map_steps";
    case Error::invalid_explore_steps:
        return "invalid_explore_steps";
    case Error::invalid_dock:
        return "invalid_dock";
    case Error::invalid_cell_size:
        return "invalid_cell_size";
    case Error::video_initialization:
        return "video_initialization";
    case Error::window_creation:
        return "window_creation";
    case Error::graphics_context:
        return "graphics_context";
    case Error::graphics_present:
        return "graphics_present";
    case Error::window_events:
        return "window_events";
    case Error::output_failure:
        return "output_failure";
    }
    return "unknown_error";
}

std::string_view error_code(simulation::ScenarioError error) noexcept {
    return simulation::scenario_error_code(error);
}

std::string_view error_code(simulation::RobotError error) noexcept {
    switch (error) {
    case simulation::RobotError::invalid_configuration:
        return "invalid_robot_configuration";
    case simulation::RobotError::invalid_spawn:
        return "invalid_robot_spawn";
    }
    return "unknown_robot_error";
}

std::string_view error_code(simulation::SensorError error) noexcept {
    switch (error) {
    case simulation::SensorError::invalid_configuration:
        return "invalid_sensor_configuration";
    }
    return "unknown_sensor_error";
}

std::string_view error_code(mapping::MapError error) noexcept {
    switch (error) {
    case mapping::MapError::invalid_spec:
        return "invalid_map_spec";
    case mapping::MapError::malformed:
        return "malformed_map";
    case mapping::MapError::incompatible:
        return "incompatible_map";
    case mapping::MapError::input_failure:
        return "map_input_failure";
    case mapping::MapError::output_failure:
        return "map_output_failure";
    }
    return "unknown_map_error";
}

} // namespace

int run_application(int argc, char **argv, std::string_view name, Run run) noexcept {
    try {
        spdlog::logger log{"argus", std::make_shared<spdlog::sinks::stderr_sink_st>()};
        log.set_pattern("%v");

        const auto arguments = std::span<const char *const>{argv, static_cast<std::size_t>(argc)};
        const auto parsed = parse_options(arguments.empty() ? arguments : arguments.subspan(1));
        if (const auto *error = std::get_if<Error>(&parsed)) {
            log.error(R"({{"event":"configuration_rejected","app":"{}","code":"{}"}})", name,
                      error_code(*error));
            return usage_error;
        }
        const auto &options = std::get<Options>(parsed);
        switch (options.action) {
        case Action::help:
            std::cout << "Usage: " << name
                      << " [--help | --version | --seed UINT32 | --warehouse NAME [--tick UINT64]"
                         " [--drive-steps N | --sensor-steps N] | --scenario-stdin [--tick UINT64]"
                         " [--drive-steps N | --sensor-steps N]"
                         " | --export-warehouse NAME]\n"
                         "Mapping: --warehouse NAME [--tick UINT64] [--cell-mm N]"
                         " (--map-steps N | --export-map N | --map-stdin)\n"
                         "  --export-map N [--map-output DIRECTORY NAME.argmap]\n"
                         "  --map-stdin [--map-sha256 HEX]\n"
                         "Exploration: --warehouse NAME [--tick UINT64] [--cell-mm N]"
                         " --explore-steps N [--dock-mm X Y]\n";
            break;
        case Action::version:
            std::cout << "ARGUS " << version << '\n';
            break;
        case Action::run: {
            if (options.warehouse_source == WarehouseSource::none) {
                log.info(
                    R"({{"event":"application_starting","app":"{}","version":"{}","seed":{}}})",
                    name, version, options.seed);
            } else {
                log.info(R"({{"event":"application_starting","app":"{}","version":"{}"}})", name,
                         version);
            }
            const auto status = options.map_mode == MapMode::none ? run(options) : run_map(options);
            if (const auto *error = std::get_if<Failure>(&status)) {
                const auto code = std::visit([](auto value) { return error_code(value); }, *error);
                log.error(R"({{"event":"application_failed","app":"{}","code":"{}"}})", name, code);
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        }
        }
        if (!std::cout.flush()) {
            log.error(R"({{"event":"application_failed","app":"{}","code":"{}"}})", name,
                      error_code(Error::output_failure));
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } catch (const std::exception &) {
        std::fputs(R"({"event":"application_failed","code":"unexpected_error"})"
                   "\n",
                   stderr);
        return EXIT_FAILURE;
    }
}

} // namespace argus::app
