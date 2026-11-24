#include "argus/mission/simulation.hpp"

#include "argus/audit/mission.hpp"
#include "argus/simulation/fixtures.hpp"
#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>
#include <set>
#include <utility>

namespace argus::mission {
namespace {

constexpr navigation::Point dock{9.875, 1.625};
constexpr std::uint64_t energy_per_meter_uj = 80000000;
constexpr double antenna_forward = 0.18;
constexpr double antenna_yaw = 0.2;
constexpr double range = 3.5;
constexpr std::uint64_t tick_energy_uj = 2500000;
constexpr std::uint64_t step_ms = robot::step_ms;
constexpr std::size_t frames_needed = 8;

template <typename T, typename E> T take(Result<T, E> result) {
    if (auto *value = std::get_if<T>(&result)) {
        return std::move(*value);
    }
    if constexpr (std::is_same_v<E, Error>) {
        throw std::get<Error>(result);
    } else {
        throw Error::invalid;
    }
}

struct Plan {
    std::vector<std::size_t> views;
    std::set<std::string, std::less<>> tags;
};

Plan schedule(const Intent &intent, inventory::Repository *catalog) {
    const auto sites = audit::demo_sites();
    Plan plan;
    if (intent.workflow == Workflow::dock) {
        return plan;
    }
    if (intent.workflow == Workflow::inspect) {
        if (std::ranges::find(sites, *intent.aisle, &audit::Site::aisle) == sites.end()) {
            throw Error::invalid;
        }
        return plan;
    }
    std::array<std::size_t, audit::max_sites> counts{};
    const auto bump = [&](const inventory::AisleId &aisle, std::size_t amount) {
        for (std::size_t i = 0; i < sites.size(); ++i) {
            if (sites[i].aisle == aisle) {
                counts.at(i) += amount;
            }
        }
    };
    if (intent.workflow == Workflow::find_item) {
        if (!catalog) {
            throw Error::invalid;
        }
        std::optional<inventory::Epc> after;
        while (true) {
            const auto page = take(catalog->find(*intent.product, after));
            if (page.units.size() > audit::max_tags - plan.tags.size()) {
                throw Error::full;
            }
            for (const auto &unit : page.units) {
                plan.tags.insert(unit.epc.value);
                bump(take(catalog->assignment(unit.epc)).aisle, 1);
            }
            if (!page.has_more) {
                break;
            }
            after = page.units.back().epc;
        }
        if (plan.tags.empty()) {
            throw Error::not_found;
        }
        if (intent.aisle) {
            if (std::ranges::find(sites, *intent.aisle, &audit::Site::aisle) == sites.end()) {
                throw Error::invalid;
            }
            bump(*intent.aisle, audit::max_tags + 1);
        }
    }
    std::vector<std::size_t> order(sites.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, [&](auto a, auto b) { return counts.at(a) > counts.at(b); });
    for (std::size_t pass = 0; pass < 2; ++pass) {
        for (const auto site : order) {
            plan.views.push_back(site * 2 + pass);
        }
    }
    return plan;
}

class RobotRun {
  public:
    RobotRun(simulation::RobotSimulation &robot, simulation::SensorRig &rig,
             mapping::Mapper &mapper, navigation::Navigator &navigator, Meters radius)
        : robot_{robot}, rig_{rig}, mapper_{mapper}, navigator_{navigator}, radius_{radius} {}

    void step(robot::Command command) {
        robot_.step(command);
        rig_.capture(robot_);
        rfid_.reset();
        while (auto reading = rig_.pop()) {
            const auto accepted = mapper_.ingest(*reading);
            if (auto *scan = std::get_if<sensors::LidarScan>(&*reading);
                scan && accepted == mapping::Update::accepted) {
                lidar_ = std::move(*scan);
            }
            if (auto *scan = std::get_if<sensors::RfidScan>(&*reading)) {
                rfid_ = std::move(*scan);
            }
        }
    }

    [[nodiscard]] robot::Telemetry telemetry() const noexcept { return robot_.telemetry(); }
    void brake() noexcept { robot_.step({}); }
    [[nodiscard]] const std::optional<mapping::Estimate> &estimate() const noexcept {
        return mapper_.estimate();
    }

    [[nodiscard]] std::optional<std::uint64_t> distance_to(navigation::Point target) const {
        if (!estimate() || !estimate()->reliable) {
            return std::nullopt;
        }
        const auto &grid = mapper_.grid();
        const auto &pose = estimate()->pose;
        const auto start = grid.locate({pose.x.value(), pose.y.value()});
        const auto goal = grid.locate(target);
        auto costmap = navigation::Costmap::build(grid, radius_);
        if (!start || !goal || !costmap) {
            return std::nullopt;
        }
        const auto path = navigation::plan(*costmap, *start, *goal);
        return path ? std::optional{static_cast<std::uint64_t>(path->cost) * grid.spec().cell_mm /
                                    10}
                    : std::nullopt;
    }

    [[nodiscard]] Readiness readiness(bool finished) const {
        const auto state = telemetry();
        const auto returning = distance_to(dock);
        const bool localized =
            estimate() && estimate()->reliable && estimate()->sample_tick == state.sample_tick;
        return {state.energy_uj,
                returning ? std::optional{*returning * (energy_per_meter_uj / 1000)} : std::nullopt,
                tick_energy_uj,
                localized,
                state.sensors_powered && lidar_ &&
                    state.sample_tick - lidar_->header.sample_tick <= 3,
                state.collision_blocked,
                state.low_battery,
                state.docked,
                finished};
    }

    [[nodiscard]] robot::Command steer() {
        return navigator_.update(mapper_, telemetry(), lidar_ ? &*lidar_ : nullptr);
    }

    [[nodiscard]] bool set(navigation::Destination destination) noexcept {
        return navigator_.set_destination(destination);
    }
    [[nodiscard]] bool return_to_dock() noexcept { return navigator_.request_return(); }
    [[nodiscard]] navigation::Progress progress() const noexcept { return navigator_.progress(); }

    bool capture(std::size_t view, const navigation::Destination &goal, Checkpoint &checkpoint) {
        if (!rfid_ || !estimate() || !estimate()->reliable ||
            rfid_->header.sample_tick != estimate()->sample_tick) {
            return false;
        }
        const auto &anchor = *estimate();
        const auto &pose = anchor.pose;
        const double yaw = pose.heading.value();
        constexpr double turn = 2.0 * std::numbers::pi;
        if (std::hypot(pose.x.value() - goal.point.x_m, pose.y.value() - goal.point.y_m) > 0.11 ||
            std::abs(std::remainder(yaw - goal.heading.value(), turn)) > 0.16) {
            return false;
        }
        audit::Footprint footprint{{pose.x.value() + antenna_forward * std::cos(yaw),
                                    pose.y.value() + antenna_forward * std::sin(yaw)},
                                   robot::Radians{std::remainder(yaw + antenna_yaw, turn)},
                                   Meters{range},
                                   Meters{anchor.distance_error_m},
                                   robot::Radians{anchor.heading_error_rad}};
        take(evidence_.ingest(*rfid_, footprint, view));
        checkpoint.raw_reads += rfid_->reads.size();
        return rfid_->truncated_reads == 0;
    }

    [[nodiscard]] const audit::Evidence &evidence() const noexcept { return evidence_; }

  private:
    simulation::RobotSimulation &robot_;
    simulation::SensorRig &rig_;
    mapping::Mapper &mapper_;
    navigation::Navigator &navigator_;
    Meters radius_;
    audit::Evidence evidence_;
    std::optional<sensors::LidarScan> lidar_;
    std::optional<sensors::RfidScan> rfid_;
};

struct Work {
    std::optional<std::size_t> active;
    std::size_t frames{};
    std::size_t dwell_ticks{};
};

std::optional<Cause> run_work(RobotRun &robot, const Plan &plan, Work &work, Checkpoint &point,
                              robot::Command &command, bool &save) {
    const auto missing = std::ranges::find_if(plan.views, [&](auto view) {
        return (point.covered_mask & (std::uint64_t{1} << view)) == 0;
    });
    if (missing == plan.views.end()) {
        return std::nullopt;
    }
    const auto view = *missing;
    const auto target = audit::demo_sites()[view / 2].views.at(view % 2);
    if (work.active != view) {
        const auto distance = robot.distance_to(target.point);
        if (!distance || !robot.set(target)) {
            return Cause::work_unavailable;
        }
        point.planned_mm += *distance;
        work = {view, 0, 0};
        save = true;
    }
    command = robot.steer();
    const auto position = robot.progress();
    if (position.state != navigation::State::finished) {
        return std::nullopt;
    }
    if (position.reason != navigation::Reason::destination_reached) {
        return Cause::work_unavailable;
    }
    if (++work.dwell_ticks > 48) {
        return Cause::sensor_degraded;
    }
    work.frames += static_cast<std::size_t>(robot.capture(view, target, point));
    if (work.frames >= frames_needed) {
        point.covered_mask |= std::uint64_t{1} << view;
        ++point.covered_scans;
        work = {};
        save = true;
    }
    return std::nullopt;
}

DemoResult execute(Store &store, RobotRun &robot, Record record, const Plan &plan,
                   std::uint32_t max_ticks) {
    struct BrakeOnExit {
        RobotRun &robot;
        ~BrakeOnExit() { robot.brake(); }
    } brake{robot};
    Checkpoint point = record.checkpoint;
    point.planned_scans = plan.views.size();
    Work work;
    robot::Command command;
    bool return_planned = false;
    bool reserve_before_low = false;
    const auto save = [&] {
        ++point.sample;
        record = take(store.checkpoint(record.lease(), point));
    };
    for (std::uint32_t tick = 0; tick < max_ticks; ++tick) {
        robot.step(command);
        command = {};
        point.elapsed_ms += step_ms;
        point.actual_mm += static_cast<std::uint64_t>(
            std::lround(std::abs(robot.telemetry().measured.linear.value()) * step_ms));
        const bool covered = !plan.views.empty() && point.planned_scans == point.covered_scans;
        if (covered && record.state == State::executing && !plan.tags.empty()) {
            const auto missing = std::ranges::count_if(plan.tags, [&](const auto &tag) {
                const auto found = robot.evidence().tags().find(tag);
                return found == robot.evidence().tags().end() ||
                       !audit::attribute(found->second, audit::demo_sites()).supported;
            });
            point.exceptions = std::max(point.exceptions, static_cast<std::uint64_t>(missing));
        }
        auto ready = robot.readiness(covered);
        const auto progress_before = robot.progress();
        if (record.state != State::returning && work.active &&
            progress_before.state == navigation::State::yielding &&
            progress_before.reason == navigation::Reason::obstacle) {
            ready.obstructed = true;
        }
        if (tick % 8 == 0 || covered || ready.docked ||
            point.elapsed_ms >= record.intent.limit_ms) {
            save();
        }
        auto cycle = take(store.cycle(record.lease(), ready));
        record = std::move(cycle.record);
        if (record.state == State::returning && record.cause == Cause::reserve &&
            !ready.low_battery && record.intent.workflow != Workflow::dock && !return_planned) {
            reserve_before_low = true;
        }
        if (terminal(record.state) || record.state == State::paused ||
            (record.state == State::executing && record.intent.workflow == Workflow::inspect)) {
            break;
        }
        if (record.state == State::blocked && work.active && ready.localized && ready.sensors_ok &&
            ready.return_energy_uj) {
            static_cast<void>(robot.steer());
        } else if (cycle.motion == Motion::work) {
            bool checkpoint_needed = false;
            if (const auto fault = run_work(robot, plan, work, point, command, checkpoint_needed)) {
                point.exceptions++;
                save();
                if (*fault == Cause::sensor_degraded) {
                    auto degraded = ready;
                    degraded.sensors_ok = false;
                    record = take(store.cycle(record.lease(), degraded)).record;
                } else {
                    record = take(store.abort(record.lease(), *fault));
                }
                break;
            }
            if (checkpoint_needed) {
                save();
            }
        } else if (cycle.motion == Motion::return_to_dock) {
            if (!return_planned) {
                const auto distance = robot.distance_to(dock);
                if (!distance || !robot.return_to_dock()) {
                    record = take(store.abort(record.lease(), Cause::return_unavailable));
                    break;
                }
                point.planned_mm += *distance;
                return_planned = true;
                save();
            }
            command = robot.steer();
            const auto progress = robot.progress();
            if (progress.state == navigation::State::finished &&
                progress.reason != navigation::Reason::docked) {
                record = take(store.abort(record.lease(), Cause::return_unavailable));
                break;
            }
        }
    }
    if (!terminal(record.state) && record.state != State::paused &&
        record.checkpoint.elapsed_ms < point.elapsed_ms) {
        save();
    }
    const auto telemetry = robot.telemetry();
    return {record, telemetry.energy_uj, telemetry.docked, reserve_before_low};
}

} // namespace

Result<DemoResult, Error> run_demo(Store &store, inventory::Repository *catalog,
                                   DemoOptions options) {
    if (options.ticks == 0 || options.ticks > 2400) {
        return Error::invalid;
    }
    try {
        const auto scenario =
            take(simulation::Warehouse::from_json(take(simulation::fixture_json("rfid-audit"))));
        const auto declared = scenario.snapshot(0);
        const Meters radius{declared.robot_radius_mm / 1000.0};
        simulation::BatteryConfig battery;
        if (options.reserve_test) {
            battery.initial_uj = 690000000;
            battery.low_threshold_uj = 100000000;
        }
        auto robot = take(simulation::RobotSimulation::create(scenario, 0, {}, battery));
        simulation::SensorConfig sensors;
        sensors.rfid.range_m = range;
        sensors.rfid.antenna_yaw_rad = antenna_yaw;
        sensors.rfid.antenna_forward_m = antenna_forward;
        auto rig = take(simulation::SensorRig::create(declared.seed, sensors));
        auto mapper = take(mapping::Mapper::create(
            {declared.scenario_id, static_cast<std::uint32_t>(declared.floor.max.x),
             static_cast<std::uint32_t>(declared.floor.max.y), 250}));
        auto navigator =
            navigation::Navigator::create(mapper.grid(), {.radius = radius, .dock = dock});
        if (!navigator) {
            return Error::invalid;
        }
        RobotRun run{robot, rig, mapper, *navigator, radius};
        if (options.resume) {
            // A newly started synthetic fixture supplies a fresh observed launch pose.
            run.step({});
            const auto &anchor = run.estimate();
            if (!anchor) {
                return Error::invalid;
            }
            take(store.resume(*options.resume, {anchor->sample_tick, anchor->distance_error_m,
                                                anchor->heading_error_rad, anchor->reliable}));
        }
        auto record = take(store.dispatch());
        if (!record) {
            return DemoResult{};
        }
        Plan plan;
        try {
            plan = schedule(record->intent, catalog);
        } catch (Error) {
            return DemoResult{take(store.abort(record->lease(), Cause::work_unavailable)),
                              robot.telemetry().energy_uj};
        }
        return execute(store, run, std::move(*record), plan, options.ticks);
    } catch (Error error) {
        return error;
    }
}

} // namespace argus::mission
