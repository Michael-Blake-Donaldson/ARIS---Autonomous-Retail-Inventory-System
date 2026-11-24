#include "argus/audit/evidence.hpp"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fuzzer/FuzzedDataProvider.h>

namespace {

// Translate fuzzer bytes into the typed sensor boundary used by the application.
double number(FuzzedDataProvider &input) {
    return std::bit_cast<double>(input.ConsumeIntegral<std::uint64_t>());
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *bytes, std::size_t size) {
    using namespace argus;
    using namespace argus::audit;
    FuzzedDataProvider input{bytes, size};
    Evidence evidence;
    const Footprint valid{
        {1.0, 2.0}, robot::Radians{0.0}, Meters{2.0}, Meters{0.1}, robot::Radians{0.1}};
    const Footprint footprint = input.ConsumeBool() ? Footprint{{number(input), number(input)},
                                                                robot::Radians{number(input)},
                                                                Meters{number(input)},
                                                                Meters{number(input)},
                                                                robot::Radians{number(input)}}
                                                    : valid;
    const auto view = input.ConsumeIntegralInRange<std::size_t>(0, 2 * max_sites);
    sensors::RfidScan scan{{sensors::SensorId::rfid, 1, robot::step_ms, 1, {}}, {}};
    if (input.ConsumeBool()) {
        scan.header.sensor = static_cast<sensors::SensorId>(input.ConsumeIntegral<unsigned char>());
        scan.header.sample_tick = input.ConsumeIntegral<std::uint64_t>();
        scan.header.timestamp_ms = input.ConsumeIntegral<std::uint64_t>();
    }
    const auto count = input.ConsumeIntegralInRange<std::size_t>(0, 260);
    scan.reads.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (input.ConsumeBool()) {
            scan.reads.push_back({input.ConsumeRandomLengthString(25), number(input)});
        } else {
            scan.reads.push_back({"A00000000000000000000001", -60.0});
        }
    }
    const auto result = evidence.ingest(scan, footprint, view);
    if (std::holds_alternative<inventory::Error>(result) &&
        (evidence.read_count() != 0 || !evidence.tags().empty())) {
        std::abort();
    }
    return 0;
}
