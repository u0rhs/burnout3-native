// HUD / 2D capture for the native renderer (hud1, docs/native-renderer.md §17), backend-independent since pic1 (§32;
// include/b3gameview/gameview.h): the observations get the guest memory as b3gv::Memory and the argument registers as
// arg[8], and the backend adapter hooks
// the entries (entry_points) and reports how many it could (hud_enable). The native adapter
// (tools/native/gameview_native.cpp) publishes its own host records (the PC settings page's text, the video skip hint)
// through b3gv::Host::extra_publish. Inactive unless the scene capture is on (B3PORT_SCENE_OUT, B3PORT_SCENE_LIVE or
// B3PORT_RENDER) and B3PORT_SCENE_HUD is not "0".
//
// The 2D layer is immediate mode: everything 2D goes through the 22 Gt2dRenderer functions (Gt2dRendererPS2.cpp,
// 0x217850-0x21A4F0), which append GS register writes (GIF PACKED A+D for state, REGLIST primitives with XYZ in GS 12.4
// after adding _gGsOffsetXYXY) to a 512-quadword buffer and copy it into RenderWare's DMA stream in _Dispatch 0x21A4F0
// [C asm; only these functions reference _gpu64Current (decomp asm scan)]. So the capture records the call stream at a
// few function entries and copies each batch at the moment it is flushed, not memory state at the poll:
//
//   Begin 0x21A0F0        a new 2D pass; the first observation after it records Gt2d's state copies, which Begin
//                         computes from RenderWare's sky state and the camera raster: _gu128Alpha_1 .. _gu128Frame_1
//                         0x67CF80 (5 qw), _gGsOffsetXYXY 0x67CD90, the gp block 0x4E28D8..0x4E2910 (current pointer,
//                         prim type, RGBAQ, texture, palette, Z values, GS offsets) and skyTextureRaster 0x4E2860
//   SetTexture 0x219A20   (CGtTexture* a0, palette a1): recorded when it changes the texture or the palette, with the
//                         raster's +0x8C and whether a flush follows (_gpuNLoops set). It writes one TEX0_1 A+D into the
//                         buffer unless the raster is a texture-cache one (+0x8C == 0) with palette <= 0: then it flushes
//                         what is pending and RpSkyTexCacheAccessRaster sets TEX0 in RenderWare's own packet [C asm
//                         0x219A48, 0x219B30, 0x219B74]. The reader pairs the buffer's TEX0 writes with these in order
//   Resume 0x219CA0       state re-emitted into the buffer by the function itself; the current texture becomes
//                         skyTextureRaster without a TEX0 write [C matched]: recorded with that pointer
//   Suspend 0x219D50, End 0x219F20   markers (both restore RenderWare's state and flush)
//   _Dispatch 0x21A4F0    the batch: (_gpu64Current + 8 - buffer) / 16 quadwords from *_gau128Buffer 0x4E1950 [C asm]
//
// Hook mechanism: the adapter observes these entries without changing what runs (native: b3host's watch hook, called
// at a watched function's entry, then the function runs as always). Observing writes no guest memory and changes no
// guest timing (include/b3gameview/gameview.h).
//
// Per rendered frame the events are collected here and published at the next retail poll (b3scene_poll, the same
// point as everything else: the HUD the game drew in the frame before it) as a host-only region of the scene capture's
// delta stream at kHudBase (outside guest RAM, so readers that do not know it never see it): {u32 'HUD1', u32 frames
// published, u32 event bytes, u32 missed flushes}, then the events {u32 kind, u32 payload bytes, payload}. A batch is
// published as a reference (kind 8: {u32 quadwords, u32 address}) to slot k of kSlotBase (one 8 KB slot per batch of
// the frame, the k-th batch in slot k): a batch that grows or shrinks then moves nothing after it, so the delta
// stream carries only the 16-byte blocks that changed (race2: 26.1 MB with the batches inline). The guest
// rasters the frame used are captured from guest RAM as well (header [0, 0xC8), its upload block at +0x24 and every
// DMAref'd packet; only when the header changed), so every texture decodes, the font included (it is not in a TXD).
#include "b3gameview/gameview.h"

#include <algorithm>
#include <iterator>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kBegin = 0x0021A0F0U, kSetTexture = 0x00219A20U, kResume = 0x00219CA0U, kSuspend = 0x00219D50U,
                        kEnd = 0x00219F20U, kDispatch = 0x0021A4F0U;
constexpr std::uint32_t kBufferPtr = 0x004E1950U;    // _gau128Buffer (pointer to the 512-qw buffer)
constexpr std::uint32_t kGpBlock = 0x004E28D8U;      // _gpu64Current .. _gnGSOffY (0x38 bytes)
constexpr std::uint32_t kCurrent = 0x004E28D8U, kCurTexture = 0x004E28F0U, kCurPalette = 0x004E28F4U;
constexpr std::uint32_t kSkyTexture = 0x004E2860U;   // skyTextureRaster (gp 0x4E8670 - 0x5E10)
constexpr std::uint32_t kStateCopies = 0x0067CF80U;  // _gu128Alpha_1, Tex1_1, Clamp_1, Scissor_1, Frame_1
constexpr std::uint32_t kGsOffset = 0x0067CD90U;     // _gGsOffsetXYXY
constexpr std::uint32_t kHudBase = 0x40000000U, kHudMax = 0x00100000U;   // host-only region, 1 MB
constexpr std::uint32_t kSlotBase = 0x40100000U, kSlotSize = 0x2000U, kSlots = 1024U;   // batch slots (512 qw each)

enum Kind : std::uint32_t { kEvBegin = 1, kEvState = 2, kEvTexture = 3, kEvResume = 4, kEvSuspend = 5, kEvEnd = 6, kEvBatch = 7,
                            kEvBatchRef = 8, kEvScene = 9, kEvObject = 10, kEvMarker = 11, kEvVideo = 12, kEvFeCar = 13,
                            kEvGlobe = 14, kEvTicker = 15 };

// The front end's 3D (menu1, §22). CB3FrontEnd::Render 0x1312D0 (E map [high]) calls CB3World::RenderFrontEnd 0x275EF0
// (E map [high]; $a0 gWorld, $a1 the car index) on the car pages, before the menu pages' 2D; it draws the backdrops of
// the static track, then the race car *(gWorld + 0x12AD28 + 4 index) twice through CB3RaceCar::Draw (a floor
// reflection: up row and position y negated, other lighting; then the car with the lighting it leaves at +0x5C), then
// RenderFrontEndShadow [C asm per the decomp's matched 0x275EF0; read 2026-11-04]. The camera is viewport 1
// (graphics manager +0x90), which the car view component's orbit attaches its CGtCamera to (AttachCameraToViewport
// 0x19EA70) [L]: the scene capture's camera already. Recorded at the entry: a FECAR event {index, car, viewport 1's
// RwCamera}; at the next poll the car is grabbed as a race car (b3scene_grab_vehicle + [0x18E0, 0x19A0)).
constexpr std::uint32_t kRenderFrontEnd = 0x00275EF0U;
constexpr std::uint32_t kWorldCars = 0x00522660U + 0x12AD28U;
// padpaint: the car select page (tools/native/car_select.h kCarSelectPage: its player at +0x18) and the menus' current
// page word (tools/native/fast_load.h kMenuCurrentPage)
constexpr std::uint32_t kCarSelectPage = 0x01E94EF0U, kMenuCurrentPage = 0x01E90430U;
// The World Tour globe: CB3Globe2dObject::_Render 0x3B2EE0 (an object callback, hooked below) draws through B3Im3d's
// own VIF packets between Gt2d Suspend / Resume, so its triangles never reach Gt2d's buffer [L draft]. Recorded for a
// native globe (not drawn yet): a GLOBE event {object, data block [$a1, +0x18) (position, latitude / longitude u16,
// shape u16 x 2, alpha, reveal, highlight amount and region), the object space [$a0, +0x20) (colour at +0x10),
// krB3GlobeSpherePosition 0x4CA140 (4 words), the textures D_01EA4608 (surface), D_01EA460C, D_01EA4618, D_01EA4614
// (highlight regions), D_01EA461C (halo), viewport 0 (graphics manager +0x310) +0x00 x 16, +0x48..+0x54, +0x84, +0x88};
// the five rasters are grabbed like the HUD's [L a reading of the drafts, 2026-11-04].
constexpr std::uint32_t kGlobeRender = 0x003B2EE0U;
constexpr std::uint32_t kGlobeTextures[5] = {0x01EA4608U, 0x01EA460CU, 0x01EA4618U, 0x01EA4614U, 0x01EA461CU};
constexpr std::uint32_t kGlobeSpherePos = 0x004CA140U, kViewport0 = 0x00665EC0U + 0x310U;

// Movies (menu1, docs/native-renderer.md §22): CGt2dObjectVideoImage::_Render 0x326960 (one of the object callbacks
// below) draws the decoder's current picture as 64 x 64 tiles: per tile Suspend, CGtVideoImage::SetTileTexture
// 0x325BD0 (an IMAGE transfer of four 16 x 64 strips straight from RAM into VRAM, in RenderWare's own packet, not
// Gt2d's buffer: so the tiles reach the reader without a texture), Resume, RenderSpritesTex [L draft
// src/nonmatching/_Render__21CGt2dObjectVideoImage...; SetTileTexture's arithmetic C from its near-miss draft]. Its data
// ($a1 = object + 0x28) +0x10 is the CGtVideoDecoder; GetImage 0x324F10 returns decoder + 0x18 x (decoder +0x34) [C
// matched], a slot {+0x00 picture, +0x04 64-px tiles across, +0x08 tiles down, +0x0C macroblock columns, +0x10
// macroblock rows, +0x14 VRAM block} filled by Prepare 0x3254F0 [L draft]; the picture is RGBA32 in 16-pixel-wide
// strips the full height (strip c at picture + c x rows x 1024, 64 bytes a row) [L SetTileTexture]. Update 0x325030
// decodes into slot +0x30 and shows slot +0x34, +0x38 counts pictures [L draft]. So at the callback's entry the shown
// picture is complete in RAM, whoever decoded it.
// Recorded: a VIDEO event {object, decoder, slot index, the slot's 6 words, decoder +0x30, +0x38, +0x3C, +0x40, +0x44,
// +0x48, 0, the picture's host slot (~0: not grabbed), its bytes, data +0x00, +0x04 (the picture's size in tiles,
// floats), data +0x14 (byte)}. With B3PORT_SCENE_MOVIE (=N: every Nth poll; default 1
// with the live feed, off for capture files: 27 MB/s at 30 pictures a second) the picture itself is copied at that
// moment (EeMemory's const view: no side effects) and published at kMovieBase + k x kMovieSlot.
constexpr std::uint32_t kVideoRender = 0x00326960U;
constexpr std::uint32_t kMovieBase = 0x40A00000U, kMovieSlot = 0x00140000U, kMovieSlots = 4U;   // 4 x 1.25 MB (640 x 480 x 4 fits)

// The front end's ticker (menu-font, 2026-11-11): CB3ScrollingText2dObject::_Render 0x31C990 (an object callback, hooked
// below) lays its text out itself, word by word, only over its clip region (16..623 on a 4:3 screen): each frame it
// starts at cursor x = left - (100 x data +0x04 - 608, wrapped by data +0x08) x scale.x, skips the words that end left of
// the strip and stops past its right end; scale = kHelpBarRecipSize (0x4C51F0, 2 floats) x the object space's size;
// the glyphs come from the font *D_00665ED0 (= graphics manager +0x10) [C decomp B3HelpBarComponent_0031C990.cpp and
// the _BuildBatchData draft]. The PS2 never draws the words outside that strip, so a wide window cannot show them by
// widening the clip; the renderer lays the ticker out again over the widened bar (view.cpp, the panel layout). Recorded:
// a TICKER event {object, data +0x00 (the text's address), +0x04, +0x08, +0x0C (floats as words), kHelpBarRecipSize x,
// y, the font's texture (CGt2dFont +0x04), the text's length n (at most 1024), then its n UTF-16 codes (padded)}.
constexpr std::uint32_t kScrollingTextRender = 0x0031C990U;
constexpr std::uint32_t kHelpBarRecipSize = 0x004C51F0U, kTickerFont = 0x00665ED0U;

// World-anchored HUD markers (hud4, docs/native-renderer.md §17.13): CB3RacePosition::DrawPositionMarker 0x1531C0 [C name,
// decomp tables] draws the marker over another car (the near arrow, a downward flat triangle through
// Gt2dRenderer::RenderTriStripFlatUntex, or the car's rank text / label / bar through stack CGt2dObjects whose callbacks
// are hooked above). Its caller CB3RacePosition::Draw 0x154350 passes the world point by address in $a1: the car's
// physics matrix position (gWorld+0x3C98C+0x30 i -> +0x204 -> +0x30) plus the vehicle data's +0xE80 y (roof height), the
// rank slot in $a3, the CB3RaceCar in $t0, the viewing player's car in $t1 [L decomp drafts src/nonmatching
// Draw__15CB3RacePositionFi.cpp / DrawPositionMarker...; argument registers C from the asm]. The marker projects it through
// the active viewport *D_00666270: p = point x viewport +0x00 (4 rows), screen = (p.x / p.z x +0x78, p.y / p.z x +0x7C),
// culled by the near / far planes +0x84 / +0x88 [L draft]. Recorded: {pc, byte offset of the next write (as OBJECT), slot,
// car, player, points mode ($t3), world xyz, viewport, viewport +0x00 x 16, +0x48 x 4, +0x78, +0x7C, +0x84, +0x88}, so
// b3replay can project the point again with each displayed frame's camera.
constexpr std::uint32_t kDrawPositionMarker = 0x001531C0U;
constexpr std::uint32_t kActiveViewport = 0x00666270U;   // D_00666270: CGtViewport* [L drafts]

// Component identity (hud2). CGt2dScene::Render 0x1D75C0 walks its layers ({flags, ?, object list, clip frame}, 16 bytes
// at +0x2C, count +0x30) and calls each object's render callback (object +0x44) with (object space, object + 0x28); the
// object's frame is +0x40 [Ghidra draft 0x1D75C0, L]. Frames: +0x20 world space (position, scale, colour), +0x48 parent
// [C Gt2dFrameHierarchy.cpp, matched]. CB3HUDManager::Render 0x1A0790 renders one scene per player HUD: CB3HUD i at
// gHUDManager + 0x70 i, its scene at +0x14, count at manager +0x170 [C matched]; a CB3HUD's components are listed from
// +0x68 (next +0xC, mode flags +0x14) [C CB3HUD::Update matched], and CB3HUDComponent::Prepare 0x19F6A0 stores the align
// at +0x18 and the player at +0x1C, and makes the component's frame (+0x8) a child of the HUD's root frame (+0x28) at
// GetAlignPos(align) = rect +0x48 + (rect +0x50 - rect +0x48) x D_004B7650[align] (align 8: (0, 0)) [C matched].
// CGt2dScene::Render 0x1D75C0 itself is not hooked (an entry hook there saw no calls [C hud2 race2 run: 0 calls
// seen]); the object callbacks it calls through jalr are. So the HUD scenes are snapshotted at
// CB3HUDManager::Render 0x1A0790, which renders every player's scene in turn [C matched].
constexpr std::uint32_t kHudManagerRender = 0x001A0790U;
constexpr std::uint32_t kHudManager = 0x0066A3D0U;   // gHUDManager [L E map, medium]; checked: its HUD scenes are the scenes rendered
constexpr std::uint32_t kAlignTable = 0x004B7650U;   // D_004B7650: 9 x CGtV2d, filled at run time (zero in the ELF [C])
// Every symbol with the object render callback's signature (const CGt2dObject::CGt2dObjectSpace&, const void*) in the
// decomp's name tables, less the two _BuildBatchData helpers (decomp config/symbol_addrs_b3names.txt) and the two
// CGt2dObjectParagraph callbacks 0x2F8C50 / 0x2F8ED0 (left out: 0 calls in race2 [C hud2 run]). A HUD
// scene object whose callback is not here is counted (unhooked_objects) and named once in the log.
constexpr std::uint32_t kObjectRender[] = {
    0x001A5740U, 0x001A6290U, 0x001D5EA0U, 0x001D5FB0U, 0x001D6170U, 0x001D63F0U, 0x001D65A0U, 0x001D66A0U, 0x001D6E80U,
    0x001D7100U, 0x0031A300U, 0x0031B130U, 0x0031C990U, 0x00326960U, 0x0033C990U, 0x0033CCD0U,
    0x0033CE80U, 0x0033CF80U, 0x0033ED30U, 0x00352AB0U, 0x00371BE0U, 0x003720F0U, 0x003B2EE0U, 0x003D2FD0U, 0x003D5D00U,
    0x0041E120U, 0x00380250U, 0x003A2850U, 0x003CED40U, 0x003CF300U, 0x003CFE40U, 0x003D6050U, 0x003D7B40U,
    // unnamed in every table, found as HUD scene objects' callbacks by this hook's own check (race2 / roadrage1 runs
    // hud2b, hud2c) [C]: func_00381030, func_00381AB0, func_003D62A0, func_003D6C30, func_003D76C0, func_003CD920
    0x00381030U, 0x00381AB0U, 0x003D62A0U, 0x003D6C30U, 0x003D76C0U, 0x003CD920U};
bool object_render(std::uint32_t pc) { return std::find(std::begin(kObjectRender), std::end(kObjectRender), pc) != std::end(kObjectRender); }

bool env_off(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && std::strcmp(v, "0") == 0;
}
bool env_set(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != 0;
}

struct Hud {
    bool on = false;
    std::vector<std::uint8_t> pending;      // this frame's events
    std::vector<std::uint8_t> published;    // header + events as last published
    std::set<std::uint32_t> textures;       // rasters this frame referenced
    bool want_state = false;
    std::uint32_t last_cur = 0;             // _gpu64Current at the last observation
    std::uint32_t seen_pc = 0, seen_cur = 0, seen_a0 = 0, seen_a1 = 0, seen_a3 = 0, seen_t0 = 0;   // the last observation (a resolver called
                                                                         // twice for one call records it once)
    std::uint64_t frames = 0, events = 0, batches = 0, batch_bytes = 0, missed = 0, truncated = 0, max_bytes = 0;
    std::uint64_t texture_grabs = 0;
    std::uint64_t scenes = 0, hud_scenes = 0, objects = 0, unhooked_objects = 0, unseen_objects = 0, markers = 0;
    std::set<std::uint32_t> expected;       // this scene's objects with hooked callbacks, not seen yet
    std::set<std::uint32_t> unhooked_fns;   // named once in the log
    // menu1: movie pictures copied this frame (picture address -> host slot), published at the next poll
    std::uint32_t movie_every = 0;          // B3PORT_SCENE_MOVIE: 0 off, N: grab on every Nth poll
    std::uint64_t polls = 0, videos = 0, movie_grabs = 0;
    std::vector<std::uint32_t> movie_src;   // per host slot: the picture address (this frame)
    std::vector<std::vector<std::uint8_t>> movie;   // per host slot: its bytes
    std::vector<std::uint32_t> fe_cars;     // menu1: cars RenderFrontEnd drew this frame (grabbed at the next poll)
    std::uint64_t fe_car_calls = 0, globes = 0;
};
Hud& hud() { static Hud& h = *new Hud; return h; }   // gsoff1: never destroyed (read at exit, b3hud_report)

bool ram(std::uint32_t a, std::uint32_t n) { return a >= 0x00100000U && a < 0x02000000U && n <= 0x02000000U - a; }
// The Gt2d buffer is in the scratchpad (*0x4E1950 = 0x70002000 in races [C capture]); read as plain memory
// (no device state).
bool spr(std::uint32_t a, std::uint32_t n) { return a >= 0x70000000U && a < 0x70004000U && n <= 0x70004000U - a; }

void put(std::vector<std::uint8_t>& b, std::uint32_t v) {
    const std::size_t n = b.size();
    b.resize(n + 4);
    std::memcpy(b.data() + n, &v, 4);
}
void event(Hud& h, std::uint32_t kind, const std::uint8_t* p, std::uint32_t n) {
    put(h.pending, kind);
    put(h.pending, n);
    h.pending.insert(h.pending.end(), p, p + n);
    while (h.pending.size() & 3U) h.pending.push_back(0);
    ++h.events;
}
void read_bytes(const b3gv::Memory& m, std::uint32_t a, std::uint32_t n, std::vector<std::uint8_t>& out) {
    out.resize(n);
    for (std::uint32_t k = 0; k < n; k += 4) {
        const std::uint32_t w = m.u32(a + k);
        std::memcpy(out.data() + k, &w, 4);
    }
}

// One HUD scene at CB3HUDManager::Render's entry: {scene, manager ($a0), HUD index, component count, the HUD's rect
// (+0x48, +0x50), its root frame (+0x28), the align table (18 floats)}, then per component {component, vtable, frame,
// mode flags, align, player}. Also the completeness check of the object events: the objects the scene will call
// (layer flag bit 0 clear) whose callbacks are hooked must each be seen before the next CB3HUDManager::Render.
void observe_scene(Hud& h, const b3gv::Memory& m, std::uint32_t manager, std::uint32_t i) {
    ++h.scenes;
    const std::uint32_t hud_at = manager + 0x70U * i, scene = hud_at + 0x14U;
    std::vector<std::uint32_t> w = {scene, manager, i, 0U};
    for (std::uint32_t k = 0; k < 4; ++k) w.push_back(hud_at ? m.u32(hud_at + 0x48U + 4U * k) : 0U);
    w.push_back(hud_at ? m.u32(hud_at + 0x28U) : 0U);
    for (std::uint32_t k = 0; k < 18; ++k) w.push_back(m.u32(kAlignTable + 4U * k));
    if (hud_at) {
        ++h.hud_scenes;
        std::uint32_t c = m.u32(hud_at + 0x68U);
        for (int n = 0; n < 64 && c != 0 && ram(c, 0x20); ++n) {
            for (const std::uint32_t off : {0x0U, 0x8U, 0x14U, 0x18U}) w.push_back(m.u32(c + off));
            w.insert(w.begin() + std::ptrdiff_t(w.size() - 4), c);
            w.push_back(m.u32(c + 0x1CU) & 0xFFU);
            ++w[3];
            c = m.u32(c + 0x0CU);
        }
    }
    event(h, kEvScene, reinterpret_cast<const std::uint8_t*>(w.data()), std::uint32_t(4 * w.size()));
    const std::uint32_t nl = m.u32(scene + 0x30U), layers = m.u32(scene + 0x2CU);
    for (std::uint32_t l = 0; l < nl && l < 64 && ram(layers + 16U * l, 16); ++l) {
        if (m.u32(layers + 16U * l) & 1U) continue;
        std::uint32_t o = m.u32(layers + 16U * l + 8U);
        for (int n = 0; n < 1024 && o != 0 && ram(o, 0x48); ++n, o = m.u32(o)) {
            const std::uint32_t fn = m.u32(o + 0x44U);
            if (object_render(fn)) { h.expected.insert(o); continue; }
            ++h.unhooked_objects;
            if (h.unhooked_fns.insert(fn).second) std::fprintf(stderr, "[B3HUD] unhooked object render callback 0x%08x\n", fn);
        }
    }
}

// menu1: a movie object's render ($a1 = its data): the decoder's shown slot, and the picture when movies are grabbed.
void observe_video(Hud& h, const b3gv::Memory& m, std::uint32_t data) {
    ++h.videos;
    std::uint32_t w[21] = {data - 0x28U, 0U, ~0U};
    w[16] = ~0U;
    const std::uint32_t dec = ram(data, 0x18) ? m.u32(data + 0x10U) : 0U;
    w[1] = dec;
    if (dec != 0) {   // the data: +0x00 the picture's size in tiles (CGtV2d), +0x14 the backing-sprite flag [L draft]
        w[18] = m.u32(data);
        w[19] = m.u32(data + 4U);
        w[20] = m.u32(data + 0x14U) & 0xFFU;
    }
    if (ram(dec, 0x50)) {
        const std::uint32_t idx = m.u32(dec + 0x34U);
        w[2] = idx;
        if (idx < 2) for (std::uint32_t k = 0; k < 6; ++k) w[3 + k] = m.u32(dec + 0x18U * idx + 4U * k);
        for (std::uint32_t k = 0; k < 6; ++k) w[9 + k] = m.u32(dec + 0x30U + 4U * (k == 0 ? 0U : k + 1U));   // +0x30, +0x38 .. +0x48
        const std::uint32_t pic = w[3], bytes = w[6] * w[7] * 1024U;
        w[17] = bytes;
        if (h.movie_every != 0 && h.polls % h.movie_every == 0 && idx < 2 && bytes != 0 && bytes <= kMovieSlot && ram(pic, bytes)) {
            std::uint32_t slot = 0;
            while (slot < h.movie_src.size() && h.movie_src[slot] != pic) ++slot;
            if (slot == h.movie_src.size() && slot < kMovieSlots) { h.movie_src.push_back(pic); h.movie.emplace_back(); }
            if (slot < h.movie_src.size()) {
                const std::uint8_t* v = m.bytes(pic, bytes);
                if (v != nullptr) h.movie[slot].assign(v, v + bytes);
                else h.movie[slot].assign(bytes, 0);   // pic1: a backend without a view (never so far)
                w[16] = slot;
                ++h.movie_grabs;
            }
        }
    }
    event(h, kEvVideo, reinterpret_cast<const std::uint8_t*>(w), sizeof w);
}

// menu-font: the ticker's render data ($a1 = object + 0x28), for the renderer's wide layout (above, kScrollingTextRender)
void observe_ticker(Hud& h, const b3gv::Memory& m, std::uint32_t data) {
    if (!ram(data, 16)) return;
    const std::uint32_t text = m.u32(data) & 0x01FFFFFFU, font = m.u32(kTickerFont) & 0x01FFFFFFU;
    std::vector<std::uint32_t> w = {data - 0x28U, text, m.u32(data + 4U), m.u32(data + 8U), m.u32(data + 12U),
                                    m.u32(kHelpBarRecipSize), m.u32(kHelpBarRecipSize + 4U), ram(font, 8) ? m.u32(font + 4U) & 0x01FFFFFFU : 0U, 0U};
    std::vector<std::uint16_t> codes;
    for (std::uint32_t k = 0; k < 1024U && ram(text + 2U * k, 2); ++k) {
        const std::uint32_t a = text + 2U * k;
        const std::uint16_t c = std::uint16_t(m.u32(a & ~3U) >> (8U * (a & 3U)));
        if (c == 0) break;
        codes.push_back(c);
    }
    w[8] = std::uint32_t(codes.size());
    if (codes.size() & 1U) codes.push_back(0);
    for (std::size_t k = 0; k < codes.size(); k += 2) w.push_back(std::uint32_t(codes[k]) | std::uint32_t(codes[k + 1]) << 16);
    event(h, kEvTicker, reinterpret_cast<const std::uint8_t*>(w.data()), std::uint32_t(4 * w.size()));
}

// menu1: the globe object's render ($a0 the object space, $a1 its data block).
void observe_globe(Hud& h, const b3gv::Memory& m, std::uint32_t space, std::uint32_t data) {
    ++h.globes;
    std::uint32_t w[1 + 6 + 8 + 4 + 5 + 16 + 4 + 2] = {data - 0x20U};
    std::uint32_t k = 1;
    for (std::uint32_t i = 0; i < 6; ++i) w[k++] = ram(data, 0x18) ? m.u32(data + 4U * i) : 0U;
    for (std::uint32_t i = 0; i < 8; ++i) w[k++] = ram(space, 0x20) ? m.u32(space + 4U * i) : 0U;
    for (std::uint32_t i = 0; i < 4; ++i) w[k++] = m.u32(kGlobeSpherePos + 4U * i);
    for (const std::uint32_t t : kGlobeTextures) {
        const std::uint32_t r = m.u32(t);
        w[k++] = r;
        if (ram(r, 0xC8)) h.textures.insert(r);
    }
    for (std::uint32_t i = 0; i < 16; ++i) w[k++] = m.u32(kViewport0 + 4U * i);
    for (std::uint32_t i = 0; i < 4; ++i) w[k++] = m.u32(kViewport0 + 0x48U + 4U * i);
    w[k++] = m.u32(kViewport0 + 0x84U);
    w[k++] = m.u32(kViewport0 + 0x88U);
    event(h, kEvGlobe, reinterpret_cast<const std::uint8_t*>(w), sizeof w);
}

// One observation at a hooked entry (before its first instruction). Idempotent for a resolver called twice.
void observe(std::uint32_t pc, const std::uint32_t* arg, const b3gv::Memory& m) {
    Hud& h = hud();
    const std::uint32_t cur = m.u32(kCurrent);
    const std::uint32_t buf = m.u32(kBufferPtr);
    const std::uint32_t a0 = arg[0], a1 = arg[1], a3 = arg[3], t0 = arg[4];
    // hud4: $a3 / $t0 too: CB3RacePosition::Draw passes every car's point at the same stack address, so a marker that
    // wrote nothing (culled) and the next car's call agree in pc, pointer, $a0 and $a1
    if (pc == h.seen_pc && cur == h.seen_cur && a0 == h.seen_a0 && a1 == h.seen_a1 && a3 == h.seen_a3 && t0 == h.seen_t0) return;
    h.seen_pc = pc; h.seen_cur = cur; h.seen_a0 = a0; h.seen_a1 = a1; h.seen_a3 = a3; h.seen_t0 = t0;
    // the scene walk runs before the scene's Begin: no flush check, no state
    if (pc == kHudManagerRender) {
        h.unseen_objects += h.expected.size();
        h.expected.clear();
        // the manager is gHUDManager [L E map]; its HUD count +0x170 [C matched Render]
        const std::uint32_t nhud = a0 == kHudManager ? m.u32(a0 + 0x170U) : 0U;
        for (std::uint32_t i = 0; i < nhud && i < 4; ++i) observe_scene(h, m, a0, i);
        return;
    }
    if (pc == kRenderFrontEnd) {
        const std::uint32_t idx = a1, car = idx < 6 ? m.u32(kWorldCars + 4U * idx) : 0U;
        // padpaint: w[3] whose car it is: 1 + the car select page's player while that page is the current one, else 0.
        // The player is the page's word +0x18 (D_01E94F08): the car select flow writes 0, or the multiplayer turn's
        // field_30 % 2 [C func_00310200 / func_0030ED90], and Update 0x318A80 passes it to the input manager as the
        // port. 0 and 1 are taken exactly; any other value is "not known" (0: the car takes player 1's paint)
        const std::uint32_t page = m.u32(kMenuCurrentPage) & 0x01FFFFFFU;
        const std::uint32_t seat = page == kCarSelectPage ? m.u32(kCarSelectPage + 0x18U) : ~0U;
        const std::uint32_t who = seat == 0U ? 1U : seat == 1U ? 2U : 0U;
        const std::uint32_t w[4] = {idx, car, m.u32(0x00665EC0U + 0x90U + 0x58U), who};
        event(h, kEvFeCar, reinterpret_cast<const std::uint8_t*>(w), sizeof w);
        if (ram(car, 0x27D0) && std::find(h.fe_cars.begin(), h.fe_cars.end(), car) == h.fe_cars.end()) h.fe_cars.push_back(car);
        ++h.fe_car_calls;
        return;   // not a Gt2d function: the write pointer is not checked here
    }
    // A flush we did not see: the write pointer went back without a batch since the last observation.
    if (pc != kBegin && pc != kResume && h.last_cur != 0 && cur != 0 && cur < h.last_cur) ++h.missed;
    if (h.want_state && pc != kBegin) {
        std::vector<std::uint8_t> s, t;
        read_bytes(m, kStateCopies, 0x50, s);
        read_bytes(m, kGsOffset, 0x10, t);
        s.insert(s.end(), t.begin(), t.end());
        read_bytes(m, kGpBlock, 0x38, t);
        s.insert(s.end(), t.begin(), t.end());
        const std::uint32_t sky = m.u32(kSkyTexture);
        s.resize(s.size() + 8, 0);
        std::memcpy(s.data() + 0x50 + 0x10 + 0x38, &sky, 4);
        event(h, kEvState, s.data(), std::uint32_t(s.size()));
        const std::uint32_t curtex = m.u32(kCurTexture);
        if (curtex) h.textures.insert(curtex);
        h.want_state = false;
    }
    if (pc == kDrawPositionMarker) {
        std::uint32_t w[34] = {pc, ~0U, arg[3], arg[4], arg[5], arg[7] & 0xFFU};
        if (cur != 0 && buf != 0 && cur + 8U >= buf && cur + 8U - buf <= 0x2000U) w[1] = cur + 8U - buf;
        if (ram(a1, 12)) for (int k = 0; k < 3; ++k) w[6 + k] = m.u32(a1 + 4U * std::uint32_t(k));
        const std::uint32_t vp = m.u32(kActiveViewport);
        w[9] = vp;
        if (ram(vp, 0x8C)) {
            for (std::uint32_t k = 0; k < 16; ++k) w[10 + k] = m.u32(vp + 4U * k);
            for (std::uint32_t k = 0; k < 4; ++k) w[26 + k] = m.u32(vp + 0x48U + 4U * k);
            w[30] = m.u32(vp + 0x78U);
            w[31] = m.u32(vp + 0x7CU);
            w[32] = m.u32(vp + 0x84U);
            w[33] = m.u32(vp + 0x88U);
        }
        event(h, kEvMarker, reinterpret_cast<const std::uint8_t*>(w), sizeof w);
        ++h.markers;
        h.last_cur = cur;
        return;
    }
    if (object_render(pc)) {
        // {pc, object, byte offset of the next write in the buffer (~0: none), object space position / size, the frame
        // and up to 7 ancestors}
        const std::uint32_t obj = a1 - 0x28U;
        std::uint32_t w[16] = {pc, obj, ~0U};
        if (cur != 0 && buf != 0 && cur + 8U >= buf && cur + 8U - buf <= 0x2000U) w[2] = cur + 8U - buf;
        if (ram(a0, 16)) for (int k = 0; k < 4; ++k) w[3 + k] = m.u32(a0 + 4U * std::uint32_t(k));
        std::uint32_t f = ram(obj, 0x48) ? m.u32(obj + 0x40U) : 0U;
        for (int k = 0; k < 8 && f != 0 && ram(f, 0x4C); ++k) { w[7 + k] = f; f = m.u32(f + 0x48U); }
        event(h, kEvObject, reinterpret_cast<const std::uint8_t*>(w), sizeof w);
        // debugging: B3HUD_TRACE_PC=<hex pc> logs the object events recorded for that callback (frame, object, offset)
        static const std::uint32_t trace_pc = [] { const char* e = std::getenv("B3HUD_TRACE_PC"); return e ? std::uint32_t(std::strtoul(e, nullptr, 16)) : 0U; }();
        if (trace_pc && pc == trace_pc)
            std::fprintf(stderr, "[B3HUD] trace: frame %llu object %08x at byte %d on %d\n", (unsigned long long)h.frames, obj, int(w[2]), int(h.on));
        ++h.objects;
        h.last_cur = cur;
        if (pc == kVideoRender) observe_video(h, m, a1);
        if (pc == kGlobeRender) observe_globe(h, m, a0, a1);
        if (pc == kScrollingTextRender) observe_ticker(h, m, a1);
        return;
    }
    switch (pc) {
    case kBegin:
        if (!h.want_state) event(h, kEvBegin, nullptr, 0);
        h.want_state = true;
        h.last_cur = 0;
        return;
    case kSetTexture: {
        // SetTexture writes TEX0 unless both equal the current ones [C asm 0x219A38, 0x219C28]
        if (a0 == m.u32(kCurTexture) && a1 == m.u32(kCurPalette)) break;
        const std::uint32_t w[4] = {a0, a1, ram(a0, 0x90) ? m.u32(a0 + 0x8C) : 0U, m.u32(0x004E28DCU) != 0U ? 1U : 0U};
        event(h, kEvTexture, reinterpret_cast<const std::uint8_t*>(w), 16);
        if (a0) h.textures.insert(a0);
        break;
    }
    case kResume: {
        const std::uint32_t sky = m.u32(kSkyTexture);
        event(h, kEvResume, reinterpret_cast<const std::uint8_t*>(&sky), 4);
        if (sky) h.textures.insert(sky);
        h.last_cur = 0;
        return;
    }
    case kSuspend:
        event(h, kEvSuspend, nullptr, 0);
        break;
    case kEnd:
        event(h, kEvEnd, nullptr, 0);
        break;
    case kDispatch: {
        if (cur == 0 || buf == 0 || cur < buf || cur - buf > 0x2000U || !(ram(buf, 0x2000U) || spr(buf, 0x2000U))) break;
        const std::uint32_t qwc = (cur - buf + 8U) >> 4;
        if (qwc == 0) break;
        std::vector<std::uint8_t> b;
        read_bytes(m, buf, qwc * 16U, b);
        b.insert(b.begin(), 4, 0);
        std::memcpy(b.data(), &qwc, 4);
        event(h, kEvBatch, b.data(), std::uint32_t(b.size()));
        ++h.batches;
        h.batch_bytes += qwc * 16U;
        h.last_cur = buf;   // _Dispatch resets the pointer to the buffer
        return;
    }
    default:
        break;
    }
    h.last_cur = cur;
}

// A guest raster and everything decode_raster replays (b3asset/raster.h): header [0, 0xC8), the upload block at +0x24
// ({transfers, ...} + 4 qw per transfer: DMAcnt + GIF BITBLTBUF, DMAref of the TRXPOS/TRXREG/TRXDIR + IMAGE packets,
// absolute addresses in RAM [RW 3.1 sky2 texcache.c: the ref tag is sent as is]); all of it only when the header's
// stable fields changed: size / depth / format [0x0C, 0x24), the upload pointer +0x24, the palette table
// +0x90 and the name +0xA8 (the sky extension's DMA and cache counters change with every use).
void grab_texture(Hud& h, std::uint32_t t) {
    if (!ram(t, 0xC8)) return;
    if (b3scene_unchanged(t + 0x0C, 0x1C) && b3scene_unchanged(t + 0x90, 0x10) &&
        b3scene_unchanged(t + 0xA8, 0x20)) return;
    b3scene_grab(t, 0xC8);
    ++h.texture_grabs;
    const std::uint32_t up = b3scene_u32(t + 0x24) & 0x0FFFFFFFU;
    if (!ram(up, 16)) return;
    const std::uint32_t n = b3scene_u32(up);
    if (n == 0 || n > 16 || !ram(up, 16 + 64 * n)) return;
    b3scene_grab(up, 16 + 64 * n);
    for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint32_t ref = b3scene_u32(up + 16 + 64 * k + 48), src = b3scene_u32(up + 16 + 64 * k + 52) & 0x0FFFFFFFU;
        const std::uint32_t qwc = ref & 0xFFFFU;
        if (((ref >> 28) & 7U) == 3U && qwc && ram(src, qwc * 16)) b3scene_grab(src, qwc * 16);
    }
}
}  // namespace

// ---- pic1: the entries, independent of the backend ----
namespace {
struct Points {
    std::vector<std::uint32_t> pcs;
    std::string who;
};
Points& points() {
    static Points& p = *new Points([] {
        Points q;
        // off without an output or with B3PORT_SCENE_HUD=0
        if (!(env_set("B3PORT_SCENE_OUT") || env_set("B3PORT_SCENE_LIVE") || env_set("B3PORT_RENDER")) || env_off("B3PORT_SCENE_HUD")) return q;
        q.pcs = {kBegin, kSetTexture, kResume, kSuspend, kEnd, kDispatch};
        if (!env_off("B3PORT_SCENE_HUD_OBJECTS")) {   // hud2's component identity: the scene walk and the object callbacks
            q.pcs.push_back(kHudManagerRender);
            if (!env_off("B3PORT_SCENE_HUD_MARKERS")) q.pcs.push_back(kDrawPositionMarker);   // hud4
            if (!env_off("B3PORT_SCENE_FRONTEND")) q.pcs.push_back(kRenderFrontEnd);           // menu1
            q.pcs.insert(q.pcs.end(), std::begin(kObjectRender), std::end(kObjectRender));
        }
        return q;
    }());
    return p;
}
b3gv::PublishHook g_publish_override = nullptr;
void* g_publish_ctx = nullptr;
}  // namespace

const std::uint32_t* b3gv::entry_points(std::size_t* count) {
    const Points& p = points();
    *count = p.pcs.size();
    return p.pcs.data();
}

std::uint32_t b3gv::hud_enable(std::size_t hooked, std::size_t wanted, const char* who) {
    Hud& h = hud();
    h.on = wanted != 0 && hooked == wanted;
    // menu1: movie pictures: B3PORT_SCENE_MOVIE=N grabs on every Nth poll, 0 never; default every poll with the live
    // feed or the in-process renderer, never for a capture file alone
    if (const char* mv = std::getenv("B3PORT_SCENE_MOVIE"); mv != nullptr && *mv) h.movie_every = std::uint32_t(std::strtoul(mv, nullptr, 10));
    else h.movie_every = env_set("B3PORT_SCENE_LIVE") || env_set("B3PORT_RENDER") ? 1U : 0U;
    points().who = who ? who : "";
    return h.movie_every;
}

bool b3gv::hud_on() { return hud().on; }

void b3gv::on_entry(std::uint32_t pc, const std::uint32_t arg[8], const b3gv::Memory& m) { observe(pc, arg, m); }

void b3gv::set_hud_publish_override(b3gv::PublishHook hook, void* ctx) { g_publish_override = hook; g_publish_ctx = ctx; }


// Called from b3scene_poll (the capture writer is valid): publishes the events of the frame(s) since the last poll.
void b3hud_publish() {
    if (g_publish_override) { g_publish_override(g_publish_ctx); return; }   // pic1: the replay's pass-through
    Hud& h = hud();
    if (!h.on) return;
    ++h.polls;
    // the backend's host records (gameview_native.cpp publish_host), each when it changed
    if (const b3gv::Host& host = b3gv::host(); host.extra_publish) host.extra_publish();
    if (h.pending.empty()) return;
    // menu1: this frame's movie pictures to their host slots (only the 16-byte blocks that changed go out)
    for (std::size_t k = 0; k < h.movie.size(); ++k)
        if (!h.movie[k].empty()) b3scene_grab_host(kMovieBase + kMovieSlot * std::uint32_t(k), h.movie[k].data(), std::uint32_t(h.movie[k].size()) & ~15U);
    h.movie_src.clear();
    h.movie.clear();
    // menu1: the front end's car(s), grabbed as the race cars are (its lighting at +0x5C is the real pass's here)
    for (const std::uint32_t car : h.fe_cars) {
        b3scene_grab_vehicle(car, car + 0x80U);
        b3scene_grab(car + 0x18E0U, 0xC0U);
    }
    h.fe_cars.clear();
    ++h.frames;
    for (const std::uint32_t t : h.textures) grab_texture(h, t);
    h.textures.clear();
    // batches to their slots; the index keeps a reference
    std::vector<std::uint8_t> index;
    index.reserve(h.pending.size() / 4);
    std::uint32_t slot = 0;
    for (std::size_t at = 0; at + 8 <= h.pending.size();) {
        std::uint32_t kind, n;
        std::memcpy(&kind, h.pending.data() + at, 4);
        std::memcpy(&n, h.pending.data() + at + 4, 4);
        const std::size_t next = at + 8 + ((n + 3U) & ~3U);
        if (kind == kEvBatch && n >= 4 && slot < kSlots) {
            std::uint32_t qwc;
            std::memcpy(&qwc, h.pending.data() + at + 8, 4);
            qwc = std::min(qwc, std::min<std::uint32_t>((n - 4) / 16, kSlotSize / 16));
            const std::uint32_t addr = kSlotBase + kSlotSize * slot++;
            b3scene_grab_host(addr, h.pending.data() + at + 12, qwc * 16);
            const std::uint32_t r[4] = {kEvBatchRef, 8, qwc, addr};
            index.insert(index.end(), reinterpret_cast<const std::uint8_t*>(r), reinterpret_cast<const std::uint8_t*>(r) + 16);
        } else {
            index.insert(index.end(), h.pending.begin() + std::ptrdiff_t(at), h.pending.begin() + std::ptrdiff_t(std::min(next, h.pending.size())));
        }
        at = next;
    }
    std::vector<std::uint8_t>& p = h.published;
    p.clear();
    std::uint32_t bytes = std::uint32_t(index.size());
    if (bytes > kHudMax - 16) { bytes = 0; ++h.truncated; }   // never seen; the reader then shows no HUD for it
    h.max_bytes = std::max<std::uint64_t>(h.max_bytes, bytes);
    put(p, 0x31445548U);   // 'HUD1'
    put(p, std::uint32_t(h.frames));
    put(p, bytes);
    put(p, std::uint32_t(h.missed));
    p.insert(p.end(), index.begin(), index.begin() + bytes);
    while (p.size() & 15U) p.push_back(0);
    b3scene_grab_host(kHudBase, p.data(), std::uint32_t(p.size()));
    h.pending.clear();
}

void b3hud_report() {
    const Hud& h = hud();
    if (!h.on) return;
    std::fprintf(stderr, "[B3HUD] %llu frames, %llu events, %llu batches (%llu bytes), largest frame %llu bytes, %llu missed flushes,"
                 " %llu truncated, %llu texture grabs; %llu scenes (%llu HUD), %llu object calls, %llu objects with unhooked"
                 " callbacks, %llu hooked objects not seen; %llu markers; %llu movie renders, %llu pictures grabbed; %llu front-end car renders, %llu globes\n", (unsigned long long)h.frames, (unsigned long long)h.events,
                 (unsigned long long)h.batches, (unsigned long long)h.batch_bytes, (unsigned long long)h.max_bytes,
                 (unsigned long long)h.missed, (unsigned long long)h.truncated, (unsigned long long)h.texture_grabs,
                 (unsigned long long)h.scenes, (unsigned long long)h.hud_scenes, (unsigned long long)h.objects,
                 (unsigned long long)h.unhooked_objects, (unsigned long long)(h.unseen_objects + h.expected.size()),
                 (unsigned long long)h.markers, (unsigned long long)h.videos, (unsigned long long)h.movie_grabs,
                 (unsigned long long)h.fe_car_calls, (unsigned long long)h.globes);
    if (const b3gv::Host& host = b3gv::host(); host.extra_report) host.extra_report();
    std::fflush(stderr);
}
