#include "occupancy_mesh.hpp"

#include "argus/core/types.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string_view>

namespace {

using namespace argus;
using namespace argus::desktop;
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::duration<double, std::milli>;

std::uint64_t checksum(std::span<const Vertex> mesh) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint32_t value) { hash = (hash ^ value) * 1099511628211ULL; };
    for (const auto &vertex : mesh) {
        mix(std::bit_cast<std::uint32_t>(vertex.x));
        mix(std::bit_cast<std::uint32_t>(vertex.y));
        mix(std::bit_cast<std::uint32_t>(vertex.z));
        for (auto component : vertex.color) {
            mix(component);
        }
    }
    return hash;
}

int run(std::size_t frames) {
    constexpr mapping::Spec spec{ScenarioId{42}, 100000, 100000, 100};
    auto first = std::get<mapping::Grid>(mapping::Grid::create(spec));
    auto second = first;
    std::vector<std::int8_t> evidence(first.evidence().size());
    for (std::uint32_t y = 0; y < first.rows(); ++y) {
        for (std::uint32_t x = 0; x < first.columns(); ++x) {
            const auto sample = (x / 8 + y / 8) % 5;
            evidence.at(static_cast<std::size_t>(y) * first.columns() + x) = sample == 0   ? 0
                                                                             : sample == 1 ? 6
                                                                                           : -4;
        }
    }
    if (!first.restore(evidence)) {
        return 1;
    }
    evidence.front() = 6;
    if (!second.restore(evidence)) {
        return 1;
    }

    OccupancyMesh mesh;
    std::vector<double> samples;
    samples.reserve(frames);
    std::uint64_t submitted = 0;
    for (std::size_t index = 0; index < frames; ++index) {
        const auto revision = index / 8;
        const auto &grid = revision % 2 == 0 ? first : second;
        const auto start = Clock::now();
        mesh.rebuild(grid);
        samples.push_back(Milliseconds(Clock::now() - start).count());
        submitted += mesh.vertices().size();
    }
    double total = 0;
    for (const auto elapsed : samples) {
        total += elapsed;
    }
    std::ranges::sort(samples);
    const auto percentile = [&](std::size_t rank) {
        return samples.at((samples.size() * rank + 99) / 100 - 1);
    };
    std::cout << nlohmann::json{{"frames", frames},
                                {"cells", first.evidence().size()},
                                {"framesPerRevision", 8},
                                {"meshTotalMs", total},
                                {"meshP50Ms", percentile(50)},
                                {"meshP95Ms", percentile(95)},
                                {"meshP99Ms", percentile(99)},
                                {"builds", mesh.builds()},
                                {"generatedVertices", mesh.generated_vertices()},
                                {"submittedVertices", submitted},
                                {"endingGeometry", checksum(mesh.vertices())}}
                     .dump()
              << '\n';
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    std::size_t frames = 0;
    if (argc == 2) {
        const std::string_view input{argv[1]};
        const auto parsed = std::from_chars(input.data(), input.data() + input.size(), frames);
        if (parsed.ec == std::errc{} && parsed.ptr == input.data() + input.size() && frames >= 8 &&
            frames <= 10000) {
            return run(frames);
        }
    }
    std::cerr << "Usage: argus-occupancy-evaluation FRAMES(8..10000)\n";
    return 2;
}
