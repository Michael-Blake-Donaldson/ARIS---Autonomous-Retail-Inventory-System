#pragma once
#include "argus/core/types.hpp"
#include <array>
#include <mutex>

namespace argus::navigation {

struct TrafficSpan {
    Meters west;
    Meters east;
};

// The two claims describe planned operations and contain no simulated truth.
class AisleAccess {
  public:
    [[nodiscard]] bool request(RobotId robot, TrafficSpan span);
    [[nodiscard]] bool request_clearance(RobotId robot);
    [[nodiscard]] bool admit(RobotId robot);
    void finish(RobotId robot, bool parked);
    [[nodiscard]] bool retained(RobotId robot) const;
    [[nodiscard]] bool working() const;

  private:
    enum class State { free, pending, granted, held };
    struct Claim {
        TrafficSpan span{Meters{0}, Meters{0}};
        std::uint64_t order{};
        State state{};
    };
    std::array<Claim, 2> claims_{};
    mutable std::mutex mutex_;
    std::uint64_t sequence_{};
};

} // namespace argus::navigation
