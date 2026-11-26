#include "argus/navigation/traffic.hpp"

#include <algorithm>
#include <limits>

namespace argus::navigation {
namespace {

bool valid(RobotId id) noexcept { return id.value() >= 1 && id.value() <= 2; }
std::size_t index(RobotId id) noexcept { return static_cast<std::size_t>(id.value() - 1); }

bool overlaps(TrafficSpan a, TrafficSpan b) noexcept {
    return a.west <= b.east && b.west <= a.east;
}

} // namespace

bool AisleAccess::request(RobotId robot, TrafficSpan span) {
    if (!valid(robot) ||
        !(span.west.value() >= -1000.0 && span.east.value() <= 1000.0 && span.west < span.east)) {
        return false;
    }
    std::scoped_lock lock{mutex_};
    auto &claim = claims_.at(index(robot));
    if (claim.state != State::free || sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    claim = {span, ++sequence_, State::pending};
    return true;
}

bool AisleAccess::request_clearance(RobotId robot) {
    if (!valid(robot)) {
        return false;
    }
    std::scoped_lock lock{mutex_};
    auto &claim = claims_.at(index(robot));
    if (claim.state != State::held) {
        return false;
    }
    claim.state = State::granted;
    return true;
}

bool AisleAccess::admit(RobotId robot) {
    if (!valid(robot)) {
        return false;
    }
    std::scoped_lock lock{mutex_};
    auto &claim = claims_.at(index(robot));
    const auto &other = claims_.at(1 - index(robot));
    if (claim.state == State::granted) {
        return true;
    }
    if (claim.state != State::pending ||
        (other.state != State::free && overlaps(claim.span, other.span) &&
         (other.state != State::pending || other.order < claim.order))) {
        return false;
    }
    claim.state = State::granted;
    return true;
}

void AisleAccess::finish(RobotId robot, bool parked) {
    if (valid(robot)) {
        std::scoped_lock lock{mutex_};
        auto &claim = claims_.at(index(robot));
        if (claim.state == State::pending || (claim.state == State::granted && parked)) {
            claim = {};
        } else if (claim.state == State::granted) {
            claim.state = State::held;
        }
    }
}

bool AisleAccess::retained(RobotId robot) const {
    if (!valid(robot)) {
        return false;
    }
    std::scoped_lock lock{mutex_};
    return claims_.at(index(robot)).state == State::held;
}

bool AisleAccess::working() const {
    std::scoped_lock lock{mutex_};
    return std::ranges::any_of(claims_, [](const auto &claim) {
        return claim.state == State::pending || claim.state == State::granted;
    });
}

} // namespace argus::navigation
