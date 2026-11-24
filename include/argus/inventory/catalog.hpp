#pragma once

#include "argus/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace argus::inventory {

template <typename Tag> struct Identifier {
    std::string value;
    bool operator==(const Identifier &) const = default;
};

using Dpci = Identifier<struct DpciTag>;
using Epc = Identifier<struct EpcTag>;
using BinId = Identifier<struct BinTag>;
using AisleId = Identifier<struct AisleTag>;
using AntennaId = Identifier<struct AntennaTag>;

enum class Category { clothing, shoes, electronics, home_goods };
enum class Format { csv, json };
enum class Error {
    invalid_data,
    too_large,
    duplicate,
    invalid_import_path,
    input_failure,
    database_failure,
    busy,
    constraint,
    not_found,
    incompatible_schema
};

struct Product {
    Dpci id;
    std::string name;
    Category category;
    bool operator==(const Product &) const = default;
};

struct Bin {
    BinId id;
    AisleId aisle;
};

struct Unit {
    Epc epc;
    Dpci product;
    BinId assigned_bin;
    bool operator==(const Unit &) const = default;
};

struct ExpectedStock {
    Dpci product;
    BinId bin;
    std::uint32_t quantity;
    bool operator==(const ExpectedStock &) const = default;
};

struct Catalog {
    std::vector<Product> products;
    std::vector<Bin> bins;
    std::vector<Unit> units;
    std::vector<ExpectedStock> stock;
};

using Status = Result<std::monostate, Error>;
inline constexpr std::size_t import_byte_limit = 512 * 1024;
inline constexpr std::size_t import_record_limit = 4096;
inline constexpr std::uint32_t max_expected_quantity = 1000000;

[[nodiscard]] std::string_view label(Error error) noexcept;
[[nodiscard]] std::string_view label(Category category) noexcept;
[[nodiscard]] bool valid(const Dpci &id) noexcept;
[[nodiscard]] bool valid(const Epc &id) noexcept;
[[nodiscard]] bool valid(const Product &product) noexcept;
[[nodiscard]] bool valid_code(std::string_view code) noexcept;
[[nodiscard]] Status validate(const Catalog &catalog);
[[nodiscard]] Result<Catalog, Error> parse_catalog(std::istream &input, Format format);
[[nodiscard]] Result<Catalog, Error> parse_catalog(std::string_view bytes, Format format);
[[nodiscard]] Result<Catalog, Error> load_catalog(const std::filesystem::path &import_directory,
                                                  std::string_view filename);

} // namespace argus::inventory
