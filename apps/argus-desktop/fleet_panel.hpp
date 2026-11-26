#pragma once

#include "fleet.hpp"
#include "fleet_photos.hpp"

namespace argus::desktop {

class FleetPanel {
  public:
    void open(const std::optional<std::filesystem::path> &directory);
    void draw(Panel panel, const FleetPhotos &photos);
    [[nodiscard]] Fleet *get() noexcept { return fleet_ && fleet_->ready() ? &*fleet_ : nullptr; }
    [[nodiscard]] const Fleet *get() const noexcept {
        return fleet_ && fleet_->ready() ? &*fleet_ : nullptr;
    }
    [[nodiscard]] std::size_t selected() const noexcept { return selected_; }
    void select(std::size_t index) noexcept;

  private:
    void robots(bool maintenance);
    void map(const Fleet &fleet);
    void locations(const FleetPhotos &photos);
    std::optional<Fleet> fleet_;
    std::size_t selected_{};
    std::size_t history_index_{};
    bool discrepancies_only_{};
};

} // namespace argus::desktop
