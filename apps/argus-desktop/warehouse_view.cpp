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

void draw_rect(RectMm rect, Color color, GLenum style = GL_QUADS) {
    glColor3ubv(color.data());
    glBegin(style);
    glVertex2i(rect.min.x, rect.min.y);
    glVertex2i(rect.max.x, rect.min.y);
    glVertex2i(rect.max.x, rect.max.y);
    glVertex2i(rect.min.x, rect.max.y);
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

} // namespace

void draw_warehouse(const world::Snapshot &view, robot::Pose estimated,
                    std::optional<robot::Pose> truth_overlay, int width, int height) {
    const auto aspect = static_cast<double>(width) / static_cast<double>(height);
    const auto half_width = static_cast<double>(view.floor.max.x) / 2.0;
    const auto half_height = static_cast<double>(view.floor.max.y) / 2.0;
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

    draw_rect(view.floor, {28, 46, 61});
    for (const auto &zone : view.zones) {
        draw_rect(zone.bounds, zone_color(zone.kind));
    }
    for (const auto &solid : view.solids) {
        draw_rect(solid.bounds, solid_color(solid.kind));
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

} // namespace argus::desktop
