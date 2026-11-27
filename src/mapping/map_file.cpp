#include "argus/mapping/map_io.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace argus::mapping {
namespace {

bool valid_leaf(std::string_view leaf) {
    constexpr std::string_view extension = ".argmap";
    if (leaf.size() <= extension.size() || leaf.size() > 80 || !leaf.ends_with(extension)) {
        return false;
    }
    const auto stem = leaf.substr(0, leaf.size() - extension.size());
    constexpr std::array<std::string_view, 4> devices{"con", "prn", "aux", "nul"};
    return std::ranges::find(devices, stem) == devices.end() &&
           !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
             stem.back() >= '0' && stem.back() <= '9') &&
           std::ranges::all_of(stem, [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
           });
}

std::string temporary_name() {
    std::random_device seed;
    return ".argus-map-" + std::to_string(seed()) + "-" + std::to_string(seed()) + ".tmp";
}

#ifdef _WIN32

class Handle {
  public:
    explicit Handle(HANDLE value) noexcept : value_{value} {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() {
        if (valid()) {
            CloseHandle(value_);
        }
    }
    [[nodiscard]] bool valid() const noexcept { return value_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }

  private:
    HANDLE value_;
};

bool publish(const std::filesystem::path &root, std::string_view leaf, std::string_view bytes) {
    const Handle directory{CreateFileW(
        root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if (!directory.valid() || !GetFileInformationByHandle(directory.get(), &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }
    std::array<wchar_t, 4096> resolved{};
    const auto size = GetFinalPathNameByHandleW(directory.get(), resolved.data(),
                                                static_cast<DWORD>(resolved.size()),
                                                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (size == 0 || size >= resolved.size()) {
        return false;
    }
    const std::filesystem::path base{std::wstring_view{resolved.data(), size}};
    const auto destination = base / leaf;
    const auto temporary = base / temporary_name();
    bool written{};
    {
        const Handle file{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!file.valid()) {
            return false;
        }
        const auto count = static_cast<DWORD>(bytes.size());
        DWORD actual = 0;
        written = WriteFile(file.get(), bytes.data(), count, &actual, nullptr) && actual == count &&
                  FlushFileBuffers(file.get());
    }
    if (written && MoveFileExW(temporary.c_str(), destination.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    DeleteFileW(temporary.c_str());
    return false;
}

#else

class Handle {
  public:
    explicit Handle(int value) noexcept : value_{value} {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() {
        if (valid()) {
            close(value_);
        }
    }
    [[nodiscard]] bool valid() const noexcept { return value_ >= 0; }
    [[nodiscard]] int get() const noexcept { return value_; }

  private:
    int value_;
};

bool write_all(int file, std::string_view bytes) {
    while (!bytes.empty()) {
        const auto actual = write(file, bytes.data(), bytes.size());
        if (actual < 0 && errno == EINTR) {
            continue;
        }
        if (actual <= 0) {
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(actual));
    }
    return fsync(file) == 0;
}

bool publish(const std::filesystem::path &root, std::string_view leaf, std::string_view bytes) {
    const Handle directory{open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (!directory.valid()) {
        return false;
    }
    const std::string destination{leaf};
    const auto temporary = temporary_name();
    const Handle file{openat(directory.get(), temporary.c_str(),
                             O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (!file.valid()) {
        return false;
    }
    if (!write_all(file.get(), bytes) ||
        renameat(directory.get(), temporary.c_str(), directory.get(), destination.c_str()) != 0) {
        unlinkat(directory.get(), temporary.c_str(), 0);
        return false;
    }
    // Publication has happened. A failed directory sync means its durability is uncertain.
    return fsync(directory.get()) == 0;
}

#endif

} // namespace

Result<std::monostate, MapError>
save_atomic(const Grid &grid, const std::filesystem::path &directory, std::string_view leaf) {
    if (directory.empty() || directory.native().size() > 4096 ||
        directory.native().find(std::filesystem::path::value_type{}) !=
            std::filesystem::path::string_type::npos ||
        !valid_leaf(leaf)) {
        return MapError::output_failure;
    }
    try {
        std::ostringstream stream;
        const auto serialized = save(grid, stream);
        if (const auto *error = std::get_if<MapError>(&serialized)) {
            return *error;
        }
        return publish(directory, leaf, stream.view())
                   ? Result<std::monostate, MapError>{std::monostate{}}
                   : Result<std::monostate, MapError>{MapError::output_failure};
    } catch (const std::system_error &) {
        return MapError::output_failure;
    } catch (const std::runtime_error &) {
        return MapError::output_failure;
    }
}

} // namespace argus::mapping
