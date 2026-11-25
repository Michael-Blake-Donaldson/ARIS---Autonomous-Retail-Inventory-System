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
    Panels(std::unique_ptr<LiveEngine> &live, bool routed, bool paced,
           std::optional<std::filesystem::path> directory = {})
        : live_{live}, routed_{routed}, run_{live->frame().run},
          workbench_{std::move(directory), paced} {}
    void poll();
    void dispatch();
    void cancel_pending() noexcept;
    [[nodiscard]] LiveFrame frame() const noexcept;
    [[nodiscard]] bool reviewing() const noexcept { return saved_map_.has_value(); }
    [[nodiscard]] bool worker_driving() const noexcept { return !live_; }
    void interrupt(bool emergency = false) noexcept { workbench_.interrupt(emergency); }
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
    [[nodiscard]] bool busy() const { return pending_.has_value() || workbench_.busy(); }
    [[nodiscard]] bool has_catalog_result() const noexcept { return page_.has_value(); }
    [[nodiscard]] const std::optional<mission::Record> &latest_mission() const noexcept {
        return mission_;
    }
    [[nodiscard]] bool saved_ready() const noexcept { return saved_ready_; }
    [[nodiscard]] bool archive_ready() const noexcept { return archive_ready_; }
    [[nodiscard]] bool audit_has_reads() const noexcept {
        return audit_session_ != 0 && report_ && report_->session.value() == audit_session_ &&
               report_->raw_reads > 0;
    }
    [[nodiscard]] std::string_view status() const noexcept { return status_; }
    [[nodiscard]] bool save();

  private:
    void close_review() noexcept;
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

    std::unique_ptr<LiveEngine> &live_;
    const bool routed_;
    RunId run_;
    std::optional<Job> pending_;
    std::uint64_t job_start_tick_{};
    std::shared_ptr<const ObservedFrame> preview_;
    std::optional<ObservedFrame> saved_map_;
    std::optional<mission::SavedRun> saved_;
    std::string saved_review_text_;
    std::optional<RunId> mission_run_;
    std::uint64_t search_session_{};
    std::uint64_t audit_session_{};
    std::string audit_json_;
    bool saved_ready_{};
    bool archive_ready_{};
    bool close_review_pending_{};
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
