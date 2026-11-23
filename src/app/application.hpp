#pragma once

#include "options.hpp"

#include <string_view>

namespace argus::app {

using Status = Result<std::monostate, Error>;
using Run = Status (*)(const Options &);

int run_application(int argc, char **argv, std::string_view name, Run run) noexcept;

} // namespace argus::app
