#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace argus::storage {

inline constexpr std::uint32_t max_database_bytes = 256U * 1024 * 1024;

class CheckBudget {
  public:
    explicit CheckBudget(sqlite3 *db) noexcept : db_{db} {
        sqlite3_progress_handler(
            db_, 1000,
            [](void *ctx) {
                auto &budget = *static_cast<CheckBudget *>(ctx);
                return ++budget.callbacks_ > 20000 ? 1 : 0;
            },
            this);
    }
    CheckBudget(const CheckBudget &) = delete;
    CheckBudget &operator=(const CheckBudget &) = delete;
    ~CheckBudget() { sqlite3_progress_handler(db_, 0, nullptr, nullptr); }

  private:
    sqlite3 *db_;
    int callbacks_{};
};

// The caller runs this in the same write transaction as schema validation and migration.
template <typename Scalar>
bool configure_database(sqlite3 *db, Scalar scalar, std::uint32_t byte_limit) {
    if (byte_limit == 0 || byte_limit > max_database_bytes) {
        return false;
    }
    const auto page_size = scalar(db, "PRAGMA main.page_size");
    if (page_size < 512 || page_size > 65536) {
        return false;
    }
    const auto page_bytes = static_cast<sqlite3_int64>(page_size);
    const auto max_pages = static_cast<std::uint64_t>(byte_limit / page_bytes);
    const auto pages = static_cast<std::uint64_t>(scalar(db, "PRAGMA main.page_count"));
    if (max_pages == 0 || pages > max_pages) {
        return false;
    }
    sqlite3_file *file = nullptr;
    sqlite3_int64 file_bytes = 0;
    if (sqlite3_file_control(db, "main", SQLITE_FCNTL_FILE_POINTER, &file) != SQLITE_OK ||
        file == nullptr || file->pMethods->xFileSize(file, &file_bytes) != SQLITE_OK ||
        file_bytes < 0 || file_bytes > static_cast<sqlite3_int64>(byte_limit) ||
        file_bytes % page_bytes != 0) {
        return false;
    }
    // SQLite does not bind PRAGMA assignments. The decimal value derives from bounded integers.
    const auto limit = "PRAGMA main.max_page_count=" + std::to_string(max_pages);
    if (static_cast<std::uint64_t>(scalar(db, limit.c_str())) != max_pages) {
        return false;
    }

    const CheckBudget budget{db};
    if (scalar(db, "SELECT count(*) FROM main.sqlite_schema WHERE type IN ('trigger','view')") !=
            0 ||
        scalar(db, "SELECT count(*) FROM pragma_table_list WHERE schema='main' "
                   "AND type IN ('virtual','shadow')") != 0) {
        return false;
    }
    sqlite3_stmt *raw = nullptr;
    const int status = sqlite3_prepare_v2(db, "PRAGMA main.quick_check(1)", -1, &raw, nullptr);
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> check{raw, &sqlite3_finalize};
    if (status != SQLITE_OK || sqlite3_step(check.get()) != SQLITE_ROW) {
        return false;
    }
    const auto *message = sqlite3_column_text(check.get(), 0);
    return message != nullptr && sqlite3_column_bytes(check.get(), 0) == 2 &&
           std::string_view{reinterpret_cast<const char *>(message), 2} == "ok" &&
           sqlite3_step(check.get()) == SQLITE_DONE;
}

} // namespace argus::storage
