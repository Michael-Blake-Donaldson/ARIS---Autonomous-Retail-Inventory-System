#include "argus/mapping/map_io.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <istream>
#include <ostream>
#include <ranges>
#include <string_view>
#include <utility>

namespace argus::mapping {
namespace {

constexpr std::string_view magic = "ARGUSMAP";
constexpr std::uint32_t version = 1;
constexpr std::uint32_t east_north_from_southwest = 1;
constexpr std::size_t header_size = 36;
constexpr std::uint32_t hash_seed = 2166136261U;

template <typename Integer> void write_le(std::span<char> bytes, Integer value) noexcept {
    for (char &byte : bytes) {
        byte = std::bit_cast<char>(static_cast<std::uint8_t>(value & 0xffU));
        value >>= 8;
    }
}

template <typename Integer> Integer read_le(std::span<const char> bytes) noexcept {
    Integer value{};
    for (char byte : bytes | std::views::reverse) {
        value = (value << 8) | static_cast<unsigned char>(byte);
    }
    return value;
}

std::uint32_t hash(std::span<const char> bytes, std::uint32_t previous) noexcept {
    for (char byte : bytes) {
        previous = (previous ^ static_cast<unsigned char>(byte)) * 16777619U;
    }
    return previous;
}

bool read_exact(std::istream &input, std::span<char> bytes) {
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(input);
}

bool end_of_input(std::istream &input) {
    try {
        return input.peek() == std::char_traits<char>::eof() && input.eof() && !input.bad();
    } catch (const std::ios_base::failure &) {
        if (input.eof() && !input.bad()) {
            return true;
        }
        throw;
    }
}

MapError read_failure(const std::istream &input) {
    return input.bad() || !input.eof() ? MapError::input_failure : MapError::malformed;
}

} // namespace

Result<std::monostate, MapError> save(const Grid &grid, std::ostream &output) {
    const auto spec = grid.spec();
    std::array<char, header_size> header{};
    std::ranges::copy(magic, header.begin());
    std::span bytes{header};
    write_le(bytes.subspan(8, 4), version);
    write_le(bytes.subspan(12, 4), east_north_from_southwest);
    write_le(bytes.subspan(16, 8), spec.scenario.value());
    write_le(bytes.subspan(24, 4), spec.width_mm);
    write_le(bytes.subspan(28, 4), spec.height_mm);
    write_le(bytes.subspan(32, 4), spec.cell_mm);

    std::vector<char> payload;
    payload.reserve(grid.evidence().size());
    for (const auto cell : grid.evidence()) {
        payload.push_back(static_cast<char>(cell + Grid::max_evidence));
    }
    std::array<char, 4> checksum{};
    write_le(std::span{checksum}, hash(payload, hash(header, hash_seed)));
    try {
        output.write(header.data(), static_cast<std::streamsize>(header.size()));
        output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        output.write(checksum.data(), static_cast<std::streamsize>(checksum.size()));
        if (!output.flush()) {
            return MapError::output_failure;
        }
        return std::monostate{};
    } catch (const std::ios_base::failure &) {
        return MapError::output_failure;
    }
}

Result<Grid, MapError> load(std::istream &input, Spec expected) {
    auto created = Grid::create(expected);
    if (const auto *error = std::get_if<MapError>(&created)) {
        return *error;
    }
    auto &grid = std::get<Grid>(created);
    std::array<char, header_size> header{};
    try {
        if (!read_exact(input, header)) {
            return read_failure(input);
        }
        const std::span<const char> bytes{header};
        if (std::string_view(bytes.data(), magic.size()) != magic) {
            return MapError::malformed;
        }
        if (read_le<std::uint32_t>(bytes.subspan(8, 4)) != version ||
            read_le<std::uint32_t>(bytes.subspan(12, 4)) != east_north_from_southwest ||
            read_le<std::uint64_t>(bytes.subspan(16, 8)) != expected.scenario.value() ||
            read_le<std::uint32_t>(bytes.subspan(24, 4)) != expected.width_mm ||
            read_le<std::uint32_t>(bytes.subspan(28, 4)) != expected.height_mm ||
            read_le<std::uint32_t>(bytes.subspan(32, 4)) != expected.cell_mm) {
            return MapError::incompatible;
        }
        std::vector<char> payload(grid.evidence().size());
        std::array<char, 4> checksum{};
        if (!read_exact(input, payload) || !read_exact(input, checksum)) {
            return read_failure(input);
        }
        if (!end_of_input(input)) {
            return input.bad() || input.fail() ? MapError::input_failure : MapError::malformed;
        }
        if (read_le<std::uint32_t>(checksum) != hash(payload, hash(header, hash_seed))) {
            return MapError::malformed;
        }

        std::vector<std::int8_t> cells;
        cells.reserve(payload.size());
        for (const char byte : payload) {
            const auto value = static_cast<unsigned char>(byte);
            if (value > 2 * Grid::max_evidence) {
                return MapError::malformed;
            }
            cells.push_back(static_cast<std::int8_t>(value - Grid::max_evidence));
        }
        if (!grid.restore(cells)) {
            return MapError::malformed;
        }
        return std::move(grid);
    } catch (const std::ios_base::failure &) {
        return read_failure(input);
    }
}

} // namespace argus::mapping
