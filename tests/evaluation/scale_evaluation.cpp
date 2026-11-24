#include "argus/mapping/mapper.hpp"
#include "argus/simulation/fixtures.hpp"
#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string_view>

namespace {

using namespace argus;
using namespace argus::simulation;
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::duration<double, std::milli>;
using Json = nlohmann::json;

Warehouse scenario(std::size_t tags) {
    const auto authored = std::get<std::string_view>(fixture_json("rfid-audit"));
    auto scene = Json::parse(authored);
    scene["actors"] = Json::parse(R"([
        {"id":"scale-worker","kind":"worker","startMm":[4500,4700],"endMm":[5300,4700],
         "sizeMm":[400,400],"legTicks":45},
        {"id":"scale-cart","kind":"cart","startMm":[6000,2700],"endMm":[7900,2700],
         "sizeMm":[550,700],"legTicks":70}
    ])");
    auto items = Json::array();
    for (std::size_t i = 0; i < tags; ++i) {
        const auto suffix = std::to_string(i);
        items.push_back({{"epc", std::string(24 - suffix.size(), '0') + suffix},
                         {"rack", i % 2 == 0 ? "soft-rack" : "tech-rack"},
                         {"shelf", 1},
                         {"offsetMm", {100 + i % 800, 100 + (i / 800 + i * 17) % 1800}}});
    }
    scene["items"] = std::move(items);
    auto imported = Warehouse::from_json(scene.dump());
    if (const auto *error = std::get_if<ScenarioError>(&imported)) {
        throw std::runtime_error(std::string(scenario_error_code(*error)));
    }
    return std::get<Warehouse>(std::move(imported));
}

double percentile(const std::vector<double> &samples, std::size_t percent) {
    return samples.at((samples.size() * percent + 99) / 100 - 1);
}

bool run(std::size_t tags, std::uint32_t ticks) {
    const auto load_started = Clock::now();
    const auto warehouse = scenario(tags);
    const double setup_ms = Milliseconds(Clock::now() - load_started).count();
    const auto initial = warehouse.snapshot(0);
    const auto moved = warehouse.snapshot(10).actors;
    const bool actors_moved = moved.size() == 2 && moved != initial.actors;
    const mapping::Spec spec{initial.scenario_id, 12000, 8000, 250};
    auto robot = std::get<RobotSimulation>(RobotSimulation::create(warehouse));
    auto rig = std::get<SensorRig>(SensorRig::create(initial.seed));
    auto mapper = std::get<mapping::Mapper>(mapping::Mapper::create(spec));

    std::vector<double> samples;
    samples.reserve(ticks);
    std::uint64_t reads = 0;
    std::uint64_t truncated = 0;
    std::uint64_t safety_holds = 0;
    std::uint64_t dropout_ticks = 0;
    const auto started = Clock::now();
    const auto cpu_start = std::clock();
    for (std::uint32_t tick = 0; tick < ticks; ++tick) {
        const auto tick_started = Clock::now();
        const bool turning = tick % 100U >= 58U;
        const bool enabled = tick % 137U < 127U;
        robot.step({.target = {MetersPerSecond{turning ? 0.0 : 0.40},
                               robot::RadiansPerSecond{turning ? 1.35 : 0.0}},
                    .deadman = true,
                    .sensors_on = enabled});
        const auto telemetry = robot.telemetry();
        safety_holds += telemetry.collision_blocked || telemetry.world_held ? 1U : 0U;
        dropout_ticks += telemetry.sensors_powered ? 0U : 1U;
        rig.capture(robot);
        while (const auto observation = rig.pop()) {
            if (const auto *scan = std::get_if<sensors::RfidScan>(&*observation)) {
                reads += scan->reads.size();
                truncated += scan->truncated_reads;
            }
            mapper.ingest(*observation);
        }
        samples.push_back(Milliseconds(Clock::now() - tick_started).count());
    }
    const double cpu_ms = 1000.0 * static_cast<double>(std::clock() - cpu_start) / CLOCKS_PER_SEC;
    const auto wall_ms = Milliseconds(Clock::now() - started).count();
    std::ranges::sort(samples);
    const auto observations = rig.throughput();
    const auto estimate = mapper.estimate();
    std::cout << Json{{"tags", tags},
                      {"ticks", ticks},
                      {"seed", initial.seed},
                      {"actors", initial.actors.size()},
                      {"actorsMoved", actors_moved},
                      {"setupMs", setup_ms},
                      {"cpuMs", cpu_ms},
                      {"wallMs", wall_ms},
                      {"tickP50Ms", percentile(samples, 50)},
                      {"tickP95Ms", percentile(samples, 95)},
                      {"tickP99Ms", percentile(samples, 99)},
                      {"tickMaxMs", samples.back()},
                      {"ticksPerSecond", 1000.0 * ticks / wall_ms},
                      {"rfidReads", reads},
                      {"rfidTruncatedReads", truncated},
                      {"rfidQueueDrops",
                       observations.streams.at(sensors::index(sensors::SensorId::rfid)).dropped},
                      {"dropoutTicks", dropout_ticks},
                      {"safetyHolds", safety_holds}}
                     .dump()
              << '\n';
    return actors_moved && reads > 0 && dropout_ticks > 0 && observations.pending == 0 &&
           estimate && std::isfinite(estimate->pose.x.value()) &&
           robot.telemetry().sample_tick == ticks;
}

template <typename Integer> bool decimal(std::string_view text, Integer &value) {
    const auto end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

} // namespace

int main(int argc, char **argv) {
    std::size_t tags = 10000;
    std::uint32_t ticks = 1000;
    if (argc != 3 || !decimal(argv[1], tags) || !decimal(argv[2], ticks) || tags == 0 ||
        tags > scenario_tag_limit || ticks < 150 || ticks > 10000) {
        std::cerr << "Usage: argus-scale-evaluation TAGS(1..10000) TICKS(150..10000)\n";
        return 2;
    }
    try {
        return run(tags, ticks) ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
