#include "argus/inventory/catalog.hpp"

#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *bytes, std::size_t size) {
    using namespace argus::inventory;
    const std::string_view input{reinterpret_cast<const char *>(bytes), size};
    static_cast<void>(parse_catalog(input, Format::csv));
    static_cast<void>(parse_catalog(input, Format::json));
    return 0;
}
