#include "application.hpp"
#include "warehouse_io.hpp"
#include "warehouse_view.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>

namespace {

using argus::app::Error;

class VideoSession {
  public:
    VideoSession() = default;
    ~VideoSession() { SDL_Quit(); }

    VideoSession(const VideoSession &) = delete;
    VideoSession &operator=(const VideoSession &) = delete;
};

bool present(SDL_Window *window, const argus::world::Snapshot &view) {
    int width{};
    int height{};
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return true;
    }
    argus::desktop::draw_warehouse(view, width, height);
    return SDL_GL_SwapWindow(window);
}

argus::app::Status run_desktop(const argus::app::Options &options) {
    using argus::app::WarehouseSource;
    using argus::simulation::ScenarioError;
    using argus::simulation::Warehouse;
    if (options.warehouse_source == WarehouseSource::export_fixture) {
        return argus::app::export_warehouse(options.fixture);
    }
    const auto loaded = argus::app::open_warehouse(options);
    if (const auto *error = std::get_if<ScenarioError>(&loaded)) {
        return *error;
    }
    const auto &warehouse = std::get<Warehouse>(loaded);
    const VideoSession video;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return Error::video_initialization;
    }

    constexpr int initial_width = 960;
    constexpr int initial_height = 600;
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
        SDL_CreateWindow("ARGUS Warehouse Preview", initial_width, initial_height,
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
    auto previous_tick = options.tick;
    if (!present(window.get(), warehouse.snapshot(previous_tick))) {
        return Error::graphics_present;
    }

    constexpr std::uint64_t tick_ms = 50;
    const auto started = SDL_GetTicks();
    for (;;) {
        SDL_Event event{};
        SDL_ClearError();
        const bool got_event = SDL_WaitEventTimeout(&event, static_cast<int>(tick_ms));
        if (!got_event && *SDL_GetError() != '\0') {
            return Error::window_events;
        }
        if (got_event &&
            (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
             (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))) {
            return std::monostate{};
        }
        const auto elapsed_ticks = (SDL_GetTicks() - started) / tick_ms;
        const auto tick =
            options.tick +
            std::min(elapsed_ticks, std::numeric_limits<std::uint64_t>::max() - options.tick);
        const bool refresh = got_event && (event.type == SDL_EVENT_WINDOW_EXPOSED ||
                                           event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
        if (tick != previous_tick || refresh) {
            if (!present(window.get(), warehouse.snapshot(tick))) {
                return Error::graphics_present;
            }
            previous_tick = tick;
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    return argus::app::run_application(argc, argv, "argus-desktop", run_desktop);
}
