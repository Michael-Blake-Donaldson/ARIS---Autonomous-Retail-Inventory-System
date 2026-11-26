#include "fleet_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace argus::desktop {
namespace {

constexpr ImVec4 pending{1.0F, 0.40F, 0.43F, 1.0F};
constexpr ImVec4 auditing{1.0F, 0.71F, 0.35F, 1.0F};
constexpr ImVec4 complete{0.40F, 0.90F, 0.64F, 1.0F};
constexpr ImVec4 accent{0.46F, 0.89F, 0.83F, 1.0F};
constexpr std::size_t products_per_page = 20;

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

DashboardPoint item_center() {
    const auto low = ImGui::GetItemRectMin();
    const auto high = ImGui::GetItemRectMax();
    return {(low.x + high.x) / 2.0F, (low.y + high.y) / 2.0F};
}

const char *hold_reason(const LiveFrame &frame, std::string_view activity) {
    if (frame.telemetry.emergency_latched) {
        return "Emergency stop";
    }
    if (frame.navigation && frame.navigation->reason != navigation::Reason::none &&
        (frame.navigation->state == navigation::State::waiting ||
         frame.navigation->state == navigation::State::yielding)) {
        return describe(frame.navigation->reason);
    }
    return activity == "Run held by operator" ? "Operator hold" : nullptr;
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

void product_name(const ShelfProduct &row, const FleetPhotos &photos) {
    const auto texture = photos.texture(row.product.id.value);
    constexpr ImVec2 thumbnail{48.0F, 48.0F};
    if (texture) {
        ImGui::Image(static_cast<ImTextureID>(texture), thumbnail);
    } else {
        ImGui::Dummy(thumbnail);
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextWrapped("%s", row.product.name.c_str());
    ImGui::TextDisabled("%s", row.product.id.value.c_str());
    ImGui::EndGroup();
    if (ImGui::TreeNode("RFID tags")) {
        if (row.tags.empty()) {
            ImGui::TextDisabled("No tags assigned or observed");
        }
        for (const auto &tag : row.tags) {
            ImGui::TextWrapped("%s", tag.epc.value.c_str());
            ImGui::TextDisabled("%s", tag.observed_here ? "Observed here" : "Not observed here");
        }
        ImGui::TreePop();
    }
}

bool matches_shelf(std::string_view label, std::string_view query) {
    constexpr auto upper_ascii = [](char ch) {
        return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
    };
    return query.empty() ||
           !std::ranges::search(label, query, {}, upper_ascii, upper_ascii).empty();
}

void centered_text(ImDrawList &draw, ImVec2 center, ImU32 color, std::string_view text) {
    const auto size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    draw.AddText({center.x - size.x / 2.0F, center.y - size.y / 2.0F}, color, text.data(),
                 text.data() + text.size());
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
        product_page_ = 0;
    }
}

void FleetPanel::check_horizontal_scroll() {
    layout_.horizontal_scroll = layout_.horizontal_scroll || ImGui::GetScrollMaxX() > 0.5F;
}

void FleetPanel::mark_maintenance_link() { layout_.maintenance = item_center(); }

void FleetPanel::draw_sidebar(Panel &panel) {
    if (!get()) {
        ImGui::TextWrapped("The fleet scenario could not start.");
        return;
    }
    if (panel == Panel::locations) {
        locations();
    } else if (panel == Panel::maintenance) {
        maintenance();
    } else {
        robots(panel);
    }
}

void FleetPanel::robots(Panel &panel) {
    auto &fleet = *get();
    for (std::size_t i = 0; i < Fleet::locations.size(); ++i) {
        const auto frame = fleet.frame(i);
        const auto &location = Fleet::locations[i];
        const auto &power = frame.telemetry;
        const auto battery = power.capacity_uj > 0 ? 100.0 * static_cast<double>(power.energy_uj) /
                                                         static_cast<double>(power.capacity_uj)
                                                   : 0.0;
        std::array<char, 50> caption{};
        std::snprintf(caption.data(), caption.size(), "Robot %zu    %.0f%% battery", i + 1,
                      battery);
        if (ImGui::Selectable(caption.data(), selected_ == i, 0, {0, 27.0F})) {
            select(i);
        }
        const auto state = fleet.state(i);
        ImGui::TextDisabled("%s  /  %s", location.label.data(),
                            state == ShelfState::auditing ? "Shelf audit" : "Idle");
        const auto activity = fleet.status(i);
        if (const auto *reason = hold_reason(frame, activity)) {
            layout_.hold_visible = true;
            ImGui::TextColored(auditing, "Hold");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", reason);
        } else if (state != ShelfState::auditing && activity != "Inventory ready" &&
                   activity != "Shelf audit complete" && activity != "Starting") {
            ImGui::TextWrapped("%.*s", static_cast<int>(activity.size()), activity.data());
        }
        ImGui::Spacing();
    }
    ImGui::Separator();
    const auto &location = Fleet::locations.at(selected_);
    ImGui::TextColored(accent, "%s", location.label.data());
    ImGui::SameLine();
    const auto state = status(fleet.state(selected_));
    ImGui::TextColored(state.color, "%s", state.text);
    const auto history = fleet.history(selected_);
    if (!history.empty() && history.front().report.needs_review()) {
        ImGui::TextColored(auditing, "Needs review");
    }
    if (!history.empty()) {
        const bool opened = ImGui::Button("Open audit", {-1, 0});
        layout_.open_audit = item_center();
        if (opened) {
            history_index_ = 0;
            product_page_ = 0;
            panel = Panel::locations;
        }
    }
    ImGui::BeginDisabled(fleet.busy(selected_));
    if (ImGui::Button(history.empty() ? "Start shelf audit" : "Audit again", {-1, 0})) {
        static_cast<void>(fleet.queue(selected_));
    }
    ImGui::EndDisabled();
    const bool inspect = ImGui::Button("Inspect robot", {-1, 0});
    layout_.maintenance = item_center();
    if (inspect) {
        panel = Panel::maintenance;
    }
}

void FleetPanel::draw_map() {
    const auto *fleet = get();
    if (!fleet) {
        return;
    }
    if (ImGui::Button("Fit map")) {
        zoom_ = 1.0F;
        pan_x_ = pan_y_ = 0.0F;
    }
    ImGui::SameLine();
    if (ImGui::Button("-##zoom-out")) {
        zoom_ = std::max(1.0F, zoom_ / 1.3F);
    }
    ImGui::SameLine();
    if (ImGui::Button("+##zoom-in")) {
        zoom_ = std::min(3.0F, zoom_ * 1.3F);
    }
    layout_.map_zoom = item_center();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", zoom_ >= 1.25F ? "Shelf view" : "Aisle view");

    const auto available = ImGui::GetContentRegionAvail();
    if (!ImGui::BeginChild("##fleet-map", available, ImGuiChildFlags_None,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::EndChild();
        return;
    }
    const auto origin = ImGui::GetCursorScreenPos();
    const auto size = ImGui::GetContentRegionAvail();
    const ImVec2 limit{origin.x + size.x, origin.y + size.y};
    const auto &io = ImGui::GetIO();
    if (ImGui::IsWindowHovered()) {
        zoom_ = std::clamp(zoom_ * std::pow(1.18F, io.MouseWheel), 1.0F, 3.0F);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            pan_x_ += io.MouseDelta.x;
            pan_y_ += io.MouseDelta.y;
        }
    }
    const auto frame = fleet->frame(selected_);
    const auto spec = frame.grid.spec();
    const auto width_m = static_cast<float>(spec.width_mm) / 1000.0F;
    const auto height_m = static_cast<float>(spec.height_mm) / 1000.0F;
    const auto scale =
        zoom_ * std::max(0.1F, std::min((size.x - 55.0F) / width_m, (size.y - 55.0F) / height_m));
    const auto pan_limit = (zoom_ - 1.0F) * 0.5F / zoom_;
    pan_x_ = std::clamp(pan_x_, -pan_limit * scale * width_m, pan_limit * scale * width_m);
    pan_y_ = std::clamp(pan_y_, -pan_limit * scale * height_m, pan_limit * scale * height_m);
    const auto x0 = origin.x + (size.x - scale * width_m) / 2.0F + pan_x_;
    const auto y0 = origin.y + (size.y + scale * height_m) / 2.0F + pan_y_;
    const auto project = [&](double x, double y) -> ImVec2 {
        return {x0 + static_cast<float>(x) * scale, y0 - static_cast<float>(y) * scale};
    };
    auto &draw = *ImGui::GetWindowDrawList();
    draw.PushClipRect(origin, limit, true);
    const auto floor_low = project(0, height_m);
    const auto floor_high = project(width_m, 0);
    draw.AddRectFilled(floor_low, floor_high, IM_COL32(20, 34, 47, 255), 6.0F);
    for (int x = 1; static_cast<float>(x) < width_m; ++x) {
        draw.AddLine(project(x, 0), project(x, height_m), IM_COL32(34, 51, 66, 255));
    }
    for (int y = 1; static_cast<float>(y) < height_m; ++y) {
        draw.AddLine(project(0, y), project(width_m, y), IM_COL32(34, 51, 66, 255));
    }
    draw.AddRect(floor_low, floor_high, IM_COL32(73, 96, 116, 255), 6.0F);
    for (std::size_t i = 1; i < frame.route.size(); ++i) {
        const auto a = frame.route[i - 1];
        const auto b = frame.route[i];
        draw.AddLine(project(a.x_m, a.y_m), project(b.x_m, b.y_m), IM_COL32(103, 235, 212, 255),
                     3.0F);
    }

    const auto marker = [&](const char *id, ImVec2 at, ImVec2 half, std::size_t index) {
        const ImVec2 low{at.x - half.x, at.y - half.y};
        const ImVec2 high{at.x + half.x, at.y + half.y};
        if (low.x < origin.x || low.y < origin.y || high.x > limit.x || high.y > limit.y) {
            return false;
        }
        ImGui::SetCursorScreenPos(low);
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::InvisibleButton(id, {half.x * 2.0F, half.y * 2.0F})) {
            select(index);
        }
        ImGui::PopID();
        return true;
    };

    for (std::size_t i = 0; i < Fleet::locations.size(); ++i) {
        const auto &location = Fleet::locations[i];
        const auto &at = location.center;
        const auto color = ImGui::ColorConvertFloat4ToU32(status(fleet->state(i)).color);
        const auto shelf_low = project(at.x_m - 0.50, at.y_m + 1.0);
        const auto shelf_high = project(at.x_m + 0.50, at.y_m - 1.0);
        draw.AddRectFilled(shelf_low, shelf_high, IM_COL32(42, 62, 79, 255), 4.0F);
        draw.AddRect(shelf_low, shelf_high, color, 4.0F, 0, i == selected_ ? 2.5F : 1.0F);
        const auto label_at = project(at.x_m, at.y_m + 1.47);
        if (marker("##shelf-marker", label_at, {40.0F, 15.0F}, i)) {
            const auto label = zoom_ >= 1.25F ? location.label : location.label.substr(0, 3);
            draw.AddRectFilled({label_at.x - 38.0F, label_at.y - 13.0F},
                               {label_at.x + 38.0F, label_at.y + 13.0F}, IM_COL32(35, 54, 70, 255),
                               6.0F);
            centered_text(draw, label_at, color, label);
            if (i == 1) {
                layout_.second_shelf = item_center();
                layout_.shelf_ids = zoom_ >= 1.25F;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s  /  %s", location.label.data(), status(fleet->state(i)).text);
            }
        }
    }
    for (std::size_t i = 0; i < Fleet::locations.size(); ++i) {
        const auto robot = fleet->frame(i);
        if (!robot.estimate) {
            continue;
        }
        const auto &pose = robot.estimate->pose;
        const auto at = project(pose.x.value(), pose.y.value());
        if (!marker("##robot-marker", at, {14.0F, 14.0F}, i)) {
            continue;
        }
        const auto color = hold_reason(robot, fleet->status(i)) ? IM_COL32(255, 181, 87, 255)
                           : i == selected_                     ? IM_COL32(103, 235, 212, 255)
                                                                : IM_COL32(197, 206, 218, 255);
        draw.AddCircleFilled(at, 12.0F, color);
        const char label = static_cast<char>('1' + i);
        centered_text(draw, at, IM_COL32(15, 27, 36, 255), {&label, 1});
    }
    draw.PopClipRect();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({0, 0});
    check_horizontal_scroll();
    ImGui::EndChild();
}

void FleetPanel::locations() {
    auto &fleet = *get();
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##shelf-search", "Search shelf ID", shelf_filter_.data(),
                             shelf_filter_.size());
    const std::string_view query{shelf_filter_.data()};
    ImGui::Separator();
    bool matched = false;
    for (std::size_t i = 0; i < Fleet::locations.size(); ++i) {
        const auto &location = Fleet::locations[i];
        if (!matches_shelf(location.label, query)) {
            continue;
        }
        matched = true;
        if (ImGui::Selectable(location.label.data(), selected_ == i, 0, {0, 27.0F})) {
            select(i);
        }
        const auto state = status(fleet.state(i));
        ImGui::TextColored(state.color, "%s", state.text);
        const auto history = fleet.history(i);
        if (!history.empty() && history.front().report.needs_review()) {
            ImGui::SameLine();
            ImGui::TextColored(auditing, "Needs review");
        }
        ImGui::Spacing();
    }
    if (!matched) {
        ImGui::TextDisabled("No matching shelves");
    }
    if (fleet.history_error()) {
        ImGui::TextWrapped("Some shelf audits could not be read.");
    } else if (!fleet.durable()) {
        ImGui::TextDisabled("Temporary session");
    }
}

void FleetPanel::maintenance() {
    const auto &fleet = *get();
    for (std::size_t i = 0; i < Fleet::locations.size(); ++i) {
        std::array<char, 32> label{};
        std::snprintf(label.data(), label.size(), "Robot %zu  /  %s", i + 1,
                      Fleet::locations[i].label.data());
        if (ImGui::Selectable(label.data(), i == selected_, 0, {0, 26.0F})) {
            select(i);
        }
    }
    ImGui::Separator();
    const auto frame = fleet.frame(selected_);
    const auto scan = frame.map.scan == ScanState::fresh         ? "Fresh"
                      : frame.map.scan == ScanState::stale       ? "Stale"
                      : frame.map.scan == ScanState::powered_off ? "Powered off"
                                                                 : "Waiting";
    if (frame.map.scan_age_ms) {
        ImGui::Text("LiDAR  %s / %llu ms", scan,
                    static_cast<unsigned long long>(*frame.map.scan_age_ms));
    } else {
        ImGui::Text("LiDAR  %s", scan);
    }
    ImGui::Text("Localization  %s", frame.map.pose == PoseState::current     ? "Current"
                                    : frame.map.pose == PoseState::uncertain ? "Uncertain"
                                                                             : "Waiting");
    if (const auto *reason = hold_reason(frame, fleet.status(selected_))) {
        ImGui::TextColored(auditing, "Hold");
        ImGui::TextWrapped("%s", reason);
    } else if (frame.navigation) {
        ImGui::TextWrapped("%s", describe(frame.navigation->state));
    }
    if (ImGui::CollapsingHeader("Sensor details")) {
        if (frame.lidar && frame.map.scan == ScanState::fresh) {
            ImGui::Text("%u LiDAR returns", frame.lidar->return_count);
        }
        ImGui::Text("%zu trail samples", frame.trace.size());
        ImGui::Text("%zu RFID reads", frame.rfid_reads);
        if (frame.estimate) {
            ImGui::Text("Bounds %.3f m / %.3f rad", frame.estimate->distance_error_m,
                        frame.estimate->heading_error_rad);
        }
        if (frame.navigation) {
            ImGui::Text("%u replans / %u yields", frame.navigation->replans,
                        frame.navigation->yields);
        }
    }
}

void FleetPanel::draw_audit(const FleetPhotos &photos) {
    auto *fleet = get();
    if (!fleet) {
        return;
    }
    const auto &location = Fleet::locations.at(selected_);
    ImGui::PushFont(nullptr, 25.0F);
    ImGui::TextUnformatted(location.label.data());
    ImGui::PopFont();
    ImGui::SameLine();
    const auto state = status(fleet->state(selected_));
    ImGui::TextColored(state.color, "%s", state.text);
    ImGui::TextDisabled("Aisle %.3s  /  Shelf %.3s", location.label.data(),
                        location.label.data() + 3);
    const auto history = fleet->history(selected_);
    if (history.empty()) {
        ImGui::TextDisabled("No completed audit");
        ImGui::BeginDisabled(fleet->busy(selected_));
        if (ImGui::Button("Start shelf audit")) {
            static_cast<void>(fleet->queue(selected_));
        }
        ImGui::EndDisabled();
        return;
    }
    history_index_ = std::min(history_index_, history.size() - 1);
    if (history.size() > 1 && ImGui::CollapsingHeader("Audit history")) {
        for (std::size_t i = 0; i < history.size(); ++i) {
            std::array<char, 45> label{};
            std::snprintf(label.data(), label.size(), "%s##audit-%zu",
                          i == 0 ? "Latest audit" : "Earlier audit", i);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(label.data(), history_index_ == i, 0, {124.0F, 0.0F})) {
                history_index_ = i;
                product_page_ = 0;
            }
            ImGui::SameLine();
            time_label(history[i].audit.completed_ms);
            ImGui::PopID();
        }
    }
    const auto &entry = history[history_index_];
    ImGui::TextDisabled("Robot %llu  /  Session %llu",
                        static_cast<unsigned long long>(entry.audit.robot.value()),
                        static_cast<unsigned long long>(entry.audit.session));
    time_label(entry.audit.completed_ms);
    if (ImGui::Checkbox("Discrepancies only", &discrepancies_only_)) {
        product_page_ = 0;
    }
    const auto matches = [&](const auto &product) {
        return !discrepancies_only_ || product.discrepancy();
    };
    const auto count =
        static_cast<std::size_t>(std::ranges::count_if(entry.report.products, matches));
    if (count == 0) {
        ImGui::TextDisabled("No matching products");
    } else {
        const auto pages = (count + products_per_page - 1) / products_per_page;
        product_page_ = std::min(product_page_, pages - 1);
        if (ImGui::BeginTable("##audit-products", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Product / DPCI", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Expected", ImGuiTableColumnFlags_WidthFixed, 76.0F);
            ImGui::TableSetupColumn("Observed", ImGuiTableColumnFlags_WidthFixed, 76.0F);
            ImGui::TableHeadersRow();
            std::size_t index = 0;
            const auto first = product_page_ * products_per_page;
            for (const auto &product : entry.report.products) {
                if (!matches(product) || index++ < first) {
                    continue;
                }
                if (index > first + products_per_page) {
                    break;
                }
                ImGui::PushID(product.product.id.value.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                product_name(product, photos);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(product.expected));
                ImGui::TableNextColumn();
                if (product.discrepancy()) {
                    ImGui::TextColored(auditing, "%zu", product.observed);
                } else {
                    ImGui::Text("%zu", product.observed);
                }
                ImGui::PopID();
                ++layout_.products;
            }
            ImGui::EndTable();
        }
        if (pages > 1) {
            ImGui::BeginDisabled(product_page_ == 0);
            if (ImGui::Button("Previous products")) {
                --product_page_;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("%zu / %zu", product_page_ + 1, pages);
            ImGui::SameLine();
            ImGui::BeginDisabled(product_page_ + 1 == pages);
            if (ImGui::Button("Next products")) {
                ++product_page_;
            }
            ImGui::EndDisabled();
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
