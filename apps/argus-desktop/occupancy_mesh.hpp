#pragma once

#include "argus/mapping/grid.hpp"

#include <array>

namespace argus::desktop {

using Color = std::array<unsigned char, 4>;

struct Vertex {
    float x, y, z;
    Color color;
    bool operator==(const Vertex &) const = default;
};

void append_surface(std::vector<Vertex> &mesh, double x, double y, double w, double h, double z,
                    Color color);

class OccupancyMesh {
  public:
    void rebuild(const mapping::Grid &grid);
    [[nodiscard]] std::span<const Vertex> vertices() const noexcept { return vertices_; }
    [[nodiscard]] std::uint64_t builds() const noexcept { return builds_; }
    [[nodiscard]] std::uint64_t generated_vertices() const noexcept { return generated_vertices_; }

  private:
    std::vector<Vertex> vertices_;
    std::uint64_t builds_{};
    std::uint64_t generated_vertices_{};
};

} // namespace argus::desktop
