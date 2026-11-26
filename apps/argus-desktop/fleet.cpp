#include "fleet.hpp"

#include "argus/simulation/fixtures.hpp"

#include <nlohmann/json.hpp>

namespace argus::desktop {
namespace {

std::unique_ptr<LiveEngine> start_robot(const ShelfLocation &location, std::size_t index) {
    auto source = simulation::fixture_json("rfid-audit");
    const auto *bytes = std::get_if<std::string_view>(&source);
    if (!bytes) {
        return {};
    }
    auto fixture = nlohmann::json::parse(*bytes);
    fixture["robot"]["spawnMm"][0] = location.spawn_x_mm;
    fixture["seed"] = fixture["seed"].get<std::uint32_t>() + index;
    auto warehouse = simulation::Warehouse::from_json(fixture.dump());
    auto *world = std::get_if<simulation::Warehouse>(&warehouse);
    if (!world) {
        return {};
    }
    auto engine = LiveEngine::create(std::move(*world), 0, true);
    auto *live = std::get_if<LiveEngine>(&engine);
    return live ? std::make_unique<LiveEngine>(std::move(*live)) : nullptr;
}

} // namespace

Fleet::Fleet(const std::optional<std::filesystem::path> &directory) {
    std::optional<mission::Store> store;
    if (directory) {
        journal_ = *directory / "missions.db";
        auto opened = mission::Store::open(*journal_);
        if (auto *value = std::get_if<mission::Store>(&opened)) {
            store.emplace(std::move(*value));
        } else {
            history_error_ = true;
        }
    }
    for (std::size_t i = 0; i < units_.size(); ++i) {
        auto engine = start_robot(locations[i], i);
        if (!engine) {
            return;
        }
        units_[i] = std::make_unique<Unit>(std::move(engine));
        if (!store) {
            continue;
        }
        auto loaded = store->shelf_history(inventory::BinId{std::string{locations[i].label}});
        auto *records = std::get_if<std::vector<mission::ShelfAudit>>(&loaded);
        if (!records) {
            history_error_ = true;
            continue;
        }
        for (auto &saved : *records) {
            auto report = decode_shelf_report(saved.report);
            if (!report || saved.robot != RobotId{i + 1} || saved.session == 0 ||
                saved.run.value() == 0 || saved.completed_ms == 0) {
                history_error_ = true;
                continue;
            }
            units_[i]->history.push_back({std::move(saved), std::move(*report)});
        }
    }
}

bool Fleet::ready() const noexcept { return units_.back() != nullptr; }

bool Fleet::queue(std::size_t index) {
    auto &unit = *units_[index];
    if (busy(index) || !unit.engine) {
        return false;
    }
    unit.pending = true;
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
        if (!report || saved.location.value != locations[index].label ||
            saved.robot != RobotId{index + 1}) {
            unit.status = "Shelf audit data unavailable";
            return;
        }
        if (unit.history.size() == mission::shelf_history_limit) {
            unit.history.pop_back();
        }
        unit.history.insert(unit.history.begin(), {std::move(saved), std::move(*report)});
    }
}

void Fleet::dispatch(Unit &unit, std::size_t index) {
    unit.pending = false;
    unit.preview = std::make_shared<ObservedFrame>(unit.engine->frame());
    const auto &location = locations[index];
    Job job{.kind = JobKind::shelf_audit,
            .text = std::string{location.aisle},
            .live = std::move(unit.engine),
            .shelf = ShelfAssignment{inventory::BinId{std::string{location.label}},
                                     RobotId{index + 1}, unit.preview->run, journal_}};
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

LiveFrame Fleet::frame(std::size_t index) const noexcept {
    const auto &unit = *units_[index];
    return unit.engine ? unit.engine->frame() : unit.preview->view();
}

ShelfState Fleet::state(std::size_t index) const noexcept {
    const auto &unit = *units_[index];
    return unit.pending || !unit.engine ? ShelfState::auditing
           : unit.history.empty()       ? ShelfState::pending
                                        : ShelfState::complete;
}

bool Fleet::busy(std::size_t index) const {
    const auto &unit = *units_[index];
    return unit.pending || !unit.engine || unit.workbench.busy();
}

std::span<const ShelfEntry> Fleet::history(std::size_t index) const noexcept {
    return units_[index]->history;
}

std::string_view Fleet::status(std::size_t index) const noexcept { return units_[index]->status; }

} // namespace argus::desktop
