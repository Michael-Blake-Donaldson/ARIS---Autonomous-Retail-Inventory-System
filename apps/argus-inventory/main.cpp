#include "argus/inventory/repository.hpp"

#include <exception>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

using namespace argus::inventory;

constexpr std::string_view usage = "Usage: argus-inventory DATABASE import DIRECTORY FILE.csv\n"
                                   "       argus-inventory DATABASE import DIRECTORY FILE.json\n"
                                   "       argus-inventory DATABASE find DPCI [AFTER_EPC]\n";

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

int run(int argc, char **argv) {
    if (argc == 2 && argument(argv[1], 6) == "--help") {
        return (std::cout << usage).flush() ? 0 : 1;
    }
    if (argc >= 4 && argc <= 5) {
        const auto path = argument(argv[1], 4096);
        const auto action = argument(argv[2], 6);
        if (!path.empty() && path.size() <= 4096 && action == "find") {
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
