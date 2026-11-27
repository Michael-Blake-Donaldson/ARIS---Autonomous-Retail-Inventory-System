#include "argus/mapping/map_io.hpp"

#include <cstdint>
#include <sstream>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *bytes, std::size_t size) {
    using namespace argus;
    using namespace argus::mapping;
    constexpr Spec spec{ScenarioId{201}, 14000, 10500, 250};
    if (size <= 1000069) {
        std::istringstream input{std::string{reinterpret_cast<const char *>(bytes), size}};
        static_cast<void>(load(input, spec));
    }
    return 0;
}
