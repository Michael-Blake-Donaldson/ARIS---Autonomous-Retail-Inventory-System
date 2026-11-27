#include "application.hpp"
#include "desktop_metrics.hpp"
#include "live_engine.hpp"
#include "panels.hpp"
#include "robot_run.hpp"
#include "warehouse_io.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>

namespace {

using namespace argus;
using namespace argus::desktop;
using app::Error;

enum class Mode {
    interactive,
    smoke,
    tour,
    record,
    reopen,
    fleet_smoke,
    fleet_reopen,
    fleet_traffic,
    fleet_traffic_reverse,
    fleet_data_fault,
    fleet_profile_report,
    fleet_review,
    fleet_review_reopen,
    dashboard_record,
    review_record
};
bool review_probe(Mode mode) noexcept {
    return mode == Mode::fleet_review || mode == Mode::fleet_review_reopen ||
           mode == Mode::review_record;
}
bool fleet_probe(Mode mode) noexcept {
    return mode == Mode::fleet_smoke || mode == Mode::fleet_reopen || mode == Mode::fleet_traffic ||
           mode == Mode::fleet_traffic_reverse || mode == Mode::fleet_data_fault ||
           mode == Mode::fleet_profile_report || mode == Mode::dashboard_record ||
           review_probe(mode);
}
bool captures_video(Mode mode) noexcept {
    return mode == Mode::record || mode == Mode::dashboard_record || mode == Mode::review_record;
}
struct ProbePointer {
    DashboardPoint point;
    bool down;
};
constexpr auto title = "ARGUS Operations Center";
std::optional<std::filesystem::path> operations_directory;

class Recorder {
  public:
    static constexpr int width = 1280;
    static constexpr int height = 720;

    bool capture(int actual_width, int actual_height) {
        if (actual_width != width || actual_height != height) {
            return false;
        }
        const auto now = SDL_GetTicks();
        if (now < next_) {
            return true;
        }
        next_ = next_ == 0 || now - next_ > 200 ? now + 50 : next_ + 50;
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, buffer_.data());
        return std::fwrite(buffer_.data(), 1, buffer_.size(), stdout) == buffer_.size();
    }

  private:
    std::vector<unsigned char> buffer_ = std::vector<unsigned char>(width * height * 3);
    std::uint64_t next_{};
};

class Graphics {
  public:
    explicit Graphics(SDL_Window *window, float test_scale)
        : window_{window}, test_scale_{test_scale} {}
    Graphics(const Graphics &) = delete;
    Graphics &operator=(const Graphics &) = delete;
    ~Graphics() { close(); }

    bool open() {
        context_ = SDL_GL_CreateContext(window_);
        if (!context_ || !SDL_GL_MakeCurrent(window_, context_)) {
            return false;
        }
        SDL_GL_SetSwapInterval(0);
        imgui_ = ImGui::CreateContext();
        if (!imgui_) {
            return false;
        }
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        configure_style();
        io.FontDefault = io.Fonts->AddFontDefaultVector();
        update_scale();
        platform_ = ImGui_ImplSDL3_InitForOpenGL(window_, context_);
        renderer_ = platform_ && ImGui_ImplOpenGL2_Init();
        if (renderer_) {
            photos_.load();
        }
        return renderer_;
    }

    bool rebuild() {
        close();
        return open();
    }

    void event(const SDL_Event &event) const {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
            update_scale();
        }
    }
    [[nodiscard]] bool photos_ready() const noexcept { return photos_.ready(); }

    bool draw(Scene &scene, const LiveEngine *engine, Panels &ui, bool focused, bool demo_scene,
              Recorder *recorder, std::optional<double> *scene_ms = nullptr,
              std::optional<ProbePointer> input = {}, Scene *fleet_scene = nullptr) const {
        int width{};
        int height{};
        if (!SDL_GetWindowSizeInPixels(window_, &width, &height)) {
            return false;
        }
        if (width <= 0 || height <= 0) {
            return true;
        }
        if (SDL_GL_GetCurrentContext() != context_ && !SDL_GL_MakeCurrent(window_, context_)) {
            return false;
        }
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (input) {
            auto &io = ImGui::GetIO();
            io.AddMousePosEvent(input->point.x, input->point.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, input->down);
        }
        ImGui::NewFrame();
        const auto canvas = ui.draw(focused, photos_);
        const auto frame = ui.display_frame();
        const auto display = ImGui::GetIO().DisplaySize;
        ImGui::Render();
        if (display.x <= 0 || display.y <= 0) {
            return true;
        }
        const auto sx = static_cast<float>(width) / display.x;
        const auto sy = static_cast<float>(height) / display.y;
        glViewport(0, 0, width, height);
        glClearColor(0.025F, 0.040F, 0.064F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        std::optional<DiagnosticsFrame> developer;
        if (engine && !frame.archived && truth_visible(ui.panel, ui.developer_truth) &&
            !ui.map_only) {
            developer = engine->diagnostics();
        }
        const Viewport viewport{static_cast<int>(canvas.x * sx),
                                height - static_cast<int>((canvas.y + canvas.height) * sy),
                                static_cast<int>(canvas.width * sx),
                                static_cast<int>(canvas.height * sy)};
        const bool fleet_view = fleet_panel(ui.panel);
        const auto regions = demo_scene && !frame.archived && !fleet_view
                                 ? ui.regions()
                                 : std::span<const EvidenceRegion>{};
        const auto overlays = ui.display_overlays(frame);
        auto camera = ui.camera;
        camera.mode = view_camera_mode(camera.mode, frame.archived);
        const auto scene_started = scene_ms ? SDL_GetPerformanceCounter() : 0;
        auto &active_scene = ui.panel == Panel::maintenance && fleet_scene ? *fleet_scene : scene;
        active_scene.draw(viewport, frame, camera, overlays, regions,
                          developer ? &*developer : nullptr,
                          frame.archived || (!fleet_view && ui.map_only));
        if (scene_ms) {
            *scene_ms = static_cast<double>(SDL_GetPerformanceCounter() - scene_started) * 1000.0 /
                        static_cast<double>(SDL_GetPerformanceFrequency());
        }
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        if (recorder && !recorder->capture(width, height)) {
            return false;
        }
        return SDL_GL_SwapWindow(window_);
    }

  private:
    void update_scale() const {
        const auto density = std::max(1.0F, SDL_GetWindowPixelDensity(window_));
        const auto scale =
            test_scale_ > 0 ? test_scale_ : SDL_GetWindowDisplayScale(window_) / density;
        ImGui::GetStyle().FontScaleDpi = std::clamp(scale, 1.0F, 1.5F);
    }

    void close() {
        if (context_) {
            SDL_GL_MakeCurrent(window_, context_);
            photos_.clear();
            if (imgui_) {
                if (renderer_) {
                    ImGui_ImplOpenGL2_Shutdown();
                }
                if (platform_) {
                    ImGui_ImplSDL3_Shutdown();
                }
                ImGui::DestroyContext(imgui_);
            }
            SDL_GL_DestroyContext(context_);
        }
        context_ = nullptr;
        imgui_ = nullptr;
        platform_ = false;
        renderer_ = false;
    }

    SDL_Window *window_;
    float test_scale_;
    SDL_GLContext context_{};
    ImGuiContext *imgui_{};
    bool platform_{};
    bool renderer_{};
    FleetPhotos photos_;
};

class Controls {
  public:
    void disarm() noexcept { drive_.disarm(); }
    [[nodiscard]] bool armed() const noexcept { return drive_.armed(); }

    void event(const SDL_Event &event, Panels &ui, bool interactive) {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED) {
            disarm();
            // Native probes ignore desktop focus changes. Their injected focus event has no
            // associated window and exercises the same fleet hold as interactive focus loss.
            if (interactive || event.window.windowID == 0) {
                ui.hold_fleet();
            }
            ui.request(LiveAction::pause);
            return;
        }
        if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
            return;
        }
        const auto pressed = event.type == SDL_EVENT_KEY_DOWN;
        const auto fresh = pressed && !event.key.repeat;
        if (pressed && ImGui::GetIO().WantCaptureKeyboard && event.key.scancode != SDL_SCANCODE_X) {
            disarm();
            return;
        }
        switch (event.key.scancode) {
        case SDL_SCANCODE_SPACE:
            drive_.deadman(pressed, fresh);
            break;
        case SDL_SCANCODE_UP:
            drive_.direction(DriveKey::forward, pressed);
            break;
        case SDL_SCANCODE_DOWN:
            drive_.direction(DriveKey::reverse, pressed);
            break;
        case SDL_SCANCODE_LEFT:
            drive_.direction(DriveKey::left, pressed);
            break;
        case SDL_SCANCODE_RIGHT:
            drive_.direction(DriveKey::right, pressed);
            break;
        case SDL_SCANCODE_X:
            if (fresh) {
                drive_.emergency();
                ui.request(LiveAction::emergency_stop);
            }
            break;
        case SDL_SCANCODE_R:
            if (fresh && !drive_.armed()) {
                ui.request(LiveAction::reset_stop);
            }
            break;
        case SDL_SCANCODE_HOME:
            if (fresh && !drive_.armed()) {
                ui.request(LiveAction::restart);
            }
            break;
        case SDL_SCANCODE_F3:
            if (fresh) {
                ui.panel = Panel::diagnostics;
            }
            break;
        case SDL_SCANCODE_F4:
            if (fresh) {
                ui.overlays.lidar = !ui.overlays.lidar;
            }
            break;
        case SDL_SCANCODE_F5:
            if (fresh) {
                ui.map_only = !ui.map_only;
                ui.panel = Panel::live_map;
                ui.camera.mode = ui.map_only ? CameraMode::plan : CameraMode::orbit;
                ui.overlays.occupancy = ui.map_only;
            }
            break;
        case SDL_SCANCODE_1:
            if (fresh) {
                ui.camera.mode = CameraMode::orbit;
            }
            break;
        case SDL_SCANCODE_2:
            if (fresh) {
                ui.camera.mode = CameraMode::plan;
            }
            break;
        case SDL_SCANCODE_3:
            if (fresh) {
                ui.camera.mode = CameraMode::follow;
            }
            break;
        default:
            break;
        }
    }

    robot::Command command() noexcept { return drive_.command(); }

  private:
    DriveInput drive_;
};

class Tour {
  public:
    Tour(Mode mode, Panels &ui) : mode_{mode}, started_{SDL_GetTicks()}, entered_{started_} {
        if (mode == Mode::smoke) {
            ui.seed_benchmark();
        }
    }

    bool advance(Panels &ui, SDL_Window *window, const LiveFrame &frame) {
        if (mode_ == Mode::interactive) {
            return false;
        }
        const auto now = SDL_GetTicks();
        if (now - started_ > 55000) {
            std::cerr << "Tour timed out at stage " << stage_ << " (status " << ui.status() << ')';
            if (ui.latest_mission()) {
                std::cerr << ", mission " << mission::label(ui.latest_mission()->state) << " / "
                          << mission::label(ui.latest_mission()->cause);
            }
            std::cerr << '\n';
            return true;
        }
        if (!run_) {
            run_ = frame.run;
        }
        if (mode_ == Mode::reopen) {
            if (stage_ == 0 && !ui.busy() && ui.queue({JobKind::reopen_run})) {
                ++stage_;
            }
            if (stage_ == 1 && !ui.busy() && ui.archive_ready()) {
                reopened_ = frame.archived && frame.run != *run_ && frame.grid.counts().free > 0;
                return true;
            }
            return false;
        }
        if (!frame.archived) {
            continuous_ =
                continuous_ && frame.run == *run_ && frame.telemetry.sample_tick >= last_sample_;
            last_sample_ = frame.telemetry.sample_tick;
        }
        saw_worker_ = saw_worker_ || ui.worker_driving();
        const auto &pose = frame.telemetry.estimated_pose;
        if (!origin_) {
            origin_ = mapping::Point{pose.x.value(), pose.y.value()};
        }
        moved_ = moved_ || (frame.lidar && std::hypot(pose.x.value() - origin_->x_m,
                                                      pose.y.value() - origin_->y_m) >= 0.12);
        if (now - entered_ < (mode_ == Mode::smoke ? 380U : 2000U) || ui.busy()) {
            return false;
        }
        bool ready = true;
        switch (stage_) {
        case 0:
            ui.panel = Panel::live_map;
            ui.request(LiveAction::explore);
            ui.overlays.occupancy = true;
            break;
        case 1:
            if (!moved_) {
                // A transient focus or frame stall can hold the initial tour command.
                if (!frame.exploring()) {
                    ui.request(LiveAction::explore);
                }
                return false;
            }
            ui.camera.mode = CameraMode::follow;
            ui.lidar_dropout = true;
            break;
        case 2:
            saw_dropout_ = frame.map.scan == ScanState::stale && frame.navigation &&
                           frame.navigation->state == navigation::State::waiting &&
                           frame.navigation->reason == navigation::Reason::stale_scan &&
                           frame.telemetry.commanded.linear.value() == 0.0;
            if (!saw_dropout_) {
                return false;
            }
            ui.lidar_dropout = false;
            break;
        case 3:
            saw_recovery_ = frame.map.scan == ScanState::fresh && frame.navigation &&
                            frame.navigation->reason != navigation::Reason::stale_scan;
            if (!saw_recovery_) {
                return false;
            }
            break;
        case 4:
            ui.panel = Panel::find_product;
            ui.camera.mode = CameraMode::orbit;
            ready = ui.queue({JobKind::lookup, "990-01-0001"});
            break;
        case 5:
            ready = ui.has_catalog_result() && ui.queue({JobKind::find, "990-01-0001"});
            break;
        case 6:
            ui.panel = Panel::inventory_audit;
            ui.overlays.antenna = true;
            ready = ui.search_ready && ui.queue({JobKind::audit, "SOFTLINES"});
            break;
        case 7:
            ui.panel = Panel::missions;
            ready = ui.audit_ready && ui.audit_has_reads() && ui.queue({JobKind::scan_mission});
            break;
        case 8:
            ready = completed(ui, mission::Workflow::scan_all) && ui.queue({JobKind::dock_mission});
            break;
        case 9:
            ready = completed(ui, mission::Workflow::dock) &&
                    ui.queue({JobKind::inspect_mission, "TECH"});
            break;
        case 10:
            if (!ui.latest_mission() ||
                ui.latest_mission()->intent.workflow != mission::Workflow::inspect ||
                ui.latest_mission()->state != mission::State::executing) {
                ready = false;
                break;
            }
            ready = ui.queue({JobKind::confirm_inspection, {}, {}, ui.latest_mission()->intent.id});
            break;
        case 11:
            if (!completed(ui, mission::Workflow::inspect)) {
                ready = false;
                break;
            }
            ui.panel = Panel::history;
            break;
        case 12:
            ui.panel = Panel::settings;
            ui.camera.mode = CameraMode::plan;
            resized_ = mode_ != Mode::smoke || SDL_SetWindowSize(window, 1120, 720);
            break;
        case 13:
            ui.panel = Panel::diagnostics;
            ui.camera.mode = CameraMode::orbit;
            ui.developer_truth = true;
            ui.rebuild = true;
            break;
        case 14:
            ui.panel = Panel::overview;
            isolated_ = !truth_visible(ui.panel, ui.developer_truth);
            break;
        case 15:
            ui.panel = Panel::history;
            ready = ui.save();
            break;
        case 16:
            ui.camera.mode = CameraMode::follow;
            ready = ui.saved_ready() && ui.queue({JobKind::reopen_run});
            break;
        case 17:
            reopened_ = ui.archive_ready() && frame.archived && frame.run == *run_ &&
                        frame.grid.counts().free > 0;
            ready = reopened_;
            break;
        default:
            finished_ = true;
            return true;
        }
        if (ready) {
            ++stage_;
            entered_ = now;
        }
        return false;
    }

    void frame(double millis, std::optional<double> scene_ms, const Panels &ui) {
        if (mode_ == Mode::interactive) {
            return;
        }
        if (samples_.size() < 5000) {
            samples_.push_back(millis);
        }
        if (scene_ms && scene_samples_.size() < 5000) {
            scene_samples_.push_back(*scene_ms);
        }
        if (ui.panel == Panel::find_product) {
            max_rows_ = std::max(max_rows_, ui.painted_rows());
        }
    }

    void graphics_rebuilt() noexcept { rebuilt_ = true; }

    bool summary(const Scene &scene) {
        if (mode_ == Mode::interactive) {
            return true;
        }
        if (mode_ == Mode::reopen) {
            std::cout << "{\"savedRunReopenedAfterRestart\":" << std::boolalpha << reopened_
                      << "}\n";
            return reopened_;
        }
        std::ranges::sort(samples_);
        std::ranges::sort(scene_samples_);
        const auto percentile = [](const std::vector<double> &samples, std::size_t rank) {
            return samples.empty() ? 10000.0 : samples[(samples.size() * rank + 99) / 100 - 1];
        };
        const auto p95 = percentile(samples_, 95);
        auto &output = mode_ == Mode::record ? std::cerr : std::cout;
        output << "{\"workflowTour\":" << std::boolalpha << finished_
               << ",\"movingObserved\":" << moved_ << ",\"truthIsolated\":" << isolated_
               << ",\"lidarDropoutHeld\":" << saw_dropout_
               << ",\"continuousRun\":" << (continuous_ && saw_worker_)
               << ",\"savedRunReopened\":" << reopened_ << ",\"lidarRecovered\":" << saw_recovery_
               << ",\"resized\":" << resized_ << ",\"contextRebuilt\":" << rebuilt_
               << ",\"frames\":" << samples_.size() << ",\"maxVisibleCatalogRows\":" << max_rows_
               << ",\"frameP95Ms\":" << p95 << ",\"frameP99Ms\":" << percentile(samples_, 99)
               << ",\"sceneP95Ms\":" << percentile(scene_samples_, 95)
               << ",\"sceneP99Ms\":" << percentile(scene_samples_, 99)
               << ",\"occupancyFrames\":" << scene.occupancy_frames()
               << ",\"occupancyBuilds\":" << scene.occupancy_mesh().builds()
               << ",\"occupancyVerticesGenerated\":" << scene.occupancy_mesh().generated_vertices()
               << "}\n";
        return finished_ && moved_ && isolated_ && saw_dropout_ && saw_recovery_ && continuous_ &&
               saw_worker_ && reopened_ && resized_ && rebuilt_ && samples_.size() >= 30 &&
               max_rows_ > 0 && max_rows_ < 40 && p95 < 50.0;
    }

  private:
    static bool completed(const Panels &ui, mission::Workflow workflow) {
        return ui.mission_ready && ui.latest_mission() &&
               ui.latest_mission()->intent.workflow == workflow &&
               ui.latest_mission()->state == mission::State::completed;
    }

    Mode mode_;
    std::uint64_t started_;
    std::uint64_t entered_;
    unsigned stage_{};
    bool finished_{};
    bool moved_{};
    bool saw_dropout_{};
    bool saw_recovery_{};
    bool continuous_{true};
    bool saw_worker_{};
    bool reopened_{};
    std::optional<RunId> run_;
    std::uint64_t last_sample_{};
    bool resized_{};
    bool rebuilt_{};
    bool isolated_{};
    std::optional<mapping::Point> origin_;
    std::vector<double> samples_;
    std::vector<double> scene_samples_;
    std::size_t max_rows_{};
};

class FleetProbe {
  public:
    explicit FleetProbe(Mode mode)
        : reopen_{mode != Mode::fleet_smoke}, recording_{mode == Mode::dashboard_record} {}

    [[nodiscard]] std::optional<bool> advance(Panels &ui, bool photos_ready) {
        input_.reset();
        auto *fleet = ui.fleet.get();
        if (!fleet || !photos_ready || fleet->history_error()) {
            std::cerr << "Fleet probe could not load robots, saved data or product photos\n";
            return false;
        }
        if (SDL_GetTicks() - started_ > 30000) {
            std::cerr << "Fleet probe timed out in review stage " << static_cast<int>(review_)
                      << ". Robot 1: " << fleet->status(0) << ". Robot 2: " << fleet->status(1)
                      << '\n';
            return false;
        }
        if (!reopen_ && !queued_ && !fleet->busy(0) && !fleet->busy(1)) {
            if (fleet->state(whole_) != ShelfState::pending ||
                fleet->state(waco_) != ShelfState::pending || !fleet->queue(0, whole_) ||
                fleet->queue(0, waco_) || fleet->queue(1, whole_) || !fleet->queue(1, waco_)) {
                std::cerr << "Fleet smoke requires a fresh data directory\n";
                return false;
            }
            queued_ = true;
            ui.panel = Panel::maintenance;
        }
        if (queued_) {
            observe_maintenance(ui, *fleet);
        }
        if (reopen_) {
            return review_dashboard(ui, *fleet);
        }
        if (fleet->state(whole_) != ShelfState::complete ||
            fleet->state(waco_) != ShelfState::complete) {
            return std::nullopt;
        }
        if (ui.panel != Panel::locations) {
            ui.panel = Panel::locations;
            ui.fleet.select(0);
            ui.fleet.select_location(whole_);
            return std::nullopt;
        }
        if (!rendered_) {
            return std::nullopt;
        }
        if (ui.fleet.selected() == 0) {
            ui.fleet.select(1);
            ui.fleet.select_location(waco_);
            return std::nullopt;
        }
        return summarize(*fleet);
    }

    void drawn() noexcept { rendered_ = true; }
    [[nodiscard]] std::optional<ProbePointer> input() const noexcept { return input_; }

  private:
    enum class Review {
        zoom,
        select_shelf,
        open_audit,
        inspect,
        return_to_report,
        reopen_maintenance,
        queue_hold,
        watch_motion,
        wait_for_hold,
        return_to_berth,
        verify_clearance,
        watch_saved_return,
        verify_saved_audit
    };

    bool click(std::optional<DashboardPoint> desired) {
        if (pressed_) {
            input_ = ProbePointer{*pressed_, false};
            pressed_.reset();
            ready_after_ = SDL_GetTicks() + 1100;
            return true;
        }
        if (desired) {
            pressed_ = desired;
            input_ = ProbePointer{*desired, true};
        }
        return false;
    }

    [[nodiscard]] std::optional<bool> review_dashboard(Panels &ui, Fleet &fleet) {
        const auto &layout = ui.fleet.layout();
        const auto viewport = ImGui::GetIO().DisplaySize;
        if (viewport.x != 1280 || viewport.y != 720) {
            return std::nullopt;
        }
        if (layout.horizontal_scroll) {
            std::cerr << "Dashboard horizontal overflow at 1280 x 720 in panel "
                      << static_cast<int>(ui.panel) << " at x=" << layout.overflow_position
                      << " width=" << layout.overflow_width << '\n';
            return false;
        }
        if (recording_ && !pressed_ && SDL_GetTicks() < ready_after_) {
            return std::nullopt;
        }
        switch (review_) {
        case Review::zoom:
            if (click(layout.map_zoom)) {
                review_ = Review::select_shelf;
            }
            break;
        case Review::select_shelf:
            if (layout.shelf_ids && click(layout.second_shelf)) {
                review_ = Review::open_audit;
            }
            break;
        case Review::open_audit:
            if (ui.fleet.selected_location() == waco_ && click(layout.open_audit)) {
                review_ = Review::inspect;
            }
            break;
        case Review::inspect:
            if (pressed_ ||
                (ui.panel == Panel::locations && ui.fleet.selected() == 1 &&
                 !fleet.history(waco_).empty() &&
                 layout.products == fleet.history(waco_).front().report.products.size() &&
                 layout.products != 0)) {
                if (click(layout.maintenance)) {
                    review_ = Review::return_to_report;
                }
            }
            break;
        case Review::return_to_report:
            if (ui.panel == Panel::maintenance && click(layout.location_link)) {
                review_ = Review::reopen_maintenance;
            }
            break;
        case Review::reopen_maintenance:
            if (ui.panel == Panel::locations && ui.fleet.selected() == 1 &&
                ui.fleet.selected_location() == waco_ && layout.products != 0 &&
                click(layout.maintenance)) {
                review_ = Review::queue_hold;
            }
            break;
        case Review::queue_hold:
            if (ui.panel == Panel::maintenance && ui.fleet.selected() == 1) {
                const auto overlays = ui.display_overlays(ui.display_frame());
                if (!overlays.lidar || !overlays.trace) {
                    return false;
                }
                if (fleet.queue(1, waco_)) {
                    review_ = Review::watch_motion;
                }
            }
            break;
        case Review::watch_motion: {
            const auto frame = fleet.frame(1);
            const auto berth = audit::stockroom_berth(RobotId{2}).position;
            const auto &pose = frame.telemetry.estimated_pose;
            const auto travelled =
                std::hypot(pose.x.value() - berth.x / 1000.0, pose.y.value() - berth.y / 1000.0);
            if (travelled > 0.9 && frame.map.scan == ScanState::fresh && frame.lidar) {
                fleet.hold();
                ui.panel = Panel::fleet;
                review_ = Review::wait_for_hold;
            }
            break;
        }
        case Review::wait_for_hold:
            if (fleet.aisle_retained(1) && layout.hold_visible) {
                review_ = Review::return_to_berth;
            }
            break;
        case Review::return_to_berth:
            if (click(layout.return_to_berth)) {
                review_ = Review::verify_clearance;
            }
            break;
        case Review::verify_clearance:
            if (fleet.status(1) == "Aisle released" && !fleet.aisle_retained(1)) {
                if (recording_) {
                    return summarize(fleet);
                }
                previous_audits_ = fleet.history(waco_).size();
                if (fleet.queue(1, waco_)) {
                    review_ = Review::watch_saved_return;
                }
            }
            break;
        case Review::watch_saved_return:
            if (fleet.frame(1).returning_to_berth) {
                SDL_Event lost{};
                lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
                if (!SDL_PushEvent(&lost)) {
                    return false;
                }
                review_ = Review::verify_saved_audit;
            }
            break;
        case Review::verify_saved_audit:
            if (!fleet.busy(1)) {
                const auto history = fleet.history(waco_);
                if (!fleet.aisle_retained(1) || history.size() < 2 ||
                    history.size() != previous_audits_ + 1 ||
                    history.front().audit.recheck_of != history[1].audit.key() ||
                    !operations_directory) {
                    std::cerr << "Saved audit or held aisle was lost after return interruption\n";
                    return false;
                }
                auto opened = mission::Store::open(*operations_directory / "missions.db");
                auto *journal = std::get_if<mission::Store>(&opened);
                if (!journal) {
                    return false;
                }
                auto rows = journal->shelf_history(history.front().audit.location);
                const auto *saved = std::get_if<std::vector<mission::ShelfAudit>>(&rows);
                if (!saved || saved->size() != history.size() ||
                    saved->front().key() != history.front().audit.key() ||
                    saved->front().report != history.front().audit.report) {
                    return false;
                }
                audit_saved_after_hold_ = true;
                return summarize(fleet);
            }
            break;
        }
        return std::nullopt;
    }

    void observe_maintenance(Panels &ui, const Fleet &fleet) {
        concurrent_ = concurrent_ || (fleet.state(whole_) == ShelfState::auditing &&
                                      fleet.state(waco_) == ShelfState::auditing &&
                                      fleet.frame(0).navigation && fleet.frame(1).navigation);
        if (ui.panel != Panel::maintenance) {
            return;
        }
        const auto i = ui.fleet.selected();
        const auto display = ui.display_frame();
        const auto overlays = ui.display_overlays(display);
        if (overlays.lidar && overlays.trace && display.map.scan == ScanState::fresh &&
            display.lidar && display.trace.size() > 2 &&
            std::abs(display.telemetry.measured.linear.value()) > 0.01) {
            maintenance_[i] = true;
            if (!maintenance_[(i + 1) % maintenance_.size()]) {
                ui.fleet.select((i + 1) % maintenance_.size());
            }
        }
    }

    [[nodiscard]] bool summarize(const Fleet &fleet) const {
        if (fleet.history(whole_).empty() || fleet.history(waco_).empty()) {
            return false;
        }
        const auto &soft = fleet.history(whole_).front();
        const auto &tech = fleet.history(waco_).front();
        const auto &products = soft.report.products;
        const auto &tech_products = tech.report.products;
        const bool valid =
            soft.audit.robot == RobotId{1} && tech.audit.robot == RobotId{2} &&
            soft.audit.run != tech.audit.run && products.size() == 2 &&
            products[0].product.id.value == "990-01-0001" && products[0].observed > 0 &&
            products[1].product.id.value == "990-02-0001" && products[1].expected == 2 &&
            products[1].observed == 0 && tech_products.size() == 1 &&
            (tech_products[0].observed == 1 || !tech.report.unresolved.empty()) &&
            fleet.state(Fleet::find("A20J06-W02").value()) == ShelfState::pending &&
            fleet.aisle_summary(1).total == 12 && fleet.aisle_summary(1).complete == 1;
        if (!valid ||
            (!reopen_ && (!concurrent_ || !std::ranges::all_of(maintenance_, std::identity{})))) {
            std::cerr << "Fleet probe did not observe the expected motion and audit evidence\n";
            return false;
        }
        auto &output = recording_ ? std::cerr : std::cout;
        output << "{\"fleetAudits\":true,\"photosLoaded\":true,\"zeroReadProduct\":true,"
               << "\"mixedLocations\":true,\"auditableLocations\":" << Fleet::locations().size()
               << ",\"wacoObserved\":" << tech_products[0].observed
               << ",\"wacoUnresolved\":" << tech.report.unresolved.size() << ','
               << (reopen_ ? "\"reopenedAfterRestart\":true,\"dashboard1280x720\":true,"
                             "\"shelfAuditClicks\":2,\"selectionPreserved\":true,"
                             "\"auditContextRestored\":true,"
                             "\"holdVisible\":true,\"returnedToBerth\":true"
                           : "\"concurrentMotion\":true,\"maintenanceBothRobots\":true");
        if (audit_saved_after_hold_) {
            output << ",\"auditSavedAfterHold\":true,\"focusLossHeldFleet\":true";
        }
        output << "}\n";
        return true;
    }

    bool reopen_;
    bool recording_;
    Uint64 started_{SDL_GetTicks()};
    Uint64 ready_after_{started_ + 1100};
    bool queued_{};
    bool audit_saved_after_hold_{};
    std::size_t previous_audits_{};
    bool concurrent_{};
    bool rendered_{};
    Review review_{Review::zoom};
    std::optional<DashboardPoint> pressed_;
    std::optional<ProbePointer> input_;
    const std::size_t whole_{Fleet::initial_location(0)};
    const std::size_t waco_{Fleet::initial_location(1)};
    std::array<bool, Fleet::robot_count> maintenance_{};
};

class AisleProbe {
  public:
    explicit AisleProbe(bool reverse) : first_{reverse ? 1U : 0U}, second_{1U - first_} {}

    std::optional<bool> advance(Panels &ui) {
        auto *fleet = ui.fleet.get();
        if (SDL_GetTicks() - started_ >= 35000) {
            std::cerr << "Shared aisle probe timed out";
            if (fleet) {
                for (std::size_t index = 0; index < Fleet::robot_count; ++index) {
                    const auto frame = fleet->frame(index);
                    const auto &pose = frame.telemetry.estimated_pose;
                    std::cerr << " [robot " << index + 1 << ": " << fleet->status(index)
                              << ", yielding " << frame.traffic_yield << ", tick "
                              << frame.telemetry.sample_tick << ", estimated position "
                              << pose.x.value() << ", " << pose.y.value();
                    if (frame.navigation) {
                        std::cerr << ", " << describe(frame.navigation->state) << ", "
                                  << describe(frame.navigation->reason);
                    }
                    std::cerr << ']';
                }
            }
            std::cerr << '\n';
            return false;
        }
        if (!fleet) {
            return std::nullopt;
        }
        if (!queued_) {
            if (SDL_GetTicks() - started_ < 1100 || fleet->busy(0) || fleet->busy(1)) {
                return std::nullopt;
            }
            if (!fleet->queue(first_, locations_[first_]) ||
                !fleet->queue(second_, locations_[second_])) {
                std::cerr << "Could not queue both audits in A20\n";
                return false;
            }
            queued_ = true;
            return std::nullopt;
        }
        const auto second = fleet->frame(second_);
        const auto speed = std::abs(second.telemetry.measured.linear.value());
        yielded_ =
            yielded_ || (second.traffic_yield && speed < 0.025 && ui.fleet.layout().hold_visible);
        for (std::size_t index = 0; index < locations_.size(); ++index) {
            if (!fleet->busy(index) && fleet->state(locations_[index]) != ShelfState::complete) {
                std::cerr << "Robot " << index + 1 << ": " << fleet->status(index) << '\n';
                return false;
            }
        }
        if (fleet->state(locations_[first_]) != ShelfState::complete) {
            return std::nullopt;
        }
        proceeded_ = proceeded_ || (!second.traffic_yield && speed > 0.05);
        if (fleet->state(locations_[second_]) != ShelfState::complete) {
            return std::nullopt;
        }
        for (std::size_t index = 0; index < locations_.size(); ++index) {
            const auto history = fleet->history(locations_[index]);
            if (history.empty() || history.front().audit.robot != RobotId{index + 1} ||
                fleet->aisle_retained(index)) {
                return false;
            }
        }
        if (!yielded_ || !proceeded_) {
            std::cerr << "Shared aisle did not show both yielding and resumed motion\n";
            return false;
        }
        std::cout << "{\"sharedFleet\":true,\"yielded\":true,\"bothAuditsCompleted\":true,"
                     "\"firstRobot\":"
                  << first_ + 1 << ",\"secondLocation\":\""
                  << Fleet::locations()[locations_[1]].id.value << "\"}\n";
        return true;
    }

  private:
    const std::size_t first_;
    const std::size_t second_;
    const Uint64 started_{SDL_GetTicks()};
    const std::array<std::size_t, 2> locations_{Fleet::find("A20J05").value(),
                                                Fleet::find("A20J07").value()};
    bool queued_{};
    bool yielded_{};
    bool proceeded_{};
};

class AuditReviewProbe {
  public:
    explicit AuditReviewProbe(Mode mode)
        : reopen_{mode == Mode::fleet_review_reopen}, recording_{mode == Mode::review_record} {}

    std::optional<bool> advance(Panels &ui, SDL_Window *window) {
        input_.reset();
        auto *fleet = ui.fleet.get();
        if (!fleet || fleet->history_error() || SDL_GetTicks() - started_ > 30000) {
            std::cerr << "Audit review probe failed at stage " << static_cast<int>(stage_);
            if (fleet) {
                std::cerr << " (" << fleet->status(0) << ')';
            }
            std::cerr << '\n';
            return false;
        }
        const auto history = fleet->history(location_);
        const auto &layout = ui.fleet.layout();
        if (history.empty() || layout.horizontal_scroll || fleet->review_error(location_)) {
            return false;
        }
        if (recording_ && !pressed_ && SDL_GetTicks() < next_action_) {
            return std::nullopt;
        }
        switch (stage_) {
        case Stage::open:
            ui.fleet.select(0);
            ui.fleet.select_location(location_);
            ui.panel = Panel::locations;
            if (!reopen_) {
                if (history.size() != 1 || !history.front().needs_review()) {
                    return false;
                }
                evidence_ = history.front().audit.report;
                review_count_ = fleet->aisle_summary(0).review;
            }
            stage_ = recording_ ? Stage::filter : Stage::focus_search;
            next_action_ = SDL_GetTicks() + 1050;
            break;
        case Stage::focus_search:
            if (click(layout.location_search)) {
                stage_ = Stage::escape_search;
            }
            break;
        case Stage::escape_search:
            if (ImGui::GetIO().WantTextInput) {
                SDL_Event escape{};
                escape.type = SDL_EVENT_KEY_DOWN;
                escape.key.windowID = SDL_GetWindowID(window);
                escape.key.key = SDLK_ESCAPE;
                escape.key.scancode = SDL_SCANCODE_ESCAPE;
                escape.key.down = true;
                if (!SDL_PushEvent(&escape)) {
                    return false;
                }
                escape.type = SDL_EVENT_KEY_UP;
                escape.key.down = false;
                if (!SDL_PushEvent(&escape)) {
                    return false;
                }
                stage_ = Stage::search_closed;
            }
            break;
        case Stage::search_closed:
            if (!ImGui::GetIO().WantTextInput && ui.panel == Panel::locations &&
                ui.fleet.selected_location() == location_) {
                escape_kept_session_ = true;
                stage_ = Stage::filter;
            }
            break;
        case Stage::filter:
            if (click(layout.review_filter)) {
                stage_ = reopen_ ? Stage::recheck : Stage::mark;
            }
            break;
        case Stage::mark:
            if (layout.filtered_reviews != 0 && click(layout.mark_reviewed)) {
                initial_filter_count_ = layout.filtered_reviews;
                stage_ = Stage::queue;
            }
            break;
        case Stage::queue:
            if (history.front().audit.reviewed_ms) {
                if (history.front().audit.report != evidence_ ||
                    fleet->aisle_summary(0).review + 1 != review_count_) {
                    return false;
                }
                if (layout.filtered_reviews + 1 == initial_filter_count_ &&
                    SDL_GetTicks() - started_ >= 1100 && click(layout.recheck)) {
                    stage_ = Stage::recheck;
                }
            }
            break;
        case Stage::recheck:
            if (history.size() == 2) {
                const auto &latest = history[0];
                const auto &source = history[1];
                if (latest.audit.recheck_of != source.audit.key() || !source.audit.reviewed_ms ||
                    !latest.needs_review() || fleet->aisle_summary(0).review == 0 ||
                    (!reopen_ && source.audit.report != evidence_)) {
                    return false;
                }
                if (layout.filtered_reviews != 0 && click(layout.compare)) {
                    stage_ = Stage::comparison;
                }
            }
            break;
        case Stage::comparison:
            if (layout.compared && layout.products >= 2 && ImGui::GetIO().DisplaySize.x == 1280 &&
                ImGui::GetIO().DisplaySize.y == 720) {
                auto &output = recording_ ? std::cerr : std::cout;
                output << "{\"reviewed\":true,\"recheckLinked\":true,\"reviewFilter\":true,"
                          "\"comparisonVisible\":true,\"escapeKeptSession\":"
                       << (escape_kept_session_ ? "true" : "false")
                       << ",\"reopened\":" << (reopen_ ? "true" : "false") << "}\n";
                return true;
            }
            break;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ProbePointer> input() const noexcept { return input_; }

  private:
    enum class Stage {
        open,
        focus_search,
        escape_search,
        search_closed,
        filter,
        mark,
        queue,
        recheck,
        comparison
    };

    bool click(std::optional<DashboardPoint> point) {
        if (pressed_) {
            input_ = ProbePointer{*pressed_, false};
            pressed_.reset();
            next_action_ = SDL_GetTicks() + 1250;
            return true;
        }
        if (point) {
            pressed_ = point;
            input_ = ProbePointer{*point, true};
        }
        return false;
    }

    const bool reopen_;
    const bool recording_;
    const Uint64 started_{SDL_GetTicks()};
    Uint64 next_action_{};
    const std::size_t location_{Fleet::initial_location(0)};
    Stage stage_{Stage::open};
    std::size_t review_count_{};
    std::size_t initial_filter_count_{};
    bool escape_kept_session_{};
    std::string evidence_;
    std::optional<DashboardPoint> pressed_;
    std::optional<ProbePointer> input_;
};

app::Status loop(SDL_Window *window, std::unique_ptr<LiveEngine> &engine, Mode mode,
                 bool demo_scene, float test_scale) {
    Graphics graphics{window, test_scale};
    if (!graphics.open()) {
        return Error::graphics_context;
    }
    Scene scene{engine->layout()};
    std::optional<Scene> fleet_scene;
    Panels ui{engine, demo_scene, mode != Mode::smoke, operations_directory};
    const bool verification = fleet_probe(mode);
    Tour tour{verification ? Mode::interactive : mode, ui};
    if (verification || (mode == Mode::interactive && demo_scene)) {
        ui.fleet.open(operations_directory);
        ui.panel = Panel::fleet;
        if (mode != Mode::fleet_smoke && verification) {
            ui.fleet.select(1);
        }
    }
    Controls controls;
    std::optional<Recorder> recorder;
    if (captures_video(mode)) {
        recorder.emplace();
    }
    auto *capture = recorder ? &*recorder : nullptr;
    auto last_step = SDL_GetTicks();
    bool pending_stop = false;
    std::optional<FleetProbe> probe;
    std::optional<AisleProbe> aisle_probe;
    std::optional<AuditReviewProbe> review;
    const auto started = SDL_GetTicks();
    if (mode == Mode::fleet_traffic || mode == Mode::fleet_traffic_reverse) {
        aisle_probe.emplace(mode == Mode::fleet_traffic_reverse);
    } else if (review_probe(mode)) {
        review.emplace(mode);
    } else if (verification && mode != Mode::fleet_data_fault &&
               mode != Mode::fleet_profile_report) {
        probe.emplace(mode);
    }
    const auto hold = [&] {
        controls.disarm();
        ui.hold_fleet();
        if (engine) {
            engine->finish_job(false);
        } else {
            ui.interrupt();
        }
    };
    while (true) {
        const auto frame_started = desktop_metrics.start();
        const auto before = SDL_GetPerformanceCounter();
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            graphics.event(event);
            if (event.type == SDL_EVENT_RENDER_DEVICE_RESET) {
                ui.rebuild = true;
            }
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE &&
                 !event.key.repeat && !ImGui::GetIO().WantTextInput)) {
                hold();
                return std::monostate{};
            }
            controls.event(event, ui, mode == Mode::interactive);
        }
        ui.poll();
        if (pending_stop && engine) {
            engine->finish_job(true);
            pending_stop = false;
        }
        if (tour.advance(ui, window, ui.frame())) {
            hold();
            return tour.summary(scene) ? app::Status{std::monostate{}}
                                       : app::Status{Error::graphics_present};
        }
        const auto now = SDL_GetTicks();
        auto due = (now - last_step) / robot::step_ms;
        bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        const bool minimized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
        focused = !minimized && (focused || mode != Mode::interactive);
        if (!focused || ImGui::GetIO().WantCaptureKeyboard) {
            controls.disarm();
        }
        if (due > 5) {
            due = 1;
            last_step = now;
            controls.disarm();
            ui.hold_fleet();
            ui.request(LiveAction::pause);
        } else {
            last_step += due * robot::step_ms;
        }
        if (ui.action == LiveAction::emergency_stop) {
            ui.hold_fleet();
        }
        ui.tick_fleet(focused);
        if (mode == Mode::fleet_profile_report) {
            ui.fleet.select_location(Fleet::initial_location(0));
            ui.panel = Panel::locations;
            if (SDL_GetTicks() - started >= 1600) {
                if (!ui.fleet.get() || ui.fleet.get()->history_error() ||
                    ui.fleet.layout().products < 20 || ui.fleet.layout().horizontal_scroll) {
                    return Error::graphics_present;
                }
                const auto size = ImGui::GetIO().DisplaySize;
                std::cout << "{\"productsVisible\":" << ui.fleet.layout().products
                          << ",\"width\":" << size.x << ",\"height\":" << size.y
                          << ",\"fontScale\":" << ImGui::GetStyle().FontScaleDpi << "}\n";
                return std::monostate{};
            }
        }
        if (mode == Mode::fleet_data_fault) {
            const auto *fleet = ui.fleet.get();
            if (!fleet || !fleet->history_error() || SDL_GetTicks() - started > 10000) {
                return Error::graphics_present;
            }
            if (ui.fleet.layout().history_issue_visible && fleet->motion_available() &&
                fleet->frame(0).telemetry.sample_tick != 0) {
                if (ui.fleet.get()->queue(0, Fleet::initial_location(0))) {
                    return Error::graphics_present;
                }
                std::cout << "{\"storageIssueVisible\":true,\"writesBlocked\":true,"
                             "\"simulationAvailable\":true}\n";
                return std::monostate{};
            }
        }
        if (fleet_panel(ui.panel)) {
            controls.disarm();
        }
        if (probe) {
            if (const auto result = probe->advance(ui, graphics.photos_ready())) {
                hold();
                return *result ? app::Status{std::monostate{}}
                               : app::Status{Error::graphics_present};
            }
        }
        if (aisle_probe) {
            if (const auto result = aisle_probe->advance(ui)) {
                hold();
                return *result ? app::Status{std::monostate{}}
                               : app::Status{Error::graphics_present};
            }
        }
        if (review) {
            if (const auto result = review->advance(ui, window)) {
                hold();
                return *result ? app::Status{std::monostate{}}
                               : app::Status{Error::graphics_present};
            }
        }
        if (!focused || ui.action == LiveAction::pause || ui.action == LiveAction::emergency_stop) {
            ui.cancel_pending();
        } else if (!controls.armed()) {
            ui.dispatch();
        }
        if (engine && !ui.reviewing()) {
            for (std::uint64_t index = 0; index < due; ++index) {
                engine->step(controls.command(), std::exchange(ui.action, LiveAction::none),
                             focused, ui.lidar_dropout);
            }
        } else {
            const auto action = std::exchange(ui.action, LiveAction::none);
            const auto command = controls.command();
            const bool emergency = action == LiveAction::emergency_stop || command.emergency_stop;
            controls.disarm();
            if (emergency && engine) {
                engine->finish_job(true);
            } else if (!engine && (!focused || emergency || action == LiveAction::pause)) {
                pending_stop = pending_stop || emergency;
                ui.interrupt(emergency);
            }
        }
        if (std::exchange(ui.rebuild, false)) {
            controls.disarm();
            hold();
            if (!graphics.rebuild()) {
                return Error::graphics_context;
            }
            tour.graphics_rebuilt();
        }
        std::optional<double> scene_ms;
        if (!fleet_scene && ui.fleet.get()) {
            fleet_scene.emplace(ui.fleet.get()->layout());
        }
        const auto pointer = review ? review->input() : (probe ? probe->input() : std::nullopt);
        if (!minimized && !graphics.draw(scene, engine.get(), ui, focused, demo_scene, capture,
                                         mode == Mode::interactive ? nullptr : &scene_ms, pointer,
                                         fleet_scene ? &*fleet_scene : nullptr)) {
            hold();
            if (capture || !graphics.rebuild() ||
                !graphics.draw(scene, engine.get(), ui, false, demo_scene, nullptr, nullptr, {},
                               fleet_scene ? &*fleet_scene : nullptr)) {
                return Error::graphics_present;
            }
        }
        if (probe && !minimized && ui.panel == Panel::locations) {
            probe->drawn();
        }
        const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - before) * 1000.0 /
                               static_cast<double>(SDL_GetPerformanceFrequency());
        tour.frame(elapsed, scene_ms, ui);
        if (desktop_metrics.enabled && !minimized && fleet_panel(ui.panel)) {
            desktop_metrics.record(ui.panel == Panel::fleet       ? Metric::fleet_frame
                                   : ui.panel == Panel::locations ? Metric::audit_frame
                                                                  : Metric::maintenance_frame,
                                   frame_started);
            if (pointer && !pointer->down) {
                desktop_metrics.record(Metric::input, frame_started);
            }
        }
        SDL_Delay(minimized ? 30U : 12U);
    }
}

app::Status desktop(const app::Options &options, Mode mode) {
    const auto *profile = SDL_getenv("ARGUS_PROFILE_DESKTOP");
    desktop_metrics.enabled =
        mode != Mode::interactive && profile && std::string_view{profile} == "1";
    if (options.warehouse_source == app::WarehouseSource::export_fixture) {
        return app::export_warehouse(options.fixture);
    }
    auto chosen = options;
    if (mode != Mode::interactive || chosen.warehouse_source == app::WarehouseSource::none) {
        chosen.warehouse_source = app::WarehouseSource::fixture;
        chosen.fixture = "rfid-audit";
    }
    auto loaded = app::open_warehouse(chosen);
    if (const auto *error = std::get_if<simulation::ScenarioError>(&loaded)) {
        return *error;
    }
    const bool demo_scene =
        chosen.warehouse_source == app::WarehouseSource::fixture && chosen.fixture == "rfid-audit";
    auto live = LiveEngine::create(std::get<simulation::Warehouse>(std::move(loaded)), options.tick,
                                   demo_scene);
    if (const auto *error = std::get_if<app::Failure>(&live)) {
        return *error;
    }
    auto engine = std::make_unique<LiveEngine>(std::get<LiveEngine>(std::move(live)));
    for (std::uint32_t step = 0; step < std::max(options.drive_steps, options.sensor_steps);
         ++step) {
        engine->step(app::probe_command, LiveAction::none, true);
    }
    struct VideoSession {
        ~VideoSession() { SDL_Quit(); }
    } video;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return Error::video_initialization;
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    const auto flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    const bool recording = captures_video(mode);
    if (recording && !app::use_binary_mode(stdout)) {
        return Error::output_failure;
    }
    const auto *layout_check = SDL_getenv("ARGUS_CHECK_LARGE_LAYOUT");
    const bool large_layout =
        mode == Mode::fleet_profile_report && layout_check && std::string_view{layout_check} == "1";
    const bool compact = recording || mode == Mode::fleet_reopen || review_probe(mode) ||
                         desktop_metrics.enabled || mode == Mode::fleet_profile_report;
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
        SDL_CreateWindow(title,
                         large_layout ? 1600
                         : compact    ? Recorder::width
                                      : 1392,
                         large_layout ? 900
                         : compact    ? Recorder::height
                                      : 860,
                         recording ? flags : flags | SDL_WINDOW_HIGH_PIXEL_DENSITY),
        SDL_DestroyWindow};
    if (!window) {
        return Error::window_creation;
    }
    SDL_SetWindowMinimumSize(window.get(), 900, 610);
    auto result = loop(window.get(), engine, mode, demo_scene, large_layout ? 1.25F : 0.0F);
    if (desktop_metrics.enabled) {
        desktop_metrics.write(std::cerr);
    }
    return result;
}

app::Status interactive(const app::Options &options) { return desktop(options, Mode::interactive); }
app::Status smoke(const app::Options &options) { return desktop(options, Mode::smoke); }
app::Status tour(const app::Options &options) { return desktop(options, Mode::tour); }
app::Status record(const app::Options &options) { return desktop(options, Mode::record); }
app::Status reopen(const app::Options &options) { return desktop(options, Mode::reopen); }
app::Status verify_fleet(const app::Options &options) {
    return desktop(options, Mode::fleet_smoke);
}
app::Status reopen_fleet(const app::Options &options) {
    return desktop(options, Mode::fleet_reopen);
}
app::Status verify_traffic(const app::Options &options) {
    return desktop(options, Mode::fleet_traffic);
}
app::Status verify_reverse_traffic(const app::Options &options) {
    return desktop(options, Mode::fleet_traffic_reverse);
}
app::Status verify_data_fault(const app::Options &options) {
    return desktop(options, Mode::fleet_data_fault);
}
app::Status profile_report(const app::Options &options) {
    return desktop(options, Mode::fleet_profile_report);
}
app::Status verify_review(const app::Options &options) {
    return desktop(options, Mode::fleet_review);
}
app::Status reopen_review(const app::Options &options) {
    return desktop(options, Mode::fleet_review_reopen);
}
app::Status record_review(const app::Options &options) {
    return desktop(options, Mode::review_record);
}
app::Status record_dashboard(const app::Options &options) {
    return desktop(options, Mode::dashboard_record);
}

} // namespace

int main(int argc, char **argv) {
    const auto option = argc > 1 ? std::string_view{argv[1]} : std::string_view{};
    if (argc == 3 &&
        (option == "--operations-data" || option == "--ui-smoke" || option == "--ui-reopen" ||
         option == "--ui-fleet-smoke" || option == "--ui-fleet-reopen" ||
         option == "--ui-fleet-traffic" || option == "--ui-fleet-traffic-reverse" ||
         option == "--ui-fleet-data-fault" || option == "--ui-fleet-profile-report" ||
         option == "--ui-fleet-review" || option == "--ui-fleet-review-reopen" ||
         option == "--ui-dashboard-record" || option == "--ui-fleet-review-record")) {
        const auto path = std::string_view{argv[2]};
        if (path.empty() || path.size() > 4096) {
            std::cerr << "argus-desktop: Choose an existing operations data directory.\n";
            return 2;
        }
        try {
            auto directory = std::filesystem::absolute(std::filesystem::path{path});
            if (!std::filesystem::is_directory(directory)) {
                std::cerr << "argus-desktop: Choose an existing operations data directory.\n";
                return 2;
            }
            operations_directory = std::move(directory);
        } catch (const std::exception &) {
            std::cerr << "argus-desktop: Could not access the operations data directory.\n";
            return 2;
        }
        const auto entry = option == "--ui-smoke"                   ? smoke
                           : option == "--ui-reopen"                ? reopen
                           : option == "--ui-fleet-smoke"           ? verify_fleet
                           : option == "--ui-fleet-reopen"          ? reopen_fleet
                           : option == "--ui-fleet-traffic"         ? verify_traffic
                           : option == "--ui-fleet-traffic-reverse" ? verify_reverse_traffic
                           : option == "--ui-fleet-data-fault"      ? verify_data_fault
                           : option == "--ui-fleet-profile-report"  ? profile_report
                           : option == "--ui-fleet-review"          ? verify_review
                           : option == "--ui-fleet-review-reopen"   ? reopen_review
                           : option == "--ui-dashboard-record"      ? record_dashboard
                           : option == "--ui-fleet-review-record"   ? record_review
                                                                    : interactive;
        return app::run_application(1, argv, "argus-desktop", entry);
    }
    if (argc == 2 && std::string_view{argv[1]} == "--ui-smoke") {
        return app::run_application(1, argv, "argus-desktop", smoke);
    }
    if (argc == 2 && std::string_view{argv[1]} == "--ui-tour") {
        return app::run_application(1, argv, "argus-desktop", tour);
    }
    if (argc == 2 && std::string_view{argv[1]} == "--ui-record") {
        return app::run_application(1, argv, "argus-desktop", record);
    }
    return app::run_application(argc, argv, "argus-desktop", interactive);
}
