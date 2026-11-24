#include "argus/world/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace argus::world {
namespace {

bool segment_hits_box(PrecisePointMm from, PrecisePointMm to, PrecisePointMm low,
                      PrecisePointMm high) noexcept {
    double first = 0.0;
    double last = 1.0;
    const auto clip = [&first, &last](double start, double end, double min, double max) {
        const auto delta = end - start;
        if (delta == 0.0) {
            return start >= min && start <= max;
        }
        const auto a = (min - start) / delta;
        const auto b = (max - start) / delta;
        first = std::max(first, std::min(a, b));
        last = std::min(last, std::max(a, b));
        return first <= last;
    };
    return clip(from.x, to.x, low.x, high.x) && clip(from.y, to.y, low.y, high.y);
}

bool segment_hits_corner(PrecisePointMm from, PrecisePointMm to, double x, double y,
                         double radius) noexcept {
    const auto dx = to.x - from.x;
    const auto dy = to.y - from.y;
    const auto length_squared = dx * dx + dy * dy;
    const auto fraction =
        length_squared > 0.0
            ? std::clamp(((x - from.x) * dx + (y - from.y) * dy) / length_squared, 0.0, 1.0)
            : 0.0;
    const auto offset_x = from.x + fraction * dx - x;
    const auto offset_y = from.y + fraction * dy - y;
    return offset_x * offset_x + offset_y * offset_y <= radius * radius;
}

} // namespace

bool overlaps(RectMm a, RectMm b) noexcept {
    return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y;
}

bool interiors_overlap(RectMm a, RectMm b) noexcept {
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
}

bool contains(RectMm outer, RectMm inner) noexcept {
    return outer.min.x <= inner.min.x && outer.min.y <= inner.min.y && outer.max.x >= inner.max.x &&
           outer.max.y >= inner.max.y;
}

bool touches_disk(RectMm rect, PointMm center, std::int32_t radius_mm) noexcept {
    if (radius_mm < 0 || rect.min.x > rect.max.x || rect.min.y > rect.max.y) {
        return false;
    }
    const auto nearest_x = std::clamp(center.x, rect.min.x, rect.max.x);
    const auto nearest_y = std::clamp(center.y, rect.min.y, rect.max.y);
    const auto dx = static_cast<std::int64_t>(center.x) - nearest_x;
    const auto dy = static_cast<std::int64_t>(center.y) - nearest_y;
    const auto radius = static_cast<std::int64_t>(radius_mm);
    // Reject distant coordinates before squaring. The surviving sum fits signed 64 bits.
    if (dx < -radius || dx > radius || dy < -radius || dy > radius) {
        return false;
    }
    return dx * dx + dy * dy <= radius * radius;
}

bool touches_swept_disk(RectMm rect, PrecisePointMm from, PrecisePointMm to,
                        double radius) noexcept {
    if (!(radius >= 0.0 && std::isfinite(radius)) || rect.min.x > rect.max.x ||
        rect.min.y > rect.max.y) {
        return false;
    }
    const auto left = static_cast<double>(rect.min.x);
    const auto right = static_cast<double>(rect.max.x);
    const auto bottom = static_cast<double>(rect.min.y);
    const auto top = static_cast<double>(rect.max.y);
    // Two strips and the four corner disks make up the rectangle expanded by a Euclidean disk.
    return segment_hits_box(from, to, {left - radius, bottom}, {right + radius, top}) ||
           segment_hits_box(from, to, {left, bottom - radius}, {right, top + radius}) ||
           segment_hits_corner(from, to, left, bottom, radius) ||
           segment_hits_corner(from, to, left, top, radius) ||
           segment_hits_corner(from, to, right, bottom, radius) ||
           segment_hits_corner(from, to, right, top, radius);
}

} // namespace argus::world
