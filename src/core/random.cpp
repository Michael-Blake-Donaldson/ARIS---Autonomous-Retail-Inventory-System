#include "argus/core/random.hpp"

namespace argus {

Random::Random(std::uint32_t seed) noexcept : engine_{seed} {}

std::uint32_t Random::next() noexcept { return static_cast<std::uint32_t>(engine_()); }

} // namespace argus
