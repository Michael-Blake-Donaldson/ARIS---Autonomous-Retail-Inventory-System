#pragma once

#include "fleet.hpp"
#include "fleet_photos.hpp"

namespace argus::desktop {

struct DashboardPoint {
    float x;
    float y;
};

enum class AuditAction { review, recheck };

struct DashboardLayout {
    std::optional<DashboardPoint> second_shelf;
    std::optional<DashboardPoint> map_zoom;
    std::optional<DashboardPoint> open_audit;
    std::optional<DashboardPoint> maintenance;
    std::optional<DashboardPoint> location_link;
    std::optional<DashboardPoint> return_to_berth;
    std::optional<DashboardPoint> mark_reviewed;
    std::optional<DashboardPoint> recheck;
    std::optional<DashboardPoint> compare;
    std::optional<DashboardPoint> review_filter;
    std::optional<DashboardPoint> location_search;
    bool horizontal_scroll{};
    float overflow_position{};
    float overflow_width{};
    bool shelf_ids{};
    bool hold_visible{};
    bool history_issue_visible{};
    bool location_no_match{};
    bool review_focused{};
    bool recheck_focused{};
    std::size_t products{};
    bool compared{};
    std::size_t filtered_reviews{};
};

class FleetPanel {
  public:
    void open(const std::optional<std::filesystem::path> &directory);
    void draw_sidebar(Panel &panel);
    void draw_map();
    void draw_audit(const FleetPhotos &photos, bool focused,
                    std::optional<AuditAction> requested_focus);
    void begin_frame() noexcept { layout_ = {}; }
    void check_horizontal_scroll();
    void mark_maintenance_link();
    void mark_location_link();
    [[nodiscard]] const DashboardLayout &layout() const noexcept { return layout_; }
    [[nodiscard]] Fleet *get() noexcept { return fleet_ && fleet_->ready() ? &*fleet_ : nullptr; }
    [[nodiscard]] const Fleet *get() const noexcept {
        return fleet_ && fleet_->ready() ? &*fleet_ : nullptr;
    }
    [[nodiscard]] std::size_t selected() const noexcept { return selected_; }
    [[nodiscard]] std::size_t selected_location() const noexcept { return location_; }
    void select(std::size_t index) noexcept;
    void select_location(std::size_t index);
    void focus_location_search() noexcept { focus_location_search_ = true; }

  private:
    void robots(Panel &panel);
    void locations();
    void maintenance();
    void select_aisle(std::size_t index);
    void location_row(std::size_t index);
    std::optional<Fleet> fleet_;
    std::size_t selected_{};
    std::size_t location_{};
    std::size_t aisle_{};
    std::optional<mission::ShelfAuditKey> prior_view_;
    std::size_t product_page_{};
    std::array<char, 24> shelf_filter_{};
    float zoom_{1.0F};
    float pan_x_{};
    float pan_y_{};
    bool discrepancies_only_{};
    bool review_only_{};
    bool focus_location_search_{};
    DashboardLayout layout_;
};

} // namespace argus::desktop
