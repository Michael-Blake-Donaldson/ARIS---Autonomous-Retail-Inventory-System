#pragma once

#include "argus/audit/stockroom.hpp"
#include "fleet_report.hpp"
#include "workbench.hpp"

namespace argus::desktop {

enum class ShelfState { pending, auditing, complete };

struct ShelfEntry {
    mission::ShelfAudit audit;
    ShelfReport report;
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
    [[nodiscard]] bool queue(std::size_t robot, std::size_t location);
    void tick(std::uint64_t steps, bool focused);
    void hold();
    [[nodiscard]] LiveFrame frame(std::size_t robot) const noexcept;
    [[nodiscard]] ShelfState state(std::size_t location) const noexcept;
    [[nodiscard]] bool busy(std::size_t robot) const;
    [[nodiscard]] std::span<const ShelfEntry> history(std::size_t location) const noexcept;
    [[nodiscard]] std::optional<std::size_t> assignment(std::size_t robot) const noexcept;
    [[nodiscard]] AisleSummary aisle_summary(std::size_t aisle) const noexcept;
    [[nodiscard]] std::string_view status(std::size_t robot) const noexcept;
    [[nodiscard]] const Layout &layout() const noexcept { return *layout_; }
    [[nodiscard]] bool history_error() const noexcept { return history_error_; }
    [[nodiscard]] bool durable() const noexcept { return journal_.has_value(); }

  private:
    struct Unit {
        explicit Unit(std::unique_ptr<LiveEngine> live) : engine{std::move(live)} {}
        std::unique_ptr<LiveEngine> engine;
        std::shared_ptr<const ObservedFrame> preview;
        std::optional<std::size_t> target;
        std::string status{"Starting"};
        bool pending{};
        Workbench workbench{{}, true, true};
    };

    void receive(Unit &unit, std::size_t index);
    void dispatch(Unit &unit, std::size_t index);
    std::array<std::unique_ptr<Unit>, robot_count> units_;
    std::vector<std::vector<ShelfEntry>> history_;
    std::optional<Layout> layout_;
    std::optional<std::filesystem::path> journal_;
    bool history_error_{};
};

} // namespace argus::desktop
