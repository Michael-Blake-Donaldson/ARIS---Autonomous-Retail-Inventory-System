#include "application.hpp"
#include "argus/core/random.hpp"

#include <iostream>

namespace {

argus::app::Status run_headless(const argus::app::Options &options) {
    argus::Random random{options.seed};
    std::cout << "{\"seed\":" << options.seed << ",\"firstSample\":" << random.next() << "}\n";
    if (!std::cout.flush()) {
        return argus::app::Error::output_failure;
    }
    return std::monostate{};
}

} // namespace

int main(int argc, char **argv) {
    return argus::app::run_application(argc, argv, "argus-headless", run_headless);
}
