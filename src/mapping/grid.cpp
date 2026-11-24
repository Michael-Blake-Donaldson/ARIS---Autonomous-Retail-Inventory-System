#include "argus/mapping/grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace argus::mapping {
namespace {

constexpr double mm_per_m = 1000.0;
constexpr std::uint32_t max_extent_mm = 100000;
constexpr std::uint32_t max_cells = 1000000;
constexpr std::uint64_t max_scenario = 9007199254740991ULL;

Occupancy classify(std::int8_t evidence) noexcept {
    if (evidence >= 2) {
        return Occupancy::occupied;
    }
    if (evidence <= -2) {
        return Occupancy::free;
    }
    return Occupancy::unknown;
}

} // namespace

Result<Grid, MapError> Grid::create(Spec spec) {
    if (spec.scenario.value() == 0 || spec.scenario.value() > max_scenario || spec.width_mm == 0 ||
        spec.height_mm == 0 || spec.width_mm > max_extent_mm || spec.height_mm > max_extent_mm ||
        spec.cell_mm < 50 || spec.cell_mm > 2000) {
        return MapError::invalid_spec;
    }
    const auto columns = (spec.width_mm + spec.cell_mm - 1U) / spec.cell_mm;
    const auto rows = (spec.height_mm + spec.cell_mm - 1U) / spec.cell_mm;
    if (columns * rows > max_cells) {
        return MapError::invalid_spec;
    }
    return Grid{spec, columns, rows};
}

Grid::Grid(Spec spec, std::uint32_t columns, std::uint32_t rows)
    : spec_{spec}, columns_{columns}, rows_{rows}, evidence_(columns * rows) {}

bool Grid::contains(Cell cell) const noexcept { return cell.x < columns_ && cell.y < rows_; }

std::size_t Grid::index(Cell cell) const noexcept {
    return static_cast<std::size_t>(cell.y) * columns_ + cell.x;
}

std::optional<Cell> Grid::locate(Point point) const noexcept {
    // Compare in meters before scaling so the public half-open boundary stays consistent.
    if (!(point.x_m >= 0.0 && point.y_m >= 0.0 && point.x_m < spec_.width_mm / mm_per_m &&
          point.y_m < spec_.height_mm / mm_per_m)) {
        return std::nullopt;
    }
    return Cell{
        std::min(columns_ - 1, static_cast<std::uint32_t>(point.x_m * mm_per_m / spec_.cell_mm)),
        std::min(rows_ - 1, static_cast<std::uint32_t>(point.y_m * mm_per_m / spec_.cell_mm))};
}

std::optional<Point> Grid::center(Cell cell) const noexcept {
    if (!contains(cell)) {
        return std::nullopt;
    }
    const auto x = cell.x * spec_.cell_mm;
    const auto y = cell.y * spec_.cell_mm;
    return Point{(x + std::min(x + spec_.cell_mm, spec_.width_mm)) / (2.0 * mm_per_m),
                 (y + std::min(y + spec_.cell_mm, spec_.height_mm)) / (2.0 * mm_per_m)};
}

Occupancy Grid::occupancy(Cell cell) const noexcept {
    return contains(cell) ? classify(evidence_.at(index(cell))) : Occupancy::unknown;
}

Counts Grid::counts() const noexcept {
    Counts result;
    for (auto value : evidence_) {
        switch (classify(value)) {
        case Occupancy::unknown:
            ++result.unknown;
            break;
        case Occupancy::free:
            ++result.free;
            break;
        case Occupancy::occupied:
            ++result.occupied;
            break;
        }
    }
    return result;
}

void Grid::observe_hit(Point origin, Point endpoint) noexcept {
    const auto start = locate(origin);
    if (!start || !std::isfinite(endpoint.x_m) || !std::isfinite(endpoint.y_m) ||
        std::abs(endpoint.x_m - origin.x_m) > 51.0 || std::abs(endpoint.y_m - origin.y_m) > 51.0) {
        return;
    }

    const auto width = spec_.width_mm / mm_per_m;
    const auto height = spec_.height_mm / mm_per_m;
    const auto dx = endpoint.x_m - origin.x_m;
    const auto dy = endpoint.y_m - origin.y_m;
    // Include nominal hits on the floor boundary and clip out-of-frame rays as free evidence.
    constexpr double epsilon = 1e-9;
    const bool hit = endpoint.x_m >= -epsilon && endpoint.x_m <= width + epsilon &&
                     endpoint.y_m >= -epsilon && endpoint.y_m <= height + epsilon;
    const auto clip = [](double from, double to, double extent, double delta) {
        if (to < 0.0) {
            return -from / delta;
        }
        if (to >= extent) {
            return (extent - from) / delta;
        }
        return 1.0;
    };
    const auto length = hit ? 1.0
                            : std::min(clip(origin.x_m, endpoint.x_m, width, dx),
                                       clip(origin.y_m, endpoint.y_m, height, dy));
    const Point clipped{std::clamp(origin.x_m + dx * length, 0.0, std::nextafter(width, 0.0)),
                        std::clamp(origin.y_m + dy * length, 0.0, std::nextafter(height, 0.0))};
    const auto finish = locate(clipped);
    if (!finish) {
        return;
    }
    const double resolution = spec_.cell_mm / mm_per_m;
    const auto first_crossing = [resolution](std::uint32_t pos, double from, double change) {
        if (change == 0.0) {
            return std::numeric_limits<double>::infinity();
        }
        const auto edge = pos + static_cast<std::uint32_t>(change > 0.0);
        return (edge * resolution - from) / change;
    };
    auto tx = first_crossing(start->x, origin.x_m, dx);
    auto ty = first_crossing(start->y, origin.y_m, dy);
    const auto advance_x =
        dx == 0.0 ? std::numeric_limits<double>::infinity() : resolution / std::abs(dx);
    const auto advance_y =
        dy == 0.0 ? std::numeric_limits<double>::infinity() : resolution / std::abs(dy);
    auto cell = *start;
    for (std::uint32_t visited = 0; visited < columns_ + rows_ && contains(cell); ++visited) {
        const bool done = cell == *finish;
        auto &value = evidence_.at(index(cell));
        const int update = done && hit ? 6 : -2;
        value = static_cast<std::int8_t>(std::clamp(static_cast<int>(value) + update,
                                                    -static_cast<int>(max_evidence),
                                                    static_cast<int>(max_evidence)));
        if (done) {
            break;
        }
        const auto next_x = cell.x == finish->x ? std::numeric_limits<double>::infinity() : tx;
        const auto next_y = cell.y == finish->y ? std::numeric_limits<double>::infinity() : ty;
        if (next_x <= next_y) {
            cell.x = dx > 0.0 ? cell.x + 1 : cell.x - 1;
            tx += advance_x;
        }
        if (next_y <= next_x) {
            cell.y = dy > 0.0 ? cell.y + 1 : cell.y - 1;
            ty += advance_y;
        }
    }
}

void Grid::clear() noexcept { std::ranges::fill(evidence_, 0); }

bool Grid::restore(std::span<const std::int8_t> cells) noexcept {
    if (cells.size() != evidence_.size() || !std::ranges::all_of(cells, [](std::int8_t cell) {
            return cell >= -max_evidence && cell <= max_evidence;
        })) {
        return false;
    }
    std::ranges::copy(cells, evidence_.begin());
    return true;
}

} // namespace argus::mapping
