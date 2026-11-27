#pragma once

#include "argus/simulation/warehouse.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace argus::simulation {

struct TagTruth {
    std::string epc;
    world::PointMm position;
    std::int32_t height_mm;
    std::int32_t heading_deg{};
    bool operator==(const TagTruth &) const = default;
};

// Simulation-side capability for constructing measurements. Keep it out of autonomy and UI.
class SensorQuery {
  public:
    explicit SensorQuery(Warehouse warehouse);

    [[nodiscard]] world::RectMm floor() const noexcept;
    [[nodiscard]] std::vector<world::RectMm> obstacles(world::RectMm region,
                                                       std::uint64_t tick) const;
    [[nodiscard]] std::vector<TagTruth> tags(world::RectMm region) const;
    // Visits matching tags in scenario order. References are valid for the callback only.
    void visit_tags(world::RectMm region, const std::function<void(const TagTruth &)> &visit) const;

  private:
    Warehouse warehouse_;
};

} // namespace argus::simulation
