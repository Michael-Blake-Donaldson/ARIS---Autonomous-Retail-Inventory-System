#include "argus/inventory/catalog.hpp"

#include <algorithm>
#include <array>
#include <optional>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace argus::inventory {
namespace {

std::optional<Format> file_format(std::string_view leaf) {
    if (leaf.size() > 80) {
        return std::nullopt;
    }
    const auto dot = leaf.find('.');
    if (dot == std::string_view::npos || dot == 0) {
        return std::nullopt;
    }
    const auto stem = leaf.substr(0, dot);
    if (!std::ranges::all_of(stem, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        })) {
        return std::nullopt;
    }
    constexpr std::array<std::string_view, 4> devices{"con", "prn", "aux", "nul"};
    if (std::ranges::find(devices, stem) != devices.end() ||
        (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
         stem.back() >= '0' && stem.back() <= '9')) {
        return std::nullopt;
    }
    const auto extension = leaf.substr(dot);
    if (extension == ".csv") {
        return Format::csv;
    }
    if (extension == ".json") {
        return Format::json;
    }
    return std::nullopt;
}

template <typename Reader> Result<Catalog, Error> read_catalog(Format format, Reader &&read) {
    std::array<char, 4096> buffer{};
    std::string bytes;
    while (true) {
        const auto result = read(buffer);
        if (const auto *error = std::get_if<Error>(&result)) {
            return *error;
        }
        const auto count = std::get<std::size_t>(result);
        if (count == 0) {
            return parse_catalog(std::string_view{bytes}, format);
        }
        if (count > import_byte_limit - bytes.size()) {
            return Error::too_large;
        }
        bytes.append(buffer.data(), count);
    }
}

#ifdef _WIN32

class OpenFile {
  public:
    explicit OpenFile(HANDLE handle) noexcept : handle_{handle} {}
    OpenFile(const OpenFile &) = delete;
    OpenFile &operator=(const OpenFile &) = delete;
    ~OpenFile() {
        if (valid()) {
            CloseHandle(handle_);
        }
    }
    [[nodiscard]] bool valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

  private:
    HANDLE handle_;
};

Result<Catalog, Error> load(const std::filesystem::path &root, std::string_view filename,
                            Format format) {
    const OpenFile directory{CreateFileW(
        root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if (!directory.valid() || !GetFileInformationByHandle(directory.get(), &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return Error::invalid_import_path;
    }

    // Keep the configured directory open without delete sharing while resolving the leaf.
    std::array<wchar_t, 4096> resolved{};
    const auto size = GetFinalPathNameByHandleW(directory.get(), resolved.data(),
                                                static_cast<DWORD>(resolved.size()),
                                                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (size == 0 || size >= resolved.size()) {
        return Error::invalid_import_path;
    }
    std::wstring path{resolved.data(), size};
    path.push_back(L'\\');
    for (const char c : filename) {
        path.push_back(static_cast<wchar_t>(c));
    }
    const OpenFile file{
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (!file.valid() || GetFileType(file.get()) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(file.get(), &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        info.nNumberOfLinks != 1) {
        return Error::invalid_import_path;
    }
    if (info.nFileSizeHigh != 0 || info.nFileSizeLow > import_byte_limit) {
        return Error::too_large;
    }
    return read_catalog(format, [&](auto &buffer) -> Result<std::size_t, Error> {
        DWORD count = 0;
        if (!ReadFile(file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count,
                      nullptr)) {
            return Error::input_failure;
        }
        return static_cast<std::size_t>(count);
    });
}

#else

class OpenFile {
  public:
    explicit OpenFile(int handle) noexcept : handle_{handle} {}
    OpenFile(const OpenFile &) = delete;
    OpenFile &operator=(const OpenFile &) = delete;
    ~OpenFile() {
        if (valid()) {
            close(handle_);
        }
    }
    [[nodiscard]] bool valid() const noexcept { return handle_ >= 0; }
    [[nodiscard]] int get() const noexcept { return handle_; }

  private:
    int handle_;
};

Result<Catalog, Error> load(const std::filesystem::path &root, std::string_view filename,
                            Format format) {
    const OpenFile directory{open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (!directory.valid()) {
        return Error::invalid_import_path;
    }
    const std::string name{filename};
    const OpenFile file{
        openat(directory.get(), name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
    struct stat info {};
    if (!file.valid() || fstat(file.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_nlink != 1 || info.st_size < 0) {
        return Error::invalid_import_path;
    }
    if (static_cast<std::uintmax_t>(info.st_size) > import_byte_limit) {
        return Error::too_large;
    }
    return read_catalog(format, [&](auto &buffer) -> Result<std::size_t, Error> {
        ssize_t count{};
        do {
            count = read(file.get(), buffer.data(), buffer.size());
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            return Error::input_failure;
        }
        return static_cast<std::size_t>(count);
    });
}

#endif

} // namespace

Result<Catalog, Error> load_catalog(const std::filesystem::path &import_directory,
                                    std::string_view filename) {
    const auto format = file_format(filename);
    const auto &root = import_directory.native();
    if (!format || root.empty() || root.size() > 4096 ||
        root.find(std::filesystem::path::value_type{}) !=
            std::filesystem::path::string_type::npos) {
        return Error::invalid_import_path;
    }
    return load(import_directory, filename, *format);
}

} // namespace argus::inventory
