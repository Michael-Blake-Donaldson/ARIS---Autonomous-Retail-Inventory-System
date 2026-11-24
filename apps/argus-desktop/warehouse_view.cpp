#include "warehouse_view.hpp"

#include <windows.h>

#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace argus::desktop {
namespace {

using namespace world;
using Color = std::array<GLubyte, 3>;

Color zone_color(ZoneKind kind) noexcept {
    switch (kind) {
    case ZoneKind::aisle:
        return {39, 60, 78};
    case ZoneKind::receiving:
        return {71, 58, 45};
    case ZoneKind::dock:
        return {43, 78, 77};
    case ZoneKind::doorway:
        return {70, 92, 108};
    case ZoneKind::charging:
        return {64, 133, 118};
    }
    return {39, 60, 78};
}

Color solid_color(SolidKind kind) noexcept {
    switch (kind) {
    case SolidKind::wall:
        return {125, 147, 162};
    case SolidKind::rack:
        return {106, 130, 211};
    case SolidKind::obstruction:
        return {164, 118, 80};
    }
    return {125, 147, 162};
}

Color actor_color(ActorKind kind) noexcept {
    switch (kind) {
    case ActorKind::worker:
        return {246, 221, 113};
    case ActorKind::cart:
        return {240, 139, 90};
    case ActorKind::u_boat:
        return {232, 161, 90};
    case ActorKind::temporary_obstruction:
        return {218, 114, 120};
    }
    return {246, 221, 113};
}

void rect_vertices(RectMm rect) {
    glVertex2i(rect.min.x, rect.min.y);
    glVertex2i(rect.max.x, rect.min.y);
    glVertex2i(rect.max.x, rect.max.y);
    glVertex2i(rect.min.x, rect.max.y);
}

void draw_rect(RectMm rect, Color color, GLenum style = GL_QUADS) {
    glColor3ubv(color.data());
    glBegin(style);
    rect_vertices(rect);
    glEnd();
}

void draw_robot(robot::Pose pose, std::int32_t radius, Color marker, bool outline) {
    constexpr double mm_per_m = 1000.0;
    const auto x = pose.x.value() * mm_per_m;
    const auto y = pose.y.value() * mm_per_m;
    constexpr int sides = 32;
    glColor3ubv(marker.data());
    glBegin(outline ? GL_LINE_LOOP : GL_TRIANGLE_FAN);
    if (!outline) {
        glVertex2d(x, y);
    }
    for (int side = 0; side <= sides; ++side) {
        const auto theta = 2.0 * std::numbers::pi * static_cast<double>(side) / sides;
        glVertex2d(x + radius * std::cos(theta), y + radius * std::sin(theta));
    }
    glEnd();
    if (!outline) {
        glColor3ub(25, 54, 51);
    }
    glBegin(GL_LINES);
    glVertex2d(x, y);
    glVertex2d(x + radius * 1.35 * std::cos(pose.heading.value()),
               y + radius * 1.35 * std::sin(pose.heading.value()));
    glEnd();
}

void draw_lidar(const sensors::LidarScan &scan) {
    constexpr double mm_per_m = 1000.0;
    const auto &pose = scan.header.estimated_pose;
    const auto x = pose.x.value() * mm_per_m;
    const auto y = pose.y.value() * mm_per_m;
    auto angle = pose.heading.value() + scan.first_angle_rad;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(111, 231, 251, 56);
    glBegin(GL_LINES);
    for (const auto &range : scan.ranges_m) {
        if (range) {
            const auto distance = *range * mm_per_m;
            glVertex2d(x, y);
            glVertex2d(x + distance * std::cos(angle), y + distance * std::sin(angle));
        }
        angle += scan.angle_step_rad;
    }
    glEnd();
    glDisable(GL_BLEND);
}

void prepare_projection(double width_mm, double height_mm, int width, int height) {
    const auto aspect = static_cast<double>(width) / static_cast<double>(height);
    const auto half_width = width_mm / 2.0;
    const auto half_height = height_mm / 2.0;
    constexpr double margin = 1.10;
    const auto projected_width = std::max(half_width, half_height * aspect) * margin;
    const auto projected_height = projected_width / aspect;

    glViewport(0, 0, width, height);
    glClearColor(0.06F, 0.09F, 0.13F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(half_width - projected_width, half_width + projected_width,
            half_height - projected_height, half_height + projected_height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

} // namespace

void draw_warehouse(const world::Snapshot &view, robot::Pose estimated,
                    std::optional<robot::Pose> truth_overlay, const sensors::LidarScan *lidar,
                    int width, int height) {
    prepare_projection(view.floor.max.x, view.floor.max.y, width, height);

    draw_rect(view.floor, {28, 46, 61});
    for (const auto &zone : view.zones) {
        draw_rect(zone.bounds, zone_color(zone.kind));
    }
    for (const auto &solid : view.solids) {
        draw_rect(solid.bounds, solid_color(solid.kind));
    }
    if (lidar != nullptr) {
        draw_lidar(*lidar);
    }
    for (const auto &actor : view.actors) {
        draw_rect(actor.bounds, actor_color(actor.kind));
    }
    draw_robot(estimated, view.robot_radius_mm, {155, 242, 200}, false);
    if (truth_overlay) {
        draw_robot(*truth_overlay, view.robot_radius_mm, {110, 213, 255}, true);
    }
    draw_rect(view.floor, {103, 130, 148}, GL_LINE_LOOP);
}

void draw_occupancy(const mapping::Mapper &mapper, int width, int height) {
    const auto &grid = mapper.grid();
    const auto spec = grid.spec();
    prepare_projection(spec.width_mm, spec.height_mm, width, height);
    const RectMm bounds{
        {0, 0},
        {static_cast<std::int32_t>(spec.width_mm), static_cast<std::int32_t>(spec.height_mm)}};
    draw_rect(bounds, {31, 42, 57});
    glBegin(GL_QUADS);
    for (std::uint32_t y = 0; y < grid.rows(); ++y) {
        for (std::uint32_t x = 0; x < grid.columns(); ++x) {
            const auto occupancy = grid.occupancy({x, y});
            if (occupancy == mapping::Occupancy::unknown) {
                continue;
            }
            if (occupancy == mapping::Occupancy::free) {
                glColor3ub(72, 129, 133);
            } else {
                glColor3ub(246, 188, 105);
            }
            const auto left = x * spec.cell_mm;
            const auto bottom = y * spec.cell_mm;
            rect_vertices(
                {{static_cast<std::int32_t>(left), static_cast<std::int32_t>(bottom)},
                 {static_cast<std::int32_t>(std::min(left + spec.cell_mm, spec.width_mm)),
                  static_cast<std::int32_t>(std::min(bottom + spec.cell_mm, spec.height_mm))}});
        }
    }
    glEnd();
    if (const auto &estimate = mapper.estimate()) {
        const Color marker = estimate->reliable ? Color{155, 242, 200} : Color{223, 120, 111};
        draw_robot(estimate->pose, 240, marker, false);
    }
    draw_rect(bounds, {103, 130, 148}, GL_LINE_LOOP);
}

} // namespace argus::desktop
