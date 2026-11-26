#include "fleet_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace argus::desktop {
namespace {

constexpr ImVec4 pending{1.0F, 0.40F, 0.43F, 1.0F};
constexpr ImVec4 auditing{1.0F, 0.71F, 0.35F, 1.0F};
constexpr ImVec4 complete{0.40F, 0.90F, 0.64F, 1.0F};

struct Status {
    const char *text;
    ImVec4 color;
};

Status status(ShelfState value) noexcept {
    switch (value) {
    case ShelfState::complete:
        return {"Complete", complete};
    case ShelfState::auditing:
        return {"In progress", auditing};
    case ShelfState::pending:
        return {"Pending", pending};
    }
    return {"Unavailable", pending};
}

void time_label(std::uint64_t millis) {
    const auto seconds = static_cast<std::time_t>(millis / 1000);
    std::tm utc{};
    std::array<char, 32> text{};
    if (gmtime_s(&utc, &seconds) == 0 &&
        std::strftime(text.data(), text.size(), "%Y-%m-%d %H:%M UTC", &utc) != 0) {
        ImGui::TextUnformatted(text.data());
    }
}

void product_row(const ShelfProduct &row, const FleetPhotos &photos) {
    ImGui::PushID(row.product.id.value.c_str());
    const auto texture = photos.texture(row.product.id.value);
    if (texture) {
        ImGui::Image(static_cast<ImTextureID>(texture), {58.0F, 58.0F});
    } else {
        ImGui::Dummy({58.0F, 58.0F});
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextWrapped("%s", row.product.name.c_str());
    ImGui::TextDisabled("DPCI %s", row.product.id.value.c_str());
    ImGui::Text("Expected %llu   Observed %zu", static_cast<unsigned long long>(row.expected),
                row.observed);
    if (row.discrepancy()) {
        ImGui::TextColored(auditing, "Needs review");
    }
    ImGui::EndGroup();
    if (ImGui::TreeNode("RFID tags")) {
        if (row.tags.empty()) {
            ImGui::TextDisabled("No tags were assigned or observed");
        }
        for (const auto &tag : row.tags) {
            ImGui::TextWrapped("%s", tag.epc.value.c_str());
            ImGui::TextDisabled("%s", tag.observed_here ? "Observed here" : "Not observed here");
        }
        ImGui::TreePop();
    }
    ImGui::Separator();
    ImGui::PopID();
}

} // namespace

void FleetPanel::open(const std::optional<std::filesystem::path> &directory) {
    if (!fleet_) {
        fleet_.emplace(directory);
    }
}

void FleetPanel::select(std::size_t index) noexcept {
    if (index != selected_ && index < Fleet::locations.size()) {
        selected_ = index;
        history_index_ = 0;
    }
}

void FleetPanel::draw(Panel panel, const FleetPhotos &photos) {
    if (!get()) {
        ImGui::TextWrapped("The fleet scenario could not start.");
        return;
    }
    if (panel == Panel::locations) {
        locations(photos);
    } else {
        robots(panel == Panel::maintenance);
    }
}

void FleetPanel::robots(bool maintenance) {
    auto &fleet = *get();
    for (std::size_t i = 0; i < Fleet::locations.size(); ++i) {
        const auto &location = Fleet::locations[i];
        const auto frame = fleet.frame(i);
        std::array<char, 48> label{};
        std::snprintf(label.data(), label.size(), "Robot %zu   /   %s", i + 1,
                      location.label.data());
        if (ImGui::Selectable(label.data(), selected_ == i, 0, {0, 30.0F})) {
            select(i);
        }
        const auto state = status(fleet.state(i));
        ImGui::TextColored(state.color, "%s", state.text);
        ImGui::SameLine();
        const auto &power = frame.telemetry;
        const auto battery = power.capacity_uj > 0 ? 100.0 * static_cast<double>(power.energy_uj) /
                                                         static_cast<double>(power.capacity_uj)
                                                   : 0.0;
        ImGui::TextDisabled("Battery %.0f%%", battery);
        const auto message = fleet.status(i);
        ImGui::TextWrapped("%.*s", static_cast<int>(message.size()), message.data());
        ImGui::Separator();
    }
    if (!maintenance) {
        map(fleet);
    } else {
        const auto frame = fleet.frame(selected_);
        const auto scan = frame.map.scan == ScanState::fresh         ? "Fresh"
                          : frame.map.scan == ScanState::stale       ? "Stale"
                          : frame.map.scan == ScanState::powered_off ? "Powered off"
                                                                     : "Waiting";
        ImGui::Text("LiDAR    %s", scan);
        if (frame.map.scan_age_ms) {
            ImGui::Text("Scan age %llu ms",
                        static_cast<unsigned long long>(*frame.map.scan_age_ms));
        }
        if (frame.lidar && frame.map.scan == ScanState::fresh) {
            ImGui::Text("%u returns", frame.lidar->return_count);
        }
        ImGui::Text("Trail    %zu samples", frame.trace.size());
        ImGui::Text("Localization    %s", frame.map.pose == PoseState::current     ? "Current"
                                          : frame.map.pose == PoseState::uncertain ? "Uncertain"
                                                                                   : "Waiting");
        if (frame.estimate) {
            ImGui::Text("Bounds %.3f m / %.3f rad", frame.estimate->distance_error_m,
                        frame.estimate->heading_error_rad);
        }
        if (frame.navigation) {
            ImGui::TextWrapped("%s", describe(frame.navigation->state));
            if (frame.navigation->reason != navigation::Reason::none) {
                ImGui::TextWrapped("%s", describe(frame.navigation->reason));
            }
        }
    }
    ImGui::BeginDisabled(fleet.busy(selected_));
    if (ImGui::Button("Audit assigned shelf", {-1, 0})) {
        static_cast<void>(fleet.queue(selected_));
    }
    ImGui::EndDisabled();
}

void FleetPanel::map(const Fleet &fleet) {
    const auto origin = ImGui::GetCursorScreenPos();
    const auto width = std::max(1.0F, ImGui::GetContentRegionAvail().x);
    constexpr float height = 150.0F;
    ImGui::Dummy({width, height});
    auto *draw = ImGui::GetWindowDrawList();
    const ImVec2 low{origin.x + 5.0F, origin.y + 5.0F};
    const ImVec2 high{origin.x + width - 5.0F, origin.y + height - 5.0F};
    draw->AddRectFilled(low, high, IM_COL32(13, 25, 37, 255), 5.0F);
    const auto point = [&](double x, double y) -> ImVec2 {
        return {low.x + static_cast<float>(x / 12.0) * (high.x - low.x),
                high.y - static_cast<float>(y / 8.0) * (high.y - low.y)};
    };
    const auto frame = fleet.frame(selected_);
    for (std::size_t i = 1; i < frame.route.size(); ++i) {
        const auto a = frame.route[i - 1];
        const auto b = frame.route[i];
        draw->AddLine(point(a.x_m, a.y_m), point(b.x_m, b.y_m), IM_COL32(98, 202, 246, 255), 2.0F);
    }
    for (std::size_t i = 0; i < Fleet::locations.size(); ++i) {
        const auto &location = Fleet::locations[i];
        const auto at = point(location.center.x_m, location.center.y_m);
        const auto color = ImGui::ColorConvertFloat4ToU32(status(fleet.state(i)).color);
        draw->AddRectFilled({at.x - 30.0F, at.y - 11.0F}, {at.x + 31.0F, at.y + 11.0F},
                            IM_COL32(28, 46, 61, 255), 3.0F);
        if (i == selected_) {
            draw->AddRect({at.x - 30.0F, at.y - 11.0F}, {at.x + 31.0F, at.y + 11.0F}, color);
        }
        draw->AddText({at.x - 26.0F, at.y - 8.0F}, color, location.label.data());
        const auto &estimate = fleet.frame(i).estimate;
        if (estimate) {
            const auto robot = point(estimate->pose.x.value(), estimate->pose.y.value());
            draw->AddCircleFilled(robot, i == selected_ ? 6.0F : 4.0F,
                                  i == selected_ ? IM_COL32(110, 248, 227, 255)
                                                 : IM_COL32(197, 206, 218, 255));
        }
    }
}

void FleetPanel::locations(const FleetPhotos &photos) {
    auto &fleet = *get();
    if (fleet.history_error()) {
        ImGui::TextWrapped("One or more saved shelf audits could not be read.");
    } else if (!fleet.durable()) {
        ImGui::TextDisabled("Temporary session");
    }
    for (std::size_t i = 0; i < Fleet::locations.size(); ++i) {
        const auto &location = Fleet::locations[i];
        if (ImGui::Selectable(location.label.data(), selected_ == i, 0, {0, 28.0F})) {
            select(i);
        }
        const auto state = status(fleet.state(i));
        ImGui::TextColored(state.color, "%s", state.text);
        const auto history = fleet.history(i);
        if (!history.empty() && history.front().report.needs_review()) {
            ImGui::SameLine();
            ImGui::TextColored(auditing, "Needs review");
        }
    }
    ImGui::Separator();
    const auto history = fleet.history(selected_);
    ImGui::Text("Aisle %.3s   Shelf %.3s", Fleet::locations[selected_].label.data(),
                Fleet::locations[selected_].label.data() + 3);
    ImGui::BeginDisabled(fleet.busy(selected_));
    if (ImGui::Button("Audit this shelf", {-1, 0})) {
        static_cast<void>(fleet.queue(selected_));
    }
    ImGui::EndDisabled();
    if (history.empty()) {
        ImGui::TextDisabled("No completed audit");
        return;
    }
    history_index_ = std::min(history_index_, history.size() - 1);
    const auto &entry = history[history_index_];
    if (history.size() > 1) {
        ImGui::BeginDisabled(history_index_ == 0);
        if (ImGui::Button("Newer")) {
            --history_index_;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(history_index_ + 1 == history.size());
        if (ImGui::Button("Older")) {
            ++history_index_;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("Audit %zu of %zu", history_index_ + 1, history.size());
    }
    ImGui::Text("Robot %llu   /   Session %llu",
                static_cast<unsigned long long>(entry.audit.robot.value()),
                static_cast<unsigned long long>(entry.audit.session));
    time_label(entry.audit.completed_ms);
    ImGui::Checkbox("Discrepancies only", &discrepancies_only_);
    ImGui::TextDisabled("%zu products   /   By DPCI", entry.report.products.size());
    ImGui::Separator();
    for (const auto &product : entry.report.products) {
        if (!discrepancies_only_ || product.discrepancy()) {
            product_row(product, photos);
        }
    }
    if (!entry.report.unidentified.empty() && ImGui::TreeNode("Unidentified RFID tags")) {
        for (const auto &epc : entry.report.unidentified) {
            ImGui::TextWrapped("%s", epc.value.c_str());
        }
        ImGui::TreePop();
    }
}

} // namespace argus::desktop
