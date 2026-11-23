#include "argus/world/geometry.hpp"

#include <algorithm>
#include <cstdint>

namespace argus::world {

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

} // namespace argus::world
