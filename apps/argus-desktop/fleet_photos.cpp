#include "fleet_photos.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <windows.h>

#include <algorithm>
#include <memory>

namespace argus::desktop {
namespace {

constexpr std::array<std::string_view, 3> codes{"990-01-0001", "990-02-0001", "990-03-0001"};

unsigned load_photo(unsigned short id) {
    const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
    if (!resource || SizeofResource(nullptr, resource) != 32502) {
        return 0;
    }
    const auto handle = LoadResource(nullptr, resource);
    const auto *data = handle ? LockResource(handle) : nullptr;
    auto *stream = data ? SDL_IOFromConstMem(data, 32502) : nullptr;
    if (!stream) {
        return 0;
    }
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> input{
        SDL_LoadBMP_IO(stream, true), &SDL_DestroySurface};
    if (!input || input->w != 104 || input->h != 104) {
        return 0;
    }
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba{
        SDL_ConvertSurface(input.get(), SDL_PIXELFORMAT_RGBA32), &SDL_DestroySurface};
    if (!rgba || rgba->pitch != rgba->w * 4) {
        return 0;
    }
    unsigned texture{};
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba->pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

} // namespace

FleetPhotos::~FleetPhotos() { clear(); }

void FleetPhotos::load() {
    for (std::size_t index = 0; index < textures_.size(); ++index) {
        textures_[index] = load_photo(static_cast<unsigned short>(101 + index));
    }
}

void FleetPhotos::clear() noexcept {
    if (std::ranges::any_of(textures_, [](unsigned texture) { return texture != 0; })) {
        glDeleteTextures(static_cast<int>(textures_.size()), textures_.data());
        textures_.fill(0);
    }
}

unsigned FleetPhotos::texture(std::string_view dpci) const noexcept {
    const auto found = std::ranges::find(codes, dpci);
    return found == codes.end() ? 0 : textures_[static_cast<std::size_t>(found - codes.begin())];
}

bool FleetPhotos::ready() const noexcept {
    return std::ranges::all_of(textures_, [](unsigned texture) { return texture != 0; });
}

} // namespace argus::desktop
