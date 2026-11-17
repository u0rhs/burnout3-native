// The app icon as an SDL surface, for SDL_SetWindowIcon: the 128 px icon built into the program (include/b3port/
// app_icon_data.h, generated from the drawing by tools/release/make_icons.py), so a window has its icon with no file
// beside the program. The surface borrows the array: it is never freed (SDL_DestroySurface would not free the pixels,
// but the data lives for the whole run).
#pragma once
#include <SDL3/SDL.h>

#include "b3port/app_icon_data.h"

namespace b3port {
inline SDL_Surface* app_icon_surface() {
    return SDL_CreateSurfaceFrom(kAppIconSize, kAppIconSize, SDL_PIXELFORMAT_RGBA32, const_cast<unsigned char*>(kAppIconRgba), kAppIconSize * 4);
}
}  // namespace b3port
