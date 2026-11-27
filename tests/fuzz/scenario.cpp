#include "argus/simulation/warehouse.hpp"

#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *bytes, std::size_t size) {
    static_cast<void>(
        argus::simulation::Warehouse::from_json({reinterpret_cast<const char *>(bytes), size}));
    return 0;
}
