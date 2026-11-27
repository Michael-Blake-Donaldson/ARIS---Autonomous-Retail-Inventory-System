#include "warehouse_io.hpp"

#include "argus/simulation/fixtures.hpp"

#include <cstdio>
#include <iostream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace argus::app {
namespace {

using simulation::ScenarioError;
using simulation::Warehouse;
} // namespace

bool use_binary_mode(std::FILE *stream) noexcept {
#ifdef _WIN32
    const auto descriptor = _fileno(stream);
    return descriptor >= 0 && _setmode(descriptor, _O_BINARY) != -1;
#else
    static_cast<void>(stream);
    return true;
#endif
}

Result<Warehouse, ScenarioError> open_warehouse(const Options &options) {
    if (options.warehouse_source == WarehouseSource::standard_input) {
        if (!use_binary_mode(stdin)) {
            return ScenarioError::input_failure;
        }
        return Warehouse::from_stream(std::cin);
    }
    const auto fixture = simulation::fixture_json(
        options.warehouse_source == WarehouseSource::none ? "small-backroom" : options.fixture);
    if (const auto *error = std::get_if<ScenarioError>(&fixture)) {
        return *error;
    }
    return Warehouse::from_json(std::get<std::string_view>(fixture));
}

Status export_warehouse(std::string_view name) {
    const auto fixture = simulation::fixture_json(name);
    if (const auto *error = std::get_if<ScenarioError>(&fixture)) {
        return *error;
    }
    const auto bytes = std::get<std::string_view>(fixture);
    const auto validated = Warehouse::from_json(bytes);
    if (const auto *error = std::get_if<ScenarioError>(&validated)) {
        return *error;
    }
    if (!use_binary_mode(stdout) || !(std::cout << bytes).flush()) {
        return Error::output_failure;
    }
    return std::monostate{};
}

} // namespace argus::app
