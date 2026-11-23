#include "application.hpp"

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
            std::cout << "Usage: " << name << " [--help | --version | --seed UINT32]\n";
            break;
        case Action::version:
            std::cout << "ARGUS " << version << '\n';
            break;
        case Action::run: {
            log.info(R"({{"event":"application_starting","app":"{}","version":"{}","seed":{}}})",
                     name, version, options.seed);
            const auto status = run(options);
            if (const auto *error = std::get_if<Error>(&status)) {
                log.error(R"({{"event":"application_failed","app":"{}","code":"{}"}})", name,
                          error_code(*error));
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
