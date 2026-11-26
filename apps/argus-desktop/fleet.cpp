#include "fleet.hpp"

#include <algorithm>

namespace argus::desktop {
namespace {

std::unique_ptr<LiveEngine> start_robot(std::size_t index) {
    auto warehouse = audit::stockroom_world(RobotId{index + 1});
    auto *world = std::get_if<simulation::Warehouse>(&warehouse);
    if (!world) {
        return {};
    }
    auto engine = LiveEngine::create(std::move(*world), 0, true);
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
    for (std::size_t i = 0; i < units_.size(); ++i) {
        auto engine = start_robot(i);
        if (!engine) {
            return;
        }
        if (!layout_) {
            layout_ = engine->layout();
        }
        units_[i] = std::make_unique<Unit>(std::move(engine));
    }
}

bool Fleet::ready() const noexcept { return units_.back() != nullptr; }

bool Fleet::queue(std::size_t robot, std::size_t location) {
    if (!ready() || robot >= units_.size() || location >= history_.size() || busy(robot) ||
        state(location) == ShelfState::auditing) {
        return false;
    }
    auto &unit = *units_[robot];
    unit.pending = true;
    unit.target = location;
    unit.status = "Audit queued";
    return true;
}

void Fleet::tick(std::uint64_t steps, bool focused) {
    if (!ready()) {
        return;
    }
    if (!focused) {
        hold();
    }
    for (std::size_t i = 0; i < units_.size(); ++i) {
        auto &unit = *units_[i];
        receive(unit, i);
        if (focused && unit.pending && unit.engine) {
            dispatch(unit, i);
        }
        if (unit.engine) {
            for (std::uint64_t step = 0; step < steps; ++step) {
                unit.engine->step({}, LiveAction::none, focused);
            }
        }
    }
}

void Fleet::receive(Unit &unit, std::size_t index) {
    if (!unit.engine) {
        if (auto preview = unit.workbench.preview()) {
            unit.preview = std::move(preview);
        }
    }
    auto result = unit.workbench.take();
    if (!result) {
        return;
    }
    unit.status = std::move(result->status);
    if (result->live) {
        unit.engine = std::move(result->live);
        unit.preview.reset();
    }
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
    unit.preview = std::make_shared<ObservedFrame>(unit.engine->frame());
    const auto &location = locations()[*unit.target];
    Job job{.kind = JobKind::shelf_audit,
            .live = std::move(unit.engine),
            .shelf = ShelfAssignment{location.id, RobotId{index + 1}, unit.preview->run, journal_}};
    if (unit.workbench.submit(std::move(job))) {
        unit.status = "Auditing shelf";
    } else {
        unit.engine = std::move(job.live);
        unit.preview.reset();
        unit.status = "Robot unavailable";
    }
}

void Fleet::hold() {
    for (auto &unit : units_) {
        if (unit) {
            if (std::exchange(unit->pending, false)) {
                unit->status = "Audit held before dispatch";
            }
            if (!unit->engine) {
                unit->workbench.interrupt();
            }
        }
    }
}

LiveFrame Fleet::frame(std::size_t robot) const noexcept {
    const auto &unit = *units_[robot];
    return unit.engine ? unit.engine->frame() : unit.preview->view();
}

ShelfState Fleet::state(std::size_t location) const noexcept {
    if (std::ranges::any_of(units_, [&](const auto &unit) {
            return unit && unit->target == location && (unit->pending || !unit->engine);
        })) {
        return ShelfState::auditing;
    }
    return history_[location].empty() ? ShelfState::pending : ShelfState::complete;
}

bool Fleet::busy(std::size_t robot) const {
    const auto &unit = *units_[robot];
    return unit.pending || !unit.engine || unit.workbench.busy();
}

std::span<const ShelfEntry> Fleet::history(std::size_t location) const noexcept {
    return history_[location];
}

std::optional<std::size_t> Fleet::assignment(std::size_t robot) const noexcept {
    return units_[robot]->target;
}

AisleSummary Fleet::aisle_summary(std::size_t aisle) const noexcept {
    AisleSummary summary;
    for (std::size_t i = 0; i < history_.size(); ++i) {
        if (locations()[i].aisle == aisle) {
            ++summary.total;
            summary.complete += state(i) == ShelfState::complete ? 1U : 0U;
            summary.review +=
                !history_[i].empty() && history_[i].front().report.needs_review() ? 1U : 0U;
        }
    }
    return summary;
}

std::string_view Fleet::status(std::size_t robot) const noexcept { return units_[robot]->status; }

} // namespace argus::desktop
