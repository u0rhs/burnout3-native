// draw.h -- the launcher's shapes on SDL_Renderer: convex fills and strokes with a one-pixel soft edge (an alpha
// fringe on SDL_RenderGeometry), rounded boxes, discs, rings, lines, gradients, check marks and chevrons. Logical units
// (the renderer's scale is the UI scale; `px` = one pixel in them, for the fringe).
#ifndef B3LAUNCHER_DRAW_H
#define B3LAUNCHER_DRAW_H

#include <SDL3/SDL.h>

#include <vector>

namespace b3launcher::draw {

struct P { float x, y; };

void set_renderer(SDL_Renderer* r, float px);
void set_round(float k);   // every rounded corner x k (the game look: squarer boxes)

SDL_FColor rgba(unsigned hex, float a = 1.0f);   // 0xRRGGBB
SDL_FColor mix(SDL_FColor a, SDL_FColor b, float t);
SDL_FColor alpha(SDL_FColor c, float a);

void rect(const SDL_FRect& r, SDL_FColor c);   // plain, pixel-aligned
void gradient(const SDL_FRect& r, SDL_FColor left_top, SDL_FColor right_top, SDL_FColor left_bottom, SDL_FColor right_bottom);
void convex(const std::vector<P>& pts, SDL_FColor c);                  // filled, soft edge (any winding)
void stroke(const std::vector<P>& pts, float w, SDL_FColor c, bool closed);
void round_rect(const SDL_FRect& r, float radius, SDL_FColor c);
void round_rect_outline(const SDL_FRect& r, float radius, float w, SDL_FColor c);
void disc(float cx, float cy, float r, SDL_FColor c);
void ring(float cx, float cy, float r, float w, SDL_FColor c);
void line(float x0, float y0, float x1, float y1, float w, SDL_FColor c);
void check(float cx, float cy, float s, float w, SDL_FColor c);         // a tick in a box of side s
void cross(float cx, float cy, float s, float w, SDL_FColor c);         // an x
void chevron(float cx, float cy, float s, int dir, float w, SDL_FColor c);   // dir: -1 left, +1 right, -2 up, +2 down
void triangle(float cx, float cy, float s, SDL_FColor c);              // the PS triangle symbol (outline)
void square(float cx, float cy, float s, SDL_FColor c);                // the PS square symbol (outline)

}  // namespace b3launcher::draw

#endif
