#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace argus::desktop {

enum class Metric {
    fleet_frame,
    maintenance_frame,
    audit_frame,
    fleet_step,
    snapshot,
    input,
    count
};

// Enabled once at startup for scripted measurements. Each sample has one writer.
class DesktopMetrics {
  public:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] Clock::time_point start() const noexcept {
        return enabled ? Clock::now() : Clock::time_point{};
    }

    void record(Metric metric, Clock::time_point start) noexcept {
        if (!enabled) {
            return;
        }
        auto &series = values_[static_cast<std::size_t>(metric)];
        const auto index = series.count.fetch_add(1, std::memory_order_relaxed);
        if (index < series.samples.size()) {
            series.samples[index] =
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        }
    }

    void grid_copy(std::size_t cells, bool allocated = true) noexcept {
        if (enabled) {
            copies_.fetch_add(1, std::memory_order_relaxed);
            cells_.fetch_add(cells, std::memory_order_relaxed);
            if (allocated) {
                allocations_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // All desktop workers have joined before metrics are emitted.
    void write(std::ostream &out) const {
        constexpr std::array names{"fleetFrame", "maintenanceFrame", "auditFrame",
                                   "fleetStep",  "snapshot",         "input"};
        out << "{\"desktopMetrics\":{";
        for (std::size_t i = 0; i < values_.size(); ++i) {
            const auto &series = values_[i];
            const auto count = std::min(series.count.load(), series.samples.size());
            std::vector<std::int64_t> ordered(series.samples.begin(),
                                              series.samples.begin() +
                                                  static_cast<std::ptrdiff_t>(count));
            std::ranges::sort(ordered);
            const auto percentile = [&](std::size_t rank) {
                return count
                           ? static_cast<double>(ordered[(count * rank + 99) / 100 - 1]) / 1000000.0
                           : 0.0;
            };
            out << '"' << names[i] << "\":{\"samples\":" << count << ",\"p95Ms\":" << percentile(95)
                << ",\"p99Ms\":" << percentile(99) << "},";
        }
        out << "\"gridCopies\":" << copies_.load() << ",\"gridCellsCopied\":" << cells_.load()
            << ",\"gridBackingAllocations\":" << allocations_.load() << "}}\n";
    }

    bool enabled{};

  private:
    struct Samples {
        std::atomic_size_t count{};
        std::array<std::int64_t, 4096> samples{};
    };
    std::array<Samples, static_cast<std::size_t>(Metric::count)> values_{};
    std::atomic_size_t copies_{};
    std::atomic_size_t cells_{};
    std::atomic_size_t allocations_{};
};

inline DesktopMetrics desktop_metrics;

} // namespace argus::desktop
