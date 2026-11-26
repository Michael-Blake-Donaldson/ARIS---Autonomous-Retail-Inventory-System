#include "fleet.hpp"

#include <algorithm>
#include <chrono>

namespace argus::desktop {
namespace {

std::unique_ptr<LiveEngine> start_robot(const simulation::Warehouse &world,
                                        std::shared_ptr<simulation::FleetClock> clock, RobotId id) {
    auto engine = LiveEngine::create(world, 0, true, FleetAttachment{id, std::move(clock)});
    auto *live = std::get_if<LiveEngine>(&engine);
    return live ? std::make_unique<LiveEngine>(std::move(*live)) : nullptr;
}

} // namespace

std::span<const audit::StockLocation> Fleet::locations() { return audit::stockroom_locations(); }

std::optional<std::size_t> Fleet::find(std::string_view id) {
    const auto list = locations();
    const auto found = std::ranges::find(
        list, id, [](const auto &location) { return std::string_view{location.id.value}; });
    return found == list.end() ? std::nullopt
                               : std::optional{static_cast<std::size_t>(found - list.begin())};
}

std::size_t Fleet::initial_location(std::size_t robot) {
    return find(robot == 0 ? "A10J05" : "A20J06-W01").value();
}

Fleet::Fleet(const std::optional<std::filesystem::path> &directory) : history_(locations().size()) {
    if (directory) {
        journal_ = *directory / "missions.db";
        auto opened = mission::Store::open(*journal_);
        if (auto *store = std::get_if<mission::Store>(&opened)) {
            for (std::size_t i = 0; i < history_.size(); ++i) {
                auto loaded = store->shelf_history(locations()[i].id);
                auto *records = std::get_if<std::vector<mission::ShelfAudit>>(&loaded);
                if (!records) {
                    history_error_ = true;
                    continue;
                }
                for (auto &saved : *records) {
                    auto report = decode_shelf_report(saved.report);
                    if (!report || saved.robot.value() == 0 || saved.robot.value() > robot_count ||
                        saved.session == 0 || saved.run.value() == 0 || saved.completed_ms == 0) {
                        history_error_ = true;
                        continue;
                    }
                    history_[i].push_back({std::move(saved), std::move(*report)});
                }
            }
        } else {
            history_error_ = true;
        }
    }
    auto model = audit::shared_stockroom_world();
    const auto *world = std::get_if<simulation::Warehouse>(&model);
    if (!world) {
        return;
    }
    for (std::size_t i = 0; i < units_.size(); ++i) {
        const RobotId id{i + 1};
        auto engine = start_robot(*world, clock_, id);
        if (!engine) {
            clock_->close();
            return;
        }
        if (!layout_) {
            layout_ = engine->layout();
        }
        units_[i] =
            std::make_unique<Unit>(Workbench::FleetWorker{id, std::move(engine), clock_, access_});
    }
}

bool Fleet::ready() const noexcept { return units_.back() != nullptr; }

simulation::FleetStop Fleet::stop_reason() const noexcept { return clock_->stop_reason(); }

bool Fleet::motion_available() const noexcept {
    return stop_reason() == simulation::FleetStop::running;
}

bool Fleet::queue(std::size_t robot, std::size_t location) {
    if (!ready() || !motion_available() || robot >= units_.size() || location >= history_.size() ||
        busy(robot) || state(location) == ShelfState::auditing || aisle_retained(robot)) {
        return false;
    }
    auto &unit = *units_[robot];
    unit.pending = true;
    unit.target = location;
    const auto reports = history(location);
    unit.recheck_of = reports.empty() ? std::nullopt : std::optional{reports.front().audit.key()};
    unit.status = "Audit queued";
    return true;
}

bool Fleet::review(std::size_t location, mission::ShelfAuditKey key) {
    if (location >= history_.size() || review_ || history_[location].empty() ||
        state(location) == ShelfState::auditing) {
        return false;
    }
    auto &entry = history_[location].front();
    if (entry.audit.key() != key || !entry.needs_review()) {
        return false;
    }
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    if (now <= 0 || static_cast<std::uint64_t>(now) < entry.audit.completed_ms) {
        review_failure_ = location;
        return false;
    }
    const auto stamp = static_cast<std::uint64_t>(now);
    review_failure_.reset();
    if (!journal_) {
        entry.audit.reviewed_ms = stamp;
        return true;
    }
    try {
        review_.emplace(
            PendingReview{location, key,
                          std::async(std::launch::async,
                                     [path = *journal_, id = entry.audit.location, key,
                                      stamp]() -> Result<std::uint64_t, mission::Error> {
                                         auto opened = mission::Store::open(path);
                                         if (auto *store = std::get_if<mission::Store>(&opened)) {
                                             return store->review_shelf_audit(id, key, stamp);
                                         }
                                         return std::get<mission::Error>(opened);
                                     })});
        return true;
    } catch (const std::exception &) {
        review_failure_ = location;
        return false;
    }
}

void Fleet::receive_review() {
    if (!review_ ||
        review_->result.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
        return;
    }
    const auto location = review_->location;
    try {
        auto result = review_->result.get();
        if (const auto *stamp = std::get_if<std::uint64_t>(&result)) {
            for (auto &entry : history_[location]) {
                if (entry.audit.key() == review_->key) {
                    entry.audit.reviewed_ms = *stamp;
                    break;
                }
            }
        } else {
            review_failure_ = location;
        }
    } catch (const std::exception &) {
        review_failure_ = location;
    }
    review_.reset();
}

bool Fleet::clear_aisle(std::size_t robot) {
    if (!ready() || !motion_available() || robot >= units_.size() || busy(robot) ||
        !aisle_retained(robot)) {
        return false;
    }
    auto &unit = *units_[robot];
    unit.pending = true;
    unit.clearing = true;
    unit.target.reset();
    unit.status = "Berth return queued";
    return true;
}

bool Fleet::aisle_retained(std::size_t robot) const {
    return access_->retained(RobotId{robot + 1});
}

void Fleet::tick(bool focused) {
    if (!ready()) {
        return;
    }
    receive_review();
    if (!focused) {
        hold();
    }
    for (std::size_t i = 0; i < units_.size(); ++i) {
        auto &unit = *units_[i];
        receive(unit, i);
        if (focused && unit.pending) {
            dispatch(unit, i);
        }
    }
}

void Fleet::receive(Unit &unit, std::size_t index) {
    if (auto preview = unit.workbench.preview()) {
        unit.preview = std::move(preview);
    }
    auto result = unit.workbench.take();
    if (!result) {
        return;
    }
    unit.status = std::move(result->status);
    if (result->shelf) {
        auto saved = std::move(*result->shelf);
        auto report = decode_shelf_report(saved.report);
        if (!report || !unit.target || saved.location != locations()[*unit.target].id ||
            saved.robot != RobotId{index + 1}) {
            unit.status = "Location audit data unavailable";
            return;
        }
        auto &history = history_[*unit.target];
        if (history.size() == mission::shelf_history_limit) {
            history.pop_back();
        }
        history.insert(history.begin(), {std::move(saved), std::move(*report)});
    }
}

void Fleet::dispatch(Unit &unit, std::size_t index) {
    unit.pending = false;
    const RobotId id{index + 1};
    if (std::exchange(unit.clearing, false)) {
        if (access_->request_clearance(id)) {
            if (unit.workbench.submit(Job{.kind = JobKind::clear_aisle})) {
                unit.status = "Returning to berth";
                return;
            }
            access_->finish(id, false);
        }
        unit.status = "Berth return unavailable";
        return;
    }
    const auto &location = locations()[*unit.target];
    const auto berth = audit::stockroom_berth(id).position.x / 1000.0;
    const auto location_x = location.center.x_m + 1.15;
    const auto pose = unit.preview->telemetry.estimated_pose.x.value();
    const navigation::TrafficSpan span{Meters{std::min({berth, location_x, pose}) - 0.9},
                                       Meters{std::max({berth, location_x, pose}) + 0.9}};
    if (!access_->request(id, span)) {
        unit.status = "Aisle request unavailable";
        return;
    }
    Job job{.kind = JobKind::shelf_audit,
            .shelf =
                ShelfAssignment{location.id, id, unit.preview->run, journal_, unit.recheck_of}};
    if (unit.workbench.submit(std::move(job))) {
        unit.status = "Auditing shelf";
    } else {
        access_->finish(id, true);
        unit.status = "Robot unavailable";
    }
}

void Fleet::hold() {
    for (auto &unit : units_) {
        if (unit) {
            if (std::exchange(unit->pending, false)) {
                unit->clearing = false;
                unit->status = "Audit held before dispatch";
            }
            if (unit->workbench.busy()) {
                unit->workbench.interrupt();
            }
        }
    }
}

LiveFrame Fleet::frame(std::size_t robot) const noexcept {
    const auto &unit = *units_[robot];
    return unit.preview->view();
}

ShelfState Fleet::state(std::size_t location) const {
    if (std::ranges::any_of(units_, [&](const auto &unit) {
            return unit && unit->target == location && (unit->pending || unit->workbench.busy());
        })) {
        return ShelfState::auditing;
    }
    return history_[location].empty() ? ShelfState::pending : ShelfState::complete;
}

bool Fleet::busy(std::size_t robot) const {
    const auto &unit = *units_[robot];
    return unit.pending || unit.workbench.busy();
}

std::span<const ShelfEntry> Fleet::history(std::size_t location) const noexcept {
    return history_[location];
}

std::optional<std::size_t> Fleet::assignment(std::size_t robot) const noexcept {
    return units_[robot]->target;
}

AisleSummary Fleet::aisle_summary(std::size_t aisle) const {
    AisleSummary summary;
    for (std::size_t i = 0; i < history_.size(); ++i) {
        if (locations()[i].aisle == aisle) {
            ++summary.total;
            summary.complete += state(i) == ShelfState::complete ? 1U : 0U;
            summary.review += !history_[i].empty() && history_[i].front().needs_review() ? 1U : 0U;
        }
    }
    return summary;
}

std::string_view Fleet::status(std::size_t robot) const noexcept { return units_[robot]->status; }

} // namespace argus::desktop
