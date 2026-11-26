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

const char *hold_reason(const LiveFrame &frame, std::string_view activity, bool aisle_retained) {
    if (frame.traffic_yield) {
        return "Waiting for shared aisle";
    }
    if (frame.telemetry.emergency_latched) {
        return "Emergency stop";
    }
    if (frame.telemetry.world_held) {
        return "Moving obstruction";
    }
    if (frame.navigation && frame.navigation->reason != navigation::Reason::none &&
        (frame.navigation->state == navigation::State::waiting ||
         frame.navigation->state == navigation::State::yielding)) {
        return describe(frame.navigation->reason);
    }
    if (aisle_retained) {
        return "Return to berth to release aisle";
    }
    if (frame.telemetry.collision_blocked) {
        return "Motion blocked";
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
            ImGui::TextDisabled("%s", tag.observed_here ? "Observed here" : "Unconfirmed here");
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
    if (index < Fleet::robot_count) {
        selected_ = index;
    }
}

void FleetPanel::select_location(std::size_t index) {
    const auto list = Fleet::locations();
    if (index < list.size() && index != location_) {
        location_ = index;
        aisle_ = list[index].aisle;
        prior_view_.reset();
        product_page_ = 0;
    }
}

void FleetPanel::select_aisle(std::size_t index) {
    const auto list = Fleet::locations();
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].aisle == index && !get()->history(i).empty()) {
            select_location(i);
            return;
        }
    }
    const auto found = std::ranges::find(list, index, &audit::StockLocation::aisle);
    if (found != list.end()) {
        select_location(static_cast<std::size_t>(found - list.begin()));
    }
}
void FleetPanel::check_horizontal_scroll() {
    const auto width = ImGui::GetScrollMaxX();
    if (width > 0.5F) {
        layout_.horizontal_scroll = true;
        layout_.overflow_position = ImGui::GetWindowPos().x;
        layout_.overflow_width = width;
    }
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
    for (std::size_t i = 0; i < Fleet::robot_count; ++i) {
        const auto frame = fleet.frame(i);
        const auto &power = frame.telemetry;
        const auto battery = power.capacity_uj > 0 ? 100.0 * static_cast<double>(power.energy_uj) /
                                                         static_cast<double>(power.capacity_uj)
                                                   : 0.0;
        std::array<char, 72> caption{};
        std::snprintf(caption.data(), caption.size(),
                      "Robot %zu    %.0f%% battery###fleet-robot-%zu", i + 1, battery, i);
        if (ImGui::Selectable(caption.data(), selected_ == i, 0, {0, 25.0F})) {
            select(i);
        }
        const auto assigned = fleet.assignment(i);
        const bool working = assigned && fleet.busy(i);
        if (working) {
            ImGui::TextDisabled("Auditing %s", Fleet::locations()[*assigned].id.value.c_str());
        } else {
            ImGui::TextDisabled("Idle");
        }
        const auto activity = fleet.status(i);
        if (const auto *reason = hold_reason(frame, activity, fleet.aisle_retained(i))) {
            layout_.hold_visible = true;
            ImGui::TextColored(auditing, "%s", frame.traffic_yield ? "Yielding" : "Hold");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", reason);
        } else if (!working && activity != "Inventory ready" &&
                   activity != "Shelf audit complete" && activity != "Starting") {
            ImGui::TextWrapped("%.*s", static_cast<int>(activity.size()), activity.data());
        }
    }
    ImGui::Separator();
    for (std::size_t i = 0; i < audit::stockroom_aisles.size(); ++i) {
        const auto summary = fleet.aisle_summary(i);
        std::array<char, 64> label{};
        std::snprintf(label.data(), label.size(), "%s   %zu/%zu complete###aisle-%zu",
                      audit::stockroom_aisles[i].data(), summary.complete, summary.total, i);
        if (ImGui::Selectable(label.data(), aisle_ == i)) {
            select_aisle(i);
        }
        if (summary.review != 0) {
            ImGui::TextColored(auditing, "%zu need review", summary.review);
        }
    }
    ImGui::Separator();
    const auto &location = Fleet::locations()[location_];
    ImGui::TextColored(accent, "%s", location.id.value.c_str());
    ImGui::SameLine();
    const auto state = status(fleet.state(location_));
    ImGui::TextColored(state.color, "%s", state.text);
    const auto history = fleet.history(location_);
    if (!history.empty()) {
        if (history.front().needs_review()) {
            ImGui::TextColored(auditing, "Needs review");
        }
        const bool opened = ImGui::Button("Open audit", {-1, 0});
        layout_.open_audit = item_center();
        if (opened) {
            prior_view_.reset();
            product_page_ = 0;
            panel = Panel::locations;
        }
    }
    if (ImGui::Button("Browse locations", {-1, 0})) {
        panel = Panel::locations;
    }
    if (fleet.aisle_retained(selected_)) {
        ImGui::BeginDisabled(fleet.busy(selected_));
        if (ImGui::Button("Return to berth", {-1, 0})) {
            static_cast<void>(fleet.clear_aisle(selected_));
        }
        ImGui::EndDisabled();
    } else {
        ImGui::BeginDisabled(fleet.busy(selected_) ||
                             fleet.state(location_) == ShelfState::auditing);
        if (ImGui::Button(history.empty() ? "Start location audit" : "Re-audit location",
                          {-1, 0})) {
            static_cast<void>(fleet.queue(selected_, location_));
        }
        ImGui::EndDisabled();
    }
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
    for (const auto &solid : fleet->layout().solids) {
        if (solid.kind == world::SolidKind::wall) {
            const auto &bounds = solid.bounds;
            draw.AddRectFilled(project(bounds.min.x / 1000.0, bounds.max.y / 1000.0),
                               project(bounds.max.x / 1000.0, bounds.min.y / 1000.0),
                               IM_COL32(73, 96, 116, 255));
        }
    }
    for (std::size_t i = 1; i < frame.route.size(); ++i) {
        const auto a = frame.route[i - 1];
        const auto b = frame.route[i];
        draw.AddLine(project(a.x_m, a.y_m), project(b.x_m, b.y_m), IM_COL32(103, 235, 212, 255),
                     3.0F);
    }

    const auto marker = [&](const char *id, ImVec2 at, ImVec2 half, std::size_t index,
                            bool aisle_marker) {
        const ImVec2 low{at.x - half.x, at.y - half.y};
        const ImVec2 high{at.x + half.x, at.y + half.y};
        if (low.x < origin.x || low.y < origin.y || high.x > limit.x || high.y > limit.y) {
            return false;
        }
        ImGui::SetCursorScreenPos(low);
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::InvisibleButton(id, {half.x * 2.0F, half.y * 2.0F})) {
            if (aisle_marker) {
                select_aisle(index);
            } else {
                select(index);
            }
        }
        ImGui::PopID();
        return true;
    };
    const auto locations = Fleet::locations();
    for (std::size_t i = 0; i < audit::stockroom_aisles.size(); ++i) {
        const auto rack = std::ranges::find(locations, i, &audit::StockLocation::aisle);
        if (rack == locations.end()) {
            continue;
        }
        const auto &at = rack->center;
        const auto summary = fleet->aisle_summary(i);
        const auto color =
            ImGui::ColorConvertFloat4ToU32(summary.complete == summary.total ? complete
                                           : summary.complete != 0           ? auditing
                                                                             : pending);
        const auto shelf_low = project(at.x_m - 0.82, at.y_m + rack->half_width.value());
        const auto shelf_high = project(at.x_m + 0.18, at.y_m - rack->half_width.value());
        draw.AddRectFilled(shelf_low, shelf_high, IM_COL32(42, 62, 79, 255), 4.0F);
        draw.AddRect(shelf_low, shelf_high, color, 4.0F, 0, i == aisle_ ? 2.5F : 1.0F);
        const auto label_at = project(at.x_m, at.y_m + 1.98);
        if (marker("##aisle-marker", label_at, {48.0F, 15.0F}, i, true)) {
            const auto label = zoom_ >= 1.25F && i == aisle_
                                   ? std::string_view{locations[location_].id.value}
                                   : audit::stockroom_aisles[i];
            draw.AddRectFilled({label_at.x - 47.0F, label_at.y - 13.0F},
                               {label_at.x + 47.0F, label_at.y + 13.0F}, IM_COL32(35, 54, 70, 255),
                               6.0F);
            centered_text(draw, label_at, color, label);
            if (i == 1) {
                layout_.second_shelf = item_center();
                layout_.shelf_ids = zoom_ >= 1.25F;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s  /  %zu of %zu complete", audit::stockroom_aisles[i].data(),
                                  summary.complete, summary.total);
            }
        }
    }
    for (std::size_t i = 0; i < Fleet::robot_count; ++i) {
        const auto robot = fleet->frame(i);
        if (!robot.estimate) {
            continue;
        }
        const auto &pose = robot.estimate->pose;
        const auto at = project(pose.x.value(), pose.y.value());
        if (!marker("##robot-marker", at, {14.0F, 14.0F}, i, false)) {
            continue;
        }
        const auto color = hold_reason(robot, fleet->status(i), fleet->aisle_retained(i))
                               ? IM_COL32(255, 181, 87, 255)
                           : i == selected_ ? IM_COL32(103, 235, 212, 255)
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

void FleetPanel::location_row(std::size_t index) {
    const auto &fleet = *get();
    const auto &location = Fleet::locations()[index];
    if (ImGui::Selectable(location.id.value.c_str(), location_ == index)) {
        select_location(index);
    }
    const auto state = status(fleet.state(index));
    ImGui::TextColored(state.color, "%s", state.text);
    const auto history = fleet.history(index);
    if (!history.empty() && history.front().needs_review()) {
        ImGui::SameLine();
        ImGui::TextColored(auditing, "Needs review");
    }
}

void FleetPanel::locations() {
    const auto &fleet = *get();
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##shelf-search", "Search shelf or Waco ID", shelf_filter_.data(),
                             shelf_filter_.size());
    ImGui::Checkbox("Needs review only", &review_only_);
    layout_.review_filter = item_center();
    const std::string_view query{shelf_filter_.data()};
    const auto list = Fleet::locations();
    ImGui::Separator();
    if (!query.empty() || review_only_) {
        bool matched = false;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto history = fleet.history(i);
            if ((query.empty() || matches_shelf(list[i].id.value, query)) &&
                (!review_only_ || (!history.empty() && history.front().needs_review()))) {
                matched = true;
                location_row(i);
                layout_.filtered_reviews += review_only_ ? 1U : 0U;
            }
        }
        if (!matched) {
            ImGui::TextDisabled("No matching locations");
        }
    } else {
        for (std::size_t aisle = 0; aisle < audit::stockroom_aisles.size(); ++aisle) {
            const auto summary = fleet.aisle_summary(aisle);
            if (aisle == aisle_) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            }
            if (!ImGui::TreeNode(audit::stockroom_aisles[aisle].data(), "%s  /  %zu of %zu",
                                 audit::stockroom_aisles[aisle].data(), summary.complete,
                                 summary.total)) {
                continue;
            }
            for (std::size_t i = 0; i < list.size();) {
                const auto &location = list[i];
                if (location.aisle != aisle) {
                    ++i;
                } else if (location.waco == 0) {
                    location_row(i++);
                } else {
                    const auto begin = i;
                    std::size_t done = 0;
                    while (i < list.size() && list[i].aisle == aisle &&
                           list[i].shelf == location.shelf && list[i].waco != 0) {
                        done += fleet.state(i) == ShelfState::complete ? 1U : 0U;
                        ++i;
                    }
                    const auto shelf = location.id.value.substr(0, 6);
                    if (location_ >= begin && location_ < i) {
                        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                    }
                    if (ImGui::TreeNode(shelf.c_str(), "J%02u   %zu/%zu Wacos", location.shelf,
                                        done, i - begin)) {
                        for (auto waco = begin; waco < i; ++waco) {
                            location_row(waco);
                        }
                        ImGui::TreePop();
                    }
                }
            }
            ImGui::TreePop();
        }
    }
    if (fleet.history_error()) {
        ImGui::TextWrapped("Some location audits could not be read.");
    } else if (!fleet.durable()) {
        ImGui::TextDisabled("Temporary session");
    }
}
void FleetPanel::maintenance() {
    const auto &fleet = *get();
    for (std::size_t i = 0; i < Fleet::robot_count; ++i) {
        std::array<char, 32> label{};
        std::snprintf(label.data(), label.size(), "Robot %zu", i + 1);
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
    if (const auto *reason =
            hold_reason(frame, fleet.status(selected_), fleet.aisle_retained(selected_))) {
        ImGui::TextColored(auditing, "%s", frame.traffic_yield ? "Yielding" : "Hold");
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
    const auto &location = Fleet::locations()[location_];
    ImGui::PushFont(nullptr, 25.0F);
    ImGui::TextUnformatted(location.id.value.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    const auto state = status(fleet->state(location_));
    ImGui::TextColored(state.color, "%s", state.text);
    if (location.waco != 0) {
        ImGui::TextDisabled("Aisle %s / Shelf J%02u / Waco %02u",
                            audit::stockroom_aisles[location.aisle].data(), location.shelf,
                            location.waco);
    } else {
        ImGui::TextDisabled("Aisle %s / Shelf J%02u",
                            audit::stockroom_aisles[location.aisle].data(), location.shelf);
    }
    const auto history = fleet->history(location_);
    if (history.empty()) {
        ImGui::TextDisabled("No completed audit");
        ImGui::BeginDisabled(fleet->busy(selected_) ||
                             fleet->state(location_) == ShelfState::auditing);
        if (ImGui::Button("Start location audit")) {
            static_cast<void>(fleet->queue(selected_, location_));
        }
        ImGui::EndDisabled();
        return;
    }
    const auto identity = [](const ShelfEntry &entry) { return entry.audit.key(); };
    auto selected =
        prior_view_ ? std::ranges::find(history, *prior_view_, identity) : history.begin();
    if (selected == history.end()) {
        prior_view_.reset();
        selected = history.begin();
        product_page_ = 0;
    }
    if (history.size() > 1 && ImGui::CollapsingHeader("Audit history")) {
        for (std::size_t i = 0; i < history.size(); ++i) {
            std::array<char, 45> label{};
            std::snprintf(label.data(), label.size(), "%s##audit-%zu",
                          i == 0 ? "Latest audit" : "Earlier audit", i);
            ImGui::PushID(static_cast<int>(i));
            const auto candidate = history.begin() + static_cast<std::ptrdiff_t>(i);
            if (ImGui::Selectable(label.data(), selected == candidate, 0, {124.0F, 0.0F})) {
                selected = candidate;
                prior_view_ = i == 0 ? std::nullopt : std::optional{candidate->audit.key()};
                product_page_ = 0;
            }
            ImGui::SameLine();
            time_label(history[i].audit.completed_ms);
            ImGui::PopID();
        }
    }
    const auto &entry = *selected;
    ImGui::TextDisabled("Robot %llu  /  Session %llu  /  Run %llu",
                        static_cast<unsigned long long>(entry.audit.robot.value()),
                        static_cast<unsigned long long>(entry.audit.session),
                        static_cast<unsigned long long>(entry.audit.run.value()));
    time_label(entry.audit.completed_ms);
    if (entry.audit.reviewed_ms) {
        ImGui::TextColored(complete, "Reviewed");
        ImGui::SameLine();
        time_label(*entry.audit.reviewed_ms);
    } else if (entry.report.needs_review()) {
        ImGui::TextColored(auditing, "Needs review");
    }
    if (selected == history.begin()) {
        const bool active = fleet->state(location_) == ShelfState::auditing;
        const bool can_recheck =
            !active && !fleet->busy(selected_) && !fleet->aisle_retained(selected_);
        ImGui::BeginDisabled(!can_recheck);
        const bool recheck = ImGui::Button("Re-audit location");
        if (can_recheck) {
            layout_.recheck = item_center();
        }
        if (recheck) {
            static_cast<void>(fleet->queue(selected_, location_));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Robot %zu", selected_ + 1);
        if (entry.needs_review()) {
            ImGui::SameLine();
            const bool can_review = !active && !fleet->review_pending();
            ImGui::BeginDisabled(!can_review);
            const bool review = ImGui::Button("Mark reviewed");
            if (can_review) {
                layout_.mark_reviewed = item_center();
            }
            if (review) {
                static_cast<void>(fleet->review(location_, entry.audit.key()));
            }
            ImGui::EndDisabled();
        }
        if (fleet->review_failed(location_)) {
            ImGui::TextColored(auditing, "Could not save review.");
        }
    }
    if (entry.audit.recheck_of) {
        const auto key = *entry.audit.recheck_of;
        const auto source = std::ranges::find(history, key, identity);
        ImGui::TextDisabled("Recheck of Robot %llu / Session %llu / Run %llu",
                            static_cast<unsigned long long>(key.robot.value()),
                            static_cast<unsigned long long>(key.session),
                            static_cast<unsigned long long>(key.run.value()));
        if (source == history.end()) {
            ImGui::TextDisabled("Source report unavailable in audit history");
        } else {
            const bool expanded = ImGui::TreeNode("Compare with source audit");
            layout_.compare = item_center();
            if (expanded) {
                const auto rows = compare_shelf_reports(source->report, entry.report);
                if (ImGui::BeginTable("##recheck-counts", 3,
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("DPCI");
                    ImGui::TableSetupColumn("Source obs. / exp.");
                    ImGui::TableSetupColumn("Recheck obs. / exp.");
                    ImGui::TableHeadersRow();
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(rows.size()));
                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                            const auto &row = rows[static_cast<std::size_t>(i)];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            const auto *item = row.current ? row.current : row.source;
                            ImGui::TextUnformatted(item->product.id.value.c_str());
                            for (const auto *product : {row.source, row.current}) {
                                ImGui::TableNextColumn();
                                if (product) {
                                    ImGui::Text("%zu / %llu", product->observed,
                                                static_cast<unsigned long long>(product->expected));
                                } else {
                                    ImGui::TextDisabled("—");
                                }
                            }
                        }
                    }
                    ImGui::EndTable();
                    layout_.compared = !rows.empty();
                }
                ImGui::TreePop();
            }
        }
    }
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
    if (!entry.report.unresolved.empty() && ImGui::TreeNode("Unresolved RFID reads")) {
        ImGui::TextWrapped("Evidence did not isolate this location.");
        for (const auto &epc : entry.report.unresolved) {
            ImGui::TextWrapped("%s", epc.value.c_str());
        }
        ImGui::TreePop();
    }
}

} // namespace argus::desktop
