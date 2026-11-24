#pragma once

#include "argus/mapping/grid.hpp"
#include "argus/sensors/observations.hpp"

#include <optional>

namespace argus::mapping {

struct Limits {
    double distance_error_m{4.0};
    double heading_error_rad{24.0};
};

struct Estimate {
    robot::Pose pose;
    double distance_error_m;
    double heading_error_rad;
    std::uint64_t sample_tick;
    bool reliable;
};

enum class Update { accepted, unrelated, invalid, missing_pose, suspended };

class Mapper {
  public:
    [[nodiscard]] static Result<Mapper, MapError> create(Spec spec, Limits limits = {});

    Update ingest(const sensors::Observation &observation) noexcept;
    [[nodiscard]] const Grid &grid() const noexcept { return grid_; }
    [[nodiscard]] const std::optional<Estimate> &estimate() const noexcept { return estimate_; }

    // The caller supplies an independently established pose. Old map evidence is invalidated.
    bool reanchor(robot::Pose pose, double distance_error_m, double heading_error_rad) noexcept;
    void reset() noexcept;

  private:
    struct Anchor {
        robot::Pose measured;
        robot::Pose reference;
        double measured_distance_error;
        double measured_heading_error;
        double reference_distance_error;
        double reference_heading_error;
    };

    Mapper(Grid grid, Limits limits);
    Update ingest_wheel(const sensors::WheelSample &sample) noexcept;
    Update ingest_lidar(const sensors::LidarScan &scan) noexcept;

    Grid grid_;
    Limits limits_;
    std::optional<sensors::WheelSample> wheel_;
    std::optional<Estimate> estimate_;
    std::optional<Anchor> anchor_;
    std::uint64_t last_scan_{};
};

} // namespace argus::mapping
