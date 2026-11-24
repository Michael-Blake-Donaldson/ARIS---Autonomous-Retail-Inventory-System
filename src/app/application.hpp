#pragma once

#include "options.hpp"

#include "argus/simulation/robot.hpp"

#include <string_view>

namespace argus::app {

using Failure = std::variant<Error, simulation::ScenarioError, simulation::RobotError>;
using Status = Result<std::monostate, Failure>;
using Run = Status (*)(const Options &);

int run_application(int argc, char **argv, std::string_view name, Run run) noexcept;

} // namespace argus::app
