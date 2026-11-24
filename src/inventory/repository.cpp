#include "argus/inventory/repository.hpp"

#include "../storage/sqlite_policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <sqlite3.h>
#include <utility>

namespace argus::inventory {
namespace {

constexpr std::int64_t application_id = 0x41524755;
constexpr std::int64_t schema_version = 2;
constexpr std::uint64_t max_sql_integer = std::numeric_limits<std::int64_t>::max();

constexpr auto catalog_schema = R"sql(
    CREATE TABLE products (
        dpci TEXT PRIMARY KEY NOT NULL CHECK (dpci GLOB '[0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9][0-9][0-9]'),
        name TEXT NOT NULL CHECK (length(CAST(name AS BLOB)) BETWEEN 1 AND 128),
        category INTEGER NOT NULL CHECK (category BETWEEN 0 AND 3)
    ) STRICT;
    CREATE TABLE bins (
        id TEXT PRIMARY KEY NOT NULL CHECK (length(id) BETWEEN 1 AND 32),
        aisle TEXT NOT NULL CHECK (length(aisle) BETWEEN 1 AND 32)
    ) STRICT;
    CREATE INDEX bins_by_aisle ON bins(aisle, id);
    CREATE TABLE units (
        epc TEXT PRIMARY KEY NOT NULL CHECK (length(epc) = 24 AND epc NOT GLOB '*[^0-9A-F]*'),
        product TEXT NOT NULL REFERENCES products(dpci),
        assigned_bin TEXT NOT NULL REFERENCES bins(id)
    ) STRICT;
    CREATE INDEX units_by_product ON units(product, epc);
    CREATE INDEX units_by_bin ON units(assigned_bin);
    CREATE TABLE expected_stock (
        product TEXT NOT NULL REFERENCES products(dpci),
        bin TEXT NOT NULL REFERENCES bins(id),
        quantity INTEGER NOT NULL CHECK (quantity BETWEEN 0 AND 1000000),
        PRIMARY KEY (product, bin)
    ) STRICT;
    CREATE INDEX expected_by_bin ON expected_stock(bin, product);
    PRAGMA application_id = 0x41524755;
    PRAGMA user_version = 1;
)sql";

constexpr auto evidence_schema = R"sql(
    CREATE TABLE missions (
        id INTEGER PRIMARY KEY CHECK (id > 0)
    ) STRICT;
    CREATE TABLE scan_sessions (
        id INTEGER PRIMARY KEY CHECK (id > 0),
        mission INTEGER NOT NULL REFERENCES missions(id),
        robot INTEGER NOT NULL CHECK (robot > 0),
        started_ms INTEGER NOT NULL CHECK (started_ms >= 0)
    ) STRICT;
    CREATE INDEX sessions_by_mission ON scan_sessions(mission, id);
    CREATE TABLE scan_evidence (
        id INTEGER PRIMARY KEY,
        session INTEGER NOT NULL REFERENCES scan_sessions(id),
        epc TEXT NOT NULL CHECK (length(epc) = 24 AND epc NOT GLOB '*[^0-9A-F]*'),
        timestamp_ms INTEGER NOT NULL CHECK (timestamp_ms >= 0),
        estimate_x_m REAL NOT NULL CHECK (abs(estimate_x_m) <= 1000),
        estimate_y_m REAL NOT NULL CHECK (abs(estimate_y_m) <= 1000),
        estimate_heading_rad REAL NOT NULL CHECK (abs(estimate_heading_rad) <= 3.141592653589793),
        antenna TEXT NOT NULL CHECK (length(antenna) BETWEEN 1 AND 32),
        confidence REAL NOT NULL CHECK (confidence BETWEEN 0 AND 1),
        signal_dbm REAL NOT NULL CHECK (signal_dbm BETWEEN -160 AND 20),
        raw BLOB NOT NULL CHECK (length(raw) <= 256)
    ) STRICT;
    CREATE INDEX evidence_by_session ON scan_evidence(session, id);
    CREATE INDEX evidence_by_epc ON scan_evidence(epc, id);
    PRAGMA user_version = 2;
)sql";

[[noreturn]] void fail(int code) {
    switch (code) {
    case SQLITE_CONSTRAINT_PRIMARYKEY:
    case SQLITE_CONSTRAINT_UNIQUE:
        throw Error::duplicate;
    default:
        break;
    }
    switch (code & 0xff) {
    case SQLITE_CONSTRAINT:
        throw Error::constraint;
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        throw Error::busy;
    default:
        throw Error::database_failure;
    }
}

void require(int code) {
    if (code != SQLITE_OK) {
        fail(code);
    }
}

template <typename Value, typename Function> Result<Value, Error> checked(Function &&operation) {
    try {
        return std::forward<Function>(operation)();
    } catch (Error error) {
        return error;
    }
}

void execute(sqlite3 *db, const char *sql) {
    require(sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}

class Transaction {
  public:
    explicit Transaction(sqlite3 *db, bool write = true) : db_{db} {
        execute(db_, write ? "BEGIN IMMEDIATE" : "BEGIN");
    }
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    ~Transaction() {
        if (db_) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }
    void commit() {
        execute(db_, "COMMIT");
        db_ = nullptr;
    }

  private:
    sqlite3 *db_;
};

struct Finalize {
    void operator()(sqlite3_stmt *statement) const noexcept { sqlite3_finalize(statement); }
};

class Statement {
  public:
    Statement(sqlite3 *db, const char *sql) {
        sqlite3_stmt *statement = nullptr;
        const int code = sqlite3_prepare_v3(db, sql, -1, 0, &statement, nullptr);
        statement_.reset(statement);
        require(code);
    }
    void text(int index, std::string_view value) {
        require(sqlite3_bind_text(statement_.get(), index, value.empty() ? "" : value.data(),
                                  static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }
    void integer(int index, std::uint64_t value) {
        require(sqlite3_bind_int64(statement_.get(), index, static_cast<std::int64_t>(value)));
    }
    void real(int index, double value) {
        require(sqlite3_bind_double(statement_.get(), index, value));
    }
    void blob(int index, std::span<const std::byte> value) {
        require(value.empty()
                    ? sqlite3_bind_zeroblob(statement_.get(), index, 0)
                    : sqlite3_bind_blob(statement_.get(), index, value.data(),
                                        static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }
    bool row() {
        const int code = sqlite3_step(statement_.get());
        if (code != SQLITE_ROW && code != SQLITE_DONE) {
            fail(code);
        }
        return code == SQLITE_ROW;
    }
    void apply() {
        if (row()) {
            throw Error::database_failure;
        }
        require(sqlite3_reset(statement_.get()));
        require(sqlite3_clear_bindings(statement_.get()));
    }
    [[nodiscard]] std::string text(int column) const {
        const auto *value = sqlite3_column_text(statement_.get(), column);
        const auto size = sqlite3_column_bytes(statement_.get(), column);
        if (!value || size <= 0 || size > 128) {
            throw Error::database_failure;
        }
        return {reinterpret_cast<const char *>(value), static_cast<std::size_t>(size)};
    }
    [[nodiscard]] std::int64_t integer(int column) const noexcept {
        return sqlite3_column_int64(statement_.get(), column);
    }
    [[nodiscard]] double real(int column) const noexcept {
        return sqlite3_column_double(statement_.get(), column);
    }
    [[nodiscard]] std::vector<std::byte> blob(int column) const {
        const auto *value =
            static_cast<const std::byte *>(sqlite3_column_blob(statement_.get(), column));
        const auto size = sqlite3_column_bytes(statement_.get(), column);
        if (size == 0) {
            return {};
        }
        if (!value || size < 0 || static_cast<std::size_t>(size) > max_raw_evidence) {
            throw Error::database_failure;
        }
        return {value, value + size};
    }

  private:
    std::unique_ptr<sqlite3_stmt, Finalize> statement_;
};

std::int64_t scalar(sqlite3 *db, const char *sql) {
    Statement get{db, sql};
    if (!get.row()) {
        throw Error::database_failure;
    }
    return get.integer(0);
}

void migrate(sqlite3 *db, std::uint32_t byte_limit) {
    Transaction transaction{db};
    const auto version = scalar(db, "PRAGMA user_version");
    const auto identity = scalar(db, "PRAGMA application_id");
    const bool fresh =
        version == 0 && identity == 0 && scalar(db, "SELECT count(*) FROM sqlite_schema") == 0;
    if (!fresh && (identity != application_id || version < 1 || version > schema_version)) {
        throw Error::incompatible_schema;
    }
    if (!storage::configure_database(db, scalar, byte_limit)) {
        throw Error::database_failure;
    }
    if (fresh) {
        execute(db, catalog_schema);
    }
    if (version < 2) {
        execute(db, evidence_schema);
    }
    transaction.commit();
}

bool positive_id(std::uint64_t value) noexcept { return value > 0 && value <= max_sql_integer; }

bool valid_scan(const ScanEvidence &scan) noexcept {
    const auto &pose = scan.estimated_pose;
    return valid(scan.epc) && valid_code(scan.antenna.value) &&
           scan.timestamp_ms <= max_sql_integer && std::abs(pose.x.value()) <= 1000 &&
           std::abs(pose.y.value()) <= 1000 && std::abs(pose.heading.value()) <= std::numbers::pi &&
           scan.confidence >= 0 && scan.confidence <= 1 && scan.signal_dbm >= -160 &&
           scan.signal_dbm <= 20 && scan.raw.size() <= max_raw_evidence;
}

void bind_product(Statement &statement, const Product &product) {
    statement.text(1, product.id.value);
    statement.text(2, product.name);
    statement.integer(3, static_cast<std::uint64_t>(product.category));
    statement.apply();
}

ScanSession read_session(sqlite3 *db, SessionId id) {
    Statement get{db, "SELECT mission, robot, started_ms FROM scan_sessions WHERE id=?1"};
    get.integer(1, id.value());
    if (!get.row()) {
        throw Error::not_found;
    }
    return {id, MissionId{static_cast<std::uint64_t>(get.integer(0))},
            RobotId{static_cast<std::uint64_t>(get.integer(1))},
            static_cast<std::uint64_t>(get.integer(2))};
}

std::vector<StoredEvidence> read_evidence(Statement &get, std::uint64_t after_id) {
    get.integer(2, after_id);
    get.integer(3, page_size);
    std::vector<StoredEvidence> result;
    while (get.row()) {
        result.push_back({static_cast<std::uint64_t>(get.integer(0)),
                          SessionId{static_cast<std::uint64_t>(get.integer(1))},
                          {Epc{get.text(2)},
                           static_cast<std::uint64_t>(get.integer(3)),
                           {Meters{get.real(4)}, Meters{get.real(5)}, robot::Radians{get.real(6)}},
                           AntennaId{get.text(7)},
                           get.real(8),
                           get.real(9),
                           get.blob(10)}});
    }
    return result;
}

} // namespace

void Repository::Close::operator()(sqlite3 *db) const noexcept { sqlite3_close_v2(db); }

Repository::Repository(std::unique_ptr<sqlite3, Close> db) noexcept : db_{std::move(db)} {}

Result<Repository, Error> Repository::open(const std::filesystem::path &file) {
    return open(file, storage::max_database_bytes);
}

Result<Repository, Error> Repository::open(const std::filesystem::path &file,
                                           std::uint32_t byte_limit) {
    return checked<Repository>([&] {
        if (byte_limit == 0 || byte_limit > storage::max_database_bytes) {
            throw Error::invalid_data;
        }
        const auto path = file.u8string();
        if (path.empty() || path.size() > 4096 || path.find(u8'\0') != std::u8string::npos ||
            path == u8":memory:" || path.starts_with(u8"file:")) {
            throw Error::invalid_data;
        }
        sqlite3 *handle = nullptr;
        const auto code = sqlite3_open_v2(reinterpret_cast<const char *>(path.c_str()), &handle,
                                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                              SQLITE_OPEN_NOFOLLOW | SQLITE_OPEN_EXRESCODE,
                                          nullptr);
        std::unique_ptr<sqlite3, Close> db{handle};
        require(code);
        require(sqlite3_busy_timeout(db.get(), 1500));
        require(sqlite3_db_config(db.get(), SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr));
        require(sqlite3_db_config(db.get(), SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr));
        sqlite3_limit(db.get(), SQLITE_LIMIT_LENGTH, 1024 * 1024);
        execute(db.get(), "PRAGMA foreign_keys = ON; PRAGMA synchronous = FULL");
        if (scalar(db.get(), "PRAGMA foreign_keys") != 1) {
            throw Error::database_failure;
        }
        migrate(db.get(), byte_limit);
        return Repository{std::move(db)};
    });
}

Status Repository::insert_catalog(const Catalog &catalog) {
    if (const auto status = validate(catalog); std::holds_alternative<Error>(status)) {
        return status;
    }
    return checked<std::monostate>([&] {
        Transaction transaction{db_.get()};
        Statement product{db_.get(), "INSERT INTO products(dpci,name,category) VALUES(?1,?2,?3)"};
        Statement bin{db_.get(), "INSERT INTO bins(id,aisle) VALUES(?1,?2)"};
        Statement unit{db_.get(), "INSERT INTO units(epc,product,assigned_bin) VALUES(?1,?2,?3)"};
        Statement stock{db_.get(),
                        "INSERT INTO expected_stock(product,bin,quantity) VALUES(?1,?2,?3)"};
        for (const auto &value : catalog.products) {
            bind_product(product, value);
        }
        for (const auto &value : catalog.bins) {
            bin.text(1, value.id.value);
            bin.text(2, value.aisle.value);
            bin.apply();
        }
        for (const auto &value : catalog.units) {
            unit.text(1, value.epc.value);
            unit.text(2, value.product.value);
            unit.text(3, value.assigned_bin.value);
            unit.apply();
        }
        for (const auto &value : catalog.stock) {
            stock.text(1, value.product.value);
            stock.text(2, value.bin.value);
            stock.integer(3, value.quantity);
            stock.apply();
        }
        transaction.commit();
        return std::monostate{};
    });
}

Status Repository::update_product(const Product &product) {
    if (!valid(product)) {
        return Error::invalid_data;
    }
    return checked<std::monostate>([&] {
        Statement update{db_.get(), "UPDATE products SET name=?2,category=?3 WHERE dpci=?1"};
        bind_product(update, product);
        if (sqlite3_changes(db_.get()) == 0) {
            throw Error::not_found;
        }
        return std::monostate{};
    });
}

Status Repository::remove_product(const Dpci &id) {
    if (!valid(id)) {
        return Error::invalid_data;
    }
    return checked<std::monostate>([&] {
        Statement remove{db_.get(), "DELETE FROM products WHERE dpci=?1"};
        remove.text(1, id.value);
        remove.apply();
        if (sqlite3_changes(db_.get()) == 0) {
            throw Error::not_found;
        }
        return std::monostate{};
    });
}

Result<ProductPage, Error> Repository::find(const Dpci &id, const std::optional<Epc> &after) {
    if (!valid(id) || (after && !valid(*after))) {
        return Error::invalid_data;
    }
    return checked<ProductPage>([&] {
        Transaction transaction{db_.get(), false};
        ProductPage result = [&] {
            Statement product{db_.get(), "SELECT name,category FROM products WHERE dpci=?1"};
            product.text(1, id.value);
            if (!product.row()) {
                throw Error::not_found;
            }
            return ProductPage{{id, product.text(0), static_cast<Category>(product.integer(1))},
                               {}};
        }();
        Statement units{db_.get(), R"sql(
            SELECT epc,assigned_bin FROM units
            WHERE product=?1 AND epc>?2 ORDER BY epc LIMIT ?3
        )sql"};
        units.text(1, id.value);
        units.text(2, after ? std::string_view{after->value} : std::string_view{});
        units.integer(3, page_size + 1U);
        while (units.row()) {
            if (result.units.size() == page_size) {
                result.has_more = true;
                break;
            }
            result.units.push_back({Epc{units.text(0)}, id, BinId{units.text(1)}});
        }
        transaction.commit();
        return result;
    });
}

Result<std::vector<Product>, Error> Repository::products(const std::optional<Dpci> &after) {
    if (after && !valid(*after)) {
        return Error::invalid_data;
    }
    return checked<std::vector<Product>>([&] {
        Statement get{
            db_.get(),
            "SELECT dpci,name,category FROM products WHERE dpci>?1 ORDER BY dpci LIMIT ?2"};
        get.text(1, after ? std::string_view{after->value} : std::string_view{});
        get.integer(2, page_size);
        std::vector<Product> result;
        while (get.row()) {
            result.push_back(
                {Dpci{get.text(0)}, get.text(1), static_cast<Category>(get.integer(2))});
        }
        return result;
    });
}

Result<std::uint64_t, Error> Repository::last_session() {
    return checked<std::uint64_t>([&] {
        return static_cast<std::uint64_t>(
            scalar(db_.get(), "SELECT coalesce(max(id),0) FROM scan_sessions"));
    });
}

Result<std::vector<ExpectedStock>, Error>
Repository::expected_at(const AisleId &aisle, const std::optional<ExpectedStock> &after) {
    if (!valid_code(aisle.value) ||
        (after && (!valid(after->product) || !valid_code(after->bin.value)))) {
        return Error::invalid_data;
    }
    return checked<std::vector<ExpectedStock>>([&] {
        Statement get{db_.get(), R"sql(
            SELECT e.product, e.bin, e.quantity
            FROM expected_stock e JOIN bins b ON b.id=e.bin
            WHERE b.aisle=?1 AND (e.bin,e.product)>(?2,?3)
            ORDER BY e.bin,e.product LIMIT ?4
        )sql"};
        get.text(1, aisle.value);
        get.text(2, after ? std::string_view{after->bin.value} : std::string_view{});
        get.text(3, after ? std::string_view{after->product.value} : std::string_view{});
        get.integer(4, page_size);
        std::vector<ExpectedStock> result;
        while (get.row()) {
            result.push_back({Dpci{get.text(0)}, BinId{get.text(1)},
                              static_cast<std::uint32_t>(get.integer(2))});
        }
        return result;
    });
}

Result<Assignment, Error> Repository::assignment(const Epc &epc) {
    if (!valid(epc)) {
        return Error::invalid_data;
    }
    return checked<Assignment>([&] {
        Statement get{db_.get(), R"sql(
            SELECT u.product,u.assigned_bin,b.aisle FROM units u JOIN bins b ON b.id=u.assigned_bin
            WHERE u.epc=?1
        )sql"};
        get.text(1, epc.value);
        if (!get.row()) {
            throw Error::not_found;
        }
        return Assignment{{epc, Dpci{get.text(0)}, BinId{get.text(1)}}, AisleId{get.text(2)}};
    });
}

Result<std::vector<Assignment>, Error> Repository::assigned_at(const AisleId &aisle,
                                                               const std::optional<Epc> &after) {
    if (!valid_code(aisle.value) || (after && !valid(*after))) {
        return Error::invalid_data;
    }
    return checked<std::vector<Assignment>>([&] {
        Statement get{db_.get(), R"sql(
            SELECT u.epc,u.product,u.assigned_bin FROM units u JOIN bins b ON b.id=u.assigned_bin
            WHERE b.aisle=?1 AND u.epc>?2 ORDER BY u.epc LIMIT ?3
        )sql"};
        get.text(1, aisle.value);
        get.text(2, after ? std::string_view{after->value} : std::string_view{});
        get.integer(3, page_size);
        std::vector<Assignment> result;
        while (get.row()) {
            result.push_back({{Epc{get.text(0)}, Dpci{get.text(1)}, BinId{get.text(2)}}, aisle});
        }
        return result;
    });
}

Status Repository::start_session(ScanSession session) {
    if (!positive_id(session.id.value()) || !positive_id(session.mission.value()) ||
        !positive_id(session.robot.value()) || session.started_ms > max_sql_integer) {
        return Error::invalid_data;
    }
    return checked<std::monostate>([&] {
        Transaction transaction{db_.get()};
        Statement mission{db_.get(),
                          "INSERT INTO missions(id) VALUES(?1) ON CONFLICT(id) DO NOTHING"};
        mission.integer(1, session.mission.value());
        mission.apply();
        Statement insert{
            db_.get(),
            "INSERT INTO scan_sessions(id,mission,robot,started_ms) VALUES(?1,?2,?3,?4)"};
        insert.integer(1, session.id.value());
        insert.integer(2, session.mission.value());
        insert.integer(3, session.robot.value());
        insert.integer(4, session.started_ms);
        insert.apply();
        transaction.commit();
        return std::monostate{};
    });
}

Result<ScanSession, Error> Repository::session(SessionId id) {
    if (!positive_id(id.value())) {
        return Error::invalid_data;
    }
    return checked<ScanSession>([&] { return read_session(db_.get(), id); });
}

Status Repository::record(SessionId id, std::span<const ScanEvidence> observations) {
    if (observations.size() > page_size) {
        return Error::too_large;
    }
    if (!positive_id(id.value()) || observations.empty() ||
        !std::ranges::all_of(observations, valid_scan)) {
        return Error::invalid_data;
    }
    return checked<std::monostate>([&] {
        Transaction transaction{db_.get()};
        const auto started = read_session(db_.get(), id).started_ms;
        Statement insert{db_.get(), R"sql(
            INSERT INTO scan_evidence(session,epc,timestamp_ms,estimate_x_m,estimate_y_m,
                                      estimate_heading_rad,antenna,confidence,signal_dbm,raw)
            VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)
        )sql"};
        for (const auto &scan : observations) {
            if (scan.timestamp_ms < started) {
                throw Error::invalid_data;
            }
            insert.integer(1, id.value());
            insert.text(2, scan.epc.value);
            insert.integer(3, scan.timestamp_ms);
            insert.real(4, scan.estimated_pose.x.value());
            insert.real(5, scan.estimated_pose.y.value());
            insert.real(6, scan.estimated_pose.heading.value());
            insert.text(7, scan.antenna.value);
            insert.real(8, scan.confidence);
            insert.real(9, scan.signal_dbm);
            insert.blob(10, scan.raw);
            insert.apply();
        }
        transaction.commit();
        return std::monostate{};
    });
}

Status Repository::record_rfid(SessionId id, const sensors::RfidScan &scan,
                               const AntennaId &antenna) {
    if (scan.reads.size() > page_size) {
        return Error::too_large;
    }
    const auto &header = scan.header;
    if (header.sensor != sensors::SensorId::rfid ||
        header.provenance != sensors::Provenance::simulated || header.sample_tick == 0 ||
        header.sample_tick > max_sql_integer / robot::step_ms ||
        header.timestamp_ms != header.sample_tick * robot::step_ms || !valid_code(antenna.value)) {
        return Error::invalid_data;
    }
    if (scan.reads.empty()) {
        const auto existing = session(id);
        if (const auto *error = std::get_if<Error>(&existing)) {
            return *error;
        }
        return header.timestamp_ms < std::get<ScanSession>(existing).started_ms
                   ? Status{Error::invalid_data}
                   : Status{std::monostate{}};
    }
    std::vector<ScanEvidence> readings;
    readings.reserve(scan.reads.size());
    for (const auto &item : scan.reads) {
        if (item.epc.size() != 24) {
            return Error::invalid_data;
        }
        readings.push_back({Epc{item.epc},
                            header.timestamp_ms,
                            header.estimated_pose,
                            antenna,
                            0.0,
                            item.signal_dbm,
                            {}});
    }
    return record(id, readings);
}

Result<std::vector<StoredEvidence>, Error> Repository::observations(SessionId id,
                                                                    std::uint64_t after_id) {
    if (!positive_id(id.value()) || after_id > max_sql_integer) {
        return Error::invalid_data;
    }
    return checked<std::vector<StoredEvidence>>([&] {
        Statement get{db_.get(), R"sql(
            SELECT id,session,epc,timestamp_ms,estimate_x_m,estimate_y_m,estimate_heading_rad,
                   antenna,confidence,signal_dbm,raw
            FROM scan_evidence WHERE session=?1 AND id>?2 ORDER BY id LIMIT ?3
        )sql"};
        get.integer(1, id.value());
        return read_evidence(get, after_id);
    });
}

Result<std::vector<StoredEvidence>, Error> Repository::observed(const Epc &epc,
                                                                std::uint64_t after_id) {
    if (!valid(epc) || after_id > max_sql_integer) {
        return Error::invalid_data;
    }
    return checked<std::vector<StoredEvidence>>([&] {
        Statement get{db_.get(), R"sql(
            SELECT id,session,epc,timestamp_ms,estimate_x_m,estimate_y_m,estimate_heading_rad,
                   antenna,confidence,signal_dbm,raw
            FROM scan_evidence WHERE epc=?1 AND id>?2 ORDER BY id LIMIT ?3
        )sql"};
        get.text(1, epc.value);
        return read_evidence(get, after_id);
    });
}

} // namespace argus::inventory
