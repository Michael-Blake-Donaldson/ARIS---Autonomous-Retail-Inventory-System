#include "argus/inventory/catalog.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <utility>

namespace argus::inventory {
namespace {

constexpr std::size_t max_label_bytes = 128;
constexpr std::size_t max_csv_line = 1024;

bool digit(char c) noexcept { return c >= '0' && c <= '9'; }

bool utf8_label(std::string_view bytes) noexcept {
    if (bytes.empty() || bytes.size() > max_label_bytes) {
        return false;
    }
    for (std::size_t i = 0; i < bytes.size();) {
        const auto first = static_cast<std::uint8_t>(bytes[i++]);
        if (first < 0x80) {
            if (first < 0x20 || first == 0x7f) {
                return false;
            }
            continue;
        }
        if (first < 0xc2 || first > 0xf4) {
            return false;
        }
        const auto count = first < 0xe0 ? 1U : first < 0xf0 ? 2U : 3U;
        const std::uint32_t minimum = count == 1 ? 0x80U : count == 2 ? 0x800U : 0x10000U;
        std::uint32_t scalar = first & (0x7fU >> (count + 1U));
        if (bytes.size() - i < count) {
            return false;
        }
        for (std::uint32_t offset = 0; offset < count; ++offset) {
            const auto next = static_cast<std::uint8_t>(bytes[i++]);
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            scalar = (scalar << 6U) | (next & 0x3fU);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff) ||
            (scalar >= 0x80 && scalar <= 0x9f)) {
            return false;
        }
    }
    return true;
}

std::optional<Category> category_from(std::string_view name) {
    for (const auto category :
         {Category::clothing, Category::shoes, Category::electronics, Category::home_goods}) {
        if (name == label(category)) {
            return category;
        }
    }
    return std::nullopt;
}

bool append(Catalog &catalog, std::string_view type, const std::string &id,
            const std::string &value, const std::string &detail) {
    if (type == "product") {
        const auto category = category_from(detail);
        if (!category) {
            return false;
        }
        catalog.products.push_back({Dpci{id}, value, *category});
    } else if (type == "bin" && detail.empty()) {
        catalog.bins.push_back({BinId{id}, AisleId{value}});
    } else if (type == "unit") {
        catalog.units.push_back({Epc{id}, Dpci{value}, BinId{detail}});
    } else if (type == "stock") {
        std::uint32_t quantity{};
        const auto [end, ec] =
            std::from_chars(detail.data(), detail.data() + detail.size(), quantity);
        if (ec != std::errc{} || end != detail.data() + detail.size()) {
            return false;
        }
        catalog.stock.push_back({Dpci{id}, BinId{value}, quantity});
    } else {
        return false;
    }
    return true;
}

std::optional<std::array<std::string, 4>> csv_row(std::string_view line) {
    std::array<std::string, 4> fields;
    std::size_t pos = 0;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        auto &field = fields.at(i);
        if (pos < line.size() && line[pos] == '"') {
            ++pos;
            bool closed = false;
            while (pos < line.size()) {
                const auto c = line[pos++];
                if (c != '"') {
                    field.push_back(c);
                } else if (pos < line.size() && line[pos] == '"') {
                    field.push_back(line[pos++]);
                } else {
                    closed = true;
                    break;
                }
            }
            if (!closed) {
                return std::nullopt;
            }
        } else {
            const auto delimiter = line.find(',', pos);
            const auto segment =
                line.substr(pos, delimiter == std::string_view::npos ? delimiter : delimiter - pos);
            if (segment.find('"') != std::string_view::npos) {
                return std::nullopt;
            }
            field = segment;
            pos += segment.size();
        }
        if (i + 1 == fields.size()) {
            if (pos != line.size()) {
                return std::nullopt;
            }
        } else if (pos == line.size() || line[pos++] != ',') {
            return std::nullopt;
        }
    }
    return fields;
}

Result<Catalog, Error> csv(std::string_view input) {
    Catalog catalog;
    std::size_t lines = 0;
    while (!input.empty()) {
        const auto newline = input.find('\n');
        auto line = input.substr(0, newline);
        input.remove_prefix(newline == std::string_view::npos ? input.size() : newline + 1);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if (line.size() > max_csv_line || lines > import_record_limit) {
            return Error::too_large;
        }
        if (lines++ == 0) {
            if (line != "kind,id,value,detail") {
                return Error::invalid_data;
            }
            continue;
        }
        const auto row = csv_row(line);
        if (!row || !append(catalog, row->at(0), row->at(1), row->at(2), row->at(3))) {
            return Error::invalid_data;
        }
    }
    if (lines < 2) {
        return Error::invalid_data;
    }
    return catalog;
}

using Json = nlohmann::json;

Result<Catalog, Error> json(std::string_view input) {
    try {
        std::array<std::set<std::string>, 5> keys;
        std::size_t events = 0;
        const auto callback = [&](int depth, Json::parse_event_t event, Json &value) {
            if (depth < 0 || static_cast<std::size_t>(depth) >= keys.size() ||
                ++events > import_record_limit * 12) {
                throw Error::too_large;
            }
            const auto index = static_cast<std::size_t>(depth);
            if (event == Json::parse_event_t::object_start && index + 1 < keys.size()) {
                keys.at(index + 1).clear();
            }
            if (value.is_string() &&
                value.get_ref<const std::string &>().size() > max_label_bytes) {
                throw Error::too_large;
            }
            if (event == Json::parse_event_t::key &&
                !keys.at(index).insert(value.get_ref<const std::string &>()).second) {
                throw Error::duplicate;
            }
            return true;
        };
        const auto root = Json::parse(input, callback);
        if (!root.is_object() || root.size() != 2 || root.at("version") != 1 ||
            !root.at("version").is_number_unsigned() || !root.at("records").is_array()) {
            return Error::invalid_data;
        }
        const auto &records = root.at("records");
        if (records.size() > import_record_limit) {
            return Error::too_large;
        }
        Catalog catalog;
        for (const auto &record : records) {
            if (!record.is_object() || record.size() != 4 ||
                !append(catalog, record.at("kind").get_ref<const std::string &>(),
                        record.at("id").get_ref<const std::string &>(),
                        record.at("value").get_ref<const std::string &>(),
                        record.at("detail").get_ref<const std::string &>())) {
                return Error::invalid_data;
            }
        }
        return catalog;
    } catch (Error error) {
        return error;
    } catch (const Json::exception &) {
        return Error::invalid_data;
    }
}

} // namespace

std::string_view label(Error error) noexcept {
    switch (error) {
    case Error::invalid_data:
        return "invalid_data";
    case Error::too_large:
        return "too_large";
    case Error::duplicate:
        return "duplicate";
    case Error::invalid_import_path:
        return "invalid_import_path";
    case Error::input_failure:
        return "input_failure";
    case Error::database_failure:
        return "database_failure";
    case Error::busy:
        return "busy";
    case Error::constraint:
        return "constraint";
    case Error::not_found:
        return "not_found";
    case Error::incompatible_schema:
        return "incompatible_schema";
    }
    return "unknown_error";
}

std::string_view label(Category category) noexcept {
    switch (category) {
    case Category::clothing:
        return "clothing";
    case Category::shoes:
        return "shoes";
    case Category::electronics:
        return "electronics";
    case Category::home_goods:
        return "home-goods";
    }
    return {};
}

bool valid(const Dpci &id) noexcept {
    const auto &value = id.value;
    return value.size() == 11 && value[3] == '-' && value[6] == '-' &&
           std::ranges::all_of(std::string_view{value}.substr(0, 3), digit) && digit(value[4]) &&
           digit(value[5]) && std::ranges::all_of(std::string_view{value}.substr(7), digit);
}

bool valid(const Epc &id) noexcept {
    return id.value.size() == 24 &&
           std::ranges::all_of(id.value, [](char c) { return digit(c) || (c >= 'A' && c <= 'F'); });
}

bool valid_code(std::string_view code) noexcept {
    return !code.empty() && code.size() <= 32 && code.front() >= 'A' && code.front() <= 'Z' &&
           std::ranges::all_of(
               code, [](char c) { return (c >= 'A' && c <= 'Z') || digit(c) || c == '-'; });
}

bool valid(const Product &product) noexcept {
    return valid(product.id) && utf8_label(product.name) && !label(product.category).empty();
}

Status validate(const Catalog &catalog) {
    std::size_t remaining = import_record_limit;
    for (const auto count : {catalog.products.size(), catalog.bins.size(), catalog.units.size(),
                             catalog.stock.size()}) {
        if (count > remaining) {
            return Error::too_large;
        }
        remaining -= count;
    }
    if (remaining == import_record_limit) {
        return Error::invalid_data;
    }
    std::set<std::string_view> product_ids;
    for (const auto &product : catalog.products) {
        if (!valid(product)) {
            return Error::invalid_data;
        }
        if (!product_ids.insert(product.id.value).second) {
            return Error::duplicate;
        }
    }
    std::set<std::string_view> bins;
    for (const auto &bin : catalog.bins) {
        if (!valid_code(bin.id.value) || !valid_code(bin.aisle.value)) {
            return Error::invalid_data;
        }
        if (!bins.insert(bin.id.value).second) {
            return Error::duplicate;
        }
    }
    std::set<std::string_view> epcs;
    for (const auto &unit : catalog.units) {
        if (!valid(unit.epc) || !valid(unit.product) || !valid_code(unit.assigned_bin.value)) {
            return Error::invalid_data;
        }
        if (!epcs.insert(unit.epc.value).second) {
            return Error::duplicate;
        }
    }
    std::set<std::pair<std::string_view, std::string_view>> stock;
    for (const auto &line : catalog.stock) {
        if (!valid(line.product) || !valid_code(line.bin.value) ||
            line.quantity > max_expected_quantity) {
            return Error::invalid_data;
        }
        if (!stock.emplace(line.product.value, line.bin.value).second) {
            return Error::duplicate;
        }
    }
    return std::monostate{};
}

Result<Catalog, Error> parse_catalog(std::istream &input, Format format) {
    std::string text;
    std::array<char, 4096> buffer{};
    try {
        while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
               input.gcount() > 0) {
            const auto count = static_cast<std::size_t>(input.gcount());
            if (count > import_byte_limit - text.size()) {
                return Error::too_large;
            }
            text.append(buffer.data(), count);
        }
    } catch (const std::ios_base::failure &) {
        return Error::input_failure;
    }
    if (input.bad() || !input.eof()) {
        return Error::input_failure;
    }
    return parse_catalog(std::string_view{text}, format);
}

Result<Catalog, Error> parse_catalog(std::string_view bytes, Format format) {
    if (format != Format::csv && format != Format::json) {
        return Error::invalid_data;
    }
    if (bytes.size() > import_byte_limit) {
        return Error::too_large;
    }
    if (bytes.find('\0') != std::string_view::npos) {
        return Error::invalid_data;
    }
    auto parsed = format == Format::csv ? csv(bytes) : json(bytes);
    if (const auto *catalog = std::get_if<Catalog>(&parsed)) {
        if (const auto status = validate(*catalog); std::holds_alternative<Error>(status)) {
            return std::get<Error>(status);
        }
    }
    return parsed;
}

} // namespace argus::inventory
