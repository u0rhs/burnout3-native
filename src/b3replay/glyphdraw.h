// glyphdraw.h -- the small vector painter of the port's own prompt pictures (mac5, 2026-11-09): the video skip hint
// (skiphint.h) and the button prompt glyphs (prompts.h). Shapes (discs, rings, rounded boxes, polygons, round-capped
// polylines) are composited on the CPU with analytic anti-aliasing (a signed distance per pixel centre: a one-pixel
// soft edge), and a monoline stroke font (A-Z, 0-9 and a few marks) draws their text. No game art: every shape here is
// drawn from code.
#ifndef B3REPLAY_GLYPHDRAW_H
#define B3REPLAY_GLYPHDRAW_H

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace b3replay::glyph {

struct Pt { float x, y; };
using Line = std::vector<Pt>;   // a polyline

// An arc of the ellipse (cx, cy, rx, ry) from angle a0 to a1 (degrees, y down: -90 is the top), appended to `l`.
void arc(Line& l, float cx, float cy, float rx, float ry, float a0, float a1);

// The canvas: premultiplied RGBA floats; each shape is composited "over" with its coverage.
struct Canvas {
    int w = 0, h = 0;
    std::vector<std::array<float, 4>> px;
    Canvas(int w_, int h_);
    void box(float cx, float cy, float hx, float hy, float r, const float (&rgba)[4]);
    void box_outline(float cx, float cy, float hx, float hy, float r, float t, const float (&rgba)[4]);
    void disc(float cx, float cy, float r, const float (&rgba)[4]);
    void ring(float cx, float cy, float r, float t, const float (&rgba)[4]);
    // A closed polygon (any winding), filled
    void polygon(const std::vector<Pt>& p, const float (&rgba)[4]);
    // Polylines `t` wide with round ends and joins (one shape: overlapping strokes do not double up)
    void strokes(const std::vector<Line>& lines, float t, const float (&rgba)[4]);
    // The rows [row0, row1) strokes(lines, t) may change (none: row0 == row1)
    void strokes_rows(const std::vector<Line>& lines, float t, int& row0, int& row1) const;
    // Straight-alpha RGBA8 rows (the GS blend takes As), written at (x0, y0) of an image `stride_px` wide; only the
    // canvas rows [row0, row1) when given (row1 < 0: to the last)
    void store(std::uint8_t* rgba, int stride_px, int x0, int y0, int row0 = 0, int row1 = -1) const;
};

// Text in the stroke font: the polylines of `s` (upper case, digits, + - / . : ( ) [ ] < > | * , ' = _ % & # ! ? and space) with its cap `cap` pixels
// high from (x, top), strokes `t` wide; returns its width. Lower case letters draw as upper case.
float text(std::string_view s, float x, float top, float cap, float t, std::vector<Line>& out);
float text_width(std::string_view s, float cap, float t);

}  // namespace b3replay::glyph

#endif
