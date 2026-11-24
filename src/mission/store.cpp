#include "argus/mission/store.hpp"

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

[[noreturn]] void fail(int code) {
    throw(code & 0xff) == SQLITE_BUSY || (code & 0xff) == SQLITE_LOCKED ? Error::busy
                                                                        : Error::storage;
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
    return checked<Store>([&] {
        if (file.empty() || file.native().size() > 4096) {
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
        if (version == 0 && owner == 0 &&
            scalar(db.get(), "SELECT count(*) FROM sqlite_schema") == 0) {
            exec(db.get(), schema);
        } else if (version != 1 || owner != identity) {
            throw Error::storage;
        }
        transaction.commit();
        return Store{std::move(db)};
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

Result<std::optional<Record>, Error> Store::dispatch() {
    auto *db = database_.get();
    return write<std::optional<Record>>(db, [&]() -> std::optional<Record> {
        const auto candidate = scalar(db, R"sql(
            SELECT coalesce((SELECT id FROM work WHERE state=1 ORDER BY priority DESC,ordinal LIMIT 1),0)
        )sql");
        if (candidate == 0) {
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

Result<std::size_t, Error> Store::recover() {
    auto *db = database_.get();
    return write<std::size_t>(db, [&] {
        const auto active = scalar(db, R"sql(
            SELECT coalesce((SELECT id FROM work WHERE state IN (2,3,5,6)),0)
        )sql");
        if (active == 0) {
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
