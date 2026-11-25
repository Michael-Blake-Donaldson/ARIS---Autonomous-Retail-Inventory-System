#pragma once

#include "scene.hpp"
#include "workbench.hpp"

#include <utility>

namespace argus::desktop {

struct Canvas {
    float x;
    float y;
    float width;
    float height;
};

class Panels {
  public:
    explicit Panels(std::optional<std::filesystem::path> directory = {})
        : workbench_{std::move(directory)} {}
    void poll();
    void request(LiveAction value) noexcept { action = prioritize(action, value); }
    [[nodiscard]] Canvas draw(const LiveFrame &frame, bool focused);
    [[nodiscard]] bool queue(Job job);
    void seed_benchmark();

    Panel panel{Panel::overview};
    Camera camera;
    Overlays overlays;
    LiveAction action{LiveAction::none};
    bool developer_truth{};
    bool map_only{};
    bool lidar_dropout{};
    bool rebuild{};
    bool search_ready{};
    bool audit_ready{};
    bool mission_ready{};
    [[nodiscard]] std::span<const EvidenceRegion> regions() const noexcept { return regions_; }
    [[nodiscard]] std::size_t painted_rows() const noexcept { return painted_rows_; }
    [[nodiscard]] bool busy() const { return workbench_.busy(); }
    [[nodiscard]] bool has_catalog_result() const noexcept { return page_.has_value(); }
    [[nodiscard]] const std::optional<mission::Record> &latest_mission() const noexcept {
        return mission_;
    }

  private:
    void panel_contents(const LiveFrame &frame, bool focused);
    void overview(const LiveFrame &frame, bool focused);
    void map(const LiveFrame &frame, bool focused);
    void find();
    void audit();
    void missions();
    void history();
    void settings();
    void diagnostics(const LiveFrame &frame);
    void inventory_table();
    void findings();

    Workbench workbench_;
    std::vector<inventory::Product> products_;
    std::optional<inventory::Dpci> catalog_cursor_;
    std::vector<std::size_t> filtered_;
    std::optional<inventory::ProductPage> page_;
    std::optional<audit::Report> report_;
    std::vector<EvidenceRegion> regions_;
    std::optional<mission::Record> mission_;
    std::vector<mission::Event> events_;
    std::string status_{"Loading demo inventory"};
    std::array<char, 12> dpci_{"990-01-0001"};
    std::array<char, 40> filter_{};
    bool filter_dirty_{true};
    bool benchmark_{};
    std::size_t painted_rows_{};
    int aisle_{};
};

void configure_style();

} // namespace argus::desktop
