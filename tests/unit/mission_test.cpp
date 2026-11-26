#include "argus/mission/store.hpp"

#include <array>
#include <barrier>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <stdexcept>
#include <thread>

namespace {

using namespace argus;
using namespace argus::mission;

class Database {
  public:
    Database() {
        const auto base = std::filesystem::temp_directory_path();
        const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            directory = base / ("argus-mission-" + std::to_string(token) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(directory)) {
                return;
            }
        }
        throw std::runtime_error("could not create a test directory");
    }
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    ~Database() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    [[nodiscard]] std::filesystem::path file() const { return directory / "missions.db"; }

  private:
    std::filesystem::path directory;
};

TEST(MissionArchive, ReopensExactEvidenceAndRetainsItAfterRejectedReplacement) {
    Database db;
    const SavedRun sample{
        RunId{17}, ScenarioId{801}, 120, 4, 5, 6, std::string{"map\0bytes", 9}, R"({"session":6})"};
    {
        auto store = std::get<Store>(Store::open(db.file()));
        EXPECT_FALSE(std::get<std::optional<SavedRun>>(store.saved_run()));
        EXPECT_TRUE(std::holds_alternative<std::monostate>(store.save_run(sample)));
        auto invalid = sample;
        invalid.report.assign(archived_report_limit + 1, 'x');
        EXPECT_EQ(std::get<Error>(store.save_run(invalid)), Error::invalid);
    }
    auto reopened = std::get<Store>(Store::open(db.file()));
    const auto archive = std::get<std::optional<SavedRun>>(reopened.saved_run());
    ASSERT_TRUE(archive);
    EXPECT_EQ(archive->id, sample.id);
    EXPECT_EQ(archive->scenario, sample.scenario);
    EXPECT_EQ(archive->sample_tick, sample.sample_tick);
    EXPECT_EQ(archive->mission, sample.mission);
    EXPECT_EQ(archive->search_session, sample.search_session);
    EXPECT_EQ(archive->audit_session, sample.audit_session);
    EXPECT_EQ(archive->map, sample.map);
    EXPECT_EQ(archive->report, sample.report);
}

Intent scan(std::uint64_t id = 1, Priority priority = Priority::routine) {
    return {MissionId{id}, Workflow::scan_all, priority, 60000};
}

Readiness ready() {
    return {.energy_uj = 900000000,
            .return_energy_uj = 300000000,
            .next_step_energy_uj = 1000000,
            .localized = true,
            .sensors_ok = true};
}

Record start(Store &store, const Intent &intent = scan()) {
    static_cast<void>(std::get<Record>(store.submit(intent)));
    const auto dispatched = std::get<std::optional<Record>>(store.dispatch()).value();
    return std::get<Cycle>(store.cycle(dispatched.lease(), ready())).record;
}

TEST(MissionArchive, MigratesVersionOneWithoutDiscardingMissionHistory) {
    Database db;
    {
        auto store = std::get<Store>(Store::open(db.file()));
        ASSERT_TRUE(std::holds_alternative<Record>(store.submit(scan(18))));
    }
    const auto path = db.file().u8string();
    sqlite3 *raw = nullptr;
    const auto opened = sqlite3_open(reinterpret_cast<const char *>(path.c_str()), &raw);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> edit{raw, &sqlite3_close};
    ASSERT_EQ(opened, SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(edit.get(),
                           "DROP TABLE shelf_audit; DROP TABLE run_archive; PRAGMA user_version=1",
                           nullptr, nullptr, nullptr),
              SQLITE_OK);
    edit.reset();

    auto upgraded = std::get<Store>(Store::open(db.file()));
    EXPECT_EQ(std::get<Record>(upgraded.get(MissionId{18})).state, State::queued);
    EXPECT_EQ(std::get<std::uint64_t>(upgraded.highest_id()), 18U);
    EXPECT_FALSE(std::get<std::optional<SavedRun>>(upgraded.saved_run()));
}

TEST(MissionStore, ASharedRunCanOnlyDispatchAndRecoverItsOwnSelectedMission) {
    Database db;
    auto store = std::get<Store>(Store::open(db.file()));
    ASSERT_TRUE(std::holds_alternative<Record>(store.submit(scan(1, Priority::urgent))));
    ASSERT_TRUE(std::holds_alternative<Record>(store.submit(scan(2))));
    EXPECT_FALSE(std::get<std::optional<Record>>(store.dispatch(MissionId{2})));
    const auto dispatched = std::get<std::optional<Record>>(store.dispatch(MissionId{1}));
    ASSERT_TRUE(dispatched);
    EXPECT_EQ(dispatched->intent.id, MissionId{1});
    EXPECT_EQ(std::get<std::size_t>(store.recover(MissionId{2})), 0U);
    EXPECT_EQ(std::get<Record>(store.get(MissionId{1})).state, State::planning);
    EXPECT_EQ(std::get<std::size_t>(store.recover(MissionId{1})), 1U);
    EXPECT_EQ(std::get<Record>(store.get(MissionId{1})).state, State::paused);
}

TEST(MissionState, TransitionMatrixCoversEveryStateAndAction) {
    const auto bits = [](std::initializer_list<Action> list) {
        unsigned mask = 0;
        for (const auto action : list) {
            mask |= 1U << static_cast<unsigned>(action);
        }
        return mask;
    };
    using A = Action;
    const std::array allowed{
        bits({A::enqueue, A::cancel}),
        bits({A::dispatch, A::cancel}),
        bits({A::begin, A::return_home, A::pause, A::obstruct, A::fail, A::cancel, A::recover}),
        bits({A::pause, A::obstruct, A::return_home, A::finish, A::fail, A::cancel, A::recover}),
        bits({A::resume, A::cancel, A::fail}),
        bits({A::retry, A::return_home, A::pause, A::recover, A::fail, A::cancel}),
        bits({A::charge, A::finish, A::pause, A::recover, A::fail, A::cancel}),
        0U,
        0U,
        0U};
    for (std::size_t i = 0; i < allowed.size(); ++i) {
        for (unsigned action = 0; action <= static_cast<unsigned>(A::charge); ++action) {
            const auto next_state = next(static_cast<State>(i), static_cast<Action>(action));
            EXPECT_EQ(next_state.has_value(), (allowed.at(i) & (1U << action)) != 0)
                << "state=" << i << " action=" << action;
        }
    }
    EXPECT_EQ(next(State::returning, A::recover), State::paused);
    EXPECT_EQ(next(State::executing, A::finish), State::completed);
}

TEST(MissionStore, SubmissionIsIdempotentAndQueueIsBounded) {
    Database db;
    auto store = std::get<Store>(Store::open(db.file()));
    for (std::size_t i = 1; i <= max_pending; ++i) {
        EXPECT_EQ(std::get<Record>(store.submit(scan(i))).state, State::queued);
    }
    EXPECT_EQ(std::get<Error>(store.submit(scan(max_pending + 1))), Error::full);
    EXPECT_EQ(std::get<Record>(store.submit(scan())).revision, 1U);
    EXPECT_EQ(std::get<Error>(store.submit(scan(1, Priority::urgent))), Error::conflict);
    EXPECT_EQ(std::get<std::vector<Event>>(store.events(MissionId{1})).size(), 2U);
    EXPECT_EQ(std::get<std::optional<Record>>(store.dispatch())->intent.id, MissionId{1});
    EXPECT_FALSE(std::get<std::optional<Record>>(store.dispatch()));
}

TEST(MissionStore, HigherPriorityPreemptsAndReturningHasPriority) {
    Database db;
    auto store = std::get<Store>(Store::open(db.file()));
    const auto original = start(store);
    static_cast<void>(std::get<Record>(store.submit(scan(2, Priority::urgent))));
    EXPECT_FALSE(std::get<std::optional<Record>>(store.dispatch()));
    EXPECT_EQ(std::get<Cycle>(store.cycle(original.lease(), ready())).motion, Motion::brake);
    const auto urgent = std::get<std::optional<Record>>(store.dispatch()).value();
    EXPECT_EQ(urgent.intent.id, MissionId{2});
    const auto paused = std::get<Record>(store.get(original.intent.id));
    EXPECT_EQ(paused.state, State::paused);
    EXPECT_EQ(paused.cause, Cause::preempted);
    EXPECT_EQ(std::get<Error>(store.cycle(original.lease(), ready())), Error::stale);

    auto low = ready();
    low.low_battery = true;
    const auto returning = std::get<Cycle>(store.cycle(urgent.lease(), low)).record;
    ASSERT_EQ(returning.state, State::returning);
    static_cast<void>(
        std::get<Record>(store.submit({MissionId{3}, Workflow::dock, Priority::safety, 60000})));
    EXPECT_FALSE(std::get<std::optional<Record>>(store.dispatch()));
}

TEST(MissionStore, ConcurrentCancellationRejectsInFlightScanCommit) {
    Database db;
    auto coordinator = std::get<Store>(Store::open(db.file()));
    auto scanner = std::get<Store>(Store::open(db.file()));
    auto running = start(coordinator);
    ASSERT_EQ(running.state, State::executing);
    const auto lease = running.lease();
    std::promise<void> in_flight;
    std::promise<void> cancelled;
    std::promise<Result<Record, Error>> result;
    const auto started = in_flight.get_future();
    auto permitted = cancelled.get_future();
    auto finished = result.get_future();
    std::jthread worker{[&] {
        const Checkpoint evidence{.sample = 1,
                                  .elapsed_ms = 50,
                                  .planned_scans = 1,
                                  .covered_scans = 1,
                                  .raw_reads = 12,
                                  .covered_mask = 1};
        in_flight.set_value();
        permitted.wait();
        result.set_value(scanner.checkpoint(lease, evidence));
    }};
    started.wait();
    const auto cancelled_record = coordinator.cancel(lease.id);
    cancelled.set_value();
    EXPECT_EQ(std::get<Record>(cancelled_record).state, State::cancelled);
    EXPECT_EQ(std::get<Error>(finished.get()), Error::stale);
    EXPECT_EQ(std::get<Record>(coordinator.get(lease.id)).checkpoint.raw_reads, 0U);
    EXPECT_EQ(std::get<Error>(coordinator.cycle(lease, ready())), Error::stale);
}

TEST(MissionStore, WorkerYieldsToQueuedPriorityBeforeIssuingAnotherWorkCommand) {
    Database db;
    auto store = std::get<Store>(Store::open(db.file()));
    const auto running = start(store);
    static_cast<void>(std::get<Record>(store.submit(scan(2, Priority::urgent))));
    const auto halted = std::get<Cycle>(store.cycle(running.lease(), ready()));
    EXPECT_EQ(halted.motion, Motion::brake);
    EXPECT_EQ(halted.record.state, State::paused);
    EXPECT_EQ(halted.record.cause, Cause::preempted);
    EXPECT_EQ(std::get<std::optional<Record>>(store.dispatch())->intent.id, MissionId{2});
}

TEST(MissionStore, ConcurrentCancellationAndCheckpointHaveOneSerializableOutcome) {
    Database db;
    auto coordinator = std::get<Store>(Store::open(db.file()));
    auto scanner = std::get<Store>(Store::open(db.file()));
    constexpr Checkpoint evidence{.sample = 1,
                                  .elapsed_ms = 50,
                                  .planned_scans = 1,
                                  .covered_scans = 1,
                                  .raw_reads = 12,
                                  .covered_mask = 1};
    for (std::uint64_t i = 1; i <= 64; ++i) {
        const auto lease = start(coordinator, scan(i)).lease();
        std::barrier gate{2};
        Result<Record, Error> checkpoint{Error::storage};
        {
            const std::jthread worker{[&] {
                gate.arrive_and_wait();
                checkpoint = scanner.checkpoint(lease, evidence);
            }};
            gate.arrive_and_wait();
            ASSERT_EQ(std::get<Record>(coordinator.cancel(lease.id)).state, State::cancelled);
        }
        const auto final = std::get<Record>(coordinator.get(lease.id));
        EXPECT_EQ(final.state, State::cancelled);
        if (const auto *error = std::get_if<Error>(&checkpoint)) {
            EXPECT_EQ(*error, Error::stale);
            EXPECT_EQ(final.checkpoint.raw_reads, 0U);
        } else {
            EXPECT_EQ(final.checkpoint.raw_reads, evidence.raw_reads);
        }
        EXPECT_EQ(std::get<Error>(scanner.checkpoint(lease, evidence)), Error::stale);
    }
}

TEST(MissionStore, MigratesAndRetainsBoundedShelfHistoryPerLocation) {
    Database db;
    {
        auto original = std::get<Store>(Store::open(db.file()));
        ASSERT_TRUE(std::holds_alternative<Record>(original.submit(scan())));
    }
    sqlite3 *raw{};
    ASSERT_EQ(sqlite3_open(db.file().string().c_str(), &raw), SQLITE_OK);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> edit{raw, &sqlite3_close};
    ASSERT_EQ(sqlite3_exec(edit.get(), "DROP TABLE shelf_audit; PRAGMA user_version=2", nullptr,
                           nullptr, nullptr),
              SQLITE_OK);
    auto store = std::get<Store>(Store::open(db.file()));
    EXPECT_TRUE(std::holds_alternative<Record>(store.get(MissionId{1})));
    ShelfAudit first{{"A10J05"}, RobotId{1}, RunId{42}, 1, 10, "example"};
    ShelfAudit second{{"A20J05"}, RobotId{2}, RunId{43}, 1, 11, "other"};
    ASSERT_EQ(store.save_shelf_audit(first).index(), 0U);
    ASSERT_EQ(store.save_shelf_audit(first).index(), 0U);
    ASSERT_EQ(store.save_shelf_audit(second).index(), 0U);
    auto conflict = first;
    conflict.report = "different";
    EXPECT_EQ(std::get<Error>(store.save_shelf_audit(conflict)), Error::conflict);
    conflict.location = {"../bad"};
    EXPECT_EQ(std::get<Error>(store.save_shelf_audit(conflict)), Error::invalid);
    for (std::size_t n = 2; n <= shelf_history_limit + 1; ++n) {
        first.session = n;
        ASSERT_EQ(store.save_shelf_audit(first).index(), 0U);
    }
    auto reopen = std::get<Store>(Store::open(db.file()));
    const auto history = std::get<std::vector<ShelfAudit>>(reopen.shelf_history(first.location));
    ASSERT_EQ(history.size(), shelf_history_limit);
    EXPECT_EQ(history.front().session, shelf_history_limit + 1);
    EXPECT_EQ(history.back().session, 2U);
    const auto other = std::get<std::vector<ShelfAudit>>(reopen.shelf_history(second.location));
    ASSERT_EQ(other.size(), 1U);
    EXPECT_EQ(other.front().robot, RobotId{2});
    EXPECT_EQ(other.front().report, second.report);
}

TEST(MissionStore, MigratesExistingShelfHistoryAndKeepsAdjacentWacosSeparate) {
    Database db;
    const ShelfAudit earlier{{"A20J05"}, RobotId{1}, RunId{42}, 1, 10, "old shelf"};
    {
        auto store = std::get<Store>(Store::open(db.file()));
        ASSERT_EQ(store.save_shelf_audit(earlier).index(), 0U);
    }
    sqlite3 *raw{};
    ASSERT_EQ(sqlite3_open(db.file().string().c_str(), &raw), SQLITE_OK);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> edit{raw, &sqlite3_close};
    ASSERT_EQ(sqlite3_exec(edit.get(), "PRAGMA user_version=3", nullptr, nullptr, nullptr),
              SQLITE_OK);
    auto store = std::get<Store>(Store::open(db.file()));
    ShelfAudit leaf{{"A20J06-W01"}, RobotId{2}, RunId{43}, 1, 12, "Waco evidence"};
    ASSERT_EQ(store.save_shelf_audit(leaf).index(), 0U);
    const auto whole = std::get<std::vector<ShelfAudit>>(store.shelf_history(earlier.location));
    ASSERT_EQ(whole.size(), 1U);
    EXPECT_EQ(whole.front().report, earlier.report);
    const auto history = std::get<std::vector<ShelfAudit>>(store.shelf_history(leaf.location));
    ASSERT_EQ(history.size(), 1U);
    EXPECT_EQ(history.front().report, leaf.report);
    EXPECT_TRUE(std::get<std::vector<ShelfAudit>>(store.shelf_history({"A20J06-W02"})).empty());
    EXPECT_EQ(std::get<Error>(store.shelf_history({"A20J06-W00"})), Error::invalid);
}

TEST(MissionStore, MigratesVersionFourAndReopensReviewAndRecheckProvenance) {
    Database db;
    const ShelfAudit source{{"A20J06-W01"}, RobotId{1}, RunId{51}, 1, 10, "source evidence"};
    {
        auto store = std::get<Store>(Store::open(db.file()));
        ASSERT_EQ(store.save_shelf_audit(source).index(), 0U);
    }
    sqlite3 *raw{};
    ASSERT_EQ(sqlite3_open(db.file().string().c_str(), &raw), SQLITE_OK);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> edit{raw, &sqlite3_close};
    ASSERT_EQ(sqlite3_exec(edit.get(),
                           "ALTER TABLE shelf_audit DROP COLUMN source_session;"
                           "ALTER TABLE shelf_audit DROP COLUMN source_run;"
                           "ALTER TABLE shelf_audit DROP COLUMN source_robot;"
                           "ALTER TABLE shelf_audit DROP COLUMN reviewed_ms;"
                           "PRAGMA user_version=4",
                           nullptr, nullptr, nullptr),
              SQLITE_OK);
    auto store = std::get<Store>(Store::open(db.file()));
    const auto restored = std::get<std::vector<ShelfAudit>>(store.shelf_history(source.location));
    ASSERT_EQ(restored.size(), 1U);
    EXPECT_EQ(restored.front().report, source.report);
    EXPECT_FALSE(restored.front().reviewed_ms);
    EXPECT_FALSE(restored.front().recheck_of);
    EXPECT_EQ(std::get<Error>(store.review_shelf_audit(source.location, source.key(), 9)),
              Error::invalid);
    EXPECT_EQ(std::get<std::uint64_t>(store.review_shelf_audit(source.location, source.key(), 15)),
              15U);
    EXPECT_EQ(std::get<std::uint64_t>(store.review_shelf_audit(source.location, source.key(), 16)),
              15U);
    ShelfAudit recheck{source.location, RobotId{2}, RunId{52}, 1, 20, "new evidence", source.key()};
    ASSERT_EQ(store.save_shelf_audit(recheck).index(), 0U);
    ASSERT_EQ(store.save_shelf_audit(recheck).index(), 0U);
    EXPECT_EQ(std::get<Error>(store.review_shelf_audit(source.location, source.key(), 25)),
              Error::conflict);
    auto reopened = std::get<Store>(Store::open(db.file()));
    const auto reports = std::get<std::vector<ShelfAudit>>(reopened.shelf_history(source.location));
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].report, recheck.report);
    EXPECT_EQ(reports[0].recheck_of, source.key());
    EXPECT_FALSE(reports[0].reviewed_ms);
    EXPECT_EQ(reports[1].report, source.report);
    EXPECT_EQ(reports[1].reviewed_ms, 15U);
}

TEST(MissionStore, RejectsForeignSourcesAndPreservesLinksAfterSourceTrimming) {
    Database db;
    auto store = std::get<Store>(Store::open(db.file()));
    const ShelfAudit source{{"A10J05"}, RobotId{1}, RunId{10}, 1, 10, "source"};
    ASSERT_EQ(store.save_shelf_audit(source).index(), 0U);
    ShelfAudit recheck{{"A20J05"}, RobotId{2}, RunId{20}, 2, 20, "recheck", source.key()};
    EXPECT_EQ(std::get<Error>(store.save_shelf_audit(recheck)), Error::conflict);
    EXPECT_TRUE(std::get<std::vector<ShelfAudit>>(store.shelf_history(recheck.location)).empty());
    recheck.location = source.location;
    ASSERT_EQ(store.save_shelf_audit(recheck).index(), 0U);
    auto conflicting = recheck;
    conflicting.recheck_of.reset();
    EXPECT_EQ(std::get<Error>(store.save_shelf_audit(conflicting)), Error::conflict);
    conflicting.recheck_of = conflicting.key();
    EXPECT_EQ(std::get<Error>(store.save_shelf_audit(conflicting)), Error::invalid);
    for (std::size_t session = 3; session <= shelf_history_limit + 1; ++session) {
        ASSERT_EQ(
            store.save_shelf_audit({source.location, RobotId{2}, RunId{20}, session, 30, "later"})
                .index(),
            0U);
    }
    ASSERT_EQ(store.save_shelf_audit(recheck).index(), 0U);
    const auto history = std::get<std::vector<ShelfAudit>>(store.shelf_history(source.location));
    ASSERT_EQ(history.size(), shelf_history_limit);
    EXPECT_EQ(history.back().key(), recheck.key());
    EXPECT_EQ(history.back().recheck_of, source.key());
}

TEST(MissionStore, ReservePreemptsWorkBeforeTheBatterySignal) {
    Database db;
    auto store = std::get<Store>(Store::open(db.file()));
    const auto running = start(store);
    auto budget = ready();
    budget.energy_uj = *budget.return_energy_uj + reserve_uj + budget.next_step_energy_uj;
    const auto entered = std::get<Cycle>(store.cycle(running.lease(), budget));
    EXPECT_EQ(entered.motion, Motion::brake);
    EXPECT_EQ(entered.record.state, State::returning);
    EXPECT_EQ(entered.record.cause, Cause::reserve);
    EXPECT_EQ(std::get<Error>(store.checkpoint(running.lease(), {.sample = 1})), Error::stale);
    const auto driving = std::get<Cycle>(store.cycle(entered.record.lease(), budget));
    EXPECT_EQ(driving.motion, Motion::return_to_dock);
    budget.docked = true;
    const auto docked = std::get<Cycle>(store.cycle(entered.record.lease(), budget));
    EXPECT_EQ(docked.record.state, State::paused);
    EXPECT_EQ(docked.record.cause, Cause::arrival);
}

TEST(MissionStore, RecoveryRequiresRelocalizationAndPreservesUnfinishedWork) {
    const auto id = MissionId{1};
    Database db;
    Lease before{id, 0};
    {
        auto store = std::get<Store>(Store::open(db.file()));
        const auto running = start(store);
        before = running.lease();
        EXPECT_EQ(std::get<Record>(store.checkpoint(before, {.sample = 1,
                                                             .elapsed_ms = 100,
                                                             .planned_scans = 2,
                                                             .covered_scans = 1,
                                                             .raw_reads = 8,
                                                             .covered_mask = 1}))
                      .checkpoint.covered_scans,
                  1U);
    }
    auto restarted = std::get<Store>(Store::open(db.file()));
    EXPECT_EQ(std::get<std::size_t>(restarted.recover()), 1U);
    EXPECT_EQ(std::get<std::size_t>(restarted.recover()), 0U);
    EXPECT_EQ(std::get<Error>(restarted.cycle(before, ready())), Error::stale);
    const auto paused = std::get<Record>(restarted.get(id));
    EXPECT_EQ(paused.state, State::paused);
    EXPECT_EQ(paused.cause, Cause::restart_relocalize);
    EXPECT_EQ(paused.checkpoint.covered_mask, 1U);
    EXPECT_EQ(std::get<Error>(restarted.resume(id, {0, 0, 0, false})), Error::invalid);
    EXPECT_FALSE(std::get<std::optional<Record>>(restarted.dispatch()));
    EXPECT_EQ(std::get<Record>(restarted.resume(id, {10, 0.1, 0.1, true})).state, State::queued);
    const auto resumed = std::get<std::optional<Record>>(restarted.dispatch()).value();
    EXPECT_EQ(resumed.checkpoint.covered_scans, 1U);
    EXPECT_EQ(resumed.state, State::planning);
}

TEST(MissionStore, CompletionRequiresCoverageAndCannotBeRecordedTwice) {
    Database db;
    auto store = std::get<Store>(Store::open(db.file()));
    const auto running = start(store);
    auto done = ready();
    done.work_finished = true;
    EXPECT_EQ(std::get<Cycle>(store.cycle(running.lease(), done)).record.state, State::executing);
    EXPECT_EQ(std::get<Error>(store.checkpoint(
                  running.lease(), {.sample = 1, .planned_scans = 1, .covered_scans = 1})),
              Error::invalid);
    static_cast<void>(std::get<Record>(store.checkpoint(
        running.lease(),
        {.sample = 1, .planned_scans = 1, .covered_scans = 1, .covered_mask = 1})));
    const auto finished = std::get<Cycle>(store.cycle(running.lease(), done)).record;
    EXPECT_EQ(finished.state, State::completed);
    EXPECT_EQ(std::get<Error>(store.cycle(running.lease(), done)), Error::stale);
    EXPECT_EQ(std::get<Error>(store.cycle(finished.lease(), done)), Error::illegal_transition);
    EXPECT_EQ(std::get<std::vector<Event>>(store.events(finished.intent.id)).size(), 5U);
}

TEST(MissionState, DegradationObstructionAndTimeoutReleaseMotion) {
    Record record{scan(), State::executing, Cause::ready, 4};
    auto input = ready();
    input.obstructed = true;
    EXPECT_EQ(decide(record, input).action, Action::obstruct);
    EXPECT_EQ(decide(record, input).motion, Motion::brake);
    record.state = State::blocked;
    EXPECT_FALSE(decide(record, input).action);
    input.obstructed = false;
    EXPECT_EQ(decide(record, input).action, Action::retry);
    input.localized = false;
    EXPECT_EQ(decide(record, input).cause, Cause::localization_lost);
    input.localized = true;
    input.sensors_ok = false;
    EXPECT_EQ(decide(record, input).cause, Cause::sensor_degraded);
    input.storage_ok = false;
    EXPECT_EQ(decide(record, input).cause, Cause::storage_error);
    input = ready();
    record.checkpoint.elapsed_ms = record.intent.limit_ms;
    EXPECT_EQ(decide(record, input).action, Action::fail);
    EXPECT_EQ(decide(record, input).cause, Cause::timeout);
}

} // namespace
