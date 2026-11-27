#include "argus/core/random.hpp"
#include "argus/inventory/repository.hpp"
#include "argus/simulation/fixtures.hpp"
#include "argus/simulation/robot.hpp"
#include "argus/simulation/sensors.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <limits>
#include <sqlite3.h>
#include <sstream>
#include <stdexcept>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

using namespace argus;
using namespace argus::inventory;
namespace fs = std::filesystem;

const Dpci shirt{"990-01-0001"};
const BinId first_bin{"SOFT-A1"};
const AisleId first_aisle{"SOFTLINES"};
const Epc first_epc{"A00000000000000000000001"};
const Epc unknown_epc{"FFFFFFFFFFFFFFFFFFFFFFFF"};
constexpr std::string_view csv = "kind,id,value,detail\n"
                                 "product,990-01-0001,\"North Loop, \"\"cotton\"\" tee\",clothing\n"
                                 "bin,SOFT-A1,SOFTLINES,\n"
                                 "unit,A00000000000000000000001,990-01-0001,SOFT-A1\n"
                                 "stock,990-01-0001,SOFT-A1,8\n";
constexpr std::string_view json =
    R"({"version":1,"records":[{"kind":"product","id":"990-01-0001","value":"Café tee","detail":"clothing"}]})";

class Folder {
  public:
    Folder() {
        const auto base = fs::temp_directory_path();
        const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path =
                base / ("argus-inventory-" + std::to_string(token) + "-" + std::to_string(attempt));
            if (fs::create_directory(path)) {
                return;
            }
        }
        throw std::runtime_error("Unable to create an isolated test folder");
    }
    Folder(const Folder &) = delete;
    Folder &operator=(const Folder &) = delete;
    ~Folder() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

Catalog catalog() { return std::get<Catalog>(parse_catalog(csv, Format::csv)); }

void put_file(const fs::path &path, std::string_view contents) {
    std::ofstream out{path, std::ios::binary};
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();
    ASSERT_TRUE(out);
}

using Raw = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;

Raw raw_db(const fs::path &path) {
    sqlite3 *db = nullptr;
    const auto bytes = path.u8string();
    const auto status = sqlite3_open(reinterpret_cast<const char *>(bytes.c_str()), &db);
    Raw result{db, &sqlite3_close};
    EXPECT_EQ(status, SQLITE_OK);
    return result;
}

void sql(sqlite3 *db, const char *command) {
    ASSERT_EQ(sqlite3_exec(db, command, nullptr, nullptr, nullptr), SQLITE_OK);
}

std::int64_t scalar(sqlite3 *db, const char *command) {
    sqlite3_stmt *raw = nullptr;
    const auto status = sqlite3_prepare_v2(db, command, -1, &raw, nullptr);
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement{raw, &sqlite3_finalize};
    if (status != SQLITE_OK || sqlite3_step(statement.get()) != SQLITE_ROW) {
        ADD_FAILURE() << "Invalid scalar test query";
        return -1;
    }
    return sqlite3_column_int64(statement.get(), 0);
}

TEST(CatalogImport, ReadsQuotedCsvAndUtf8JsonWithStrictIdentifiers) {
    const auto loaded = catalog();
    ASSERT_EQ(loaded.products.size(), 1U);
    EXPECT_EQ(loaded.products.front().name, "North Loop, \"cotton\" tee");
    EXPECT_EQ(loaded.units.front().epc, first_epc);
    EXPECT_EQ(loaded.stock.front().quantity, 8U);
    const auto parsed = parse_catalog(json, Format::json);
    ASSERT_TRUE(std::holds_alternative<Catalog>(parsed));
    EXPECT_EQ(std::get<Catalog>(parsed).products.front().name, "Café tee");

    auto duplicate = loaded;
    duplicate.units.push_back(duplicate.units.front());
    EXPECT_EQ(validate(duplicate), Status{Error::duplicate});
    EXPECT_FALSE(valid(Dpci{"990-01-0001'"}));
    EXPECT_FALSE(valid(Epc{"a00000000000000000000001"}));
}

TEST(CatalogImport, RejectsNulAndAnythingAfterIt) {
    std::string suffixed{json};
    suffixed.append("\0ignored", 8);
    EXPECT_EQ(std::get<Error>(parse_catalog(suffixed, Format::json)), Error::invalid_data);
}

TEST(CatalogImport, RejectsMalformedCorpusAndEnforcesResourceBounds) {
    constexpr std::array<std::string_view, 8> bad_csv{
        "",
        "wrong,header,value,detail\n",
        "kind,id,value,detail\nproduct,990-01-0001,\"unfinished,clothing",
        "kind,id,value,detail\nproduct,990-01-0001,\"name\"suffix,clothing",
        "kind,id,value,detail\nbin,A1,SOFT,extra",
        "kind,id,value,detail\nstock,990-01-0001,A1,-1",
        "kind,id,value,detail\nstock,990-01-0001,A1,4294967296",
        "kind,id,value,detail\nproduct,990-01-0001,\xc0\xaf,clothing"};
    for (const auto bytes : bad_csv) {
        EXPECT_TRUE(std::holds_alternative<Error>(parse_catalog(bytes, Format::csv)));
    }
    constexpr std::array<std::string_view, 8> bad_json{
        "[]",
        R"({"version":1,"records":[]})",
        R"({"version":1,"version":1,"records":[]})",
        R"({"version":1,"records":[{"kind":"bin","id":"A1","id":"A2","value":"SOFT","detail":""}]})",
        R"({"version":1,"records":[{"kind":"product","id":"990-01-0001","value":"\uD800","detail":"clothing"}]})",
        R"({"version":1,"records":[{"kind":"product","id":"990-01-0001","value":"x\u0000y","detail":"clothing"}]})",
        R"({"version":1,"records":[[[[[[[[]]]]]]]]})",
        R"({"version":1,"records":[{"kind":"product","id":"990-01-0001","value":1e999,"detail":"clothing"}]})"};
    for (const auto bytes : bad_json) {
        EXPECT_TRUE(std::holds_alternative<Error>(parse_catalog(bytes, Format::json)));
    }
    for (std::size_t size = 0; size < json.size(); ++size) {
        EXPECT_TRUE(
            std::holds_alternative<Error>(parse_catalog(json.substr(0, size), Format::json)));
    }
    Random fuzz{707};
    for (const auto &[source, format] :
         {std::pair{csv, Format::csv}, std::pair{json, Format::json}}) {
        for (int sample = 0; sample < 256; ++sample) {
            std::string mutated{source};
            const auto offset = fuzz.next() % mutated.size();
            mutated.at(offset) = static_cast<char>(fuzz.next() & 0xffU);
            const auto result = parse_catalog(mutated, format);
            if (const auto *accepted = std::get_if<Catalog>(&result)) {
                EXPECT_EQ(validate(*accepted), Status{std::monostate{}});
            }
        }
    }
    std::istringstream oversized{std::string(import_byte_limit + 1, 'x')};
    EXPECT_EQ(std::get<Error>(parse_catalog(oversized, Format::csv)), Error::too_large);
    std::string many_rows{"kind,id,value,detail\n"};
    for (std::size_t i = 0; i <= import_record_limit; ++i) {
        many_rows += "bin,A1,SOFT,\n";
    }
    EXPECT_EQ(std::get<Error>(parse_catalog(many_rows, Format::csv)), Error::too_large);
}

TEST(CatalogImport, RefusesTraversalLinksNonregularAndOversizedFiles) {
    Folder root;
    Folder outside;
    put_file(root.path / "inside.csv", csv);
    put_file(outside.path / "outside.csv", csv);
    EXPECT_TRUE(std::holds_alternative<Catalog>(load_catalog(root.path, "inside.csv")));

    for (const std::string_view candidate :
         {"../outside.csv", "..\\outside.csv", "/outside.csv", "C:\\outside.csv",
          "inside.csv:stream", "con.csv", "nested/inside.csv", "inside.csv.exe"}) {
        EXPECT_EQ(std::get<Error>(load_catalog(root.path, candidate)), Error::invalid_import_path);
    }
    auto nul = std::string{"inside.csv"};
    nul.push_back('\0');
    EXPECT_EQ(std::get<Error>(load_catalog(root.path, nul)), Error::invalid_import_path);
    EXPECT_TRUE(fs::create_directory(root.path / "folder.csv"));
    EXPECT_EQ(std::get<Error>(load_catalog(root.path, "folder.csv")), Error::invalid_import_path);

    std::error_code link_error;
    fs::create_symlink(outside.path / "outside.csv", root.path / "alias.csv", link_error);
#ifdef _WIN32
    if (link_error) {
        RecordProperty("symlink_fixture", "Windows symbolic link creation unavailable");
    }
#else
    ASSERT_FALSE(link_error);
#endif
    if (!link_error) {
        EXPECT_EQ(std::get<Error>(load_catalog(root.path, "alias.csv")),
                  Error::invalid_import_path);
    }
    fs::create_hard_link(outside.path / "outside.csv", root.path / "twin.csv");
    EXPECT_EQ(std::get<Error>(load_catalog(root.path, "twin.csv")), Error::invalid_import_path);
#ifndef _WIN32
    ASSERT_EQ(mkfifo((root.path / "pipe.csv").c_str(), 0600), 0);
    EXPECT_EQ(std::get<Error>(load_catalog(root.path, "pipe.csv")), Error::invalid_import_path);
#endif
    put_file(root.path / "large.json", std::string(import_byte_limit + 1, 'x'));
    EXPECT_EQ(std::get<Error>(load_catalog(root.path, "large.json")), Error::too_large);
}

TEST(InventoryRepository, ProductCrudAndReferentialConstraintsSurviveReopen) {
    Folder folder;
    const auto file = folder.path / "catalog.db";
    {
        auto repo = std::get<Repository>(Repository::open(file));
        ASSERT_EQ(repo.insert_catalog(catalog()), Status{std::monostate{}});
        const Dpci unused{"990-01-0002"};
        Catalog extra;
        extra.products.push_back({unused, "Spare sample", Category::shoes});
        EXPECT_EQ(repo.insert_catalog(extra), Status{std::monostate{}});
        EXPECT_EQ(repo.remove_product(unused), Status{std::monostate{}});
        EXPECT_EQ(repo.remove_product(unused), Status{Error::not_found});
        EXPECT_EQ(repo.remove_product(shirt), Status{Error::constraint});
        EXPECT_EQ(
            repo.update_product({shirt, "O'Brien'); DROP TABLE products; --", Category::clothing}),
            Status{std::monostate{}});
        EXPECT_EQ(repo.update_product(extra.products.front()), Status{Error::not_found});
    }
    auto repo = std::get<Repository>(Repository::open(file));
    const auto found = std::get<ProductPage>(repo.find(shirt));
    EXPECT_EQ(found.product.name, "O'Brien'); DROP TABLE products; --");
    ASSERT_EQ(found.units.size(), 1U);
    EXPECT_EQ(found.units.front(), (Unit{first_epc, shirt, first_bin}));
    EXPECT_EQ(std::get<Error>(repo.find(Dpci{"990-01-0001' OR 1=1 --"})), Error::invalid_data);
    const auto expected = std::get<std::vector<ExpectedStock>>(repo.expected_at(first_aisle));
    EXPECT_EQ(expected, (std::vector<ExpectedStock>{{shirt, first_bin, 8}}));
    EXPECT_TRUE(std::get<std::vector<ExpectedStock>>(repo.expected_at(AisleId{"TECH"})).empty());
}

TEST(InventoryRepository, RejectsDuplicateEpcAndRollsBackBeforeConstraintFailures) {
    Folder folder;
    auto repo = std::get<Repository>(Repository::open(folder.path / "rollback.db"));
    ASSERT_EQ(repo.insert_catalog(catalog()), Status{std::monostate{}});
    const Dpci extra{"990-01-0003"};
    Catalog batch;
    batch.products.push_back({extra, "Rollback marker", Category::home_goods});
    batch.units.push_back({first_epc, extra, first_bin});
    EXPECT_EQ(repo.insert_catalog(batch), Status{Error::duplicate});
    EXPECT_EQ(std::get<Error>(repo.find(extra)), Error::not_found);

    batch.units.front() = {unknown_epc, extra, BinId{"MISSING"}};
    EXPECT_EQ(repo.insert_catalog(batch), Status{Error::constraint});
    EXPECT_EQ(std::get<Error>(repo.find(extra)), Error::not_found);

    batch.units.front().assigned_bin = first_bin;
    EXPECT_EQ(repo.insert_catalog(batch), Status{std::monostate{}});
    EXPECT_EQ(std::get<ProductPage>(repo.find(extra)).units.front().epc, unknown_epc);
}

TEST(InventoryRepository, PagesAssignedEpcsWithoutDuplicates) {
    Folder folder;
    auto repo = std::get<Repository>(Repository::open(folder.path / "page.db"));
    auto entries = catalog();
    entries.units.clear();
    for (std::size_t i = 0; i <= page_size; ++i) {
        std::ostringstream hex;
        hex << std::uppercase << std::hex << std::setfill('0') << std::setw(24) << i;
        entries.units.push_back({Epc{hex.str()}, shirt, first_bin});
    }
    ASSERT_EQ(repo.insert_catalog(entries), Status{std::monostate{}});
    const auto first = std::get<ProductPage>(repo.find(shirt));
    ASSERT_EQ(first.units.size(), page_size);
    ASSERT_TRUE(first.has_more);
    const auto second = std::get<ProductPage>(repo.find(shirt, first.units.back().epc));
    EXPECT_FALSE(second.has_more);
    ASSERT_EQ(second.units.size(), 1U);
    EXPECT_EQ(second.units.front(), entries.units.back());
}

TEST(InventoryRepository, CatalogProductCursorDoesNotRepeatOrSkipRows) {
    Folder folder;
    auto repo = std::get<Repository>(Repository::open(folder.path / "products.db"));
    Catalog catalog_page;
    for (std::size_t i = 0; i <= page_size; ++i) {
        std::ostringstream digits;
        digits << "990-06-" << std::setfill('0') << std::setw(4) << i;
        catalog_page.products.push_back({Dpci{digits.str()}, "Sample", Category::home_goods});
    }
    ASSERT_EQ(repo.insert_catalog(catalog_page), Status{std::monostate{}});
    const auto first = std::get<std::vector<Product>>(repo.products());
    ASSERT_EQ(first.size(), page_size);
    const auto later = std::get<std::vector<Product>>(repo.products(first.back().id));
    ASSERT_EQ(later.size(), 1U);
    EXPECT_EQ(later.front(), catalog_page.products.back());
}

TEST(InventoryRepository, RetriesCleanlyAfterAnotherWriterReleasesTheLock) {
    Folder folder;
    const auto file = folder.path / "busy.db";
    auto repo = std::get<Repository>(Repository::open(file));
    auto other = raw_db(file);
    sql(other.get(), "BEGIN IMMEDIATE");
    EXPECT_EQ(repo.insert_catalog(catalog()), Status{Error::busy});
    sql(other.get(), "ROLLBACK");
    EXPECT_EQ(std::get<Error>(repo.find(shirt)), Error::not_found);
    EXPECT_EQ(repo.insert_catalog(catalog()), Status{std::monostate{}});
}

TEST(InventoryRepository, PersistsEvidenceWithoutChangingExpectedOrAssignedStock) {
    Folder folder;
    const auto file = folder.path / "evidence.db";
    const ScanSession session{SessionId{5}, MissionId{7}, RobotId{9}, 100};
    const ScanEvidence read{unknown_epc,
                            150,
                            {Meters{1.25}, Meters{2.5}, robot::Radians{0.5}},
                            AntennaId{"FRONT"},
                            0.75,
                            -64.5,
                            {std::byte{0}, std::byte{0xff}}};
    {
        auto repo = std::get<Repository>(Repository::open(file));
        ASSERT_EQ(repo.insert_catalog(catalog()), Status{std::monostate{}});
        ASSERT_EQ(repo.start_session(session), Status{std::monostate{}});
        EXPECT_EQ(repo.start_session(session), Status{Error::duplicate});
        EXPECT_EQ(repo.record(SessionId{100}, std::span{&read, 1}), Status{Error::not_found});

        auto early = read;
        early.timestamp_ms = 99;
        const std::array batch{read, early};
        EXPECT_EQ(repo.record(session.id, batch), Status{Error::invalid_data});
        EXPECT_TRUE(std::get<std::vector<StoredEvidence>>(repo.observations(session.id)).empty());
        auto invalid = read;
        invalid.confidence = std::numeric_limits<double>::quiet_NaN();
        EXPECT_EQ(repo.record(session.id, std::span{&invalid, 1}), Status{Error::invalid_data});
        ASSERT_EQ(repo.record(session.id, std::span{&read, 1}), Status{std::monostate{}});
    }
    auto repo = std::get<Repository>(Repository::open(file));
    EXPECT_EQ(std::get<ScanSession>(repo.session(session.id)), session);
    const auto observed = std::get<std::vector<StoredEvidence>>(repo.observed(unknown_epc));
    ASSERT_EQ(observed.size(), 1U);
    EXPECT_EQ(observed.front().session, session.id);
    EXPECT_EQ(observed.front().scan.timestamp_ms, read.timestamp_ms);
    EXPECT_EQ(observed.front().scan.estimated_pose.heading, read.estimated_pose.heading);
    EXPECT_EQ(observed.front().scan.antenna, read.antenna);
    EXPECT_EQ(observed.front().scan.confidence, read.confidence);
    EXPECT_EQ(observed.front().scan.signal_dbm, read.signal_dbm);
    EXPECT_EQ(observed.front().scan.raw, read.raw);
    EXPECT_TRUE(
        std::get<std::vector<StoredEvidence>>(repo.observed(unknown_epc, observed.front().id))
            .empty());
    EXPECT_EQ(std::get<ProductPage>(repo.find(shirt)).units.size(), 1U);
    EXPECT_EQ(std::get<std::vector<ExpectedStock>>(repo.expected_at(first_aisle)).front().quantity,
              8U);
}

TEST(InventoryRepository, RecordsOnlyRfidObservationsFromASimulatedMission) {
    Folder folder;
    auto repo = std::get<Repository>(Repository::open(folder.path / "mission.db"));
    const SessionId session{1};
    ASSERT_EQ(repo.start_session({session, MissionId{2}, RobotId{3}, 0}), Status{std::monostate{}});
    const auto json_fixture =
        std::get<std::string_view>(simulation::fixture_json("cluttered-receiving"));
    const auto warehouse =
        std::get<simulation::Warehouse>(simulation::Warehouse::from_json(json_fixture));
    auto robot =
        std::get<simulation::RobotSimulation>(simulation::RobotSimulation::create(warehouse));
    auto rig = std::get<simulation::SensorRig>(simulation::SensorRig::create(781));
    std::size_t recorded = 0;
    for (int i = 0; i < 180; ++i) {
        robot.step(
            {.target = {MetersPerSecond{0.55}, robot::RadiansPerSecond{1.1}}, .deadman = true});
        rig.capture(robot);
        while (const auto observation = rig.pop()) {
            if (const auto *scan = std::get_if<sensors::RfidScan>(&*observation)) {
                ASSERT_EQ(repo.record_rfid(session, *scan, AntennaId{"FRONT"}),
                          Status{std::monostate{}});
                recorded += scan->reads.size();
            }
        }
    }
    ASSERT_GT(recorded, 0U);
    const auto stored = std::get<std::vector<StoredEvidence>>(repo.observations(session));
    EXPECT_EQ(stored.size(), std::min(recorded, page_size));
    EXPECT_DOUBLE_EQ(stored.front().scan.confidence, 0.0);
    EXPECT_TRUE(stored.front().scan.raw.empty());
}

TEST(InventoryRepository, MigratesVersionOneAtomicallyAndRejectsForeignOrFutureDatabases) {
    Folder folder;
    const auto file = folder.path / "migration.db";
    {
        auto repo = std::get<Repository>(Repository::open(file));
        ASSERT_EQ(repo.insert_catalog(catalog()), Status{std::monostate{}});
    }
    auto raw = raw_db(file);
    sql(raw.get(), "DROP TABLE scan_evidence; DROP TABLE scan_sessions; DROP TABLE missions;"
                   "PRAGMA user_version=1; CREATE TABLE scan_sessions (conflict INTEGER)");
    EXPECT_EQ(std::get<Error>(Repository::open(file)), Error::database_failure);
    EXPECT_EQ(scalar(raw.get(), "PRAGMA user_version"), 1);
    EXPECT_EQ(scalar(raw.get(), "SELECT count(*) FROM sqlite_schema WHERE name='missions'"), 0);
    sql(raw.get(), "DROP TABLE scan_sessions");
    {
        auto migrated = std::get<Repository>(Repository::open(file));
        EXPECT_EQ(scalar(raw.get(), "PRAGMA user_version"), 2);
        EXPECT_EQ(std::get<ProductPage>(migrated.find(shirt)).units.front().epc, first_epc);
        EXPECT_EQ(migrated.start_session({SessionId{1}, MissionId{2}, RobotId{3}, 0}),
                  Status{std::monostate{}});
    }
    sql(raw.get(), "PRAGMA user_version=3");
    EXPECT_EQ(std::get<Error>(Repository::open(file)), Error::incompatible_schema);
    EXPECT_EQ(scalar(raw.get(), "PRAGMA user_version"), 3);
    const auto foreign_path = folder.path / "foreign.db";
    const auto foreign = raw_db(foreign_path);
    sql(foreign.get(), "CREATE TABLE someone_elses_data (id INTEGER)");
    EXPECT_EQ(std::get<Error>(Repository::open(foreign_path)), Error::incompatible_schema);
    EXPECT_EQ(scalar(foreign.get(), "PRAGMA user_version"), 0);
}

TEST(InventoryRepository, StorageQuotaRollsBackEveryCatalogRowOnSqliteFull) {
    Folder folder;
    const auto file = folder.path / "quota.db";
    auto repo = std::get<Repository>(Repository::open(file, 128 * 1024));
    Catalog large;
    const auto seed = catalog();
    large.products = seed.products;
    large.bins = seed.bins;
    for (std::size_t n = 1; n <= import_record_limit - 2; ++n) {
        const auto suffix = std::to_string(n);
        large.units.push_back(
            {Epc{std::string(24 - suffix.size(), '0') + suffix}, shirt, first_bin});
    }
    ASSERT_EQ(validate(large), Status{std::monostate{}});
    EXPECT_EQ(repo.insert_catalog(large), Status{Error::database_failure});
    const auto raw = raw_db(file);
    EXPECT_EQ(scalar(raw.get(), "SELECT count(*) FROM products"), 0);
    EXPECT_EQ(scalar(raw.get(), "SELECT count(*) FROM bins"), 0);
    EXPECT_EQ(scalar(raw.get(), "SELECT count(*) FROM units"), 0);
    EXPECT_EQ(repo.insert_catalog(seed), Status{std::monostate{}});
}

TEST(InventoryRepository, RejectsDamagedPagesAndUnexpectedSchemaPrograms) {
    Folder folder;
    const auto file = folder.path / "changed.db";
    {
        auto repo = std::get<Repository>(Repository::open(file));
        ASSERT_EQ(repo.insert_catalog(catalog()), Status{std::monostate{}});
    }
    {
        const auto raw = raw_db(file);
        sql(raw.get(), "CREATE VIEW unexpected AS SELECT id FROM bins");
        auto rejected = Repository::open(file);
        ASSERT_TRUE(std::holds_alternative<Error>(rejected));
        EXPECT_EQ(std::get<Error>(rejected), Error::database_failure);
        sql(raw.get(), "DROP VIEW unexpected");
    }
    const auto original_size = fs::file_size(file);
    fs::resize_file(file, original_size - 512);
    auto rejected = Repository::open(file);
    ASSERT_TRUE(std::holds_alternative<Error>(rejected));
    EXPECT_EQ(std::get<Error>(rejected), Error::database_failure);
    EXPECT_EQ(fs::file_size(file), original_size - 512);
}

} // namespace
