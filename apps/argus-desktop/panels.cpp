#include "panels.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace argus::desktop {
namespace {

constexpr ImVec4 primary{0.93F, 0.95F, 0.98F, 1.0F};
constexpr ImVec4 muted{0.61F, 0.68F, 0.77F, 1.0F};
constexpr ImVec4 teal{0.46F, 0.89F, 0.83F, 1.0F};
constexpr ImVec4 amber{1.0F, 0.72F, 0.40F, 1.0F};
constexpr std::array<const char *, 2> aisle_names{"SOFTLINES", "TECH"};
constexpr std::size_t product_limit = 12000;
constexpr ImGuiWindowFlags fixed = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings;

void text(std::string_view value) {
    if (!value.empty()) {
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
    }
}

void quiet(std::string_view value) {
    ImGui::PushStyleColor(ImGuiCol_Text, muted);
    text(value);
    ImGui::PopStyleColor();
}

void heading(std::string_view value, float size = 23.0F) {
    ImGui::PushFont(nullptr, size);
    text(value);
    ImGui::PopFont();
}

void pane(const char *name, ImVec2 position, ImVec2 size, ImGuiWindowFlags flags = 0) {
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(name, nullptr, fixed | flags);
}

void title(std::string_view value) {
    ImGui::Spacing();
    heading(value, 19.0F);
    ImGui::Spacing();
}

void camera_picker(Camera &camera) {
    for (const auto &[mode, caption] :
         {std::pair{CameraMode::orbit, "Orbit"}, std::pair{CameraMode::plan, "Top-down"},
          std::pair{CameraMode::follow, "Follow"}}) {
        if (camera.mode == mode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.18F, 0.36F, 0.38F, 1.0F});
        }
        const bool selected = camera.mode == mode;
        const bool clicked = ImGui::Button(caption);
        if (selected) {
            ImGui::PopStyleColor();
        }
        if (clicked) {
            camera.mode = mode;
        }
        if (mode != CameraMode::follow) {
            ImGui::SameLine();
        }
    }
}

} // namespace

void configure_style() {
    auto &style = ImGui::GetStyle();
    style.WindowPadding = {17.0F, 12.0F};
    style.FramePadding = {10.0F, 7.0F};
    style.ItemSpacing = {9.0F, 8.0F};
    style.WindowRounding = 0;
    style.ChildRounding = 5.0F;
    style.FrameRounding = 5.0F;
    style.GrabRounding = 4.0F;
    style.WindowBorderSize = 0;
    style.ScrollbarSize = 10.0F;
    style.FontSizeBase = 15.0F;
    style.Colors[ImGuiCol_Text] = primary;
    style.Colors[ImGuiCol_TextDisabled] = muted;
    style.Colors[ImGuiCol_WindowBg] = ImVec4{0.064F, 0.091F, 0.135F, 1.0F};
    style.Colors[ImGuiCol_ChildBg] = ImVec4{0.045F, 0.066F, 0.10F, 1.0F};
    style.Colors[ImGuiCol_Border] = ImVec4{0.18F, 0.24F, 0.31F, 1.0F};
    style.Colors[ImGuiCol_Button] = ImVec4{0.13F, 0.20F, 0.28F, 1.0F};
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4{0.20F, 0.36F, 0.40F, 1.0F};
    style.Colors[ImGuiCol_ButtonActive] = ImVec4{0.20F, 0.42F, 0.44F, 1.0F};
    style.Colors[ImGuiCol_Header] = ImVec4{0.12F, 0.29F, 0.32F, 1.0F};
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4{0.17F, 0.29F, 0.37F, 1.0F};
    style.Colors[ImGuiCol_FrameBg] = ImVec4{0.10F, 0.15F, 0.22F, 1.0F};
    style.Colors[ImGuiCol_PlotHistogram] = teal;
    style.Colors[ImGuiCol_CheckMark] = teal;
}

void Panels::poll() {
    auto result = workbench_.take();
    if (!result) {
        return;
    }
    status_ = std::move(result->status);
    if (result->kind == JobKind::catalog && !benchmark_) {
        catalog_cursor_ = result->products.size() == inventory::page_size
                              ? std::optional{result->products.back().id}
                              : std::nullopt;
        const auto available = product_limit - products_.size();
        for (std::size_t index = 0; index < std::min(available, result->products.size()); ++index) {
            products_.push_back(std::move(result->products[index]));
        }
        filter_dirty_ = true;
    }
    if (result->page) {
        page_ = std::move(result->page);
    }
    if (result->audit) {
        search_ready = search_ready || result->kind == JobKind::find;
        audit_ready = audit_ready || result->kind == JobKind::audit;
        report_ = std::move(result->audit);
        regions_.clear();
        for (const auto &row : report_->rows) {
            if (!row.evidence || row.finding == audit::Finding::supported) {
                continue;
            }
            for (const auto &entry : row.evidence->views) {
                if (regions_.size() < 16) {
                    regions_.push_back({entry.footprint.origin, entry.footprint.heading.value(),
                                        entry.footprint.range.value()});
                }
            }
        }
    }
    if (result->mission) {
        mission_ready = true;
        mission_ = std::move(result->mission);
        events_ = std::move(result->events);
    }
}

bool Panels::queue(Job job) {
    const auto kind = job.kind;
    if (!workbench_.submit(std::move(job))) {
        return false;
    }
    if (kind == JobKind::scan_mission || kind == JobKind::dock_mission ||
        kind == JobKind::inspect_mission || kind == JobKind::confirm_inspection ||
        kind == JobKind::cancel_mission) {
        mission_ready = false;
    }
    status_ = "Running in the warehouse simulator";
    return true;
}

void Panels::seed_benchmark() {
    benchmark_ = true;
    products_.clear();
    products_.reserve(product_limit);
    for (int index = 0; index < 12000; ++index) {
        std::array<char, 12> code{};
        std::snprintf(code.data(), code.size(), "991-%02d-%04d", index / 10000, index % 10000);
        products_.push_back({inventory::Dpci{code.data()},
                             "Fictional sample " + std::to_string(index),
                             inventory::Category::home_goods});
    }
    filter_dirty_ = true;
}

Canvas Panels::draw(const LiveFrame &frame, bool focused) {
    const auto size = ImGui::GetIO().DisplaySize;
    const auto left = std::clamp(size.x * 0.148F, 160.0F, 210.0F);
    const auto right = std::clamp(size.x * 0.269F, 260.0F, 392.0F);
    constexpr float header = 72.0F;
    constexpr float footer = 41.0F;
    const auto body = std::max(1.0F, size.y - header - footer);
    pane("##header", {0, 0}, {size.x, header}, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, teal);
    heading("ARGUS", 31.0F);
    ImGui::PopStyleColor();
    ImGui::SameLine(142.0F);
    ImGui::AlignTextToFramePadding();
    heading("Warehouse operations", 23.0F);
    ImGui::SameLine();
    quiet("Simulation");
    ImGui::End();

    pane("##nav", {0, header}, {left, body});
    title("Workspace");
    for (std::size_t index = 0; index < panel_names.size(); ++index) {
        if (ImGui::Selectable(panel_names[index].data(), static_cast<std::size_t>(panel) == index,
                              0, {0, 30.0F})) {
            panel = static_cast<Panel>(index);
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    quiet("Live robot");
    if (ImGui::Button(frame.exploring ? "Pause exploration" : "Explore", {-1, 0})) {
        request(frame.exploring ? LiveAction::pause : LiveAction::explore);
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.44F, 0.16F, 0.21F, 1.0F});
    if (ImGui::Button("Emergency stop", {-1, 0})) {
        request(LiveAction::emergency_stop);
    }
    ImGui::PopStyleColor();
    ImGui::End();

    const Canvas canvas{left, header, std::max(1.0F, size.x - left - right), body};
    pane("##warehouse", {canvas.x, canvas.y}, {canvas.width, canvas.height},
         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    heading(map_only ? "Estimated occupancy" : "Live warehouse", 22.0F);
    if (truth_visible(panel, developer_truth) && !map_only) {
        ImGui::TextColored(amber, "SIMULATED TRUTH OVERLAY");
    } else {
        quiet("Observed robot pose and sensor returns");
    }
    camera_picker(camera);
    const auto remaining = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##orbit-hitbox",
                           {std::max(1.0F, remaining.x), std::max(1.0F, remaining.y)},
                           ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemActive()) {
        const auto delta = ImGui::GetIO().MouseDelta;
        camera.orbit(delta.x, delta.y);
    }
    if (ImGui::IsItemHovered()) {
        camera.scale(ImGui::GetIO().MouseWheel);
    }
    ImGui::End();

    pane("##details", {size.x - right, header}, {right, body});
    heading(panel_names[static_cast<std::size_t>(panel)]);
    ImGui::Separator();
    panel_contents(frame, focused);
    ImGui::End();

    pane("##footer", {0, size.y - footer}, {size.x, footer}, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(teal, "%s", frame.exploring ? "AUTONOMOUS" : "MANUAL");
    ImGui::SameLine();
    ImGui::Text("Sample %llu", static_cast<unsigned long long>(frame.telemetry.sample_tick));
    ImGui::SameLine();
    ImGui::Text("LiDAR %u returns", frame.lidar ? frame.lidar->return_count : 0);
    ImGui::SameLine();
    ImGui::Text("RFID %zu reads", frame.rfid_reads);
    ImGui::SameLine();
    quiet(status_);
    ImGui::End();
    return canvas;
}

void Panels::panel_contents(const LiveFrame &frame, bool focused) {
    switch (panel) {
    case Panel::overview:
        overview(frame, focused);
        break;
    case Panel::live_map:
        map();
        break;
    case Panel::find_product:
        find();
        break;
    case Panel::inventory_audit:
        audit();
        break;
    case Panel::missions:
        missions();
        break;
    case Panel::history:
        history();
        break;
    case Panel::settings:
        settings();
        break;
    case Panel::diagnostics:
        diagnostics(frame);
        break;
    }
}

void Panels::overview(const LiveFrame &frame, bool focused) {
    const auto &data = frame.telemetry;
    title("Robot status");
    const auto fraction = data.capacity_uj > 0 ? static_cast<float>(data.energy_uj) /
                                                     static_cast<float>(data.capacity_uj)
                                               : 0.0F;
    if (data.emergency_latched) {
        ImGui::TextColored(amber, "Emergency stop latched");
    } else if (!focused) {
        quiet("Motion disarmed");
    } else if (data.collision_blocked) {
        quiet("Collision guard active");
    }
    ImGui::ProgressBar(fraction, {-1, 0});
    quiet("Battery");
    ImGui::Text("East %.2f m   North %.2f m", data.estimated_pose.x.value(),
                data.estimated_pose.y.value());
    ImGui::Text("Speed %.2f m/s", data.measured.linear.value());
    title("Live observations");
    ImGui::Text("%zu distinct EPCs", frame.unique_epcs);
    ImGui::Text("%zu path samples", frame.trace.size());
    ImGui::Text("%zu route points", frame.route.size());
    title("Drive");
    quiet("Hold Space and use the arrow keys");
    quiet("X stops · R clears the stop");
}

void Panels::map() {
    title("Layers");
    ImGui::Checkbox("LiDAR returns", &overlays.lidar);
    ImGui::Checkbox("Estimated occupancy", &overlays.occupancy);
    ImGui::Checkbox("Planned route", &overlays.route);
    ImGui::Checkbox("Odometry trail", &overlays.trace);
    ImGui::Checkbox("RFID antenna region", &overlays.antenna);
    ImGui::Checkbox("Uncertain evidence", &overlays.uncertainty);
    if (ImGui::Checkbox("Map only", &map_only) && map_only) {
        overlays.occupancy = true;
        camera.mode = CameraMode::plan;
    }
}

void Panels::find() {
    title("Search by DPCI");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##dpci", "DDD-DD-DDDD", dpci_.data(), dpci_.size());
    const inventory::Dpci request{dpci_.data()};
    ImGui::BeginDisabled(busy() || !inventory::valid(request));
    if (ImGui::Button("Lookup")) {
        static_cast<void>(queue({JobKind::lookup, request.value}));
    }
    ImGui::SameLine();
    if (ImGui::Button("Investigate")) {
        static_cast<void>(queue({JobKind::find, request.value}));
    }
    ImGui::EndDisabled();
    if (page_) {
        heading(page_->product.name, 18.0F);
        ImGui::Text("%zu assigned EPCs on page", page_->units.size());
        if (ImGui::BeginChild("##units", {0, 100.0F}, ImGuiChildFlags_Borders)) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(page_->units.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    text(page_->units.at(static_cast<std::size_t>(row)).epc.value);
                }
            }
        }
        ImGui::EndChild();
        if (page_->has_more && ImGui::Button("Next EPC page")) {
            static_cast<void>(
                queue({JobKind::lookup, page_->product.id.value, page_->units.back().epc}));
        }
    }
    if (report_ && std::holds_alternative<audit::FindItem>(report_->request)) {
        findings();
    }
    inventory_table();
}

void Panels::inventory_table() {
    title("Catalog");
    ImGui::SetNextItemWidth(-1.0F);
    filter_dirty_ = ImGui::InputTextWithHint("##filter", "Filter product or DPCI", filter_.data(),
                                             filter_.size()) ||
                    filter_dirty_;
    if (filter_dirty_) {
        filtered_.clear();
        const std::string_view search{filter_.data()};
        for (std::size_t index = 0; index < products_.size(); ++index) {
            const auto &product = products_[index];
            if (product.id.value.find(search) != std::string::npos ||
                product.name.find(search) != std::string::npos) {
                filtered_.push_back(index);
            }
        }
        filter_dirty_ = false;
    }
    ImGui::Text("%zu products", filtered_.size());
    painted_rows_ = 0;
    if (ImGui::BeginChild("##products",
                          {0, std::clamp(ImGui::GetContentRegionAvail().y, 120.0F, 280.0F)},
                          ImGuiChildFlags_Borders)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered_.size()));
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const auto &product = products_[filtered_[static_cast<std::size_t>(index)]];
                if (ImGui::Selectable(product.id.value.c_str(), false,
                                      ImGuiSelectableFlags_AllowOverlap)) {
                    std::snprintf(dpci_.data(), dpci_.size(), "%s", product.id.value.c_str());
                }
                ImGui::SameLine(110.0F);
                text(product.name);
                ++painted_rows_;
            }
        }
    }
    ImGui::EndChild();
    if (catalog_cursor_ && products_.size() < product_limit) {
        ImGui::BeginDisabled(busy());
        if (ImGui::Button("Load next catalog page")) {
            static_cast<void>(queue({JobKind::catalog, catalog_cursor_->value}));
        }
        ImGui::EndDisabled();
    }
}

void Panels::audit() {
    title("Aisle verification");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::Combo("##aisle", &aisle_, aisle_names.data(), static_cast<int>(aisle_names.size()));
    ImGui::BeginDisabled(busy());
    if (ImGui::Button("Run audit", {-1, 0})) {
        static_cast<void>(
            queue({JobKind::audit, aisle_names.at(static_cast<std::size_t>(aisle_))}));
    }
    ImGui::EndDisabled();
    if (report_ && std::holds_alternative<audit::AuditAisle>(report_->request)) {
        ImGui::Text("%zu unique EPCs · %zu raw reads", report_->unique_observed,
                    report_->raw_reads);
        title("Expected / observed");
        for (const auto &stock : report_->quantities) {
            ImGui::Text("%s   %llu / %zu", stock.product.value.c_str(),
                        static_cast<unsigned long long>(stock.expected),
                        stock.uniquely_observed_in_aisle);
        }
        findings();
    }
}

void Panels::findings() {
    title("Aisle evidence");
    quiet("Heuristic · review uncertain findings");
    if (ImGui::BeginChild("##evidence", {0, 179.0F}, ImGuiChildFlags_Borders)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(report_->rows.size()));
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const auto &row = report_->rows[static_cast<std::size_t>(index)];
                ImGui::Text("%s   %s", row.epc.value.c_str() + 16,
                            audit::label(row.finding).data());
            }
        }
    }
    ImGui::EndChild();
}

void Panels::missions() {
    title("Simulated mission queue");
    ImGui::BeginDisabled(busy());
    for (const auto &[kind, label] : {std::pair{JobKind::scan_mission, "Scan all aisles"},
                                      std::pair{JobKind::dock_mission, "Return to dock"},
                                      std::pair{JobKind::inspect_mission, "Manual inspection"}}) {
        if (ImGui::Button(label, {-1, 0})) {
            static_cast<void>(queue({kind, kind == JobKind::inspect_mission ? "TECH" : ""}));
        }
    }
    if (mission_ && !mission::terminal(mission_->state)) {
        if (mission_->intent.workflow == mission::Workflow::inspect &&
            mission_->state == mission::State::executing && ImGui::Button("Confirm inspection")) {
            static_cast<void>(queue({JobKind::confirm_inspection, {}, {}, mission_->intent.id}));
        }
        if (ImGui::Button("Cancel open mission")) {
            static_cast<void>(queue({JobKind::cancel_mission, {}, {}, mission_->intent.id}));
        }
    }
    ImGui::EndDisabled();
    if (mission_) {
        title("Latest mission");
        ImGui::Text("%s · %s", mission::label(mission_->intent.workflow).data(),
                    mission::label(mission_->state).data());
        ImGui::Text("%llu / %llu views",
                    static_cast<unsigned long long>(mission_->checkpoint.covered_scans),
                    static_cast<unsigned long long>(mission_->checkpoint.planned_scans));
        ImGui::Text("%llu mm odometry",
                    static_cast<unsigned long long>(mission_->checkpoint.actual_mm));
    }
}

void Panels::history() {
    title("Mission transitions");
    if (!mission_) {
        quiet("Run a mission to see its history here.");
        return;
    }
    ImGui::Text("Mission %llu", static_cast<unsigned long long>(mission_->intent.id.value()));
    if (ImGui::BeginChild("##history-events", {0, 0}, ImGuiChildFlags_Borders)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(events_.size()));
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const auto &event = events_[static_cast<std::size_t>(index)];
                ImGui::Text("%5.2fs   %s   %s", static_cast<double>(event.elapsed_ms) / 1000.0,
                            mission::label(event.to).data(), mission::label(event.cause).data());
            }
        }
    }
    ImGui::EndChild();
}

void Panels::settings() {
    title("Camera");
    camera_picker(camera);
    if (ImGui::Button("Reset camera")) {
        camera = {};
    }
    quiet("Right-drag to orbit. Scroll to zoom.");
    title("Live simulation");
    if (ImGui::Button("Restart live run", {-1, 0})) {
        request(LiveAction::restart);
    }
    if (ImGui::Button("Clear emergency latch", {-1, 0})) {
        request(LiveAction::reset_stop);
    }
    quiet("Restart and stop reset require Space released.");
}

void Panels::diagnostics(const LiveFrame &frame) {
    title("Sensor quality");
    ImGui::Text("Navigation   %s", frame.navigation.data());
    ImGui::Text("Position bound %.3f m", frame.telemetry.wheels.distance_error_m);
    ImGui::Text("Heading bound %.3f rad", frame.telemetry.wheels.heading_error_rad);
    title("Developer view");
    ImGui::Checkbox("Enable simulated truth overlay", &developer_truth);
    ImGui::TextWrapped("Pink outlines compare the simulator pose and actors with the observed "
                       "scene. This overlay is limited to Diagnostics.");
    if (ImGui::Button("Rebuild graphics context", {-1, 0})) {
        rebuild = true;
    }
}

} // namespace argus::desktop
