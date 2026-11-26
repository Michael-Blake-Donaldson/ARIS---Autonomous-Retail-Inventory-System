#pragma once

#include "occupancy_mesh.hpp"
#include "operations_model.hpp"

namespace argus::desktop {

struct Viewport {
    int x;
    int y;
    int width;
    int height;
};

struct EvidenceRegion {
    mapping::Point origin;
    double heading;
    double range;
};

class Scene {
  public:
    explicit Scene(const Layout &layout);
    void draw(Viewport viewport, const LiveFrame &frame, const Camera &camera,
              const Overlays &overlays, std::span<const EvidenceRegion> uncertainty,
              const DiagnosticsFrame *diagnostics, bool map_only);
    [[nodiscard]] std::uint64_t occupancy_frames() const noexcept { return occupancy_frames_; }
    [[nodiscard]] const OccupancyMesh &occupancy_mesh() const noexcept { return occupancy_; }

  private:
    static void submit(std::span<const Vertex> mesh, unsigned mode);
    static void line(std::vector<Vertex> &mesh, mapping::Point a, mapping::Point b, double z,
                     Color color);
    static void box(std::vector<Vertex> &mesh, double x, double y, double w, double h, double base,
                    double height, Color color);
    static void sector(std::vector<Vertex> &mesh, mapping::Point origin, double heading,
                       double radius, double aperture, Color color);
    void draw_layout();
    void draw_overlays(const LiveFrame &frame, const Overlays &overlays,
                       std::span<const EvidenceRegion> uncertainty);

    const Layout &layout_;
    std::vector<Vertex> built_;
    std::vector<Vertex> ground_;
    std::vector<Vertex> grid_;
    std::vector<Vertex> temporary_;
    OccupancyMesh occupancy_;
    std::uint64_t occupancy_frames_{};
};

} // namespace argus::desktop
