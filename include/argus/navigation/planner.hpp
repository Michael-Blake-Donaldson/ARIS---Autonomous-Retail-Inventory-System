#pragma once

#include "argus/mapping/grid.hpp"

#include <optional>
#include <span>
#include <vector>

namespace argus::navigation {

using mapping::Cell;
using mapping::Point;

struct NoGo {
    Point low;
    Point high;
};

class Costmap {
  public:
    [[nodiscard]] static std::optional<Costmap> build(const mapping::Grid &grid, Meters radius,
                                                      std::span<const NoGo> zones = {});
    [[nodiscard]] bool traversable(Cell cell) const noexcept;
    [[nodiscard]] std::uint32_t columns() const noexcept { return columns_; }
    [[nodiscard]] std::uint32_t rows() const noexcept { return rows_; }

  private:
    Costmap(std::uint32_t columns, std::uint32_t rows, std::vector<bool> traversable);
    std::uint32_t columns_;
    std::uint32_t rows_;
    std::vector<bool> traversable_;
};

struct Route {
    std::vector<Cell> cells;
    std::uint32_t cost;
};

[[nodiscard]] std::uint32_t octile(Cell from, Cell goal) noexcept;
[[nodiscard]] std::optional<Route> plan(const Costmap &costmap, Cell start, Cell goal);

struct Frontier {
    std::optional<Cell> goal;
    bool inaccessible{};
};

// Costs are in A* units, ten per cardinal cell.
[[nodiscard]] Frontier select_frontier(const mapping::Grid &grid, const Costmap &costmap,
                                       Cell start, std::uint32_t cost_budget,
                                       std::span<const Cell> excluded = {});

} // namespace argus::navigation
