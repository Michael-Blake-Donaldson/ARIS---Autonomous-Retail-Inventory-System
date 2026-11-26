#pragma once

#include "argus/audit/stockroom.hpp"
#include "fleet_report.hpp"
#include "workbench.hpp"

#include <future>

namespace argus::desktop {

enum class ShelfState { pending, auditing, complete };

struct ShelfEntry {
    mission::ShelfAudit audit;
    ShelfReport report;
    [[nodiscard]] bool needs_review() const noexcept {
        return !audit.reviewed_ms && report.needs_review();
    }
};

struct AisleSummary {
    std::size_t complete{};
    std::size_t total{};
    std::size_t review{};
};

class Fleet {
  public:
    static constexpr std::size_t robot_count = audit::stockroom_robot_count;
    [[nodiscard]] static std::span<const audit::StockLocation> locations();
    [[nodiscard]] static std::optional<std::size_t> find(std::string_view id);
    [[nodiscard]] static std::size_t initial_location(std::size_t robot);

    explicit Fleet(const std::optional<std::filesystem::path> &directory);
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] simulation::FleetStop stop_reason() const noexcept;
    [[nodiscard]] bool motion_available() const noexcept;
    [[nodiscard]] bool queue(std::size_t robot, std::size_t location);
    [[nodiscard]] bool review(std::size_t location, mission::ShelfAuditKey key);
    [[nodiscard]] bool review_pending() const noexcept { return review_.has_value(); }
    [[nodiscard]] const char *review_error(std::size_t location) const noexcept {
        return review_failure_ == location ? review_error_ : nullptr;
    }
    [[nodiscard]] bool clear_aisle(std::size_t robot);
    [[nodiscard]] bool aisle_retained(std::size_t robot) const;
    void tick(bool focused);
    void hold();
    [[nodiscard]] LiveFrame frame(std::size_t robot) const noexcept;
    [[nodiscard]] ShelfState state(std::size_t location) const;
    [[nodiscard]] bool busy(std::size_t robot) const;
    [[nodiscard]] std::span<const ShelfEntry> history(std::size_t location) const noexcept;
    [[nodiscard]] std::optional<std::size_t> assignment(std::size_t robot) const noexcept;
    [[nodiscard]] AisleSummary aisle_summary(std::size_t aisle) const;
    [[nodiscard]] std::string_view status(std::size_t robot) const noexcept;
    [[nodiscard]] const Layout &layout() const noexcept { return *layout_; }
    [[nodiscard]] bool history_error() const noexcept { return history_issue_ != nullptr; }
    [[nodiscard]] const char *history_issue() const noexcept { return history_issue_; }
    [[nodiscard]] bool durable() const noexcept { return journal_.has_value(); }

  private:
    struct Unit {
        explicit Unit(Workbench::FleetWorker worker)
            : preview{std::make_shared<ObservedFrame>(worker.engine->frame())},
              workbench{{}, true, std::move(worker)} {}
        std::shared_ptr<const ObservedFrame> preview;
        std::optional<std::size_t> target;
        std::optional<mission::ShelfAuditKey> recheck_of;
        std::string status{"Starting"};
        bool pending{};
        bool clearing{};
        Workbench workbench;
    };

    void receive(Unit &unit, std::size_t index);
    void dispatch(Unit &unit, std::size_t index);
    void receive_review();
    struct PendingReview {
        std::size_t location;
        mission::ShelfAuditKey key;
        std::future<Result<std::uint64_t, mission::Error>> result;
    };
    std::array<std::unique_ptr<Unit>, robot_count> units_;
    std::shared_ptr<simulation::FleetClock> clock_{std::make_shared<simulation::FleetClock>()};
    std::shared_ptr<navigation::AisleAccess> access_{std::make_shared<navigation::AisleAccess>()};
    std::vector<std::vector<ShelfEntry>> history_;
    std::optional<Layout> layout_;
    std::optional<std::filesystem::path> journal_;
    const char *history_issue_{};
    std::optional<PendingReview> review_;
    std::optional<std::size_t> review_failure_;
    const char *review_error_{};
};

} // namespace argus::desktop
