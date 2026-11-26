#pragma once

#include "argus/simulation/robot.hpp"

#include <array>
#include <condition_variable>
#include <mutex>

namespace argus::simulation {

// Each participant owns its robot between calls. Both commands are required before either
// robot changes state. Closing releases a participant waiting for its peer.
class FleetClock {
  public:
    [[nodiscard]] bool step(RobotId id, RobotSimulation &robot, robot::Command command);
    void close() noexcept;

  private:
    struct Participant {
        RobotSimulation *robot{};
        robot::Command command{};
        bool waiting{};
    };

    [[nodiscard]] bool advance() noexcept;
    std::array<Participant, 2> members_{};
    std::mutex mutex_;
    std::condition_variable condition_;
    std::uint64_t generation_{};
    bool closed_{};
};

} // namespace argus::simulation
