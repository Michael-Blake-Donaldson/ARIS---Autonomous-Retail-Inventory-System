#pragma once

#include "fleet_report.hpp"
#include "workbench.hpp"

namespace argus::desktop {

enum class ShelfState { pending, auditing, complete };

struct ShelfLocation {
    std::string_view label;
    std::string_view aisle;
    mapping::Point center;
    std::int32_t spawn_x_mm;
};

struct ShelfEntry {
    mission::ShelfAudit audit;
    ShelfReport report;
};

class Fleet {
  public:
    static constexpr std::array<ShelfLocation, 2> locations{{
        {"A10J05", "SOFTLINES", {2.7, 5.8}, 4125},
        {"A20J05", "TECH", {8.3, 5.8}, 7375},
    }};

    explicit Fleet(const std::optional<std::filesystem::path> &directory);
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool queue(std::size_t index);
    void tick(std::uint64_t steps, bool focused);
    void hold();
    [[nodiscard]] LiveFrame frame(std::size_t index) const noexcept;
    [[nodiscard]] ShelfState state(std::size_t index) const noexcept;
    [[nodiscard]] bool busy(std::size_t index) const;
    [[nodiscard]] std::span<const ShelfEntry> history(std::size_t index) const noexcept;
    [[nodiscard]] std::string_view status(std::size_t index) const noexcept;
    [[nodiscard]] bool history_error() const noexcept { return history_error_; }
    [[nodiscard]] bool durable() const noexcept { return journal_.has_value(); }

  private:
    struct Unit {
        explicit Unit(std::unique_ptr<LiveEngine> live) : engine{std::move(live)} {}
        std::unique_ptr<LiveEngine> engine;
        std::shared_ptr<const ObservedFrame> preview;
        std::vector<ShelfEntry> history;
        std::string status{"Starting"};
        bool pending{};
        Workbench workbench;
    };

    void receive(Unit &unit, std::size_t index);
    void dispatch(Unit &unit, std::size_t index);
    std::array<std::unique_ptr<Unit>, locations.size()> units_;
    std::optional<std::filesystem::path> journal_;
    bool history_error_{};
};

} // namespace argus::desktop
