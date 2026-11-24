#include "argus/navigation/planner.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace argus::navigation {
namespace {

constexpr auto infinity = std::numeric_limits<std::uint32_t>::max();
constexpr std::array<std::pair<int, int>, 8> offsets{
    {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};

std::uint32_t index(Cell cell, std::uint32_t columns) noexcept {
    return cell.y * columns + cell.x;
}

Cell cell_at(std::uint32_t position, std::uint32_t columns) noexcept {
    return {position % columns, position / columns};
}

template <typename Visitor>
void adjacent(Cell at, std::uint32_t columns, std::uint32_t rows, Visitor visit) {
    for (const auto &[dx, dy] : offsets) {
        const auto nx = static_cast<int>(at.x) + dx;
        const auto ny = static_cast<int>(at.y) + dy;
        if (nx >= 0 && ny >= 0 && nx < static_cast<int>(columns) && ny < static_cast<int>(rows)) {
            visit(Cell{static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny)}, dx, dy);
        }
    }
}

template <typename Visitor>
void neighbors(const Costmap &map, Cell from, Visitor visit) {
    adjacent(from, map.columns(), map.rows(), [&](Cell to, int dx, int dy) {
        const bool diagonal = dx != 0 && dy != 0;
        if (map.traversable(to) &&
            (!diagonal || (map.traversable({from.x, to.y}) && map.traversable({to.x, from.y})))) {
            visit(to, diagonal ? 14U : 10U);
        }
    });
}

struct Entry {
    std::uint32_t total;
    std::uint32_t distance;
    std::uint32_t position;
    auto operator<=>(const Entry &) const = default;
};

struct Search {
    std::vector<std::uint32_t> distances;
    std::vector<std::uint32_t> parents;
};

Search search(const Costmap &map, Cell start, std::optional<Cell> goal, std::uint32_t budget) {
    const auto columns = map.columns();
    const auto count = columns * map.rows();
    Search result{std::vector<std::uint32_t>(count, infinity),
                  goal ? std::vector<std::uint32_t>(count, infinity) : std::vector<std::uint32_t>{}};
    const auto start_pos = index(start, columns);
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> pending;
    pending.push({goal ? octile(start, *goal) : 0U, 0, start_pos});
    result.distances.at(start_pos) = 0;
    while (!pending.empty()) {
        const auto next = pending.top();
        pending.pop();
        if (next.distance != result.distances.at(next.position)) {
            continue;
        }
        const auto current = cell_at(next.position, columns);
        if (goal && current == *goal) {
            break;
        }
        neighbors(map, current, [&](Cell neighbor, std::uint32_t edge) {
            const auto distance = next.distance + edge;
            const auto pos = index(neighbor, columns);
            if (distance <= budget && distance < result.distances.at(pos)) {
                result.distances.at(pos) = distance;
                if (goal) {
                    result.parents.at(pos) = next.position;
                }
                pending.push({distance + (goal ? octile(neighbor, *goal) : 0U), distance, pos});
            }
        });
    }
    return result;
}

bool valid_zone(const NoGo &zone, const mapping::Spec &spec) noexcept {
    return zone.low.x_m >= 0.0 && zone.low.y_m >= 0.0 && zone.low.x_m < zone.high.x_m &&
           zone.low.y_m < zone.high.y_m && zone.high.x_m <= spec.width_mm / 1000.0 &&
           zone.high.y_m <= spec.height_mm / 1000.0;
}

bool touches_zone(Point at, double padding, const NoGo &zone) noexcept {
    return at.x_m + padding >= zone.low.x_m && at.x_m - padding <= zone.high.x_m &&
           at.y_m + padding >= zone.low.y_m && at.y_m - padding <= zone.high.y_m;
}

bool frontier_cell(const mapping::Grid &grid, Cell cell) {
    if (grid.occupancy(cell) != mapping::Occupancy::free) {
        return false;
    }
    bool unknown = false;
    adjacent(cell, grid.columns(), grid.rows(), [&](Cell next, int dx, int dy) {
        if ((dx == 0 || dy == 0) && grid.occupancy(next) == mapping::Occupancy::unknown) {
            unknown = true;
        }
    });
    return unknown;
}

std::uint32_t information(const mapping::Grid &grid, Cell cell) {
    const auto left = cell.x >= 2 ? cell.x - 2 : 0;
    const auto bottom = cell.y >= 2 ? cell.y - 2 : 0;
    std::uint32_t gain = 0;
    for (auto y = bottom; y <= std::min(cell.y + 2, grid.rows() - 1); ++y) {
        for (auto x = left; x <= std::min(cell.x + 2, grid.columns() - 1); ++x) {
            gain += static_cast<std::uint32_t>(grid.occupancy({x, y}) == mapping::Occupancy::unknown);
        }
    }
    return gain;
}

} // namespace

Costmap::Costmap(std::uint32_t columns, std::uint32_t rows, std::vector<bool> traversable)
    : columns_{columns}, rows_{rows}, traversable_{std::move(traversable)} {}

std::optional<Costmap> Costmap::build(const mapping::Grid &grid, Meters radius_m,
                                      std::span<const NoGo> zones) {
    const auto spec = grid.spec();
    const auto radius = radius_m.value();
    if (!(radius > 0.0 && radius <= 5.0) || zones.size() > 32 ||
        !std::ranges::all_of(zones, [&spec](const NoGo &zone) { return valid_zone(zone, spec); })) {
        return std::nullopt;
    }
    const auto columns = grid.columns();
    const auto rows = grid.rows();
    const auto resolution = spec.cell_mm / 1000.0;
    const auto expand = static_cast<std::uint16_t>(std::floor(radius / resolution + 0.5));
    std::vector<std::uint16_t> distance(columns * rows, std::numeric_limits<std::uint16_t>::max());
    std::vector<bool> free(columns * rows);
    std::queue<Cell> sources;
    for (std::uint32_t y = 0; y < rows; ++y) {
        for (std::uint32_t x = 0; x < columns; ++x) {
            const Cell cell{x, y};
            const auto pos = index(cell, columns);
            const auto occupancy = grid.occupancy(cell);
            const auto point = grid.center(cell).value();
            if (occupancy == mapping::Occupancy::occupied) {
                distance.at(pos) = 0;
                sources.push(cell);
            }
            free.at(pos) = occupancy == mapping::Occupancy::free &&
                           point.x_m > radius && point.y_m > radius &&
                           point.x_m + radius < spec.width_mm / 1000.0 &&
                           point.y_m + radius < spec.height_mm / 1000.0 &&
                           !std::ranges::any_of(zones, [&](const NoGo &zone) {
                               return touches_zone(point, radius + resolution / 2.0, zone);
                           });
        }
    }

    // Multi-source Chebyshev inflation conservatively surrounds occupied cell rectangles.
    while (!sources.empty()) {
        const auto current = sources.front();
        sources.pop();
        const auto depth = distance.at(index(current, columns));
        free.at(index(current, columns)) = false;
        if (depth >= expand) {
            continue;
        }
        adjacent(current, columns, rows, [&](Cell next, int, int) {
            auto &value = distance.at(index(next, columns));
            if (value == std::numeric_limits<std::uint16_t>::max()) {
                value = static_cast<std::uint16_t>(depth + 1U);
                sources.push(next);
            }
        });
    }
    return Costmap{columns, rows, std::move(free)};
}

bool Costmap::traversable(Cell cell) const noexcept {
    return cell.x < columns_ && cell.y < rows_ && traversable_.at(index(cell, columns_));
}

std::uint32_t octile(Cell from, Cell goal) noexcept {
    const auto dx = std::max(from.x, goal.x) - std::min(from.x, goal.x);
    const auto dy = std::max(from.y, goal.y) - std::min(from.y, goal.y);
    const auto estimate = 10ULL * std::max(dx, dy) + 4ULL * std::min(dx, dy);
    return estimate < infinity ? static_cast<std::uint32_t>(estimate) : infinity;
}

std::optional<Route> plan(const Costmap &map, Cell start, Cell goal) {
    if (!map.traversable(start) || !map.traversable(goal)) {
        return std::nullopt;
    }
    const auto found = search(map, start, goal, infinity);
    const auto last = index(goal, map.columns());
    const auto cost = found.distances.at(last);
    if (cost == infinity) {
        return std::nullopt;
    }
    Route route{{}, cost};
    for (auto pos = last; pos != infinity; pos = found.parents.at(pos)) {
        route.cells.push_back(cell_at(pos, map.columns()));
    }
    std::ranges::reverse(route.cells);
    return route;
}

Frontier select_frontier(const mapping::Grid &grid, const Costmap &costmap, Cell start,
                         std::uint32_t budget, std::span<const Cell> excluded) {
    Frontier result;
    if (grid.columns() != costmap.columns() || grid.rows() != costmap.rows() ||
        !costmap.traversable(start)) {
        result.inaccessible = true;
        return result;
    }
    const auto found = search(costmap, start, std::nullopt, budget);
    auto best = std::numeric_limits<std::int64_t>::min();
    for (std::uint32_t y = 0; y < grid.rows(); ++y) {
        for (std::uint32_t x = 0; x < grid.columns(); ++x) {
            const Cell candidate{x, y};
            if (!frontier_cell(grid, candidate)) {
                continue;
            }
            const auto distance = found.distances.at(index(candidate, grid.columns()));
            if (distance == infinity || candidate == start ||
                std::ranges::find(excluded, candidate) != excluded.end()) {
                result.inaccessible = true;
                continue;
            }
            const auto score = static_cast<std::int64_t>(information(grid, candidate)) * 24 -
                               distance;
            if (score > best) {
                best = score;
                result.goal = candidate;
            }
        }
    }
    return result;
}

} // namespace argus::navigation
