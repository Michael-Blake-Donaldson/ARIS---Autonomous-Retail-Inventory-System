#include "occupancy_mesh.hpp"

#include <algorithm>
#include <cmath>

namespace argus::desktop {

void append_surface(std::vector<Vertex> &mesh, double x, double y, double w, double h, double z,
                    Color color) {
    const auto left = static_cast<float>(x);
    const auto right = static_cast<float>(x + w);
    const auto bottom = static_cast<float>(y);
    const auto top = static_cast<float>(y + h);
    const auto level = static_cast<float>(z);
    mesh.insert(mesh.end(), {{left, bottom, level, color},
                             {right, bottom, level, color},
                             {right, top, level, color},
                             {left, top, level, color}});
}

void OccupancyMesh::update(const mapping::Grid &grid, OccupancyVersion version) {
    const Key key{grid.spec(), version};
    if (key_ == key) {
        return;
    }
    rebuild(grid);
    key_ = key;
}

void OccupancyMesh::rebuild(const mapping::Grid &grid) {
    key_.reset();
    constexpr double mm = 1000.0;
    constexpr double sample_target = 18000.0;
    const auto spec = grid.spec();
    const auto total = static_cast<std::uint64_t>(grid.rows()) * grid.columns();
    const auto stride = static_cast<std::uint32_t>(
        std::max(1.0, std::ceil(std::sqrt(static_cast<double>(total) / sample_target))));
    vertices_.clear();
    for (std::uint32_t y = 0; y < grid.rows(); y += stride) {
        for (std::uint32_t x = 0; x < grid.columns(); x += stride) {
            const auto cell = grid.occupancy({x, y});
            if (cell == mapping::Occupancy::unknown) {
                continue;
            }
            const auto left = x * spec.cell_mm;
            const auto bottom = y * spec.cell_mm;
            append_surface(vertices_, left / mm, bottom / mm,
                           std::min(spec.cell_mm, spec.width_mm - left) / mm,
                           std::min(spec.cell_mm, spec.height_mm - bottom) / mm, 0.026,
                           cell == mapping::Occupancy::free ? Color{57, 183, 175, 120}
                                                            : Color{244, 159, 82, 220});
        }
    }
    ++builds_;
    generated_vertices_ += vertices_.size();
}

} // namespace argus::desktop
