#include "argus/core/random.hpp"

namespace argus {

Random::Random(std::uint32_t seed) noexcept : engine_{seed} {}

std::uint32_t Random::next() noexcept { return static_cast<std::uint32_t>(engine_()); }

double Random::unit_interval() noexcept { return static_cast<double>(next()) / 4294967296.0; }

} // namespace argus
