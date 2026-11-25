#include "application.hpp"
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
#include <iostream>
#include <memory>
#include <utility>

namespace {

using namespace argus;
using namespace argus::desktop;
using app::Error;

enum class Mode { interactive, smoke, tour, record, reopen };
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
    explicit Graphics(SDL_Window *window) : window_{window} {}
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
        platform_ = ImGui_ImplSDL3_InitForOpenGL(window_, context_);
        renderer_ = platform_ && ImGui_ImplOpenGL2_Init();
        return renderer_;
    }

    bool rebuild() {
        close();
        return open();
    }

    void event(const SDL_Event &event) const { ImGui_ImplSDL3_ProcessEvent(&event); }

    bool draw(Scene &scene, const LiveEngine *engine, Panels &ui, bool focused, bool demo_scene,
              Recorder *recorder) const {
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
        ImGui::NewFrame();
        const auto frame = ui.frame();
        const auto canvas = ui.draw(frame, focused);
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
        const auto regions =
            demo_scene && !frame.archived ? ui.regions() : std::span<const EvidenceRegion>{};
        const auto overlays =
            frame.archived ? Overlays{.lidar = false, .occupancy = true} : ui.overlays;
        scene.draw(viewport, frame, ui.camera, overlays, regions, developer ? &*developer : nullptr,
                   frame.archived || ui.map_only);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        if (recorder && !recorder->capture(width, height)) {
            return false;
        }
        return SDL_GL_SwapWindow(window_);
    }

  private:
    void close() {
        if (context_) {
            SDL_GL_MakeCurrent(window_, context_);
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
    SDL_GLContext context_{};
    ImGuiContext *imgui_{};
    bool platform_{};
    bool renderer_{};
};

class Controls {
  public:
    void disarm() noexcept { drive_.disarm(); }
    [[nodiscard]] bool armed() const noexcept { return drive_.armed(); }

    void event(const SDL_Event &event, Panels &ui) {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED) {
            disarm();
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

    void frame(double millis, const Panels &ui) {
        if (mode_ == Mode::interactive) {
            return;
        }
        if (samples_.size() < 5000) {
            samples_.push_back(millis);
        }
        if (ui.panel == Panel::find_product) {
            max_rows_ = std::max(max_rows_, ui.painted_rows());
        }
    }

    void graphics_rebuilt() noexcept { rebuilt_ = true; }

    bool summary() {
        if (mode_ == Mode::interactive) {
            return true;
        }
        if (mode_ == Mode::reopen) {
            std::cout << "{\"savedRunReopenedAfterRestart\":" << std::boolalpha << reopened_
                      << "}\n";
            return reopened_;
        }
        std::ranges::sort(samples_);
        const auto p95 = samples_.empty() ? 10000.0 : samples_[samples_.size() * 95 / 100];
        auto &output = mode_ == Mode::record ? std::cerr : std::cout;
        output << "{\"workflowTour\":" << std::boolalpha << finished_
               << ",\"movingObserved\":" << moved_ << ",\"truthIsolated\":" << isolated_
               << ",\"lidarDropoutHeld\":" << saw_dropout_
               << ",\"continuousRun\":" << (continuous_ && saw_worker_)
               << ",\"savedRunReopened\":" << reopened_ << ",\"lidarRecovered\":" << saw_recovery_
               << ",\"resized\":" << resized_ << ",\"contextRebuilt\":" << rebuilt_
               << ",\"frames\":" << samples_.size() << ",\"maxVisibleCatalogRows\":" << max_rows_
               << ",\"frameP95Ms\":" << p95 << "}\n";
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
    std::size_t max_rows_{};
};

app::Status loop(SDL_Window *window, std::unique_ptr<LiveEngine> &engine, Mode mode,
                 bool demo_scene) {
    Graphics graphics{window};
    if (!graphics.open()) {
        return Error::graphics_context;
    }
    Scene scene{engine->layout()};
    Panels ui{engine, demo_scene, mode != Mode::smoke, operations_directory};
    Tour tour{mode, ui};
    Controls controls;
    std::optional<Recorder> recorder;
    if (mode == Mode::record) {
        recorder.emplace();
    }
    auto *capture = recorder ? &*recorder : nullptr;
    auto last_step = SDL_GetTicks();
    bool pending_stop = false;
    const auto hold = [&] {
        controls.disarm();
        if (engine) {
            engine->finish_job(false);
        } else {
            ui.interrupt();
        }
    };
    while (true) {
        const auto before = SDL_GetPerformanceCounter();
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            graphics.event(event);
            if (event.type == SDL_EVENT_RENDER_DEVICE_RESET) {
                ui.rebuild = true;
            }
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)) {
                hold();
                return std::monostate{};
            }
            controls.event(event, ui);
        }
        ui.poll();
        if (pending_stop && engine) {
            engine->finish_job(true);
            pending_stop = false;
        }
        if (tour.advance(ui, window, ui.frame())) {
            hold();
            return tour.summary() ? app::Status{std::monostate{}}
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
            ui.request(LiveAction::pause);
        } else {
            last_step += due * robot::step_ms;
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
        if (!minimized && !graphics.draw(scene, engine.get(), ui, focused, demo_scene, capture)) {
            hold();
            if (capture || !graphics.rebuild() ||
                !graphics.draw(scene, engine.get(), ui, false, demo_scene, nullptr)) {
                return Error::graphics_present;
            }
        }
        const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - before) * 1000.0 /
                               static_cast<double>(SDL_GetPerformanceFrequency());
        tour.frame(elapsed, ui);
        SDL_Delay(minimized ? 30U : 12U);
    }
}

app::Status desktop(const app::Options &options, Mode mode) {
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
    const bool recording = mode == Mode::record;
    if (recording && !app::use_binary_mode(stdout)) {
        return Error::output_failure;
    }
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
        SDL_CreateWindow(title, recording ? Recorder::width : 1392,
                         recording ? Recorder::height : 860,
                         recording ? flags : flags | SDL_WINDOW_HIGH_PIXEL_DENSITY),
        SDL_DestroyWindow};
    if (!window) {
        return Error::window_creation;
    }
    SDL_SetWindowMinimumSize(window.get(), 900, 610);
    return loop(window.get(), engine, mode, demo_scene);
}

app::Status interactive(const app::Options &options) { return desktop(options, Mode::interactive); }
app::Status smoke(const app::Options &options) { return desktop(options, Mode::smoke); }
app::Status tour(const app::Options &options) { return desktop(options, Mode::tour); }
app::Status record(const app::Options &options) { return desktop(options, Mode::record); }
app::Status reopen(const app::Options &options) { return desktop(options, Mode::reopen); }

} // namespace

int main(int argc, char **argv) {
    const auto option = argc > 1 ? std::string_view{argv[1]} : std::string_view{};
    if (argc == 3 &&
        (option == "--operations-data" || option == "--ui-smoke" || option == "--ui-reopen")) {
        const auto path = std::string_view{argv[2]};
        if (path.empty() || path.size() > 4096) {
            return 2;
        }
        try {
            operations_directory = std::filesystem::path{path};
        } catch (const std::exception &) {
            return 2;
        }
        const auto entry = option == "--ui-smoke"    ? smoke
                           : option == "--ui-reopen" ? reopen
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
