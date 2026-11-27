#pragma once

#include "argus/core/result.hpp"
#include "argus/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace argus::mapping {

struct Spec {
    ScenarioId scenario;
    std::uint32_t width_mm;
    std::uint32_t height_mm;
    std::uint32_t cell_mm{250};
    bool operator==(const Spec &) const = default;
};

struct Point {
    double x_m;
    double y_m;
};

struct Cell {
    std::uint32_t x;
    std::uint32_t y;
    bool operator==(const Cell &) const = default;
};

enum class Occupancy { unknown, free, occupied };
enum class MapError { invalid_spec, malformed, incompatible, input_failure, output_failure };

struct Counts {
    std::size_t unknown{};
    std::size_t free{};
    std::size_t occupied{};
};

class Grid {
  public:
    [[nodiscard]] static Result<Grid, MapError> create(Spec spec);
    [[nodiscard]] Spec spec() const noexcept { return spec_; }
    [[nodiscard]] std::uint32_t columns() const noexcept { return columns_; }
    [[nodiscard]] std::uint32_t rows() const noexcept { return rows_; }

    // East/north coordinates use a southwest origin and a half-open map domain.
    [[nodiscard]] std::optional<Cell> locate(Point point) const noexcept;
    [[nodiscard]] std::optional<Point> center(Cell cell) const noexcept;
    [[nodiscard]] Occupancy occupancy(Cell cell) const noexcept;
    [[nodiscard]] Counts counts() const noexcept;

    void observe_hit(Point origin, Point endpoint) noexcept;
    void clear() noexcept;
    [[nodiscard]] std::span<const std::int8_t> evidence() const noexcept { return evidence_; }
    [[nodiscard]] bool restore(std::span<const std::int8_t> cells) noexcept;

    static constexpr std::int8_t max_evidence = 20;

  private:
    Grid(Spec spec, std::uint32_t columns, std::uint32_t rows);
    [[nodiscard]] bool contains(Cell cell) const noexcept;
    [[nodiscard]] std::size_t index(Cell cell) const noexcept;

    Spec spec_;
    std::uint32_t columns_;
    std::uint32_t rows_;
    std::vector<std::int8_t> evidence_;
};

} // namespace argus::mapping
