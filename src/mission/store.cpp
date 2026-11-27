#include "argus/mission/store.hpp"

#include "../storage/sqlite_policy.hpp"

#include <limits>
#include <sqlite3.h>
#include <utility>

namespace argus::mission {
namespace {

constexpr auto identity = 0x41524d53;
constexpr std::uint64_t max_id = std::numeric_limits<std::int64_t>::max();
constexpr auto schema = R"sql(
    CREATE TABLE work (
        ordinal INTEGER PRIMARY KEY, id INTEGER NOT NULL UNIQUE CHECK(id>0),
        kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 3),
        priority INTEGER NOT NULL CHECK(priority BETWEEN 0 AND 2),
        limit_ms INTEGER NOT NULL, dpci TEXT NOT NULL, aisle TEXT NOT NULL,
        state INTEGER NOT NULL CHECK(state BETWEEN 0 AND 9),
        cause INTEGER NOT NULL CHECK(cause BETWEEN 0 AND 18),
        revision INTEGER NOT NULL CHECK(revision BETWEEN 0 AND 255),
        sample INTEGER NOT NULL DEFAULT 0, elapsed_ms INTEGER NOT NULL DEFAULT 0,
        planned_mm INTEGER NOT NULL DEFAULT 0, actual_mm INTEGER NOT NULL DEFAULT 0,
        planned_scans INTEGER NOT NULL DEFAULT 0, covered_scans INTEGER NOT NULL DEFAULT 0,
        raw_reads INTEGER NOT NULL DEFAULT 0, exceptions INTEGER NOT NULL DEFAULT 0,
        covered_mask INTEGER NOT NULL DEFAULT 0
    ) STRICT;
    CREATE INDEX queue_priority ON work(state, priority DESC, ordinal);
    CREATE UNIQUE INDEX single_active ON work((1)) WHERE state IN (2,3,5,6);
    CREATE TABLE mission_event (
        mission INTEGER NOT NULL REFERENCES work(id),
        revision INTEGER NOT NULL, before INTEGER NOT NULL, after INTEGER NOT NULL,
        cause INTEGER NOT NULL, elapsed_ms INTEGER NOT NULL,
        PRIMARY KEY(mission,revision)
    ) STRICT;
    PRAGMA application_id=0x41524d53;
    PRAGMA user_version=1;
)sql";

constexpr auto archive_schema = R"sql(
    CREATE TABLE run_archive (
        slot INTEGER PRIMARY KEY CHECK(slot=1), run_id INTEGER NOT NULL CHECK(run_id>0),
        scenario INTEGER NOT NULL CHECK(scenario>0), sample INTEGER NOT NULL CHECK(sample>0),
        mission INTEGER NOT NULL CHECK(mission>=0), search_session INTEGER NOT NULL CHECK(search_session>=0),
        audit_session INTEGER NOT NULL CHECK(audit_session>0),
        map BLOB NOT NULL CHECK(length(map) BETWEEN 1 AND 131072),
        report TEXT NOT NULL CHECK(length(CAST(report AS BLOB)) BETWEEN 1 AND 524288)
    ) STRICT;
    PRAGMA user_version=2;
)sql";

constexpr auto shelf_schema = R"sql(
    CREATE TABLE shelf_audit (
        id INTEGER PRIMARY KEY,
        location TEXT NOT NULL CHECK(location GLOB '[A-Z][0-9][0-9][A-Z][0-9][0-9]'),
        robot INTEGER NOT NULL CHECK(robot>0),
        run INTEGER NOT NULL CHECK(run>0),
        session INTEGER NOT NULL CHECK(session>0),
        completed_ms INTEGER NOT NULL CHECK(completed_ms>0),
        report TEXT NOT NULL CHECK(length(CAST(report AS BLOB)) BETWEEN 1 AND 131072),
        UNIQUE(location, robot, run, session)
    ) STRICT;
    CREATE INDEX shelf_audit_history ON shelf_audit(location, id DESC);
    PRAGMA user_version=3;
)sql";

constexpr auto location_schema = R"sql(
    DROP INDEX shelf_audit_history;
    ALTER TABLE shelf_audit RENAME TO shelf_audit_v3;
    CREATE TABLE shelf_audit (
        id INTEGER PRIMARY KEY,
        location TEXT NOT NULL CHECK(
            location GLOB '[A-Z][0-9][0-9][A-Z][0-9][0-9]' OR
            (location GLOB '[A-Z][0-9][0-9][A-Z][0-9][0-9]-W[0-9][0-9]' AND
             substr(location,9,2) <> '00')),
        robot INTEGER NOT NULL CHECK(robot>0),
        run INTEGER NOT NULL CHECK(run>0),
        session INTEGER NOT NULL CHECK(session>0),
        completed_ms INTEGER NOT NULL CHECK(completed_ms>0),
        report TEXT NOT NULL CHECK(length(CAST(report AS BLOB)) BETWEEN 1 AND 131072),
        UNIQUE(location, robot, run, session)
    ) STRICT;
    INSERT INTO shelf_audit SELECT id,location,robot,run,session,completed_ms,report
        FROM shelf_audit_v3;
    DROP TABLE shelf_audit_v3;
    CREATE INDEX shelf_audit_history ON shelf_audit(location, id DESC);
    PRAGMA user_version=4;
)sql";

constexpr auto review_schema = R"sql(
    ALTER TABLE shelf_audit ADD COLUMN reviewed_ms INTEGER
        CHECK(reviewed_ms IS NULL OR reviewed_ms >= completed_ms);
    ALTER TABLE shelf_audit ADD COLUMN source_robot INTEGER CHECK(source_robot > 0);
    ALTER TABLE shelf_audit ADD COLUMN source_run INTEGER CHECK(source_run > 0);
    ALTER TABLE shelf_audit ADD COLUMN source_session INTEGER CHECK(
        (source_robot IS NULL AND source_run IS NULL AND source_session IS NULL) OR
        (source_robot IS NOT NULL AND source_run IS NOT NULL AND
         source_session IS NOT NULL AND source_session > 0 AND
         (source_robot <> robot OR source_run <> run OR source_session <> session)));
    PRAGMA user_version=5;
)sql";

[[noreturn]] void fail(int code) {
    switch (code & 0xff) {
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        throw Error::busy;
    case SQLITE_FULL:
        throw Error::storage_full;
    case SQLITE_READONLY:
    case SQLITE_PERM:
        throw Error::read_only;
    default:
        throw Error::storage;
    }
}

void require(int code) {
    if (code != SQLITE_OK) {
        fail(code);
    }
}

void exec(sqlite3 *db, const char *sql) {
    require(sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}

class Transaction {
  public:
    explicit Transaction(sqlite3 *db) : db_{db} { exec(db_, "BEGIN IMMEDIATE"); }
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    ~Transaction() {
        if (db_) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }
    void commit() {
        exec(db_, "COMMIT");
        db_ = nullptr;
    }

  private:
    sqlite3 *db_;
};

struct Finalize {
    void operator()(sqlite3_stmt *stmt) const noexcept { sqlite3_finalize(stmt); }
};

class Statement {
  public:
    Statement(sqlite3 *db, const char *sql) {
        sqlite3_stmt *stmt = nullptr;
        const int code = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        stmt_.reset(stmt);
        require(code);
    }
    void put(int index, std::uint64_t value) {
        require(sqlite3_bind_int64(stmt_.get(), index, static_cast<std::int64_t>(value)));
    }
    void put(int index, std::string_view value) {
        require(sqlite3_bind_text(stmt_.get(), index, value.empty() ? "" : value.data(),
                                  static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }
    void put_null(int index) { require(sqlite3_bind_null(stmt_.get(), index)); }
    [[nodiscard]] bool is_null(int column) const {
        return sqlite3_column_type(stmt_.get(), column) == SQLITE_NULL;
    }
    void put_blob(int index, std::string_view bytes) {
        require(sqlite3_bind_blob(stmt_.get(), index, bytes.data(), static_cast<int>(bytes.size()),
                                  SQLITE_TRANSIENT));
    }
    template <typename Enum> void put_enum(int index, Enum value) {
        put(index, static_cast<std::uint64_t>(value));
    }
    bool row() {
        const auto code = sqlite3_step(stmt_.get());
        if (code == SQLITE_ROW) {
            return true;
        }
        if (code != SQLITE_DONE) {
            fail(code);
        }
        return false;
    }
    void write() {
        if (row()) {
            throw Error::storage;
        }
    }
    [[nodiscard]] std::uint64_t integer(int column) const {
        const auto value = sqlite3_column_int64(stmt_.get(), column);
        if (value < 0) {
            throw Error::storage;
        }
        return static_cast<std::uint64_t>(value);
    }
    [[nodiscard]] std::string text(int column) const {
        const auto *value = sqlite3_column_text(stmt_.get(), column);
        const auto count = sqlite3_column_bytes(stmt_.get(), column);
        if (!value || count < 0 || count > 32) {
            throw Error::storage;
        }
        return {reinterpret_cast<const char *>(value), static_cast<std::size_t>(count)};
    }
    [[nodiscard]] std::string bytes(int column, int type, std::size_t limit) const {
        if (sqlite3_column_type(stmt_.get(), column) != type) {
            throw Error::storage;
        }
        const auto count = sqlite3_column_bytes(stmt_.get(), column);
        if (count <= 0 || static_cast<std::size_t>(count) > limit) {
            throw Error::storage;
        }
        const auto *value =
            type == SQLITE_BLOB
                ? static_cast<const char *>(sqlite3_column_blob(stmt_.get(), column))
                : reinterpret_cast<const char *>(sqlite3_column_text(stmt_.get(), column));
        if (!value) {
            throw Error::storage;
        }
        return {value, static_cast<std::size_t>(count)};
    }

  private:
    std::unique_ptr<sqlite3_stmt, Finalize> stmt_;
};

std::uint64_t scalar(sqlite3 *db, const char *sql) {
    Statement get{db, sql};
    if (!get.row()) {
        throw Error::storage;
    }
    return get.integer(0);
}

bool valid_key(ShelfAuditKey key) noexcept {
    return key.robot.value() > 0 && key.robot.value() <= max_id && key.run.value() > 0 &&
           key.run.value() <= max_id && key.session > 0 && key.session <= max_id;
}

void bind_key(Statement &statement, int index, ShelfAuditKey key) {
    statement.put(index, key.robot.value());
    statement.put(index + 1, key.run.value());
    statement.put(index + 2, key.session);
}

std::optional<ShelfAuditKey> read_source(const Statement &statement, int index) {
    if (statement.is_null(index) && statement.is_null(index + 1) && statement.is_null(index + 2)) {
        return std::nullopt;
    }
    const ShelfAuditKey key{RobotId{statement.integer(index)}, RunId{statement.integer(index + 1)},
                            statement.integer(index + 2)};
    if (!valid_key(key)) {
        throw Error::storage;
    }
    return key;
}

template <typename T, typename F> Result<T, Error> checked(F &&operation) {
    try {
        return std::forward<F>(operation)();
    } catch (Error error) {
        return error;
    }
}

template <typename T, typename F> Result<T, Error> write(sqlite3 *db, F &&operation) {
    return checked<T>([&] {
        Transaction transaction{db};
        auto result = std::forward<F>(operation)();
        transaction.commit();
        return result;
    });
}

bool valid_id(MissionId id) noexcept { return id.value() > 0 && id.value() <= max_id; }

std::optional<Record> lookup(sqlite3 *db, MissionId id) {
    if (!valid_id(id)) {
        throw Error::invalid;
    }
    Statement get{db, R"sql(
        SELECT kind,priority,limit_ms,dpci,aisle,state,cause,revision,sample,elapsed_ms,
               planned_mm,actual_mm,planned_scans,covered_scans,raw_reads,exceptions,covered_mask
        FROM work WHERE id=?1
    )sql"};
    get.put(1, id.value());
    if (!get.row()) {
        return std::nullopt;
    }
    Intent intent{id, static_cast<Workflow>(get.integer(0)), static_cast<Priority>(get.integer(1)),
                  get.integer(2)};
    auto dpci = get.text(3);
    if (!dpci.empty()) {
        intent.product = inventory::Dpci{std::move(dpci)};
    }
    auto aisle = get.text(4);
    if (!aisle.empty()) {
        intent.aisle = inventory::AisleId{std::move(aisle)};
    }
    return Record{std::move(intent),
                  static_cast<State>(get.integer(5)),
                  static_cast<Cause>(get.integer(6)),
                  get.integer(7),
                  {get.integer(8), get.integer(9), get.integer(10), get.integer(11),
                   get.integer(12), get.integer(13), get.integer(14), get.integer(15),
                   get.integer(16)}};
}

Record read(sqlite3 *db, MissionId id) {
    auto record = lookup(db, id);
    if (!record) {
        throw Error::not_found;
    }
    return std::move(*record);
}

Record owned(sqlite3 *db, Lease lease) {
    auto record = read(db, lease.id);
    if (record.revision != lease.revision) {
        throw Error::stale;
    }
    if (record.state != State::planning && record.state != State::executing &&
        record.state != State::blocked && record.state != State::returning) {
        throw Error::illegal_transition;
    }
    return record;
}

void append(sqlite3 *db, const Record &record, State before) {
    Statement event{db, "INSERT INTO mission_event VALUES(?1,?2,?3,?4,?5,?6)"};
    event.put(1, record.intent.id.value());
    event.put(2, record.revision);
    event.put_enum(3, before);
    event.put_enum(4, record.state);
    event.put_enum(5, record.cause);
    event.put(6, record.checkpoint.elapsed_ms);
    event.write();
}

void apply(sqlite3 *db, Record &record, Action action, Cause cause) {
    const auto destination = next(record.state, action);
    if (!destination) {
        throw Error::illegal_transition;
    }
    // Leave room for a recovery and a terminal decision even after many preemptions.
    if (record.revision >= max_revision || (record.revision >= max_revision - 2 &&
                                            !terminal(*destination) && action != Action::recover)) {
        throw Error::full;
    }
    const auto before = record.state;
    ++record.revision;
    record.state = *destination;
    record.cause = cause;
    Statement update{db, "UPDATE work SET state=?1,cause=?2,revision=?3 WHERE id=?4"};
    update.put_enum(1, record.state);
    update.put_enum(2, record.cause);
    update.put(3, record.revision);
    update.put(4, record.intent.id.value());
    update.write();
    append(db, record, before);
}

bool progresses(const Checkpoint &a, const Checkpoint &b) noexcept {
    return b.sample > a.sample && b.elapsed_ms >= a.elapsed_ms && b.planned_mm >= a.planned_mm &&
           b.actual_mm >= a.actual_mm && b.planned_scans >= a.planned_scans &&
           b.covered_scans >= a.covered_scans && b.raw_reads >= a.raw_reads &&
           b.exceptions >= a.exceptions && (a.covered_mask & b.covered_mask) == a.covered_mask;
}

} // namespace

void Store::Close::operator()(sqlite3 *db) const noexcept { sqlite3_close_v2(db); }

Store::Store(std::unique_ptr<sqlite3, Close> database) noexcept : database_{std::move(database)} {}

Result<Store, Error> Store::open(const std::filesystem::path &file) {
    return open(file, storage::max_database_bytes);
}

Result<Store, Error> Store::open(const std::filesystem::path &file, std::uint32_t byte_limit) {
    return checked<Store>([&] {
        if (file.empty() || file.native().size() > 4096 || byte_limit == 0 ||
            byte_limit > storage::max_database_bytes) {
            throw Error::invalid;
        }
        const auto path = file.u8string();
        if (path.size() > 4096 || path.find(u8'\0') != std::u8string::npos ||
            path.starts_with(u8"file:") || path == u8":memory:") {
            throw Error::invalid;
        }
        sqlite3 *handle = nullptr;
        const auto code = sqlite3_open_v2(
            reinterpret_cast<const char *>(path.c_str()), &handle,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOFOLLOW, nullptr);
        std::unique_ptr<sqlite3, Close> db{handle};
        require(code);
        require(sqlite3_busy_timeout(db.get(), 1500));
        require(sqlite3_db_config(db.get(), SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr));
        require(sqlite3_db_config(db.get(), SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr));
        sqlite3_limit(db.get(), SQLITE_LIMIT_LENGTH, 1024 * 1024);
        exec(db.get(), "PRAGMA foreign_keys=ON; PRAGMA synchronous=FULL");
        if (scalar(db.get(), "PRAGMA foreign_keys") != 1) {
            throw Error::storage;
        }
        Transaction transaction{db.get()};
        const auto version = scalar(db.get(), "PRAGMA user_version");
        const auto owner = scalar(db.get(), "PRAGMA application_id");
        const bool fresh = version == 0 && owner == 0 &&
                           scalar(db.get(), "SELECT count(*) FROM sqlite_schema") == 0;
        if ((!fresh && ((version < 1 || version > 5) || owner != identity)) ||
            !storage::configure_database(db.get(), scalar, byte_limit)) {
            throw Error::storage;
        }
        if (fresh) {
            exec(db.get(), schema);
        }
        if (version < 2) {
            exec(db.get(), archive_schema);
        }
        if (version < 3) {
            exec(db.get(), shelf_schema);
        }
        if (version < 4) {
            exec(db.get(), location_schema);
        }
        if (version < 5) {
            exec(db.get(), review_schema);
        }
        transaction.commit();
        return Store{std::move(db)};
    });
}

Result<std::uint64_t, Error> Store::highest_id() {
    return checked<std::uint64_t>(
        [&] { return scalar(database_.get(), "SELECT coalesce(max(id),0) FROM work"); });
}

Result<std::monostate, Error> Store::save_run(const SavedRun &run) {
    if (run.id.value() == 0 || run.id.value() > max_id || run.scenario.value() == 0 ||
        run.scenario.value() > max_id || run.sample_tick == 0 || run.sample_tick > max_id ||
        run.mission > max_id || run.search_session > max_id || run.audit_session == 0 ||
        run.audit_session > max_id || run.map.empty() || run.map.size() > archived_map_limit ||
        run.report.empty() || run.report.size() > archived_report_limit ||
        run.report.find('\0') != std::string::npos) {
        return Error::invalid;
    }
    auto *db = database_.get();
    return write<std::monostate>(db, [&] {
        Statement save{db, R"sql(
            INSERT INTO run_archive(slot,run_id,scenario,sample,mission,search_session,audit_session,map,report)
            VALUES(1,?1,?2,?3,?4,?5,?6,?7,?8)
            ON CONFLICT(slot) DO UPDATE SET run_id=excluded.run_id, scenario=excluded.scenario,
                sample=excluded.sample, mission=excluded.mission, search_session=excluded.search_session,
                audit_session=excluded.audit_session, map=excluded.map, report=excluded.report
        )sql"};
        save.put(1, run.id.value());
        save.put(2, run.scenario.value());
        save.put(3, run.sample_tick);
        save.put(4, run.mission);
        save.put(5, run.search_session);
        save.put(6, run.audit_session);
        save.put_blob(7, run.map);
        save.put(8, run.report);
        save.write();
        return std::monostate{};
    });
}

Result<std::optional<SavedRun>, Error> Store::saved_run() {
    return checked<std::optional<SavedRun>>([&]() -> std::optional<SavedRun> {
        Statement find{database_.get(), R"sql(
            SELECT run_id,scenario,sample,mission,search_session,audit_session,map,report
            FROM run_archive WHERE slot=1
        )sql"};
        if (!find.row()) {
            return std::nullopt;
        }
        return SavedRun{RunId{find.integer(0)},
                        ScenarioId{find.integer(1)},
                        find.integer(2),
                        find.integer(3),
                        find.integer(4),
                        find.integer(5),
                        find.bytes(6, SQLITE_BLOB, archived_map_limit),
                        find.bytes(7, SQLITE_TEXT, archived_report_limit)};
    });
}

bool valid_shelf_location(const inventory::BinId &location) noexcept {
    const auto &code = location.value;
    const auto digit = [](char ch) { return ch >= '0' && ch <= '9'; };
    const auto letter = [](char ch) { return ch >= 'A' && ch <= 'Z'; };
    return (code.size() == 6 || code.size() == 10) && letter(code[0]) && digit(code[1]) &&
           digit(code[2]) && letter(code[3]) && digit(code[4]) && digit(code[5]) &&
           (code.size() == 6 || (code[6] == '-' && code[7] == 'W' && digit(code[8]) &&
                                 digit(code[9]) && code.substr(8) != "00"));
}

Result<std::monostate, Error> Store::save_shelf_audit(const ShelfAudit &audit) {
    if (!valid_shelf_location(audit.location) || !valid_key(audit.key()) || audit.reviewed_ms ||
        (audit.recheck_of && (!valid_key(*audit.recheck_of) || *audit.recheck_of == audit.key())) ||
        audit.completed_ms == 0 || audit.completed_ms > max_id || audit.report.empty() ||
        audit.report.size() > shelf_report_limit || audit.report.find('\0') != std::string::npos) {
        return Error::invalid;
    }
    auto *db = database_.get();
    return write<std::monostate>(db, [&] {
        Statement save{db, R"sql(
            INSERT INTO shelf_audit(location,robot,run,session,completed_ms,report,
                                    source_robot,source_run,source_session)
            VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)
            ON CONFLICT(location,robot,run,session) DO NOTHING
        )sql"};
        save.put(1, audit.location.value);
        bind_key(save, 2, audit.key());
        save.put(5, audit.completed_ms);
        save.put(6, audit.report);
        if (audit.recheck_of) {
            bind_key(save, 7, *audit.recheck_of);
        } else {
            save.put_null(7);
            save.put_null(8);
            save.put_null(9);
        }
        save.write();
        if (sqlite3_changes(db) == 0) {
            Statement existing{db, R"sql(
                SELECT completed_ms,report,source_robot,source_run,source_session FROM shelf_audit
                WHERE location=?1 AND robot=?2 AND run=?3 AND session=?4
            )sql"};
            existing.put(1, audit.location.value);
            bind_key(existing, 2, audit.key());
            if (!existing.row() || existing.integer(0) != audit.completed_ms ||
                existing.bytes(1, SQLITE_TEXT, shelf_report_limit) != audit.report ||
                read_source(existing, 2) != audit.recheck_of) {
                throw Error::conflict;
            }
        } else if (audit.recheck_of) {
            Statement source{db, R"sql(
                SELECT robot,run,session FROM shelf_audit
                WHERE location=?1 ORDER BY id DESC LIMIT 1 OFFSET 1
            )sql"};
            source.put(1, audit.location.value);
            if (!source.row() || read_source(source, 0) != audit.recheck_of) {
                throw Error::conflict;
            }
        }
        Statement trim{db, R"sql(
            DELETE FROM shelf_audit WHERE location=?1 AND id < (
                SELECT id FROM shelf_audit WHERE location=?1 ORDER BY id DESC LIMIT 1 OFFSET ?2
            )
        )sql"};
        trim.put(1, audit.location.value);
        trim.put(2, shelf_history_limit - 1);
        trim.write();
        return std::monostate{};
    });
}

Result<std::uint64_t, Error> Store::review_shelf_audit(const inventory::BinId &location,
                                                       ShelfAuditKey key,
                                                       std::uint64_t reviewed_ms) {
    if (!valid_shelf_location(location) || !valid_key(key) || reviewed_ms == 0 ||
        reviewed_ms > max_id) {
        return Error::invalid;
    }
    auto *db = database_.get();
    return write<std::uint64_t>(db, [&] {
        Statement latest{db, R"sql(
            SELECT id,robot,run,session,completed_ms,reviewed_ms
            FROM shelf_audit WHERE location=?1 ORDER BY id DESC LIMIT 1
        )sql"};
        latest.put(1, location.value);
        if (!latest.row() || ShelfAuditKey{RobotId{latest.integer(1)}, RunId{latest.integer(2)},
                                           latest.integer(3)} != key) {
            throw Error::conflict;
        }
        if (!latest.is_null(5)) {
            return latest.integer(5);
        }
        if (reviewed_ms < latest.integer(4)) {
            throw Error::invalid;
        }
        Statement update{db, "UPDATE shelf_audit SET reviewed_ms=?1 WHERE id=?2"};
        update.put(1, reviewed_ms);
        update.put(2, latest.integer(0));
        update.write();
        return reviewed_ms;
    });
}

Result<std::vector<ShelfAudit>, Error> Store::shelf_history(const inventory::BinId &location) {
    if (!valid_shelf_location(location)) {
        return Error::invalid;
    }
    return checked<std::vector<ShelfAudit>>([&] {
        Statement entries{database_.get(), R"sql(
            SELECT robot,run,session,completed_ms,report,source_robot,source_run,source_session,
                   reviewed_ms
            FROM shelf_audit WHERE location=?1 ORDER BY id DESC LIMIT ?2
        )sql"};
        entries.put(1, location.value);
        entries.put(2, shelf_history_limit);
        std::vector<ShelfAudit> result;
        while (entries.row()) {
            ShelfAudit audit{location,
                             RobotId{entries.integer(0)},
                             RunId{entries.integer(1)},
                             entries.integer(2),
                             entries.integer(3),
                             entries.bytes(4, SQLITE_TEXT, shelf_report_limit),
                             read_source(entries, 5)};
            if (!entries.is_null(8)) {
                audit.reviewed_ms = entries.integer(8);
            }
            if (!valid_key(audit.key()) || audit.completed_ms == 0 ||
                audit.recheck_of == audit.key() ||
                (audit.reviewed_ms && *audit.reviewed_ms < audit.completed_ms)) {
                throw Error::storage;
            }
            result.push_back(std::move(audit));
        }
        return result;
    });
}

Result<Record, Error> Store::submit(const Intent &intent) {
    if (!valid(intent)) {
        return Error::invalid;
    }
    auto *db = database_.get();
    return write<Record>(db, [&] {
        if (auto existing = lookup(db, intent.id)) {
            if (existing->intent != intent) {
                throw Error::conflict;
            }
            return std::move(*existing);
        }
        if (scalar(db, "SELECT count(*) FROM work WHERE state<7") >= max_pending ||
            scalar(db, "SELECT count(*) FROM work") >= max_records) {
            throw Error::full;
        }
        Statement add{db, R"sql(
            INSERT INTO work(id,kind,priority,limit_ms,dpci,aisle,state,cause,revision)
            VALUES(?1,?2,?3,?4,?5,?6,0,0,0)
        )sql"};
        add.put(1, intent.id.value());
        add.put_enum(2, intent.workflow);
        add.put_enum(3, intent.priority);
        add.put(4, intent.limit_ms);
        add.put(5, intent.product ? std::string_view{intent.product->value} : std::string_view{});
        add.put(6, intent.aisle ? std::string_view{intent.aisle->value} : std::string_view{});
        add.write();
        Record record{intent, State::draft, Cause::submitted, 0};
        append(db, record, State::draft);
        apply(db, record, Action::enqueue, Cause::queued);
        return record;
    });
}

Result<Record, Error> Store::get(MissionId id) {
    return checked<Record>([&] { return read(database_.get(), id); });
}

Result<std::vector<Event>, Error> Store::events(MissionId id) {
    return checked<std::vector<Event>>([&] {
        if (!valid_id(id)) {
            throw Error::invalid;
        }
        Statement get{database_.get(), R"sql(
            SELECT revision,before,after,cause,elapsed_ms FROM mission_event
            WHERE mission=?1 ORDER BY revision LIMIT 256
        )sql"};
        get.put(1, id.value());
        std::vector<Event> events;
        while (get.row()) {
            events.push_back({get.integer(0), static_cast<State>(get.integer(1)),
                              static_cast<State>(get.integer(2)),
                              static_cast<Cause>(get.integer(3)), get.integer(4)});
        }
        if (events.empty()) {
            throw Error::not_found;
        }
        return events;
    });
}

Result<std::optional<Record>, Error> Store::latest() {
    return checked<std::optional<Record>>([&]() -> std::optional<Record> {
        auto *db = database_.get();
        const auto id = scalar(db, "SELECT coalesce(max(id),0) FROM work");
        return id == 0 ? std::nullopt : lookup(db, MissionId{id});
    });
}

Result<std::optional<Record>, Error> Store::dispatch(std::optional<MissionId> expected) {
    auto *db = database_.get();
    return write<std::optional<Record>>(db, [&]() -> std::optional<Record> {
        const auto candidate = scalar(db, R"sql(
            SELECT coalesce((SELECT id FROM work WHERE state=1 ORDER BY priority DESC,ordinal LIMIT 1),0)
        )sql");
        if (candidate == 0 || (expected && expected->value() != candidate)) {
            return std::nullopt;
        }
        auto incoming = read(db, MissionId{candidate});
        const auto running = scalar(db, R"sql(
            SELECT coalesce((SELECT id FROM work WHERE state IN (2,3,5,6)),0)
        )sql");
        if (running != 0) {
            return std::nullopt;
        }
        apply(db, incoming, Action::dispatch, Cause::scheduled);
        return incoming;
    });
}

Result<Record, Error> Store::cancel(MissionId id) {
    auto *db = database_.get();
    return write<Record>(db, [&] {
        auto record = read(db, id);
        apply(db, record, Action::cancel, Cause::operator_request);
        return record;
    });
}

Result<Record, Error> Store::resume(MissionId id, LocalizationProof proof) {
    if (!proof.reliable || proof.observation_tick == 0 ||
        !(proof.position_error_m >= 0 && proof.position_error_m <= 0.25) ||
        !(proof.heading_error_rad >= 0 && proof.heading_error_rad <= 0.35)) {
        return Error::invalid;
    }
    auto *db = database_.get();
    return write<Record>(db, [&] {
        auto record = read(db, id);
        apply(db, record, Action::resume, Cause::ready);
        return record;
    });
}

Result<Record, Error> Store::confirm_inspection(MissionId id) {
    auto *db = database_.get();
    return write<Record>(db, [&] {
        auto record = read(db, id);
        if (record.intent.workflow != Workflow::inspect || record.state != State::executing) {
            throw Error::illegal_transition;
        }
        apply(db, record, Action::finish, Cause::inspected);
        return record;
    });
}

Result<Record, Error> Store::abort(Lease lease, Cause cause) {
    if (cause != Cause::work_unavailable && cause != Cause::return_unavailable &&
        cause != Cause::storage_error && cause != Cause::timeout) {
        return Error::invalid;
    }
    auto *db = database_.get();
    return write<Record>(db, [&] {
        auto record = owned(db, lease);
        apply(db, record, Action::fail, cause);
        return record;
    });
}

Result<std::size_t, Error> Store::recover(std::optional<MissionId> expected) {
    auto *db = database_.get();
    return write<std::size_t>(db, [&] {
        const auto active = scalar(db, R"sql(
            SELECT coalesce((SELECT id FROM work WHERE state IN (2,3,5,6)),0)
        )sql");
        if (active == 0 || (expected && expected->value() != active)) {
            return std::size_t{0};
        }
        auto record = read(db, MissionId{active});
        apply(db, record, Action::recover, Cause::restart_relocalize);
        return std::size_t{1};
    });
}

Result<Record, Error> Store::checkpoint(Lease lease, const Checkpoint &point) {
    if (!valid(point)) {
        return Error::invalid;
    }
    auto *db = database_.get();
    return write<Record>(db, [&] {
        auto record = owned(db, lease);
        const auto &prior = record.checkpoint;
        if (point.sample <= prior.sample) {
            throw Error::stale;
        }
        const bool scan_work = record.intent.workflow == Workflow::scan_all ||
                               record.intent.workflow == Workflow::find_item;
        if (!progresses(prior, point) || (!scan_work && (point.planned_scans || point.raw_reads)) ||
            (record.state != State::executing &&
             (point.covered_scans != prior.covered_scans || point.raw_reads != prior.raw_reads))) {
            throw Error::invalid;
        }
        Statement update{db, R"sql(
            UPDATE work SET sample=?1,elapsed_ms=?2,planned_mm=?3,actual_mm=?4,
                   planned_scans=?5,covered_scans=?6,raw_reads=?7,exceptions=?8,covered_mask=?9
            WHERE id=?10
        )sql"};
        update.put(1, point.sample);
        update.put(2, point.elapsed_ms);
        update.put(3, point.planned_mm);
        update.put(4, point.actual_mm);
        update.put(5, point.planned_scans);
        update.put(6, point.covered_scans);
        update.put(7, point.raw_reads);
        update.put(8, point.exceptions);
        update.put(9, point.covered_mask);
        update.put(10, record.intent.id.value());
        update.write();
        record.checkpoint = point;
        return record;
    });
}

Result<Cycle, Error> Store::cycle(Lease lease, const Readiness &readiness) {
    auto *db = database_.get();
    return write<Cycle>(db, [&] {
        auto record = owned(db, lease);
        const auto decision = decide(record, readiness);
        if (record.state != State::returning && !decision.action) {
            Statement queued{db, "SELECT 1 FROM work WHERE state=1 AND priority>?1 LIMIT 1"};
            queued.put_enum(1, record.intent.priority);
            if (queued.row()) {
                apply(db, record, Action::pause, Cause::preempted);
                return Cycle{std::move(record), Motion::brake, Cause::preempted};
            }
        }
        if (decision.action) {
            apply(db, record, *decision.action, decision.cause);
        }
        return Cycle{std::move(record), decision.motion, decision.cause};
    });
}

} // namespace argus::mission
