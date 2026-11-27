#pragma once

#include <cstdint>

namespace argus::world {

struct PointMm {
    std::int32_t x;
    std::int32_t y;
    bool operator==(const PointMm &) const = default;
};

struct PrecisePointMm {
    double x;
    double y;
};

// Minimum and maximum corners in the floor plane. Positive Y points north.
struct RectMm {
    PointMm min;
    PointMm max;
    bool operator==(const RectMm &) const = default;
};

[[nodiscard]] bool overlaps(RectMm left, RectMm right) noexcept;
[[nodiscard]] bool interiors_overlap(RectMm left, RectMm right) noexcept;
[[nodiscard]] bool contains(RectMm outer, RectMm inner) noexcept;
[[nodiscard]] bool touches_disk(RectMm rect, PointMm center, std::int32_t radius_mm) noexcept;
[[nodiscard]] bool touches_swept_disk(RectMm rect, PrecisePointMm from, PrecisePointMm to,
                                      double radius_mm) noexcept;

} // namespace argus::world
