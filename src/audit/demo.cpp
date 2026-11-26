#include "argus/audit/mission.hpp"

#include "argus/simulation/fixtures.hpp"
#include "argus/simulation/observed_run.hpp"
#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <set>
#include <utility>

namespace argus::audit {
namespace {

using inventory::Assignment;
using inventory::Error;
using inventory::Repository;
using inventory::SessionId;

constexpr std::size_t frames_per_view = 14;
constexpr std::size_t max_view_ticks = 430;
constexpr std::uint32_t max_mission_ticks = 1800;
constexpr double antenna_range = 3.5;
constexpr double antenna_yaw = 0.2;
constexpr double antenna_forward = 0.18;
constexpr double full_turn = 2.0 * std::numbers::pi;

const std::array<Site, 2> sites{
    {{AisleId{"SOFTLINES"},
      {1.5, 4.35},
      {4.4, 7.4},
      {{{{3.875, 4.125}, robot::Radians{1.98}}, {{4.375, 5.625}, robot::Radians{2.94}}}}},
     {AisleId{"TECH"},
      {7.1, 4.35},
      {9.7, 7.4},
      {{{{6.375, 4.125}, robot::Radians{0.58}}, {{6.875, 5.625}, robot::Radians{0.08}}}}}}};

struct Scope {
    std::vector<Assignment> targets;
    std::map<std::string, Quantity, std::less<>> quantities;
};

template <typename T> T take(Result<T, Error> result) {
    if (const auto *error = std::get_if<Error>(&result)) {
        throw *error;
    }
    return std::get<T>(std::move(result));
}

template <typename T, typename E> T configured(Result<T, E> result) {
    if (auto *value = std::get_if<T>(&result)) {
        return std::move(*value);
    }
    throw Error::invalid_data;
}

bool configured_aisle(const AisleId &aisle) {
    return std::ranges::find(sites, aisle, &Site::aisle) != sites.end();
}

Scope load_scope(Repository &repository, const Request &request) {
    Scope scope;
    std::optional<Epc> after;
    if (const auto *find = std::get_if<FindItem>(&request)) {
        if (find->last_known && !configured_aisle(*find->last_known)) {
            throw Error::invalid_data;
        }
        while (true) {
            const auto page = take(repository.find(find->product, after));
            if (page.units.size() > max_tags - scope.targets.size()) {
                throw Error::too_large;
            }
            for (const auto &unit : page.units) {
                scope.targets.push_back(take(repository.assignment(unit.epc)));
            }
            if (!page.has_more) {
                break;
            }
            after = page.units.back().epc;
        }
    } else {
        const auto &aisle = std::get<AuditAisle>(request).aisle;
        if (!configured_aisle(aisle)) {
            throw Error::invalid_data;
        }
        while (true) {
            auto page = take(repository.assigned_at(aisle, after));
            if (page.size() > max_tags - scope.targets.size()) {
                throw Error::too_large;
            }
            const bool finished = page.size() < inventory::page_size;
            if (!finished) {
                after = page.back().unit.epc;
            }
            for (auto &entry : page) {
                scope.targets.push_back(std::move(entry));
            }
            if (finished) {
                break;
            }
        }
        std::optional<inventory::ExpectedStock> stock_after;
        std::size_t stock_count = 0;
        while (true) {
            auto page = take(repository.expected_at(aisle, stock_after));
            if (page.size() > max_tags - stock_count) {
                throw Error::too_large;
            }
            stock_count += page.size();
            for (const auto &row : page) {
                auto &total = scope.quantities.try_emplace(row.product.value, Quantity{row.product})
                                  .first->second;
                total.expected += row.quantity;
            }
            if (page.size() < inventory::page_size) {
                break;
            }
            stock_after = std::move(page.back());
        }
    }
    return scope;
}

std::vector<std::size_t> priorities(const Scope &scope, const Request &request,
                                    const Evidence &evidence, bool second) {
    std::array<std::size_t, sites.size()> counts{};
    const auto bump = [&](const AisleId &aisle, std::size_t weight) {
        for (std::size_t i = 0; i < sites.size(); ++i) {
            if (sites.at(i).aisle == aisle) {
                counts.at(i) += weight;
            }
        }
    };
    const auto *find = std::get_if<FindItem>(&request);
    if (!second) {
        if (find && find->last_known) {
            bump(*find->last_known, max_tags * 2);
        } else if (const auto *audit = std::get_if<AuditAisle>(&request)) {
            bump(audit->aisle, max_tags * 2);
        }
    }
    for (const auto &target : scope.targets) {
        if (second) {
            const auto detected = evidence.tags().find(target.unit.epc.value);
            if (detected != evidence.tags().end()) {
                const auto inferred = attribute(detected->second, sites);
                if (inferred.supported) {
                    continue;
                }
                // A competing aisle gets its own alternate view even for a different assignment.
                for (const auto &view : detected->second.views) {
                    counts.at(view.view / 2) += 2;
                }
            } else {
                // An unseen EPC can have crossed an aisle. Check the assigned aisle first.
                for (auto &count : counts) {
                    ++count;
                }
            }
        }
        bump(target.aisle, 3);
    }
    std::vector<std::size_t> order(sites.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, [&](auto a, auto b) { return counts.at(a) > counts.at(b); });
    if (second) {
        std::erase_if(order, [&](auto i) { return counts.at(i) == 0; });
    }
    return order;
}

class Survey {
  public:
    Survey(simulation::ObservedRun &run, navigation::Navigator &navigator, Repository &repository,
           SessionId session)
        : run_{run}, navigator_{navigator}, repository_{repository}, session_{session} {}

    Visit investigate(std::size_t site_index, std::size_t pass) {
        const auto &site = sites.at(site_index);
        const auto &destination = site.views.at(pass);
        Visit visit{site.aisle, pass};
        if (!navigator_.set_destination(destination)) {
            visit.outcome = "navigation_unavailable";
            return visit;
        }
        robot::Command command;
        for (std::size_t i = 0; i < max_view_ticks && ticks_ < max_mission_ticks; ++i) {
            step(command);
            command = navigator_.update(run_.mapper, run_.robot.telemetry(),
                                        run_.lidar ? &*run_.lidar : nullptr);
            const auto progress = navigator_.progress();
            if (progress.state == navigation::State::finished) {
                visit.outcome = navigation::label(progress.reason);
                if (progress.reason == navigation::Reason::destination_reached) {
                    collect(site_index, pass, visit);
                }
                return visit;
            }
        }
        visit.outcome = "navigation_step_limit";
        return visit;
    }

    [[nodiscard]] const Evidence &evidence() const noexcept { return evidence_; }
    [[nodiscard]] std::uint32_t ticks() const noexcept { return ticks_; }

  private:
    void step(robot::Command command) {
        ++ticks_;
        radio_ = simulation::step_observed(run_, command, navigator_);
    }

    void collect(std::size_t site, std::size_t pass, Visit &visit) {
        const auto &goal = sites.at(site).views.at(pass);
        std::size_t frames_received = 0;
        for (std::size_t i = 0;
             i < 48 && ticks_ < max_mission_ticks && frames_received < frames_per_view; ++i) {
            step({});
            const auto &estimate = run_.mapper.estimate();
            if (!radio_ || !estimate || !estimate->reliable ||
                estimate->sample_tick != radio_->header.sample_tick) {
                continue;
            }
            const auto &pose = estimate->pose;
            const auto heading = pose.heading.value();
            if (std::hypot(pose.x.value() - goal.point.x_m, pose.y.value() - goal.point.y_m) >
                    0.11 ||
                std::abs(std::remainder(heading - goal.heading.value(), full_turn)) > 0.16 ||
                run_.robot.telemetry().collision_blocked) {
                continue;
            }
            const Footprint footprint{
                {pose.x.value() + antenna_forward * std::cos(heading),
                 pose.y.value() + antenna_forward * std::sin(heading)},
                robot::Radians{std::remainder(heading + antenna_yaw, full_turn)},
                Meters{antenna_range},
                Meters{estimate->distance_error_m},
                robot::Radians{estimate->heading_error_rad}};
            take(evidence_.ingest(*radio_, footprint, site * 2 + pass));
            take(repository_.record_rfid(session_, *radio_, inventory::AntennaId{"FRONT"}));
            ++frames_received;
            visit.frames += static_cast<std::size_t>(radio_->truncated_reads == 0);
        }
        visit.outcome = visit.frames >= min_coverage_frames ? "scanned" : "incomplete_scan";
    }

    simulation::ObservedRun &run_;
    navigation::Navigator &navigator_;
    Repository &repository_;
    SessionId session_;
    Evidence evidence_;
    std::optional<sensors::RfidScan> radio_;
    std::uint32_t ticks_{};
};

std::size_t resolved(const Scope &scope, const Evidence &evidence) {
    return static_cast<std::size_t>(
        std::ranges::count_if(scope.targets, [&](const Assignment &target) {
            const auto read = evidence.tags().find(target.unit.epc.value);
            return read != evidence.tags().end() && attribute(read->second, sites).supported;
        }));
}

Row row(std::optional<Assignment> assignment, const Epc &epc, const Evidence &evidence,
        std::span<const Visit> visits) {
    const auto detected = evidence.tags().find(epc.value);
    const auto *tag = detected != evidence.tags().end() ? &detected->second : nullptr;
    const auto attribution = tag ? attribute(*tag, sites) : Attribution{};
    const auto status = classify(assignment ? std::optional{assignment->aisle} : std::nullopt, tag,
                                 attribution, visits);
    return {epc, std::move(assignment), status, attribution,
            tag ? std::optional{*tag} : std::nullopt};
}

void finish(Repository &repository, Scope &scope, const Evidence &evidence, Report &report) {
    for (const auto &target : scope.targets) {
        report.rows.push_back(row(target, target.unit.epc, evidence, report.visits));
    }
    if (const auto *audit = std::get_if<AuditAisle>(&report.request)) {
        std::set<std::string_view> assigned_epcs;
        for (const auto &target : scope.targets) {
            assigned_epcs.insert(target.unit.epc.value);
            const auto &product = target.unit.product;
            ++scope.quantities.try_emplace(product.value, Quantity{product})
                  .first->second.catalog_assigned;
        }
        for (const auto &entry : evidence.tags()) {
            const auto &tag = entry.second;
            if (assigned_epcs.contains(tag.epc.value)) {
                continue;
            }
            auto assignment = repository.assignment(tag.epc);
            if (const auto *error = std::get_if<Error>(&assignment)) {
                if (*error != Error::not_found) {
                    throw *error;
                }
                report.rows.push_back(row({}, tag.epc, evidence, report.visits));
            } else {
                report.rows.push_back(
                    row(take(std::move(assignment)), tag.epc, evidence, report.visits));
            }
        }
        for (const auto &item : report.rows) {
            if (item.assignment && item.attribution.supported &&
                item.attribution.aisle == audit->aisle) {
                const auto &product = item.assignment->unit.product;
                ++scope.quantities.try_emplace(product.value, Quantity{product})
                      .first->second.uniquely_observed_in_aisle;
            }
        }
        for (auto &[key, quantity] : scope.quantities) {
            static_cast<void>(key);
            report.quantities.push_back(std::move(quantity));
        }
    }
}

} // namespace

std::span<const Site> demo_sites() noexcept { return sites; }

Result<Report, Error> run_demo(Repository &repository, const Request &request, SessionId session,
                               bool enable_second_pass) {
    try {
        const auto fixture = configured(simulation::fixture_json("rfid-audit"));
        const auto warehouse = configured(simulation::Warehouse::from_json(fixture));
        const auto config = warehouse.snapshot(0);
        auto robot = configured(simulation::RobotSimulation::create(warehouse));
        simulation::SensorConfig settings;
        settings.rfid.range_m = antenna_range;
        settings.rfid.antenna_yaw_rad = antenna_yaw;
        settings.rfid.antenna_forward_m = antenna_forward;
        auto rig = configured(simulation::SensorRig::create(config.seed, settings));
        auto mapper = configured(mapping::Mapper::create(
            {config.scenario_id, static_cast<std::uint32_t>(config.floor.max.x),
             static_cast<std::uint32_t>(config.floor.max.y), 250}));
        std::optional<sensors::LidarScan> lidar;
        simulation::ObservedRun run{robot, rig, mapper,
                                    lidar, {},  Meters{config.robot_radius_mm / 1000.0}};
        return run_observed(repository, request, session, run, enable_second_pass);
    } catch (Error error) {
        return error;
    }
}

namespace {

struct SurveyPlan {
    RobotId robot{1};
    bool second_pass{true};
    std::optional<std::size_t> shelf{};
};

Result<Report, Error> run_survey(Repository &repository, const Request &request, SessionId session,
                                 simulation::ObservedRun &run, SurveyPlan plan) {
    try {
        auto scope = load_scope(repository, request);
        auto made = navigation::Navigator::create(run.mapper.grid(), {.radius = run.radius});
        if (!valid_sites(sites) || run.mapper.grid().spec().scenario != ScenarioId{801}) {
            return Error::invalid_data;
        }
        if (!made) {
            return Error::invalid_data;
        }
        const auto tick = run.robot.telemetry().sample_tick;
        if (tick > static_cast<std::uint64_t>(INT64_MAX) / robot::step_ms) {
            return Error::invalid_data;
        }
        take(repository.start_session(
            {session, MissionId{session.value()}, plan.robot, tick * robot::step_ms}));
        Survey survey{run, *made, repository, session};
        Report report{session, request, {}, {}, {}};
        if (std::holds_alternative<FindItem>(request) && scope.targets.empty()) {
            return report;
        }
        if (plan.shelf) {
            report.visits.push_back(survey.investigate(*plan.shelf, 0));
        } else {
            for (const auto site : priorities(scope, request, survey.evidence(), false)) {
                report.visits.push_back(survey.investigate(site, 0));
            }
        }
        report.first_pass_resolved = resolved(scope, survey.evidence());
        if (plan.shelf) {
            report.visits.push_back(survey.investigate(*plan.shelf, 1));
        } else if (plan.second_pass) {
            for (const auto site : priorities(scope, request, survey.evidence(), true)) {
                report.visits.push_back(survey.investigate(site, 1));
            }
        }
        const auto &evidence = survey.evidence();
        report.raw_reads = evidence.read_count();
        report.unique_observed = evidence.tags().size();
        report.resolved = resolved(scope, evidence);
        report.motion_ticks = survey.ticks();
        finish(repository, scope, evidence, report);
        return report;
    } catch (Error error) {
        return error;
    }
}

} // namespace

Result<Report, Error> run_observed(Repository &repository, const Request &request,
                                   SessionId session, simulation::ObservedRun &run,
                                   bool enable_second_pass) {
    return run_survey(repository, request, session, run, {.second_pass = enable_second_pass});
}

Result<Report, Error> run_shelf(Repository &repository, const AuditAisle &site, SessionId session,
                                RobotId robot, simulation::ObservedRun &run) {
    const auto position = std::ranges::find(sites, site.aisle, &Site::aisle);
    if (position == sites.end() || robot.value() == 0) {
        return Error::invalid_data;
    }
    const auto index = static_cast<std::size_t>(position - sites.begin());
    return run_survey(repository, site, session, run, {.robot = robot, .shelf = index});
}

} // namespace argus::audit
