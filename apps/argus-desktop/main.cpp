#include "application.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <windows.h>

#include <GL/gl.h>
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

bool present(SDL_Window *window) {
    int width{};
    int height{};
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
        return false;
    }
    glViewport(0, 0, width, height);
    glClearColor(0.08F, 0.12F, 0.17F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    return SDL_GL_SwapWindow(window);
}

argus::app::Status run_desktop(const argus::app::Options &) {
    const VideoSession video;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return Error::video_initialization;
    }

    constexpr int initial_width = 960;
    constexpr int initial_height = 600;
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
        SDL_CreateWindow("ARGUS", initial_width, initial_height,
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
    if (!present(window.get())) {
        return Error::graphics_present;
    }

    SDL_Event event{};
    while (SDL_WaitEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
            return std::monostate{};
        }
        if ((event.type == SDL_EVENT_WINDOW_EXPOSED ||
             event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) &&
            !present(window.get())) {
            return Error::graphics_present;
        }
    }
    return Error::window_events;
}

} // namespace

int main(int argc, char **argv) {
    return argus::app::run_application(argc, argv, "argus-desktop", run_desktop);
}
