#pragma once

#include <cstdint>
#include <random>

namespace argus {

class Random {
  public:
    explicit Random(std::uint32_t seed) noexcept;

    [[nodiscard]] std::uint32_t next() noexcept;
    [[nodiscard]] double unit_interval() noexcept;

  private:
    std::mt19937 engine_;
};

} // namespace argus
