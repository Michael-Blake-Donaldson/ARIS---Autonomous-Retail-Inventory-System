#include "argus/navigation/navigator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace argus::navigation {
namespace {

constexpr double pi = std::numbers::pi_v<double>;
constexpr std::size_t max_returns = 720;
constexpr std::size_t max_excluded = 64;
constexpr std::uint64_t map_interval = 10;
constexpr std::uint64_t retry_interval = 16;
constexpr std::uint64_t waypoint_timeout = 220;

Point position(robot::Pose pose) noexcept { return {pose.x.value(), pose.y.value()}; }

double distance(Point a, Point b) noexcept { return std::hypot(a.x_m - b.x_m, a.y_m - b.y_m); }

bool finite(robot::Pose pose) noexcept {
    return std::isfinite(pose.x.value()) && std::isfinite(pose.y.value()) &&
           std::isfinite(pose.heading.value());
}

bool safe_edge(const mapping::Grid &grid, const Costmap &map, Cell from, Cell to) noexcept {
    const auto free = [&](Cell at) {
        return map.traversable(at) && grid.occupancy(at) == mapping::Occupancy::free;
    };
    const auto dx = std::max(from.x, to.x) - std::min(from.x, to.x);
    const auto dy = std::max(from.y, to.y) - std::min(from.y, to.y);
    return dx <= 1 && dy <= 1 && free(to) && free({from.x, to.y}) && free({to.x, from.y});
}

bool safe_advance(const mapping::Grid &grid, const Costmap &map, Cell start, robot::Pose pose,
                  double speed) {
    const auto here = position(pose);
    const auto heading = pose.heading.value();
    const auto advance = 0.20 * speed + speed * speed / 3.6;
    const auto segments = static_cast<int>(std::ceil(advance * 2000.0 / grid.spec().cell_mm));
    const auto dx = advance * std::cos(heading);
    const auto dy = advance * std::sin(heading);
    for (int i = 1; i <= segments; ++i) {
        const double fraction = static_cast<double>(i) / segments;
        const auto next = grid.locate({here.x_m + fraction * dx, here.y_m + fraction * dy});
        if (!next || !safe_edge(grid, map, start, *next)) {
            return false;
        }
        start = *next;
    }
    return true;
}

} // namespace

std::string_view label(State state) noexcept {
    switch (state) {
    case State::waiting:
        return "waiting";
    case State::exploring:
        return "exploring";
    case State::yielding:
        return "yielding";
    case State::returning:
        return "returning";
    case State::finished:
        return "finished";
    }
    return "unknown";
}

std::string_view label(Reason reason) noexcept {
    switch (reason) {
    case Reason::none:
        return "none";
    case Reason::stale_scan:
        return "stale_scan";
    case Reason::uncertain_pose:
        return "uncertain_pose";
    case Reason::emergency_stop:
        return "emergency_stop";
    case Reason::depleted:
        return "depleted";
    case Reason::invalid_configuration:
        return "invalid_configuration";
    case Reason::unsafe_position:
        return "unsafe_position";
    case Reason::obstacle:
        return "obstacle";
    case Reason::no_frontiers:
        return "no_frontiers";
    case Reason::unreachable_frontiers:
        return "unreachable_frontiers";
    case Reason::home_unreachable:
        return "home_unreachable";
    case Reason::dock_unreachable:
        return "dock_unreachable";
    case Reason::home_reached:
        return "home_reached";
    case Reason::docked:
        return "docked";
    case Reason::destination_reached:
        return "destination_reached";
    case Reason::destination_unreachable:
        return "destination_unreachable";
    }
    return "unknown";
}

Navigator::Navigator(mapping::Spec spec, Settings settings, std::span<const NoGo> zones)
    : spec_{spec}, settings_{settings}, zones_{zones.begin(), zones.end()}, home_{settings.dock} {}

std::optional<Navigator> Navigator::create(const mapping::Grid &grid, Settings settings,
                                           std::span<const NoGo> zones) {
    if (settings.reserve_uj == 0 || settings.energy_per_meter_uj == 0 ||
        (settings.dock && !grid.locate(*settings.dock)) ||
        !Costmap::build(grid, settings.radius, zones)) {
        return std::nullopt;
    }
    return Navigator{grid.spec(), settings, zones};
}

robot::Command Navigator::stop(State state, Reason reason) noexcept {
    progress_.state = state;
    progress_.reason = reason;
    return {};
}

bool Navigator::set_destination(Destination destination) noexcept {
    const auto point = destination.point;
    if (returning_ ||
        (progress_.state == State::finished && progress_.reason != Reason::destination_reached &&
         progress_.reason != Reason::destination_unreachable) ||
        !(point.x_m >= 0.0 && point.x_m < spec_.width_mm / 1000.0 && point.y_m >= 0.0 &&
          point.y_m < spec_.height_mm / 1000.0 && std::abs(destination.heading.value()) <= pi)) {
        return false;
    }
    destination_ = destination;
    route_.reset();
    blocked_since_ = 0;
    return_wait_since_ = 0;
    destination_retries_ = 0;
    stop(State::waiting, Reason::none);
    return true;
}

bool Navigator::request_return() noexcept {
    if (!settings_.dock ||
        (progress_.state == State::finished && progress_.reason != Reason::destination_reached &&
         progress_.reason != Reason::destination_unreachable &&
         progress_.reason != Reason::docked)) {
        return false;
    }
    if (!returning_) {
        returning_ = true;
        route_.reset();
        blocked_since_ = 0;
        return_wait_since_ = 0;
        stop(State::returning, Reason::none);
    }
    return true;
}

bool Navigator::observe(const sensors::LidarScan &scan) {
    const auto &header = scan.header;
    constexpr auto max_tick = std::numeric_limits<std::uint64_t>::max();
    const auto stamp = header.sample_tick <= max_tick / robot::step_ms
                           ? header.sample_tick * robot::step_ms
                           : max_tick;
    if (header.sensor != sensors::SensorId::lidar ||
        header.provenance != sensors::Provenance::simulated || header.timestamp_ms != stamp ||
        !finite(header.estimated_pose) || !(std::abs(scan.first_angle_rad) <= pi) ||
        !(scan.angle_step_rad > 0.0 && scan.angle_step_rad <= pi / 36.0) ||
        !(scan.range_error_m >= 0.0 && scan.range_error_m <= 0.5) ||
        scan.ranges_m.size() > max_returns || scan.return_count == 0 ||
        scan.return_count > scan.ranges_m.size() ||
        std::abs(scan.angle_step_rad * static_cast<double>(scan.ranges_m.size()) - 2.0 * pi) >
            1e-9) {
        return false;
    }
    if (header.sample_tick == scan_tick_ && scan_tick_ != 0) {
        return true;
    }
    if (header.sample_tick <= scan_tick_ ||
        !std::ranges::all_of(scan.ranges_m, [](const auto &range) {
            return !range || (*range >= 0.0 && *range <= 50.0);
        })) {
        return false;
    }

    const auto origin = position(header.estimated_pose);
    if (!(std::abs(origin.x_m) <= 1000.0 && std::abs(origin.y_m) <= 1000.0)) {
        return false;
    }
    returns_.clear();
    std::vector<Detection> found;
    const auto elapsed = header.sample_tick - scan_tick_;
    const double seconds = static_cast<double>(std::min(elapsed, std::uint64_t{6})) / 20.0;
    std::optional<Point> first;
    Point last{};
    Point sum{};
    std::size_t count = 0;
    const auto close_cluster = [&] {
        if (count >= 2 && count <= 32 && first && distance(*first, last) <= 0.85) {
            const auto samples = static_cast<double>(count);
            const Point center{sum.x_m / samples, sum.y_m / samples};
            Point velocity{};
            bool confirmed = false;
            double best = 0.15 + 2.0 * seconds;
            for (const auto &previous : detections_) {
                const double offset = distance(previous.position, center);
                if (elapsed <= 6 && offset < best) {
                    best = offset;
                    const double speed = offset / seconds;
                    velocity = speed >= 0.15 && speed <= 2.0
                                   ? Point{(center.x_m - previous.position.x_m) / seconds,
                                           (center.y_m - previous.position.y_m) / seconds}
                                   : Point{};
                    confirmed = velocity.x_m * previous.velocity.x_m +
                                    velocity.y_m * previous.velocity.y_m >=
                                0.15 * 0.15;
                }
            }
            found.push_back({center, velocity, confirmed});
        }
        first.reset();
        count = 0;
        sum = {};
    };

    for (std::size_t i = 0; i < scan.ranges_m.size(); ++i) {
        const auto range = scan.ranges_m.at(i);
        if (!range) {
            close_cluster();
            continue;
        }
        const double angle = header.estimated_pose.heading.value() + scan.first_angle_rad +
                             scan.angle_step_rad * static_cast<double>(i);
        const double conservative = std::max(0.0, *range - scan.range_error_m);
        const Point point{origin.x_m + conservative * std::cos(angle),
                          origin.y_m + conservative * std::sin(angle)};
        returns_.push_back(point);
        if (first && distance(last, point) > 0.30) {
            close_cluster();
        }
        if (!first) {
            first = point;
        }
        last = point;
        sum.x_m += point.x_m;
        sum.y_m += point.y_m;
        ++count;
    }
    close_cluster();
    if (returns_.size() != scan.return_count) {
        returns_.clear();
        detections_.clear();
        scan_tick_ = 0;
        return false;
    }
    detections_.swap(found);
    scan_tick_ = header.sample_tick;
    return true;
}

bool Navigator::blocked(robot::Pose pose, double speed, bool &moving) const noexcept {
    const auto origin = position(pose);
    const auto c = std::cos(pose.heading.value());
    const auto s = std::sin(pose.heading.value());
    const double width = settings_.radius.value() + 0.08;
    const double length = width + 0.20 * speed + speed * speed / 3.6;
    const auto in_corridor = [&](Point point, double margin) {
        const double dx = point.x_m - origin.x_m;
        const double dy = point.y_m - origin.y_m;
        const double longitudinal = dx * c + dy * s;
        return longitudinal >= -margin && longitudinal <= length + margin &&
               std::abs(dy * c - dx * s) <= width + margin;
    };
    for (const auto &detection : detections_) {
        if (!detection.confirmed) {
            continue;
        }
        for (const double seconds : {0.0, 0.15, 0.30, 0.45, 0.60, 0.75, 0.90}) {
            if (in_corridor({detection.position.x_m + seconds * detection.velocity.x_m,
                             detection.position.y_m + seconds * detection.velocity.y_m},
                            0.20)) {
                moving = true;
                return true;
            }
        }
    }
    return std::ranges::any_of(returns_, [&](Point point) { return in_corridor(point, 0.0); });
}

std::uint32_t Navigator::budget(std::uint64_t energy) const noexcept {
    if (energy <= settings_.reserve_uj) {
        return 0;
    }
    const long double meters =
        static_cast<long double>(energy - settings_.reserve_uj) / settings_.energy_per_meter_uj;
    const long double units = meters * 10000.0L / spec_.cell_mm;
    return static_cast<std::uint32_t>(
        std::min(units, static_cast<long double>(std::numeric_limits<std::uint32_t>::max())));
}

robot::Command Navigator::follow(const mapping::Grid &grid, Cell start,
                                 const robot::Telemetry &telemetry, robot::Pose pose) {
    const auto tick = telemetry.sample_tick;
    const auto target_cell = route_->cells.at(waypoint_);
    auto target = grid.center(target_cell).value();
    const bool final = waypoint_ + 1 == route_->cells.size();
    if (returning_ && final) {
        target = *home_;
    } else if (destination_ && final) {
        target = destination_->point;
    }
    const auto here = position(pose);
    const double remaining = distance(here, target);
    const double tolerance = std::min(0.06, spec_.cell_mm / 5000.0);
    if (remaining <= tolerance) {
        blocked_since_ = 0;
        waypoint_since_ = tick;
        if (final) {
            if (returning_) {
                if (!settings_.dock || telemetry.docked) {
                    return stop(State::finished, telemetry.docked && settings_.dock
                                                     ? Reason::docked
                                                     : Reason::home_reached);
                }
                if (dock_arrival_ == 0) {
                    dock_arrival_ = tick;
                }
                return tick - dock_arrival_ >= retry_interval
                           ? stop(State::finished, Reason::dock_unreachable)
                           : stop(State::returning, Reason::none);
            }
            if (destination_) {
                if (telemetry.collision_blocked) {
                    return stop(State::yielding, Reason::obstacle);
                }
                progress_.state = State::exploring;
                progress_.reason = Reason::none;
                const auto turn =
                    std::remainder(destination_->heading.value() - pose.heading.value(), 2.0 * pi);
                if (std::abs(turn) <= 0.09) {
                    if (std::abs(telemetry.measured.linear.value()) <= 0.08 &&
                        std::abs(telemetry.measured.angular.value()) <= 0.15) {
                        return stop(State::finished, Reason::destination_reached);
                    }
                    return {};
                }
                return {.target = {MetersPerSecond{0},
                                   robot::RadiansPerSecond{std::clamp(turn * 3.0, -1.4, 1.4)}},
                        .deadman = true};
            }
            if (excluded_.size() == max_excluded) {
                return stop(State::finished, Reason::unreachable_frontiers);
            }
            excluded_.push_back(target_cell);
            route_.reset();
        } else {
            ++waypoint_;
        }
        return {};
    }
    const double angle = std::remainder(
        std::atan2(target.y_m - here.y_m, target.x_m - here.x_m) - pose.heading.value(), 2.0 * pi);
    const double linear = std::abs(angle) < 0.24 ? std::min(0.55, remaining * 3.0) : 0.0;
    const double speed = std::max(std::abs(telemetry.measured.linear.value()), linear);
    bool moving = false;
    const bool obstacle = (speed > 0.01 && blocked(telemetry.estimated_pose, speed, moving)) ||
                          telemetry.collision_blocked;
    if (obstacle) {
        last_obstacle_tick_ = tick;
    }
    const bool unsafe_edge = !safe_edge(grid, *costmap_, start, target_cell) ||
                             !safe_advance(grid, *costmap_, start, pose, speed);
    if (obstacle || unsafe_edge || tick - waypoint_since_ >= waypoint_timeout) {
        if (blocked_since_ == 0) {
            blocked_since_ = tick;
            ++progress_.yields;
            if (moving) {
                ++progress_.moving_yields;
            }
        }
        if (tick - blocked_since_ >= retry_interval || tick - waypoint_since_ >= waypoint_timeout) {
            ++progress_.replans;
            progress_.last_replan = obstacle      ? ReplanCause::obstacle
                                    : unsafe_edge ? ReplanCause::unsafe_route
                                                  : ReplanCause::waypoint_timeout;
            ++progress_.stuck;
            if (returning_) {
                if (++return_retries_ >= 3) {
                    return stop(State::finished, settings_.dock ? Reason::dock_unreachable
                                                                : Reason::home_unreachable);
                }
            } else if (destination_) {
                if (++destination_retries_ >= 3) {
                    return stop(State::finished, Reason::destination_unreachable);
                }
            } else {
                if (excluded_.size() == max_excluded) {
                    return stop(State::finished, Reason::unreachable_frontiers);
                }
                excluded_.push_back(route_->cells.back());
            }
            route_.reset();
            blocked_since_ = 0;
        }
        return stop(State::yielding, obstacle ? Reason::obstacle : Reason::unsafe_position);
    }
    if (last_obstacle_tick_ != 0 && tick - last_obstacle_tick_ < 6) {
        return stop(State::yielding, Reason::obstacle);
    }
    blocked_since_ = 0;
    progress_.state = returning_ ? State::returning : State::exploring;
    progress_.reason = Reason::none;
    return {.target = {MetersPerSecond{linear},
                       robot::RadiansPerSecond{std::clamp(angle * 3.0, -1.4, 1.4)}},
            .deadman = true};
}

robot::Command Navigator::update(const mapping::Mapper &mapper, robot::Telemetry telemetry,
                                 const sensors::LidarScan *scan) {
    if (progress_.state == State::finished) {
        return {};
    }
    const auto tick = telemetry.sample_tick;
    const auto &grid = mapper.grid();
    const auto &estimate = mapper.estimate();
    if (grid.spec() != spec_) {
        return stop(State::finished, Reason::invalid_configuration);
    }
    if (telemetry.emergency_latched || telemetry.depleted) {
        return stop(State::finished,
                    telemetry.depleted ? Reason::depleted : Reason::emergency_stop);
    }
    if (tick <= control_tick_) {
        return stop(State::waiting, Reason::uncertain_pose);
    }
    control_tick_ = tick;
    if (!estimate || !estimate->reliable || !finite(estimate->pose) ||
        !finite(telemetry.estimated_pose) || estimate->sample_tick > tick ||
        tick - estimate->sample_tick > max_pose_age_ticks ||
        !(std::abs(telemetry.measured.linear.value()) <= 5.0) ||
        !(std::abs(telemetry.measured.angular.value()) <= 10.0)) {
        return stop(State::waiting, Reason::uncertain_pose);
    }
    if (!telemetry.sensors_powered || !scan || scan->header.sample_tick > tick ||
        tick - scan->header.sample_tick > max_scan_age_ticks || !observe(*scan)) {
        return stop(State::waiting, Reason::stale_scan);
    }
    if (returning_ && settings_.dock && telemetry.docked) {
        return stop(State::finished, Reason::docked);
    }

    const auto here = position(estimate->pose);
    if (!home_) {
        home_ = here;
    }
    const auto start = grid.locate(here);
    const auto home = grid.locate(*home_);
    if (!costmap_ || tick - map_tick_ >= map_interval || !route_) {
        costmap_ = Costmap::build(grid, settings_.radius, zones_);
        map_tick_ = tick;
    }
    if (!start || grid.occupancy(*start) != mapping::Occupancy::free ||
        !costmap_->traversable(*start)) {
        if (retry_after_ == 0) {
            retry_after_ = tick;
        }
        if (tick - retry_after_ >= retry_interval) {
            return stop(State::finished, Reason::unsafe_position);
        }
        return stop(State::waiting, Reason::unsafe_position);
    }
    retry_after_ = 0;

    const auto available = budget(telemetry.energy_uj);
    if (!returning_ && (!route_ || tick == map_tick_ || telemetry.low_battery)) {
        const auto home_route = home ? plan(*costmap_, *start, *home) : std::nullopt;
        if (telemetry.low_battery || !home_route || available <= home_route->cost ||
            (route_ && available - home_route->cost < 2U * route_->cost)) {
            returning_ = true;
            route_.reset();
        }
    }
    if (!route_) {
        std::optional<Cell> goal = home;
        if (!returning_ && destination_) {
            goal = grid.locate(destination_->point);
        } else if (!returning_) {
            // First try with the full energy allowance, then decide whether lack of a
            // feasible outbound and return path calls for returning or ending exploration.
            const auto candidate =
                select_frontier(grid, *costmap_, *start, available, excluded_, home);
            goal = candidate.goal;
            if (!goal) {
                const auto any = select_frontier(
                    grid, *costmap_, *start, std::numeric_limits<std::uint32_t>::max(), excluded_);
                if (!any.goal) {
                    return stop(State::finished, any.inaccessible ? Reason::unreachable_frontiers
                                                                  : Reason::no_frontiers);
                }
                returning_ = true;
                goal = home;
            }
        }
        route_ = goal ? plan(*costmap_, *start, *goal) : std::nullopt;
        if (!route_) {
            if (!returning_ && !destination_) {
                return stop(State::finished, Reason::unreachable_frontiers);
            }
            if (return_wait_since_ == 0) {
                return_wait_since_ = tick;
            }
            const auto reason = !returning_      ? Reason::destination_unreachable
                                : settings_.dock ? Reason::dock_unreachable
                                                 : Reason::home_unreachable;
            return stop(tick - return_wait_since_ < retry_interval ? State::waiting
                                                                   : State::finished,
                        reason);
        }
        if (!returning_ && destination_) {
            const auto back = home ? plan(*costmap_, *goal, *home) : std::nullopt;
            if (!back || route_->cost > available || back->cost > available - route_->cost) {
                return stop(State::finished, Reason::destination_unreachable);
            }
        }
        return_wait_since_ = 0;
        waypoint_ = route_->cells.size() > 1 ? 1 : 0;
        waypoint_since_ = tick;
        ++progress_.plans;
    }
    return follow(grid, *start, telemetry, estimate->pose);
}

} // namespace argus::navigation
