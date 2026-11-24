#include "application.hpp"
#include "map_run.hpp"
#include "robot_run.hpp"
#include "warehouse_io.hpp"
#include "warehouse_view.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using argus::app::Error;
using argus::robot::Command;
using argus::robot::Telemetry;
using argus::sensors::LidarScan;
using argus::simulation::RobotSimulation;
using argus::simulation::SensorRig;

constexpr std::string_view preview_title = "ARGUS Robot Preview";

class VideoSession {
  public:
    VideoSession() = default;
    ~VideoSession() { SDL_Quit(); }

    VideoSession(const VideoSession &) = delete;
    VideoSession &operator=(const VideoSession &) = delete;
};

class Controls {
  public:
    bool on_event(const SDL_Event &event) noexcept {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED) {
            disarm();
            return false;
        }
        if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
            return false;
        }
        const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
        const bool fresh = pressed && !event.key.repeat;
        switch (event.key.scancode) {
        case SDL_SCANCODE_SPACE:
            if (!pressed || fresh) {
                deadman_ = pressed;
            }
            break;
        case SDL_SCANCODE_UP:
            forward_ = pressed;
            break;
        case SDL_SCANCODE_DOWN:
            reverse_ = pressed;
            break;
        case SDL_SCANCODE_LEFT:
            left_ = pressed;
            break;
        case SDL_SCANCODE_RIGHT:
            right_ = pressed;
            break;
        case SDL_SCANCODE_X:
            if (fresh) {
                emergency_ = true;
                disarm();
            }
            break;
        case SDL_SCANCODE_R:
            reset_stop_ = reset_stop_ || (fresh && !deadman_);
            break;
        case SDL_SCANCODE_HOME:
            restart_ = restart_ || (fresh && !deadman_);
            break;
        case SDL_SCANCODE_F3:
            if (fresh) {
                truth_ = !truth_;
                return true;
            }
            break;
        case SDL_SCANCODE_F4:
            if (fresh) {
                lidar_ = !lidar_;
                return true;
            }
            break;
        case SDL_SCANCODE_F5:
            if (fresh) {
                map_ = !map_;
                return true;
            }
            break;
        default:
            break;
        }
        return false;
    }

    void disarm() noexcept {
        deadman_ = false;
        forward_ = false;
        reverse_ = false;
        left_ = false;
        right_ = false;
    }

    Command command() noexcept {
        constexpr double manual_linear = 1.2;
        constexpr double manual_angular = 1.6;
        const auto linear =
            deadman_ ? (static_cast<int>(forward_) - static_cast<int>(reverse_)) * manual_linear
                     : 0.0;
        const auto angular =
            deadman_ ? (static_cast<int>(left_) - static_cast<int>(right_)) * manual_angular : 0.0;
        return {.target = {argus::MetersPerSecond{linear}, argus::robot::RadiansPerSecond{angular}},
                .deadman = deadman_,
                .emergency_stop = std::exchange(emergency_, false),
                .reset_stop = std::exchange(reset_stop_, false)};
    }

    [[nodiscard]] bool show_truth() const noexcept { return truth_; }
    [[nodiscard]] bool show_lidar() const noexcept { return lidar_; }
    [[nodiscard]] bool show_map() const noexcept { return map_; }
    [[nodiscard]] bool restart_requested() noexcept {
        return std::exchange(restart_, false) && !emergency_;
    }

  private:
    bool deadman_{};
    bool forward_{};
    bool reverse_{};
    bool left_{};
    bool right_{};
    bool emergency_{};
    bool reset_stop_{};
    bool restart_{};
    bool truth_{};
    bool lidar_{};
    bool map_{};
};

void capture_preview_sensors(SensorRig &rig, const RobotSimulation &robot,
                             std::optional<LidarScan> &latest_lidar,
                             argus::mapping::Mapper &mapper) {
    rig.capture(robot);
    while (auto sample = rig.pop()) {
        mapper.ingest(*sample);
        if (auto *scan = std::get_if<LidarScan>(&*sample)) {
            latest_lidar = std::move(*scan);
        }
    }
    if (!robot.telemetry().sensors_powered) {
        latest_lidar.reset();
    }
}

std::string window_title(const Telemetry &read, const Controls &controls,
                         const argus::mapping::Mapper &mapper) {
    std::string title{preview_title};
    title += " | Battery " + std::to_string(read.energy_uj * 100U / read.capacity_uj) + "%";
    if (read.emergency_latched) {
        title += " | STOP LATCHED";
    } else if (read.world_held) {
        title += " | Actor update held";
    } else if (read.charging) {
        title += " | Charging";
    } else if (read.depleted) {
        title += " | Battery depleted";
    } else if (read.collision_blocked) {
        title += " | Collision blocked";
    } else if (read.low_battery) {
        title += " | Low battery";
    } else {
        title += " | Space + arrows to drive";
    }
    if (controls.show_map()) {
        title += " | Occupancy mapping + odometry";
        if (mapper.estimate() && !mapper.estimate()->reliable) {
            title += " | Mapping suspended";
        }
        return title;
    }
    if (controls.show_truth()) {
        title += " | TRUTH overlay";
    }
    if (controls.show_lidar()) {
        title += " | LiDAR returns";
    }
    return title;
}

bool present(SDL_Window *window, const argus::simulation::Warehouse &warehouse,
             const RobotSimulation &robot, const Controls &controls,
             const std::optional<LidarScan> &latest_lidar, const argus::mapping::Mapper &mapper,
             std::string &last_title) {
    const auto read = robot.telemetry();
    const auto title = window_title(read, controls, mapper);
    if (title != last_title) {
        if (!SDL_SetWindowTitle(window, title.c_str())) {
            return false;
        }
        last_title = title;
    }
    int width{};
    int height{};
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return true;
    }
    if (controls.show_map()) {
        argus::desktop::draw_occupancy(mapper, width, height);
        return SDL_GL_SwapWindow(window);
    }
    std::optional<argus::robot::Pose> truth;
    if (controls.show_truth()) {
        truth = robot.debug_truth().pose;
    }
    const auto *lidar = controls.show_lidar() && latest_lidar ? &*latest_lidar : nullptr;
    argus::desktop::draw_warehouse(warehouse.snapshot(read.world_tick), read.estimated_pose, truth,
                                   lidar, width, height);
    return SDL_GL_SwapWindow(window);
}

argus::app::Status run_desktop(const argus::app::Options &options) {
    using argus::app::WarehouseSource;
    using argus::simulation::RobotError;
    using argus::simulation::ScenarioError;
    using argus::simulation::SensorError;
    using argus::simulation::Warehouse;
    if (options.warehouse_source == WarehouseSource::export_fixture) {
        return argus::app::export_warehouse(options.fixture);
    }
    const auto loaded = argus::app::open_warehouse(options);
    if (const auto *error = std::get_if<ScenarioError>(&loaded)) {
        return *error;
    }
    const auto &warehouse = std::get<Warehouse>(loaded);
    const auto scene = warehouse.snapshot(options.tick);
    auto map_result = argus::mapping::Mapper::create(argus::app::map_spec(scene));
    if (const auto *error = std::get_if<argus::mapping::MapError>(&map_result)) {
        return *error;
    }
    auto &mapper = std::get<argus::mapping::Mapper>(map_result);
    const auto created = RobotSimulation::create(warehouse, options.tick, {}, {},
                                                 argus::simulation::warehouse_encoder_effects);
    if (const auto *error = std::get_if<RobotError>(&created)) {
        return *error;
    }
    const auto &initial = std::get<RobotSimulation>(created);
    auto robot = initial;
    auto sensor_setup = SensorRig::create(scene.seed);
    if (const auto *error = std::get_if<SensorError>(&sensor_setup)) {
        return *error;
    }
    auto &rig = std::get<SensorRig>(sensor_setup);
    std::optional<LidarScan> latest_lidar;
    const auto probe_steps = std::max(options.drive_steps, options.sensor_steps);
    for (std::uint32_t step = 0; step < probe_steps; ++step) {
        robot.step(argus::app::probe_command);
        capture_preview_sensors(rig, robot, latest_lidar, mapper);
    }
    const VideoSession video;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return Error::video_initialization;
    }

    constexpr int initial_width = 960;
    constexpr int initial_height = 600;
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
        SDL_CreateWindow(preview_title.data(), initial_width, initial_height,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE),
        SDL_DestroyWindow};
    if (!window) {
        return Error::window_creation;
    }

    const std::unique_ptr<std::remove_pointer_t<SDL_GLContext>, decltype(&SDL_GL_DestroyContext)>
        context{SDL_GL_CreateContext(window.get()), SDL_GL_DestroyContext};
    if (!context) {
        return Error::graphics_context;
    }
    Controls controls;
    std::string title;
    if (!present(window.get(), warehouse, robot, controls, latest_lidar, mapper, title)) {
        return Error::graphics_present;
    }

    constexpr auto tick_ms = argus::robot::step_ms;
    constexpr std::uint64_t max_catchup_steps = 5;
    auto last_step = SDL_GetTicks();
    for (;;) {
        const auto age = SDL_GetTicks() - last_step;
        const auto wait = age < tick_ms ? tick_ms - age : 0;
        SDL_Event event{};
        SDL_ClearError();
        const bool got_event = SDL_WaitEventTimeout(&event, static_cast<int>(wait));
        if (!got_event && *SDL_GetError() != '\0') {
            return Error::window_events;
        }
        if (got_event &&
            (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
             (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))) {
            return std::monostate{};
        }
        const bool control_refresh = got_event && controls.on_event(event);
        const bool restarted = controls.restart_requested() && !robot.telemetry().emergency_latched;
        if (restarted) {
            robot = initial;
            rig.reset();
            mapper.reset();
            latest_lidar.reset();
            controls.disarm();
        }

        const auto now = SDL_GetTicks();
        const auto due = (now - last_step) / tick_ms;
        if (!(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS)) {
            controls.disarm();
        }
        if (due > max_catchup_steps) {
            controls.disarm();
            last_step = now;
        } else {
            last_step += due * tick_ms;
        }
        for (std::uint64_t step = 0; step < std::min(due, max_catchup_steps); ++step) {
            robot.step(controls.command());
            capture_preview_sensors(rig, robot, latest_lidar, mapper);
        }
        const bool redraw = got_event && (event.type == SDL_EVENT_WINDOW_EXPOSED ||
                                          event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
        if ((due > 0 || redraw || control_refresh || restarted) &&
            !present(window.get(), warehouse, robot, controls, latest_lidar, mapper, title)) {
            return Error::graphics_present;
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    return argus::app::run_application(argc, argv, "argus-desktop", run_desktop);
}
