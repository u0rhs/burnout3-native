// b3replay HUD / 2D reader (hud1, docs/native-renderer.md §17): the Gt2dRenderer call stream that
// src/gameview/hud_capture.cpp records per rendered frame (host-only region 0x40000000 of a scene capture), decoded
// into triangle batches in the game's 2D space with their GS state, and the guest rasters they use decoded from the
// capture's image (b3asset's raster decoder on the raster's own upload chain).
#ifndef B3REPLAY_HUD_H
#define B3REPLAY_HUD_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "b3live/menu_input.h"

namespace b3replay {

class Image;

constexpr std::uint32_t kHudBase = 0x40000000U;   // hud_capture.cpp kHudBase

// paint2 / paint3: a car page's thumbnail car (a class's car n, 128 x 64, one CLUT per paint): View::hud_list draws it
// from the car's own model in the player's paint
bool hud_thumbnail_name(const std::string& name);   // "<CLASS>CAR<n>": COMPCAR1, CUPECAR3, COMPCAR10 ...
// paint3: a picture with its coverage from two renders of it, on black and on white (RGBA8, n pixels each): alpha = 1 -
// the largest channel difference, colour = the render on black / alpha x `gain` (0 where nothing covers)
void picture_from_backgrounds(const std::uint8_t* on_black, const std::uint8_t* on_white, std::size_t n, float gain,
                              std::vector<std::uint8_t>& rgba);

// A guest raster as Gt2dRenderer::SetTexture selected it (raster, palette index), decoded to RGBA8.
struct HudTexture {
    std::uint32_t raster = 0;       // guest address (CGtTexture* = the RwRaster with the game's tail)
    int palette = 0;                // SetTexture's palette argument (<= 0: the raster's own CLUT)
    std::string name;               // raster +0xA8
    bool ok = false;
    std::string why;                // when !ok
    int w = 0, h = 0;               // the raster's size (the decoded image)
    int tw = 0, th = 0;             // TEX0 2^TW x 2^TH: what GS ST coordinates are normalised to
    int psm = 0, tfx = 0;
    bool tcc = true;
    std::vector<std::uint8_t> rgba; // level 0, PS2 alpha 0x80 -> 0xFF
    int img_w = 0, img_h = 0;       // rgba's size: w x h, larger for an HD pack replacement (b3asset/hdpack.h)
    std::vector<std::uint8_t> original;   // hdkey: w x h, the decode an HD replacement stands for (the HD TEXTURES key)
};

struct HudVertex {
    float x = 0, y = 0;             // the game's 2D space (camera raster pixels; GS 12.4 / 16 - Gt2d's offset)
    float u = 0, v = 0;             // normalised to the decoded image (ST x 2^TW / w)
    std::uint8_t rgba[4] = {0x80, 0x80, 0x80, 0x80};   // GS RGBAQ bytes (0x80 = 1.0)
    std::uint32_t z = 0;
};

// Consecutive primitives with the same GS state, as a triangle list.
struct HudDraw {
    std::uint64_t texture = 0;      // key into Capture::hud_textures, 0 = untextured (PRIM TME clear)
    std::uint8_t alpha = 0x44;      // ALPHA_1 bits 0-7 (A, B, C, D); applied when PRIM ABE is set
    std::uint8_t fix = 0x80;        // ALPHA_1 FIX
    bool abe = true;                // PRIM ABE
    bool alpha_only = false;        // FRAME_1 FBMSK clear with ALPHA 0x6A: only the frame alpha changes (Gt2d mode 7)
    bool rgb_masked = false;        // FRAME_1 FBMSK low 24 bits all set (nothing but alpha is written)
    bool linear = true;             // TEX1_1 MMAG
    bool tfx_decal = false;         // TEX0 TFX = DECAL (1)
    bool tcc = true;                // TEX0 TCC
    std::uint8_t wms = 1, wmt = 1;  // CLAMP_1 WMS / WMT: 0 REPEAT, 1 CLAMP, 2 REGION_CLAMP, 3 REGION_REPEAT
    int scissor[4] = {0, 0, 0, 0};  // x0, y0, x1, y1 inclusive, 2D space
    std::uint32_t fbp = 0, fbw = 0; // FRAME_1 target: Gt2d also draws into small offscreen buffers (effects passes)
    int object = -1;                // index into HudFrame::objects: the Gt2d object whose callback wrote it (-1: none,
                                    // or a capture without object events)
    int marker = -1;                // index into HudFrame::markers: the world-anchored marker that drew it (hud4)
    int video = -1;                 // menu1: a movie object's textured tiles: index into HudFrame::videos (u, v then
                                    // address its whole picture, HudVideo::picture), -1: none
    std::vector<HudVertex> tri;
    // gsoff1 (§29): its pass's origin (Begin's scissor x0 / y0, frame buffer pixels). In a split-screen frame each view's
    // pass draws in its own viewport's space; decode moves those vertices onto one frame-sized space (HudFrame::split)
    int pass_ox = 0, pass_oy = 0;
};

// menu1 (docs/native-renderer.md §22): a movie picture as CGtVideoDecoder holds it (RGBA32 in 16-pixel strips), copied by
// the capture (hud_capture.cpp kMovieBase) and decoded to RGBA8 rows here; shared between frames while unchanged.
struct HudPicture {
    std::uint32_t src = 0;          // its guest address (the decoder slot's picture)
    int w = 0, h = 0;               // macroblock columns x 16, rows x 16
    std::vector<std::uint8_t> rgba; // w x h, PS2 alpha 0x80 -> 0xFF
};

// One CGt2dObjectVideoImage render (menu1, VIDEO event at 0x326960): the decoder's shown slot.
struct HudVideo {
    std::uint32_t object = 0, decoder = 0, slot = 0;   // the slot index (decoder +0x34)
    std::uint32_t pic = 0;          // slot +0x00: the picture
    int tiles_x = 0, tiles_y = 0, mb_w = 0, mb_h = 0;  // slot +0x04 .. +0x10
    std::uint32_t decoded = 0;      // decoder +0x38: pictures decoded since the (re)start [L]
    float tiles_w = 0, tiles_h = 0; // data +0x00: the picture's size in 64-px tiles (the object's size maps onto it)
    bool backing = false;           // data +0x14: an untextured sprite behind the picture
    std::shared_ptr<const HudPicture> picture;   // the captured picture (this frame's, or the last one of `pic`); null: none
};

// menu1: one call of CB3World::RenderFrontEnd 0x275EF0 (the car pages' 3D car), FECAR event.
// padpaint: `player` 1 + the car select page's player while that page is up (0: another page, or a capture without it)
struct HudFeCar { std::uint32_t index = 0, car = 0, rwcamera = 0, player = 0; };

// menu1: one CB3Globe2dObject::_Render 0x3B2EE0 (the World Tour globe; drawn by B3Im3d outside Gt2d's buffer, so not in
// the draws), GLOBE event: the state a native globe needs (hud_capture.cpp observe_globe) [L layout, a reading of the code].
struct HudGlobe {
    std::uint32_t object = 0;
    float pos[3] = {};               // data +0x0
    std::uint16_t lat = 0, lon = 0;  // data +0xC / +0xE: 0..65535 = 0..180 / 0..360 degrees
    std::uint16_t shape[2] = {};     // data +0x10 (spin / halo), +0x12 (sphere -> flat morph)
    std::uint8_t alpha = 0, reveal = 0, highlight = 0;   // data +0x14 .. +0x16
    std::int8_t region = -1;         // data +0x17
    float space[8] = {};             // the object space (position, size, colour +0x10)
    float sphere_pos[4] = {};        // krB3GlobeSpherePosition 0x4CA140
    std::uint32_t texture[5] = {};   // surface, highlight regions 0-2, halo (guest rasters)
    float view[4][4] = {};           // viewport 0 +0x00
    float k[4] = {};                 // viewport 0 +0x48..+0x54
    float znear = 0, zfar = 0;       // viewport 0 +0x84 / +0x88
    // fe2 (§24): the five rasters' HudDecoder::textures keys (0: not decoded), and the globe's place in the draws: the
    // draw count at the Resume that ends its callback (the draws its Suspend flushed come before it)
    std::uint64_t tex_key[5] = {};
    std::size_t draw_index = 0;
};

// A HUD component of the frame's HUD scene (hud2: CB3HUD's component list at CGt2dScene::Render, hud_capture.cpp).
struct HudComponent {
    std::uint32_t addr = 0, vtable = 0, frame = 0, flags = 0;
    int align = -1;                 // EHUDAlign (CB3HUDComponent +0x18): D_004B7650[align] is its frame's place in the
                                    // HUD rect; 8 = none (absolute)
    int player = 0, hud = -1;       // +0x1C; the CB3HUD index whose scene listed it
    std::uint32_t root = 0;         // fe3: that HUD's root frame (the SCENE event's +0x20)
    float fx = 0.5f, fy = 0.5f;     // D_004B7650[align] (align 8 or out of range: 0.5, 0.5 = not moved)
};

// One call of a Gt2d object's render callback (hud2).
struct HudObject {
    std::uint32_t pc = 0, addr = 0;
    std::uint32_t frames[8] = {};   // its frame (+0x40) and the frame's ancestors (+0x48)
    float pos[2] = {0, 0}, size[2] = {0, 0};   // the object space the scene passed (world position, size)
    int component = -1;             // index into HudFrame::components (the first frame in the chain that is a
                                    // component's frame), -1: not in a listed component
    // menu-font: a CB3ScrollingText2dObject (the front end's ticker): its render data as the callback saw it (TICKER
    // event, src/gameview/hud_capture.cpp observe_ticker); ticker_font 0: none captured
    std::u16string ticker_text;
    float ticker_scroll = 0, ticker_extent = 0, ticker_gap = 0, ticker_recip[2] = {0, 0};
    std::uint32_t ticker_font = 0;  // the font's texture (HudTexture::raster)
    // 1.3.1 (tools/native/pc_menu.h, b3gv::set_ticker_text): ticker_text is the port's text and the game drew its own
    // there; the renderer lays it out in place of the game's draws at every size (view.cpp), its extent from its own width
    bool ticker_port = false;
};

// A world-anchored marker (hud4, docs/native-renderer.md §17.13): one call of CB3RacePosition::DrawPositionMarker 0x1531C0
// (the arrow / rank text over another car, or a points value over a vehicle), with the world point it projected and
// the viewport it used, so the viewer can project the point again with each displayed frame's camera.
struct HudMarker {
    std::uint32_t slot = 0, car = 0, player = 0;   // $a3 rank slot, $t0 CB3RaceCar (0: a vehicle value), $t1 viewer's car
    bool points = false;            // $t3: points mode (a value popup, DrawSingleVehicleValue 0x1540A0)
    float world[3] = {};            // the point ($a1): car matrix position + (0, vehicle data +0xE80 y, 0)
    float screen[4][4] = {};        // viewport +0x00: world -> screen (x / z, y / z in 0..1)
    float k[4] = {};                // viewport +0x48..+0x54
    int sw = 0, sh = 0;             // viewport +0x78 / +0x7C (the 2D space)
    float znear = 0, zfar = 0;      // viewport +0x84 / +0x88
    // derived (the game's projection, DrawPositionMarker's first lines [L draft]): the projected point in the 2D space,
    // its depth and the marker's size (1.5 (1 + (z - 1) / 74) / z x sw / 2 x +0x50, at most 320)
    bool valid = false;             // in front of the near plane, inside the far plane
    float x = 0, y = 0, z = 0, size = 0;
    // the vehicle it is anchored to at the capture's tick (Capture::decode: CB3RaceCar id, else the vehicle under the point)
    std::uint32_t vehicle = 0;      // Vehicle::id, 0: none (the point stays as captured)
    int view = 0;                   // gsoff1 (§29): in a split-screen frame, the view it was drawn in (0 top, 1 bottom);
                                    // x / y are then frame-space (its pass origin added)
    int pass_ox = 0, pass_oy = 0;   // gsoff1: the pass the event came in (Begin's scissor origin)
    float offset[3] = {};           // world - the vehicle's position (its main matrix row 3) at that tick
    float match = -1;               // horizontal distance between the point and that position (m)
};

// A vehicle's position, for anchoring markers (Capture::decode passes the tick's vehicles).
struct HudAnchorRef { std::uint32_t id = 0; float pos[3] = {}; bool race_car = false; };

// hudmods (hudmods.h): a speedometer's or boost bar's values as the game's HUD read them at the scene (hud_capture.cpp
// observe_gauge, GAUGE events): the engine revs for the RPM METER, the boosting flag for BOOST BAR SHAKE.
struct HudGauge {
    std::uint32_t component = 0;    // _gSpeedoComponentP1 / P2 or _gBoostBarComponentP1 / P2 (HudComponent::addr)
    int kind = 0;                   // 1 speedometer, 2 boost bar
    std::uint32_t car = 0;          // speedometer: its car; boost bar: the car's CB3Score
    float revs = 0, limit = 0, low = 0;   // speedometer: the transmission's +0x54, +0x38, +0x3C
    int gear = 0;                   // speedometer: -1 reverse, 0 neutral, 1..
    bool manual = false;            // speedometer: no automatic shifts (+0x70)
    bool boosting = false;          // boost bar
    // speedometer: the RPM METER's fill, 0 at the low end, 1 at the rev limit [L hud_capture.cpp observe_gauge]
    float rpm_fraction() const {
        const float span = limit - low;
        if (!(span > 0)) return 0.0f;
        const float f = (10.0f * revs - low) / span;
        return f > 1.0f ? 1.0f : f > 0.0f ? f : 0.0f;
    }
};

struct HudStats {
    std::uint32_t prims = 0, triangles = 0, sprites = 0, strips = 0, lines = 0, unknown_texture = 0, batches = 0;
    // font1: textured triangles after a TEX0 write that no SetTexture event announced (no raster: they draw as solid
    // blocks): any nonzero count means the capture missed a SetTexture path (once a native SetTexture overload,
    // 0x219C90, hid the text's SetTexture calls from the hook)
    std::uint32_t unbound_texture = 0;
    std::uint32_t passes = 0;       // Begin..End passes in the frame
    std::uint32_t scenes = 0, hud_scenes = 0, objects = 0, objects_no_offset = 0;
};

struct HudFrame {
    std::uint32_t serial = 0;       // the hook's frame count (one per frame that drew 2D)
    std::uint32_t missed = 0;       // flushes the hook did not see (cumulative; 0 = complete)
    int width = 0, height = 0;      // the 2D space: Begin's scissor (the camera raster) [C Begin 0x21A0F0]
    int ox = 0, oy = 0;             // its offset in the frame buffer window
    std::uint64_t frame_reg = 0;    // FRAME_1 at Begin (FBP: which buffer the 2D goes into)
    std::vector<HudDraw> draws;
    std::vector<HudObject> objects;
    std::vector<HudComponent> components;
    std::vector<HudMarker> markers; // hud4 captures (MARKER events)
    std::vector<HudVideo> videos;   // menu1 captures (VIDEO events)
    std::vector<HudFeCar> fe_cars;  // menu1 captures (FECAR events)
    std::vector<HudGlobe> globes;   // menu1 captures (GLOBE events)
    std::vector<HudGauge> gauges;   // hudmods captures (GAUGE events): the speedometers and boost bars of the HUD scenes
    bool has_objects = false;       // the capture records object events (hud2 captures)
    float hud_rect[4] = {0, 0, 0, 0};   // CB3HUD +0x48 (min x, y), +0x50 (max x, y) of the last HUD scene
    float align_table[18] = {};     // D_004B7650 as captured: (fx, fy) per EHUDAlign 0..8
    bool split = false;             // gsoff1 (§29): a 2-player split-screen frame, its 2D on one space of both views
    HudStats stats;
};

// The game's projection of a marker's point with its captured viewport (sets valid, x, y, z, size).
void hud_marker_project(HudMarker& m);
// Anchors each marker to a vehicle of `refs` (the tick's): the race car it names, else the vehicle under its point.
void hud_anchor_markers(HudFrame& hf, const std::vector<HudAnchorRef>& refs);

// hud4: a world point's place in the HUD's 2D space (the game's 640 x 448 units, as hud_list maps them to an output
// of `aspect`) through `view_proj`; false behind the eye. `depth`: the clip w (view-space distance along the view axis).
bool hud_project(const float view_proj[16], float aspect, int width, int height, const float world[3], float* x, float* y, float* depth);
// gsoff1 (§29): the same for a split-screen frame: through view `view`'s view-projection (its own half, 0 top / 1 bottom)
// into the frame-sized 2D space. Diagnostics only (B3HUD_MARKER_TRACE on split shots): split frames draw their markers
// where the game put them (not reprojected; residual up to ~3 px against the game's projection, not settled).
bool hud_project_split(const float view_proj[16], int view, float aspect, int width, int height, const float world[3], float* x, float* y,
                       float* depth);
// The game's marker size rule as a function of depth (DrawPositionMarker: (1 + (z - 1) / 74) / z, before its scale and cap).
float hud_marker_size_rule(float z);

// wide1: the front end's wide layout (View::hud_list with `frontend`, ext > 0: the extra half width in 2D units).
// A draw spans when one of its triangles covers x 0..`right` (bands, panels, the grid; not text that only spans as a
// whole); an untextured, non-movie draw is a fill when it reaches an edge from inside (or both). Only these extend.
struct HudFrontendRule {
    bool span = false, fill = false;
    // fe3 (--fe-layout panels): a cap moves whole by `move`; the vertices at x `end_x` move by `end_dx` (a band's line
    // meeting its moved cap); `fade`: the ticker's glyphs fade out over kFeTickerFade units at its clip region's ends
    float move = 0.0f, end_x = -1e30f, end_dx = 0.0f;
    // mac5: a linear shift across the piece -- a vertex at x moves by stretch_dx at stretch_lo, stretch_dx_hi at
    // stretch_hi, linearly between: the heading's big_curve, its foot out to the window's edge, its top end half as far
    float stretch_lo = 0.0f, stretch_hi = 0.0f, stretch_dx = 0.0f, stretch_dx_hi = 0.0f;
    // mac5: a span that tiles its texture over the added width (repeat) instead of stretching it (the garage's static)
    bool tile = false;
    bool fade = false;
    bool extends() const { return span || fill || move != 0.0f || end_dx != 0.0f || stretch_dx != 0.0f; }
    float stretch_at(float x) const {
        const float t = stretch_hi > stretch_lo ? (x - stretch_lo) / (stretch_hi - stretch_lo) : 0.0f;
        return stretch_dx + (stretch_dx_hi - stretch_dx) * (t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t);
    }
};
HudFrontendRule hud_frontend_rule(const HudDraw& d, float right);
// fe3: the front end's panel layout (docs/native-renderer.md §30). wide1's rules decided per draw by shape; these decide
// per element class (the Gt2d object's render callback [C names, decomp tables]) and the texture's content:
// - a textured triangle covering 0..right stretches only if what it samples is smooth sideways (`bg`'s vignette, the
//   prompt bar's strip), repeats (the grid) or is CB3SelectionBar2dObject's bar (wide1); any other is a picture and keeps 4:3 (the
//   loading pictures, the Crash Nav zoom's map layers, a CB3FlashText2dObject line flying in);
// - a textured piece at exactly one edge is a cap when an untextured draw of its object ends where it begins (the prompt
//   bar's small_curve at 608..640 and its line 0..608): both move to the window edge; else it stretches only if smooth
//   (the loading bars 140..640); else it stays (big_curve with the logo, B3Logo, the map tiles);
// - CGt2dObjectBox fills are content (the map's highlight and crosshair, a sliding panel's borders) and keep 4:3; other
//   untextured fills extend as in wide1 (band bodies, gradients, panels, a movie's backing);
// - CB3ScrollingText2dObject glyphs in a clip region fade at its ends (the ticker);
// - movies keep 4:3. `backdrop`: the draw after which the side fill goes (the frame's first movie reaching an edge, its last
//   draw; else the first picture, a span kept 4:3 for its texture, exactly at 0..right with some alpha), -1 none.
// `roughness(d)`: the 99th percentile of the largest channel change between neighbouring texel columns of what d samples
// (View: hud_texture_roughness). Smooth: below kFeSmooth [calibrated on home1's front end: bg 5-8, loading_bars 4, the
// prompt bar's and the panels' strips 0; CARS0004 43, USA1 45-75, big_curve 76, the other pictures and glyphs 82-254].
struct HudFrontendPlan {
    std::vector<HudFrontendRule> rule;   // per draw
    int backdrop = -1;
    float box[4] = {0, 0, 0, 0};          // the backdrop's extent (x0, y0, x1, y1), the union of its movie's draws
    // mac5 (Mac 2026-11-08: the Crash Nav map's edges): other pictures reaching a 4:3 edge (the map's tiles), each
    // side-filled as the backdrop is, over its own extent
    struct EdgeImage { int draw = -1; float box[4] = {0, 0, 0, 0}; };
    std::vector<EdgeImage> edge_images;
};
constexpr float kFeTickerFade = 48.0f, kFeSmooth = 16.0f;
// `tv_static(d)` (optional): d samples the game's TV static texture ("Camchange", the garage between cars and while a car
// loads): a full-width effect over the page, opaque or not, so it covers the window (mac5, a report: Windows, the static
// left the widescreen sides uncovered).
HudFrontendPlan hud_frontend_plan(const HudFrame& f, float right, float ext, const std::function<float(const HudDraw&)>& roughness,
                                  const std::function<bool(const HudDraw&)>& tv_static = {});
// perf1 (§33): a race frame's 2D that is an overlay of the front end's panels (the pause menu, the event intro): some object
// of CHeadingBar2dObject 0x31B130 or CPromptBarPanel2dObject 0x3D7B40 and no draw in a HUD component.
bool hud_overlay_panels(const HudFrame& f);
// perf1: such an overlay's content in a wide output (ext > 0), per draw (0: as hud2 lays it out): the heading band's other
// draws (the title, PAUSED) and the prompt row (the prompts under the prompt bar's line) each move as a group to the side
// of their centre (+-ext), the body's draws entirely left of x 280 (the menu column) with the left edge unless a CB3RoundedPanel2dObject dialog
// drawn after them covers them; the bands, caps
// and anything at an edge stay with hud2's rules, the rest (a centred dialog) stays. The EA TRAX panel: View.
// `vertexwise` (if given) marks the CB3SelectionBar2dObject bar of a moved row: its vertices at the 4:3 edges go to the
// window's edges and the others (the brackets around the item) move by the shift, so its line stretches to meet them.
std::vector<float> hud_overlay_shifts(const HudFrame& f, float right, float ext, std::vector<bool>* vertexwise = nullptr);
// The moved x (and u / v: a repeating texture keeps its texel scale) of vertex `vi` of `d` under `r`.
void hud_frontend_vertex(const HudDraw& d, const HudFrontendRule& r, std::size_t vi, float right, float ext, bool repeat,
                         float& x, float& u, float& v);
// A draw inside a clip region narrower than the raster (Gt2d's SetClipRegion): it neither votes for nor follows its
// object's edge anchor (cam1, wide1).
inline bool hud_in_clip_region(const HudDraw& d, float right) { return d.scissor[0] > 0 || float(d.scissor[2] + 1) < right; }

// The host UI layer (wide1): quads drawn over the 2D in the 640 x 448 picture, centred 4:3 (View::host_ui_list). The PC
// settings page builds one (pcpage.h PcPage::ui).
struct HostUiQuad { float x[4], y[4], u[4], v[4]; std::uint32_t rgba = 0, path = 0;
                   std::uint8_t cover = 0; };   // corners TL, TR, BR, BL; RGBA straight; cover: 1 x, 2 y, 3 both: drawn over the window's whole
                                                // extent in that axis (not clipped to the 4:3 box; a coordinate at the picture's edge
                                                // moves out to the window's edge): the page's backdrop on widescreen sides; 4 (options3):
                                                // not clipped to the 4:3 box, its coordinates as they are (DRIVER LABELS over the wide picture)
// The 640 x 448 picture units a window of `aspect` shows (the 4:3 box is the part 0..640 x 0..448): where a HostUiQuad::cover
// coordinate at the picture's edge goes (wide1 / wsmenu1).
struct HostUiExtent { float left, right, top, bottom; };
inline HostUiExtent host_ui_extent(float aspect) {
    const float fw = std::min(1.0f, (4.0f / 3.0f) / aspect), fh = std::min(1.0f, aspect / (4.0f / 3.0f));
    return {320.0f - 320.0f / fw, 320.0f + 320.0f / fw, 224.0f - 224.0f / fh, 224.0f + 224.0f / fh};
}
struct HostUi {
    std::uint32_t serial = 0;
    std::vector<std::string> paths;   // [0] "": white
    std::vector<HostUiQuad> quads;    // in draw order
    // settingsui: pictures the host made (the PC SETTINGS page's button glyphs), a path "img:<name>" each; a new
    // picture gets a new name (the renderer keeps what it uploaded by path)
    struct Image { std::string name; int w = 0, h = 0; std::vector<std::uint8_t> rgba; };
    std::vector<std::shared_ptr<const Image>> images;
};
// settings1 (docs/pc-settings.md): our PC settings (published when they change, at
// kHostSetBase: {u32 'HSET', u32 serial, u32 bytes, 0}, then the ini text of include/b3settings/settings.h).
constexpr std::uint32_t kHostSetBase = 0x41101000U, kHostSetMax = 0x1000U;   // in the HUD's host-only region
struct HostSettings {
    std::uint32_t serial = 0;
    std::string text;
};
// The record if its serial changed since `prev` (else prev); nullptr: none captured.
// mac5: the game's three 2D fonts (CGt2dFont, the graphics manager D_00665EC0 +0x10 / +0x14 / +0x18), published once by
// the native capture (tools/native/gameview_native.cpp) at kHostFontBase: {u32 'HFN1', u32 serial, u32 bytes, u32 fonts},
// then per font {u32 raster (the texture's guest address, HudTexture::raster), f32 cell scale x, y, f32 baseline, u32
// glyphs} and the glyph records as the game holds them (0x20 bytes: uv x y, size x y, offset x y, advance, u16 code).
// Lays text out as CGt2dFont::RenderString / GetStringWidth do (0x1D7B90 / 0x1D8150).
constexpr std::uint32_t kHostFontBase = 0x41102000U, kHostFontMax = 0x10000U;
struct HostFontGlyph { float u = 0, v = 0, w = 0, h = 0, ox = 0, oy = 0, advance = 0; std::uint16_t code = 0; };
struct HostFont {
    std::uint32_t raster = 0;
    float scale_x = 0, scale_y = 0, baseline = 0;
    std::vector<HostFontGlyph> glyphs;
    const HostFontGlyph* find(std::uint16_t c) const;   // the game's lookup falls back to the last glyph
    float width(std::u16string_view text, float size) const;   // GetStringWidth x size
};
struct HostFonts {
    std::uint32_t serial = 0;
    std::vector<HostFont> fonts;
};
std::shared_ptr<const HostFonts> host_fonts_decode(const Image& im, std::shared_ptr<const HostFonts> prev);
// menu-font: the ticker (a HudObject with ticker_font) laid out as CB3ScrollingText2dObject::_Render 0x31C990 and its
// _BuildBatchData 0x31C5D0 do, over the strip widened by `ext` 2D units on each side: the game's strip is its object
// space (16..624 on the 4:3 screen) and 608 units of scroll; here it runs from x - ext to x + w + ext and the scroll
// covers 608 + 2 ext / scale units, so the text enters at the wide strip's right end and leaves at its left, at the
// game's speed. ext 0 is the game's own layout (the draw's vertices, to 1/16 unit). `f`: the ticker's font; `nu` / `nv`:
// font UV -> the decoded image (HudTexture tw / w, th / h); `rgba`, `z`: the draw's flat colour and depth. Appends a
// triangle list (the game's strips, degenerate joins included) to `out`; false if the object has no ticker record.
bool hud_ticker_layout(const HudObject& o, const HostFont& f, float nu, float nv, float ext, const std::uint8_t rgba[4], std::uint32_t z,
                       std::vector<HudVertex>& out);
// mac5: the video skip hint's opacity (native_play, tools/native/gameview_native.cpp set_skip_hint: {u32 'HSK1', u32
// serial, u32 opacity 0..255, 0} at kHostSkipBase, published when it changes); 0 when hidden or never captured.
constexpr std::uint32_t kHostSkipBase = 0x41112000U;   // after the fonts' 64 KB
std::uint8_t host_skip_hint_decode(const Image& im);
// autosave1: native_play holds the display on the last page while the AUTOSAVE page runs (tools/native/save_screens.h;
// {u32 'HHD1', u32 serial, u32 hold 0 / 1, 0} at kHostHoldBase, published when it changes); false when never captured.
constexpr std::uint32_t kHostHoldBase = 0x41112100U;
bool host_hold_decode(const Image& im);
// todrace (tools/native/race_time.h): the race's DAY / NIGHT choice and the track page's label ({u32 'HTD1', u32
// serial, u32 race 0 Default / 1 Day / 2 Night (weather: 3 Rain, 4 Night rain), u32 label: its choice | opacity 0..255 << 8} at kHostRaceTimeBase,
// published when it changes); all 0 when never captured.
constexpr std::uint32_t kHostRaceTimeBase = 0x41112200U;
// raceopts: the label word's bits 16..31 are the race's records word's bits 0..15, the race word's bits 16..31 its bits
// 16..31, and (options3) a fifth word its bits 32..63 (24 bytes; a capture with 16 reads 0 there) (tools/native/race_choice.h
// record_word, with elim1's, heavyseries', gameopts', gearbox's and game_mods.h's bits: records.h options_folder; 0 the
// game's own race: its records are the player's). gameopts (tools/native/game_options.h): the race
// word's bits 9..10 are the race's GAMEPLAY options (bit 0 MIRROR MODE: the 3D drawn mirrored; bit 1 CRASHBREAKER IN EVERY
// MODE). The race word's bits 0..7 are the race's time of day (every field's owner: tests/test_bit_owners.cpp)
struct HostRaceTime { std::uint8_t race = 0, label_choice = 0, label_opacity = 0, game_options = 0; std::uint64_t options = 0; };
HostRaceTime host_race_time_decode(const Image& im);
// paintsel (tools/native/paint_select.h): the PAINT prompt on SELECT CAR ({u32 'HPS1', u32 serial, u32 opacity 0..255, u32
// player 0 / 1} at kHostPaintSelBase, published when it changes); opacity 0 when hidden or never captured.
constexpr std::uint32_t kHostPaintSelBase = 0x41112300U;
struct HostPaintSel { std::uint8_t opacity = 0, player = 0; };
HostPaintSel host_paint_sel_decode(const Image& im);
// paint3 (tools/native/carousel_models.h): the car pages' carousel slots whose model is not their picture's ({u32 'HCM1',
// u32 serial, u32 count, 0} then 32 bytes a slot at kHostCarouselBase); the record if its serial changed since `prev`
// (else prev); nullptr: none captured
constexpr std::uint32_t kHostCarouselBase = 0x41112600U;
struct HostCarouselSlot {
    std::uint32_t raster = 0;     // the picture the slot draws (a donor's)
    int cls = 0, index = 0, paint = 0, slot = 0;
    bool paintable = false;       // the player's paint applies (else its game colours)
    std::string model;            // "COMPCAR18": PVEH/COMP/CAR18.BTV / .BGV
};
struct HostCarousel {
    std::uint32_t serial = 0;
    std::vector<HostCarouselSlot> slots;
};
std::shared_ptr<const HostCarousel> host_carousel_decode(const Image& im, std::shared_ptr<const HostCarousel> prev);
// replayview (tools/native/replay_view.h): the race replay's state ({u32 'HRV1', u32 serial, u32 state, 0} at
// kHostReplayBase, published when it changes): 0 none (or never captured), 1 REPLAY offered (a results page), 2 the
// replay playing (the renderer's ghost and best-time recorders take none of it; its cameras: replaycam.h)
constexpr std::uint32_t kHostReplayBase = 0x41112800U;
std::uint8_t host_replay_decode(const Image& im);
// elim1 (tools/native/single_eliminator.h): TIMED ELIMINATION's countdown ('HEL1' {u32 'HEL1', u32 serial, u32 whole
// seconds left (0xFFFFFFFF none), u32 0, u32 colours (RGBA, red low): the race HUD's +0x30, +0x40, +0x50, +0x60, then the
// HUD captions' style 1 ramp c0..c3} at kHostElimTimerBase, published when it changes); seconds -1 when none
constexpr std::uint32_t kHostElimTimerBase = 0x41112400U;
struct HostElimTimer { int seconds = -1; std::uint32_t c30 = 0, c40 = 0, c50 = 0, c60 = 0, gold[4] = {}; };
HostElimTimer host_elim_timer_decode(const Image& im);
// gearbox (tools/native/gearbox.h): the manual gearbox's gear ('HGB1' {u32 'HGB1', u32 serial, s32 gear (-1 reverse, 0
// neutral, 1..; -2 none), u32 0, u32 colours[8] as 'HEL1'} at kHostGearBase, published when it changes); gear -2: none
constexpr std::uint32_t kHostGearBase = 0x41112500U;
struct HostGear { int gear = -2; std::uint32_t c30 = 0, c40 = 0, c50 = 0, c60 = 0; };
HostGear host_gear_decode(const Image& im);
std::shared_ptr<const HostSettings> host_settings_decode(const Image& im, std::shared_ptr<const HostSettings> prev);
// mac5: the game's menus on screen for the mouse (native_play's 'HMN1' record, include/b3live/menu_input.h at
// b3live::kHostMenuBase): the record if its serial changed since `prev` (else prev); nullptr: none captured.
std::shared_ptr<const b3live::MenuRecord> host_menu_decode(const Image& im, std::shared_ptr<const b3live::MenuRecord> prev);
// A PNG file (8-bit grey / RGB / RGBA / grey-alpha, not interlaced) as RGBA rows. A
// Windows path ("C:\...") that does not open is tried as WSL's /mnt/c/... (offline replays on Linux).
bool read_png_rgba(const std::string& path, int& w, int& h, std::vector<std::uint8_t>& rgba);

class HudDecoder {
public:
    // Decodes the region at kHudBase of `im` if its serial changed since `prev` (else returns prev). Textures are
    // decoded into `textures` (keyed by raster address, header bytes and palette) on first use. nullptr: no region.
    // `refs`: the tick's vehicles (hud4: markers are anchored to the one they were drawn over).
    std::shared_ptr<const HudFrame> decode(const Image& im, std::shared_ptr<const HudFrame> prev,
                                           const std::vector<HudAnchorRef>* refs = nullptr);
    std::map<std::uint64_t, std::shared_ptr<const HudTexture>> textures;
    std::map<std::uint32_t, std::shared_ptr<const HudPicture>> pictures;   // menu1: the last picture per guest address
private:
    // perf (2026-11-12): the captured bytes each picture in `pictures` was built from (compared whole, instead of a
    // byte-wise hash of ~1 MB per tick), and the buffer the next tick's bytes are read into
    std::map<std::uint32_t, std::vector<std::uint8_t>> picture_raw_;
    std::vector<std::uint8_t> video_scratch_;
};

}  // namespace b3replay

#endif
