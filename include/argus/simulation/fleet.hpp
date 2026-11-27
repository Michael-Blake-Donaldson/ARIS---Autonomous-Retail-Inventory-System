#pragma once

#include "argus/simulation/robot.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace argus::simulation {

enum class FleetStop { running, closed, invalid_pair, peer_timeout };

// Each participant owns its robot between calls. Both commands are required before either
// robot changes state. Closing releases a participant waiting for its peer.
class FleetClock {
  public:
    explicit FleetClock(std::chrono::milliseconds peer_timeout = std::chrono::seconds{5}) noexcept;
    [[nodiscard]] bool step(RobotId id, RobotSimulation &robot, robot::Command command);
    void close() noexcept;
    [[nodiscard]] FleetStop stop_reason() const noexcept;

  private:
    struct Participant {
        RobotSimulation *robot{};
        robot::Command command{};
        bool waiting{};
    };

    [[nodiscard]] bool advance() noexcept;
    // Requires mutex_.
    void stop(FleetStop reason) noexcept;
    std::array<Participant, 2> members_{};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    const std::chrono::milliseconds peer_timeout_;
    std::chrono::steady_clock::time_point deadline_{};
    std::uint64_t generation_{};
    FleetStop reason_{FleetStop::running};
};

} // namespace argus::simulation
