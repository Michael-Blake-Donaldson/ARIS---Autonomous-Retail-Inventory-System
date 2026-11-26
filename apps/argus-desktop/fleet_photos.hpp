#pragma once

#include <array>
#include <string_view>

namespace argus::desktop {

class FleetPhotos {
  public:
    FleetPhotos() = default;
    FleetPhotos(const FleetPhotos &) = delete;
    FleetPhotos &operator=(const FleetPhotos &) = delete;
    ~FleetPhotos();
    void load();
    void clear() noexcept;
    [[nodiscard]] unsigned texture(std::string_view dpci) const noexcept;
    [[nodiscard]] bool ready() const noexcept;

  private:
    std::array<unsigned, 3> textures_{};
};

} // namespace argus::desktop
