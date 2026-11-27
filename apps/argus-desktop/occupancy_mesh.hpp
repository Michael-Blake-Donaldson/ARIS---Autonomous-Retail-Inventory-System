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

struct OccupancyVersion {
    RunId run;
    std::uint64_t revision;
    bool archived;
    bool operator==(const OccupancyVersion &) const = default;
};

void append_surface(std::vector<Vertex> &mesh, double x, double y, double w, double h, double z,
                    Color color);

class OccupancyMesh {
  public:
    void rebuild(const mapping::Grid &grid);
    // Within a run and source, a version identifies the same observed grid across snapshots.
    void update(const mapping::Grid &grid, OccupancyVersion version);
    [[nodiscard]] std::span<const Vertex> vertices() const noexcept { return vertices_; }
    [[nodiscard]] std::uint64_t builds() const noexcept { return builds_; }
    [[nodiscard]] std::uint64_t generated_vertices() const noexcept { return generated_vertices_; }

  private:
    struct Key {
        mapping::Spec spec;
        OccupancyVersion version;
        bool operator==(const Key &) const = default;
    };
    std::optional<Key> key_;
    std::vector<Vertex> vertices_;
    std::uint64_t builds_{};
    std::uint64_t generated_vertices_{};
};

} // namespace argus::desktop
