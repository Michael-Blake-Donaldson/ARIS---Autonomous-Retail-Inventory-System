#include "panels.hpp"

#include "argus/mapping/map_io.hpp"

#include <sstream>

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

std::string_view finding_name(audit::Finding finding) noexcept {
    switch (finding) {
    case audit::Finding::supported:
        return "Assigned aisle";
    case audit::Finding::likely_misplaced:
        return "Likely misplaced";
    case audit::Finding::ambiguous:
        return "Ambiguous aisle";
    case audit::Finding::unscanned:
        return "Unscanned";
    case audit::Finding::insufficient_coverage:
        return "One view, no read";
    case audit::Finding::not_observed_after_two_views:
        return "Two views, no read";
    case audit::Finding::unknown_epc:
        return "Unknown EPC";
    }
    return {};
}

void camera_picker(Camera &camera, bool archived = false) {
    for (const auto &[mode, caption] :
         {std::pair{CameraMode::orbit, "Orbit"}, std::pair{CameraMode::plan, "Top-down"},
          std::pair{CameraMode::follow, "Follow"}}) {
        if (archived && mode == CameraMode::follow) {
            break;
        }
        const bool selected = view_camera_mode(camera.mode, archived) == mode;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.18F, 0.36F, 0.38F, 1.0F});
        }
        const bool clicked = ImGui::Button(caption);
        if (selected) {
            ImGui::PopStyleColor();
        }
        if (clicked) {
            camera.mode = mode;
        }
        if (mode == CameraMode::orbit || (!archived && mode == CameraMode::plan)) {
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
    if (std::exchange(close_review_pending_, false)) {
        close_review();
    }
    if (!live_) {
        if (auto preview = workbench_.preview()) {
            preview_ = std::move(preview);
        }
    } else if (const auto id = live_->frame().run; id != run_) {
        run_ = id;
        search_session_ = audit_session_ = 0;
        audit_json_.clear();
        report_.reset();
        regions_.clear();
        search_ready = audit_ready = saved_ready_ = archive_ready_ = false;
        mission_run_.reset();
    }
    auto result = workbench_.take();
    if (!result) {
        return;
    }
    status_ = std::move(result->status);
    const bool returned_live = static_cast<bool>(result->live);
    if (returned_live) {
        live_ = std::move(result->live);
        preview_.reset();
    }
    if (result->kind == JobKind::save_run) {
        saved_ready_ = status_ == "Run evidence saved";
    }
    if (result->archive) {
        auto &archive = *result->archive;
        const auto current = live_->frame();
        std::istringstream bytes{archive.map};
        auto loaded = mapping::load(bytes, current.grid.spec());
        if (auto *grid = std::get_if<mapping::Grid>(&loaded);
            grid && archive.scenario == current.grid.spec().scenario) {
            LiveFrame saved_frame{.telemetry = {.sample_tick = archive.sample_tick},
                                  .grid = *grid,
                                  .lidar = nullptr,
                                  .route = {},
                                  .trace = {},
                                  .rfid_reads = 0,
                                  .unique_epcs = 0,
                                  .map = {},
                                  .estimate = {},
                                  .navigation = {},
                                  .run = archive.id,
                                  .archived = true};
            saved_map_.emplace(saved_frame);
            saved_ = std::move(archive);
            saved_review_text_ = std::move(result->review_text);
            live_->finish_job(false);
            archive_ready_ = true;
            panel = Panel::history;
        } else {
            status_ = "Saved map is damaged or belongs to another warehouse";
        }
    }
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
        if (result->kind == JobKind::find) {
            search_session_ = report_->session.value();
        } else if (result->kind == JobKind::audit) {
            audit_session_ = 0;
            audit_json_.clear();
            std::ostringstream report;
            if (std::holds_alternative<std::monostate>(
                    audit::write_report(report, *report_, audit::Export::json)) &&
                report.view().size() <= mission::archived_report_limit) {
                audit_session_ = report_->session.value();
                audit_json_ = std::move(report).str();
            }
        }
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
        if (returned_live) {
            mission_run_ = run_;
        }
    }
}

bool Panels::queue(Job job) {
    const auto kind = job.kind;
    if (pending_ || reviewing()) {
        return false;
    }
    const bool motion = kind == JobKind::find || kind == JobKind::audit ||
                        kind == JobKind::scan_mission || kind == JobKind::dock_mission ||
                        kind == JobKind::inspect_mission;
    if (motion) {
        if (mission_ && !mission::terminal(mission_->state)) {
            status_ = "Finish or cancel the existing mission before starting routed work";
            return false;
        }
        if (!routed_ || !live_ || lidar_dropout || live_->frame().telemetry.emergency_latched) {
            status_ = !routed_ ? "Routed work uses the RFID audit warehouse"
                               : "Clear the stop and LiDAR fault before starting routed work";
            return false;
        }
        if (workbench_.busy()) {
            return false;
        }
        pending_.emplace(std::move(job));
    } else if (!workbench_.submit(std::move(job))) {
        return false;
    }
    if (kind == JobKind::scan_mission || kind == JobKind::dock_mission ||
        kind == JobKind::inspect_mission || kind == JobKind::confirm_inspection ||
        kind == JobKind::cancel_mission) {
        mission_ready = false;
    }
    status_ = motion ? "Running on the live robot" : "Working";
    return true;
}

void Panels::dispatch() {
    if (!pending_) {
        return;
    }
    auto job = std::exchange(pending_, std::nullopt).value();
    if (lidar_dropout || live_->frame().telemetry.emergency_latched) {
        status_ = "Routed work held before dispatch";
        return;
    }
    preview_ = std::make_shared<ObservedFrame>(live_->frame());
    job_start_tick_ = preview_->telemetry.sample_tick;
    job.live = std::move(live_);
    if (!workbench_.submit(std::move(job))) {
        live_ = std::move(job.live);
        preview_.reset();
        status_ = "Live run is busy";
    }
}

void Panels::cancel_pending() noexcept { pending_.reset(); }

LiveFrame Panels::frame() const noexcept {
    if (saved_map_) {
        return saved_map_->view();
    }
    return live_ ? live_->frame() : preview_->view();
}

bool Panels::save() {
    if (!live_ || reviewing() || audit_json_.empty() || busy()) {
        return false;
    }
    const auto current = live_->frame();
    std::ostringstream bytes;
    if (!std::holds_alternative<std::monostate>(mapping::save(current.grid, bytes))) {
        status_ = "Map could not be saved";
        return false;
    }
    saved_ready_ = false;
    Job job{JobKind::save_run};
    job.archive =
        mission::SavedRun{current.run,
                          current.grid.spec().scenario,
                          current.telemetry.sample_tick,
                          mission_ && mission_run_ == current.run ? mission_->intent.id.value() : 0,
                          search_session_,
                          audit_session_,
                          std::move(bytes).str(),
                          audit_json_};
    return queue(std::move(job));
}

void Panels::close_review() noexcept {
    saved_map_.reset();
    saved_.reset();
    saved_review_text_.clear();
    archive_ready_ = false;
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
    ImGui::BeginDisabled(reviewing());
    for (std::size_t index = 0; index < panel_names.size(); ++index) {
        if (ImGui::Selectable(panel_names[index].data(), static_cast<std::size_t>(panel) == index,
                              0, {0, 30.0F})) {
            panel = static_cast<Panel>(index);
        }
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::Separator();
    quiet("Live robot");
    ImGui::BeginDisabled(worker_driving() || reviewing());
    if (ImGui::Button(frame.exploring() ? "Pause exploration" : "Explore", {-1, 0})) {
        request(frame.exploring() ? LiveAction::pause : LiveAction::explore);
    }
    ImGui::EndDisabled();
    if (worker_driving() && ImGui::Button("Hold routed work", {-1, 0})) {
        interrupt();
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
    heading(frame.archived ? "Saved occupancy"
            : map_only     ? "Estimated occupancy"
                           : "Live warehouse",
            22.0F);
    if (!frame.archived && !worker_driving() && truth_visible(panel, developer_truth) &&
        !map_only) {
        ImGui::TextColored(amber, "SIMULATED TRUTH OVERLAY");
    } else if (!frame.archived) {
        quiet("Observed robot pose and sensor returns");
    } else {
        quiet("Captured map evidence");
    }
    camera_picker(camera, frame.archived);
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
    heading(frame.archived ? "Lineage / History" : panel_names[static_cast<std::size_t>(panel)]);
    ImGui::Separator();
    if (worker_driving() && panel != Panel::live_map) {
        const auto elapsed = frame.telemetry.sample_tick - job_start_tick_;
        ImGui::Text("Live robot   %.1f s elapsed",
                    static_cast<double>(elapsed) * robot::step_ms / 1000.0);
        if (frame.navigation) {
            quiet(describe(frame.navigation->state));
            if (frame.navigation->reason != navigation::Reason::none) {
                ImGui::TextWrapped("%s", describe(frame.navigation->reason));
            }
        }
        ImGui::Separator();
    }
    if (frame.archived) {
        history();
    } else {
        panel_contents(frame, focused);
    }
    ImGui::End();

    pane("##footer", {0, size.y - footer}, {size.x, footer}, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(teal, "%s",
                       frame.archived      ? "SAVED RUN"
                       : worker_driving()  ? "ROUTED WORK"
                       : frame.exploring() ? "AUTONOMOUS"
                                           : "MANUAL");
    ImGui::SameLine();
    ImGui::Text("Sample %llu", static_cast<unsigned long long>(frame.telemetry.sample_tick));
    if (!frame.archived) {
        ImGui::SameLine();
        if (frame.map.scan == ScanState::fresh && frame.lidar) {
            ImGui::Text("LiDAR %u returns", frame.lidar->return_count);
        } else {
            quiet(frame.map.scan == ScanState::stale ? "LiDAR stale" : "LiDAR unavailable");
        }
        ImGui::SameLine();
        ImGui::Text("RFID %zu reads", frame.rfid_reads);
    }
    ImGui::SameLine();
    quiet(status_);
    ImGui::End();
    constexpr float toolbar_height = 102.0F;
    return {canvas.x, canvas.y + toolbar_height, canvas.width,
            std::max(1.0F, canvas.height - toolbar_height)};
}

void Panels::panel_contents(const LiveFrame &frame, bool focused) {
    switch (panel) {
    case Panel::overview:
        overview(frame, focused);
        break;
    case Panel::live_map:
        map(frame, focused);
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

void Panels::map(const LiveFrame &frame, bool focused) {
    title("Scan and localization");
    switch (frame.map.scan) {
    case ScanState::powered_off:
        quiet("LiDAR power off");
        break;
    case ScanState::waiting:
        quiet("Waiting for the first LiDAR scan");
        break;
    case ScanState::fresh:
    case ScanState::stale: {
        const bool fresh = frame.map.scan == ScanState::fresh;
        ImGui::TextColored(fresh ? teal : amber, "LiDAR %s   %llu ms old",
                           fresh ? "fresh" : "stale",
                           static_cast<unsigned long long>(frame.map.scan_age_ms.value_or(0)));
        break;
    }
    }
    if (frame.map.pose == PoseState::waiting) {
        quiet("Waiting for odometry");
    } else {
        const bool current = frame.map.pose == PoseState::current;
        ImGui::TextColored(current ? teal : amber, "%s",
                           current ? "Localization current" : "Localization uncertain");
    }
    if (frame.estimate) {
        ImGui::Text("Error bounds %.2f m   %.2f rad", frame.estimate->distance_error_m,
                    frame.estimate->heading_error_rad);
    }
    const auto padding = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {padding.x, 4.0F});
    ImGui::Checkbox("Simulate LiDAR dropout", &lidar_dropout);

    title("Navigation");
    if (frame.telemetry.emergency_latched) {
        ImGui::TextColored(amber, "Emergency stop latched");
    } else if (!focused) {
        quiet("Motion disarmed");
    } else if (frame.navigation) {
        ImGui::TextUnformatted(describe(frame.navigation->state));
        if (frame.navigation->reason != navigation::Reason::none) {
            ImGui::TextWrapped("%s", describe(frame.navigation->reason));
        }
    } else {
        quiet(frame.telemetry.collision_blocked ? "Collision guard active" : "Manual control");
    }
    if (frame.navigation) {
        ImGui::Text("Yields %u   Replans %u", frame.navigation->yields, frame.navigation->replans);
        if (frame.navigation->last_replan) {
            ImGui::TextWrapped("Last replan: %s", describe(*frame.navigation->last_replan));
        }
    }

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
    ImGui::PopStyleVar();
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
                text(std::string_view{row.epc.value}.substr(16));
                ImGui::SameLine(105.0F);
                ImGui::TextColored(row.finding == audit::Finding::supported ? teal : amber, "%s",
                                   finding_name(row.finding).data());
                if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 23);
                    text(row.epc.value);
                    text(audit::rationale(row.finding));
                    if (row.attribution.aisle) {
                        ImGui::Text("Candidate aisle: %s", row.attribution.aisle->value.c_str());
                    }
                    if (row.evidence) {
                        ImGui::Text("%zu reads across %zu views", row.evidence->read_count,
                                    row.evidence->views.size());
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
        }
    }
    ImGui::EndChild();
}

void Panels::missions() {
    title("Simulated mission queue");
    const bool active = mission_ && !mission::terminal(mission_->state);
    ImGui::BeginDisabled(busy() || active);
    for (const auto &[kind, label] : {std::pair{JobKind::scan_mission, "Scan all aisles"},
                                      std::pair{JobKind::dock_mission, "Return to dock"},
                                      std::pair{JobKind::inspect_mission, "Manual inspection"}}) {
        if (ImGui::Button(label, {-1, 0})) {
            static_cast<void>(queue({kind, kind == JobKind::inspect_mission ? "TECH" : ""}));
        }
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(busy());
    if (active) {
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
        title(mission_run_ == run_ ? "Latest mission" : "Earlier mission");
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
    if (saved_) {
        title("Saved run");
        if (ImGui::Button("Return to live", {-1, 0})) {
            // The rendered frame still borrows the archive until the next UI tick.
            close_review_pending_ = true;
        }
        ImGui::Text("Run %llu", static_cast<unsigned long long>(saved_->id.value()));
        if (saved_->search_session) {
            ImGui::Text("Search session %llu",
                        static_cast<unsigned long long>(saved_->search_session));
        }
        ImGui::Text("Audit session %llu", static_cast<unsigned long long>(saved_->audit_session));
        if (saved_->mission) {
            ImGui::Text("Mission %llu", static_cast<unsigned long long>(saved_->mission));
        }
        if (ImGui::Button("Copy audit JSON", {-1, 0})) {
            ImGui::SetClipboardText(saved_->report.c_str());
        }
        auto &text = saved_review_text_.empty() ? saved_->report : saved_review_text_;
        ImGui::InputTextMultiline("##saved-report", text.data(), text.size() + 1, {-1, 180},
                                  ImGuiInputTextFlags_ReadOnly);
        return;
    }
    title("Run evidence");
    ImGui::Text("Run %llu", static_cast<unsigned long long>(run_.value()));
    ImGui::BeginDisabled(audit_json_.empty() || busy());
    if (ImGui::Button("Save map and audit", {-1, 0})) {
        static_cast<void>(save());
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(busy());
    if (ImGui::Button("Open saved run", {-1, 0})) {
        static_cast<void>(queue({JobKind::reopen_run}));
    }
    ImGui::EndDisabled();
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
    ImGui::Text("Navigation   %s", frame.navigation ? describe(frame.navigation->state) : "Manual");
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
