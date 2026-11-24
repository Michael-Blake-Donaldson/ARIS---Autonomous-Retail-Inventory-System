#include "argus/audit/mission.hpp"

#include <charconv>
#include <exception>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

namespace {

namespace audit = argus::audit;
using namespace argus::inventory;

constexpr std::string_view usage =
    "Usage: argus-inventory DATABASE import DIRECTORY FILE.csv\n"
    "       argus-inventory DATABASE import DIRECTORY FILE.json\n"
    "       argus-inventory DATABASE find DPCI [AFTER_EPC]\n"
    "       argus-inventory DATABASE find-item DPCI SESSION [LAST_AISLE]\n"
    "       argus-inventory DATABASE audit AISLE SESSION [json|csv]\n";

std::string_view argument(const char *input, std::size_t max) noexcept {
    std::size_t size = 0;
    while (size <= max && input[size] != '\0') {
        ++size;
    }
    return {input, size};
}

int error(Error code, int status = 1) {
    std::cerr << "argus-inventory: " << label(code) << '\n';
    return status;
}

int print(const nlohmann::json &value) {
    if (!(std::cout << value.dump() << '\n').flush()) {
        std::cerr << "argus-inventory: output_failure\n";
        return 1;
    }
    return 0;
}

int import_file(const std::filesystem::path &file, std::string_view root, std::string_view leaf) {
    const auto loaded = load_catalog(std::filesystem::path{root}, leaf);
    if (const auto *rejected = std::get_if<Error>(&loaded)) {
        return error(*rejected);
    }
    auto opened = Repository::open(file);
    if (const auto *rejected = std::get_if<Error>(&opened)) {
        return error(*rejected);
    }
    const auto &catalog = std::get<Catalog>(loaded);
    const auto result = std::get<Repository>(opened).insert_catalog(catalog);
    if (const auto *rejected = std::get_if<Error>(&result)) {
        return error(*rejected);
    }
    return print({{"status", "imported"},
                  {"products", catalog.products.size()},
                  {"bins", catalog.bins.size()},
                  {"units", catalog.units.size()},
                  {"stockRows", catalog.stock.size()}});
}

int find(const std::filesystem::path &file, const Dpci &id, const std::optional<Epc> &after) {
    if (!valid(id) || (after && !valid(*after))) {
        return error(Error::invalid_data, 2);
    }
    auto opened = Repository::open(file);
    if (const auto *rejected = std::get_if<Error>(&opened)) {
        return error(*rejected);
    }
    const auto found = std::get<Repository>(opened).find(id, after);
    if (const auto *rejected = std::get_if<Error>(&found)) {
        return error(*rejected);
    }
    const auto &page = std::get<ProductPage>(found);
    auto units = nlohmann::json::array();
    for (const auto &unit : page.units) {
        units.push_back({{"epc", unit.epc.value}, {"assignedBin", unit.assigned_bin.value}});
    }
    return print({{"dpci", page.product.id.value},
                  {"name", page.product.name},
                  {"category", label(page.product.category)},
                  {"units", std::move(units)},
                  {"nextAfterEpc", page.has_more ? nlohmann::json(page.units.back().epc.value)
                                                 : nlohmann::json(nullptr)}});
}

int investigate(const std::filesystem::path &file, std::string_view action, int argc, char **argv) {
    const auto id = argument(argv[4], 19);
    std::uint64_t number = 0;
    const auto [end, ec] = std::from_chars(id.data(), id.data() + id.size(), number);
    if (id.empty() || id.size() > 19 || ec != std::errc{} || end != id.data() + id.size() ||
        number == 0 ||
        number > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return error(Error::invalid_data, 2);
    }
    audit::Export format = audit::Export::json;
    audit::Request request;
    if (action == "find-item") {
        Dpci product{std::string(argument(argv[3], 11))};
        std::optional<AisleId> last;
        if (argc == 6) {
            last = AisleId{std::string(argument(argv[5], 32))};
        }
        if (!valid(product) || (last && !valid_code(last->value))) {
            return error(Error::invalid_data, 2);
        }
        request = audit::FindItem{std::move(product), std::move(last)};
    } else {
        AisleId aisle{std::string(argument(argv[3], 32))};
        const auto output = argc == 6 ? argument(argv[5], 4) : "json";
        if (!valid_code(aisle.value) || (output != "json" && output != "csv")) {
            return error(Error::invalid_data, 2);
        }
        format = output == "csv" ? audit::Export::csv : audit::Export::json;
        request = audit::AuditAisle{std::move(aisle)};
    }
    auto opened = Repository::open(file);
    if (const auto *rejected = std::get_if<Error>(&opened)) {
        return error(*rejected);
    }
    const auto result = audit::run_demo(std::get<Repository>(opened), request, SessionId{number});
    if (const auto *rejected = std::get_if<Error>(&result)) {
        return error(*rejected);
    }
    const auto written = audit::write_report(std::cout, std::get<audit::Report>(result), format);
    if (const auto *rejected = std::get_if<Error>(&written)) {
        return error(*rejected);
    }
    return 0;
}

int run(int argc, char **argv) {
    if (argc == 2 && argument(argv[1], 6) == "--help") {
        return (std::cout << usage).flush() ? 0 : 1;
    }
    if (argc >= 4 && argc <= 6) {
        const auto path = argument(argv[1], 4096);
        const auto action = argument(argv[2], 9);
        if (!path.empty() && path.size() <= 4096 && action == "find" && argc <= 5) {
            const Dpci id{std::string{argument(argv[3], 11)}};
            std::optional<Epc> after;
            if (argc == 5) {
                after = Epc{std::string{argument(argv[4], 24)}};
            }
            return find(std::filesystem::path{path}, id, after);
        }
        if (!path.empty() && path.size() <= 4096 && action == "import" && argc == 5) {
            return import_file(std::filesystem::path{path}, argument(argv[3], 4096),
                               argument(argv[4], 80));
        }
        if (!path.empty() && path.size() <= 4096 && argc >= 5 &&
            (action == "find-item" || action == "audit")) {
            return investigate(std::filesystem::path{path}, action, argc, argv);
        }
    }
    std::cerr << usage;
    return 2;
}

} // namespace

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &) {
        return error(Error::input_failure);
    }
}
