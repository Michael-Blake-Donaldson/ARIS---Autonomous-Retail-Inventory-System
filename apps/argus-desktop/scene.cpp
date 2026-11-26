#include "scene.hpp"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace argus::desktop {
namespace {

constexpr double mm = 1000.0;
constexpr double pi = std::numbers::pi;

struct Rect {
    double x, y, w, h;
};

Rect rect(world::RectMm item) noexcept {
    return {item.min.x / mm, item.min.y / mm, (item.max.x - item.min.x) / mm,
            (item.max.y - item.min.y) / mm};
}

mapping::Point position(robot::Pose pose) noexcept { return {pose.x.value(), pose.y.value()}; }

std::array<unsigned char, 4> shade(std::array<unsigned char, 4> color, double amount) noexcept {
    for (std::size_t i = 0; i < 3; ++i) {
        color.at(i) = static_cast<unsigned char>(color.at(i) * amount);
    }
    return color;
}

} // namespace

Scene::Scene(const Layout &layout) : layout_{layout} { draw_layout(); }

void Scene::submit(std::span<const Vertex> mesh, unsigned mode) {
    if (mesh.empty()) {
        return;
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, static_cast<GLsizei>(sizeof(Vertex)), &mesh.front().x);
    glColorPointer(4, GL_UNSIGNED_BYTE, static_cast<GLsizei>(sizeof(Vertex)),
                   mesh.front().color.data());
    glDrawArrays(mode, 0, static_cast<GLsizei>(mesh.size()));
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void Scene::line(std::vector<Vertex> &mesh, mapping::Point a, mapping::Point b, double z,
                 Color color) {
    const auto level = static_cast<float>(z);
    mesh.push_back({static_cast<float>(a.x_m), static_cast<float>(a.y_m), level, color});
    mesh.push_back({static_cast<float>(b.x_m), static_cast<float>(b.y_m), level, color});
}

void Scene::box(std::vector<Vertex> &mesh, double x, double y, double w, double h, double base,
                double height, Color color) {
    append_surface(mesh, x, y, w, h, base + height, color);
    const auto low = static_cast<float>(base);
    const auto high = static_cast<float>(base + height);
    const std::array<std::array<float, 2>, 4> vertices{
        {{static_cast<float>(x), static_cast<float>(y)},
         {static_cast<float>(x + w), static_cast<float>(y)},
         {static_cast<float>(x + w), static_cast<float>(y + h)},
         {static_cast<float>(x), static_cast<float>(y + h)}}};
    constexpr std::array<double, 4> tones{0.78, 0.56, 0.64, 0.88};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto a = vertices.at(i);
        const auto b = vertices.at((i + 1) % vertices.size());
        const auto side = shade(color, tones.at(i));
        mesh.insert(mesh.end(), {{a[0], a[1], low, side},
                                 {b[0], b[1], low, side},
                                 {b[0], b[1], high, side},
                                 {a[0], a[1], high, side}});
    }
}

void Scene::sector(std::vector<Vertex> &mesh, mapping::Point origin, double heading, double radius,
                   double aperture, Color color) {
    const auto end = [&](int index) {
        const double angle = heading - aperture / 2.0 + aperture * index / 24.0;
        return mapping::Point{origin.x_m + radius * std::cos(angle),
                              origin.y_m + radius * std::sin(angle)};
    };
    line(mesh, origin, end(0), 0.065, color);
    for (int i = 0; i < 24; ++i) {
        line(mesh, end(i), end(i + 1), 0.065, color);
    }
    line(mesh, end(24), origin, 0.065, color);
}

void Scene::draw_layout() {
    const auto floor = rect(layout_.floor);
    box(ground_, floor.x, floor.y, floor.w, floor.h, -0.18, 0.18, {42, 54, 70, 255});
    const double gap = std::max(0.5, std::max(floor.w, floor.h) / 66.0);
    for (double x = floor.x; x <= floor.x + floor.w; x += gap) {
        line(grid_, {x, floor.y}, {x, floor.y + floor.h}, 0.014, {85, 107, 132, 115});
    }
    for (double y = floor.y; y <= floor.y + floor.h; y += gap) {
        line(grid_, {floor.x, y}, {floor.x + floor.w, y}, 0.014, {85, 107, 132, 115});
    }
    for (const auto &zone : layout_.zones) {
        const auto area = rect(zone.bounds);
        const auto color = zone.kind == world::ZoneKind::charging    ? Color{48, 171, 147, 255}
                           : zone.kind == world::ZoneKind::dock      ? Color{55, 106, 107, 255}
                           : zone.kind == world::ZoneKind::receiving ? Color{114, 82, 54, 255}
                                                                     : Color{52, 76, 91, 255};
        append_surface(ground_, area.x, area.y, area.w, area.h, 0.004, color);
        if (zone.kind == world::ZoneKind::charging) {
            box(ground_, area.x, area.y, area.w, area.h, 0.015, 0.045, color);
            for (int i = 1; i <= 3; ++i) {
                append_surface(ground_, area.x + area.w * i / 4.0, area.y + 0.07, area.w * 0.07,
                               std::max(0.01, area.h - 0.14), 0.064, {134, 228, 194, 255});
            }
        }
    }
    constexpr std::array<Color, 4> packages{
        {{226, 157, 91, 255}, {143, 172, 185, 255}, {180, 139, 114, 255}, {197, 182, 132, 255}}};
    for (const auto &solid : layout_.solids) {
        const auto area = rect(solid.bounds);
        if (solid.kind != world::SolidKind::rack) {
            const bool wall = solid.kind == world::SolidKind::wall;
            box(built_, area.x, area.y, area.w, area.h, 0.02, wall ? 0.85 : 0.52,
                wall ? Color{106, 126, 152, 255} : Color{165, 110, 72, 255});
            continue;
        }
        const auto shelf_count = solid.shelf_heights_mm.size();
        const double upper = shelf_count == 0 ? 1.8 : solid.shelf_heights_mm.back() / mm + 0.32;
        const double pole = std::min({0.085, area.w / 4.0, area.h / 4.0});
        for (const double x : {area.x, area.x + area.w - pole}) {
            for (const double y : {area.y, area.y + area.h - pole}) {
                box(built_, x, y, pole, pole, 0.02, upper, {129, 153, 179, 255});
            }
        }
        for (std::size_t i = 0; i < shelf_count; ++i) {
            const auto z = solid.shelf_heights_mm.at(i) / mm;
            box(built_, area.x, area.y, area.w, area.h, z, 0.06, {106, 128, 153, 255});
            if (i >= 5) {
                continue;
            }
            for (int parcel = 0; parcel < 3; ++parcel) {
                const auto color =
                    packages.at((i + static_cast<std::size_t>(parcel)) % packages.size());
                box(built_, area.x + area.w * 0.16, area.y + area.h * (0.11 + parcel * 0.29),
                    area.w * 0.58, area.h * 0.20, z + 0.06, 0.16 + 0.045 * parcel, color);
            }
        }
    }
}

void Scene::draw_overlays(const LiveFrame &frame, const Overlays &overlays,
                          std::span<const EvidenceRegion> uncertainty) {
    if (overlays.occupancy) {
        ++occupancy_frames_;
        occupancy_.rebuild(frame.grid);
        submit(occupancy_.vertices(), GL_QUADS);
    }
    temporary_.clear();
    const auto trace = [&](std::span<const mapping::Point> points, Color color, double z) {
        for (std::size_t i = 1; i < points.size(); ++i) {
            line(temporary_, points[i - 1], points[i], z, color);
        }
    };
    if (overlays.trace) {
        trace(frame.trace, {189, 159, 246, 210}, 0.055);
    }
    if (overlays.route) {
        trace(frame.route, {255, 182, 104, 255}, 0.085);
    }
    if (overlays.lidar && frame.map.scan == ScanState::fresh && frame.lidar) {
        const auto origin = position(frame.lidar->header.estimated_pose);
        auto angle =
            frame.lidar->header.estimated_pose.heading.value() + frame.lidar->first_angle_rad;
        for (const auto &range : frame.lidar->ranges_m) {
            if (range) {
                line(temporary_, origin,
                     {origin.x_m + *range * std::cos(angle), origin.y_m + *range * std::sin(angle)},
                     0.09, {131, 237, 255, 44});
            }
            angle += frame.lidar->angle_step_rad;
        }
    }
    if (overlays.antenna) {
        const auto pose = frame.telemetry.estimated_pose;
        const auto at = position(pose);
        const auto yaw = pose.heading.value();
        sector(temporary_, {at.x_m + 0.18 * std::cos(yaw), at.y_m + 0.18 * std::sin(yaw)},
               yaw + 0.2, 8.0, pi, {255, 200, 122, 180});
    }
    if (overlays.uncertainty) {
        for (const auto &region : uncertainty) {
            sector(temporary_, region.origin, region.heading, region.range, 1.5,
                   {248, 152, 241, 140});
        }
    }
    glLineWidth(1.4F);
    submit(temporary_, GL_LINES);
    glLineWidth(1.0F);
}

void Scene::draw(Viewport viewport, const LiveFrame &frame, const Camera &camera,
                 const Overlays &overlays, std::span<const EvidenceRegion> uncertainty,
                 const DiagnosticsFrame *diagnostics, bool map_only) {
    if (viewport.width <= 0 || viewport.height <= 0) {
        return;
    }
    const auto floor = rect(layout_.floor);
    const auto robot = position(frame.telemetry.estimated_pose);
    const auto aspect = static_cast<double>(viewport.width) / viewport.height;
    const auto span = std::hypot(floor.w, floor.h);
    const auto follow = camera.mode == CameraMode::follow;
    const auto plan = camera.mode == CameraMode::plan;
    const auto width = (follow ? std::min(8.0, span) : span * 0.57) / camera.zoom;
    const auto half_x = std::max(width, width * aspect * 0.73);
    const auto half_y = half_x / aspect;
    glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-half_x, half_x, -half_y, half_y, -span * 2, span * 2);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotated(plan ? 0.0 : camera.pitch, 1, 0, 0);
    glRotated(plan     ? 0.0
              : follow ? -frame.telemetry.estimated_pose.heading.value() * 180.0 / pi - 90
                       : camera.yaw,
              0, 0, 1);
    glTranslated(follow ? -robot.x_m : -floor.x - floor.w / 2.0,
                 follow ? -robot.y_m : -floor.y - floor.h / 2.0, 0.0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (map_only) {
        temporary_.clear();
        append_surface(temporary_, floor.x, floor.y, floor.w, floor.h, 0.0, {39, 54, 68, 255});
        submit(temporary_, GL_QUADS);
    } else {
        submit(ground_, GL_QUADS);
        submit(grid_, GL_LINES);
        submit(built_, GL_QUADS);
    }
    draw_overlays(frame, overlays, uncertainty);
    if (frame.archived) {
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        return;
    }
    temporary_.clear();
    const double radius = layout_.radius_mm / mm;
    if (!map_only && diagnostics) {
        for (const auto &actor : diagnostics->actors) {
            const auto a = rect(actor.bounds);
            box(temporary_, a.x, a.y, a.w, a.h, 0.02,
                actor.kind == world::ActorKind::worker ? 1.42 : 0.62, {246, 129, 214, 255});
        }
        const auto true_pose = position(diagnostics->true_pose);
        box(temporary_, true_pose.x_m - radius, true_pose.y_m - radius, radius * 2, radius * 2,
            0.03, 0.10, {231, 98, 188, 255});
    }
    const auto pose_color = frame.map.pose == PoseState::current     ? Color{120, 229, 209, 255}
                            : frame.map.pose == PoseState::uncertain ? Color{244, 159, 82, 255}
                                                                     : Color{157, 175, 187, 255};
    box(temporary_, robot.x_m - radius * 0.72, robot.y_m - radius * 0.72, radius * 1.44,
        radius * 1.44, 0.12, 0.21, pose_color);
    box(temporary_, robot.x_m - radius * 0.26, robot.y_m - radius * 0.26, radius * 0.52,
        radius * 0.52, 0.33, 0.23, {218, 253, 240, 255});
    submit(temporary_, GL_QUADS);
    temporary_.clear();
    const double heading = frame.telemetry.estimated_pose.heading.value();
    line(temporary_, robot,
         {robot.x_m + std::cos(heading) * radius * 2, robot.y_m + std::sin(heading) * radius * 2},
         0.52, {229, 255, 241, 255});
    submit(temporary_, GL_LINES);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
}

} // namespace argus::desktop
