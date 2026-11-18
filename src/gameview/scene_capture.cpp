// Scene capture for the native renderer (docs/native-renderer.md §12, tools/render b3replay), backend-independent since
// pic1 (§32; include/b3gameview/gameview.h), with the guest memory (b3gv::Memory), the schedule stamp (b3gv::Host)
// and the renderer library (dlopen on Linux / macOS as well as LoadLibrary on Windows). Adapters call b3gv::on_poll once per retail poll: the native
// backend at UpdatePort(0)'s return (tools/native/gameview_native.cpp), the replay from a .b3scene (gameview_replay).
//
// When. Once per retail controller poll. The poll is the input update inside the game's fixed 1/60 s tick loop
// (docs/frame-rate.md "The main loop"), so one record = one simulation tick. The state read at poll k is the state the
// game rendered in the frame before it: ticks, then UpdatePostSimulation (camera), then render, then the next frame's
// ticks. A rendered frame of N ticks shows as N records whose camera only changes at the first of them.
//
// What. Guest main RAM only (addresses below 32 MiB; never MMIO, scratchpad or anything with read side effects), read
// with the guest's own pointers each poll, using the decomp's layouts (retail addresses; tags as in the doc):
//   CB3Game frame fields 0x51BA80..0x51BAD0 (ticks per frame +0x2DA84 etc.)          [C frame-rate.md]
//   gWorld 0x522660 [0, 0x60) (its CGtTimer)                                         [C retail literal 0x522660 in Draw__16CB3TrafficSystem]
//   graphics manager 0x665EC0 [0, 0x3C0): viewports +0x90 / +0x130 / +0x310, params +0x1D0 / +0x270, the open
//     viewport +0x3B0; and its four camera records +0x470 (0x230 each)              [C OpenViewport 0x227360, DoUnitFrustumTesting]
//   each viewport's CGtCamera (+0x68): fov +0x4, quat +0x10, position +0x20          [C CGtViewport::Update 0x211ED0]
//     and its RwCamera (+0x58) [0, 0xA0) with the RwFrame (+0x4) [0, 0x90): modelling matrix +0x10 (the camera's
//     right/up/at/pos as CGtViewport::Update wrote them), LTM +0x50              [C Update; L RW 3.x layouts]
//   race cars: count gWorld+0x12AD1C, CB3RaceCar* [6] gWorld+0x12AD28               [C RenderRaceCars 0x199A20]
//     per car [0, 0x80) (CB3VehicleEssentialParams: matrix, vehicle data +0x40, lit bits +0x58, paint +0x59),
//     CB3VehicleParams at +0x80: wheels/panels [0, 0x380), states [0x4A0, 0x500), glass/deform [0x1000, 0x1030),
//     deform matrices [0x500, 0xC80) (body 8 at +0x500, panel p 2 at +0x900 + 0x80 p) while +0x101B or a panel's
//     +0x101C[p] is set [C RenderVehicle 0x21E6F0 -> RenderDeformedObject 0x21F790]; Draw fields [0x18E0, 0x19A0)  [C RenderVehicle 0x21E6F0, Draw 0x151A40]
//     vehicle data header [0, 0x70) (model fingerprint for the viewer), its raster tail [0xA0, 0xC8)
//   streamed track 0 gWorld+0x12A348 [0, 0x1CC) (+0x44 on, +0x58 visibility, +0x7C high / +0xC4 LOD block per slot,
//     +0x1C4 current unit's high block, +0x1C8 its backdrop list); each resident block's header [0, 0x60) (unit index +4, 0 = high / 1 = LOD at +0x58)
//                                                                                    [C IsUnitVisibleToMainCamera 0x21CFF0, RenderBeforeVehicles 0x21C780]
//   split screen (split1): streamed track 1 gWorld+0x12A514 the same way, player 2's (CB3World::Render(1))
//   split screen (split2, docs/native-renderer.md §29.6): player 2's camera manager's IsPlayerVisible inputs (0x4EE040 +
//     0x28E00 + 0xBB0, as player 1's below), and view 1's per-vehicle Draw results copied at CB3World::Render(1)'s entry
//     (on_world_render; the host record 'HSV1' at kHostView1Base, published at the next poll)
//   static track 0x64C990 [0, 0x10) and its data header [0, 0x60) (route fingerprint: file size +4, unit count +0x54)
//   traffic gWorld+0x4DE0: 254 CB3TrafficVehicle of 0x180 at +0x127D0; the active ones (+0x174 bit 0):
//     [0x70, 0xF0) (EssentialParams with the 9 SH coefficients +0xCC that Draw's GetIrradiance writes: render6),
//     [0x120, 0x180) (wheel spin/steer, flags), vehicle data header                 [C/L Draw__16CB3TrafficSystem 0x3078F0]
//     and the per-race-car lists of traffic Draw walks [0x35C20, 0x36360), the player's race-car index
//     gWorld+0x12AD50+0x19AC (render6)                                               [L Draw 0x3078F0]
//   crashed vehicles: count 0x64517C, pointers gWorld+0x122A10; per vehicle +0x200 [0, 0x10) (+0x204 the matrix Draw
//     measures the distance from), EssentialParams +0x1360 [0, 0x80) and VehicleParams +0x13E0 as for race cars
//                                                                                    [L Draw__16CB3TrafficSystem]
//   kRightAxis / kAtAxis 0x4B9D80 [0, 0x20) (the vehicle renderer's tilt axes)       [C retail_data_names]
//   kXAxisVector / kYAxisVector 0x4C4950 [0, 0x20) (traffic wheel spin / steer axes, zero in the ELF: set at run time)
//                                                                                    [C E map names, Draw 0x3083A0 / 0x3083FC]
//   props (version 2, props1): the prop manager 0x64A854 [0, 0x9C) (= gWorld+0x1281F4: +0x4 its copy of the route's
//     prop placements, 0x40 each, the matrices the prop draws and the prop bodies use; +0xC dynamic count; +0x10
//     placement count; +0x12 prop-type count; +0x14 u8 count[unit][type]; +0x18 CB3Prop list[unit][type]; +0x1C
//     CB3Prop[32] dynamic list; a CB3Prop is {u16 placement, u8 type, u8 state}: 2 = not drawn)
//                                                                                    [C Prepare 0x2B7300, Render 0x2F99E0, DynamiseProp 0x2B7230, KillProp 0x2B71A0]
//     every per-unit list (counts and CB3Props, unit count from the static track data +0x54), and the 0x40-byte
//     placement of each dynamic-list prop (CB3PropPhysics moves it in place: prop bodies point +0x4 at it)
//                                                                                    [C CB3PhysicsManager::Prepare 0x1BA020..0x1BA0A0, DynamisePropBody 0x1B4530]
//   CB3PhysicsManager 0x55EF70: the prop-body and body-part masks +0xE9218 / +0xE9220, and each CB3PropPhysics in use
//     (+0xC3900 + 0x780 k) [0x200, 0x230) (+0x204 matrix pointer, +0x216, +0x220 its CB3Prop, +0x224 expiry time)
//                                                                                    [C UpdateActiveBodyList 0x1B7F30, func_001BE3C0, PropPhysics Update 0x1BE250]
//   props on or off: D_0051BA88 -> +0x1B4 (the stage logic, CB3*Logic vtables [L names]) -> vtable slot 0xB8 and that
//     function's first 8 bytes; when it returns nonzero CB3PropManager::Render returns at once and
//     CB3PhysicsManager::Prepare builds no prop bodies (CB3OfflineRoadRageLogic's: `jr ra; li v0, 1`)
//                                                                                    [C Render 0x2F99E0, Prepare 0x1B9F44, vtables in retail_data_names]
//   crash-mode game data (crash2, same version; readers detect it by its presence): CB3GameData gWorld+0x125B40
//     [0, 0x590) with its crash pickups and ramps, their data structs and counts, D_00665EA1 (the ramps' B3Im3d
//     switch), player 1's camera manager CGtQuatPos gGame+0x29010     [C CB3GameData::Draw 0x14E8C0, Prepare 0x14F050]
//     and func_0021C010's inputs for the STATIC +0x28 barriers: D_00665E53, the view's race car +0x18EA, the stage
//     logic's vtable slots 0x98 / 0x9C / 0xAC with their code, logic +0x40  [C asm 0x21C010, 0x131E40, 0x1342F0]
//   animated track materials (anim1, same version; docs/native-renderer.md §15): the static track data's update list
//     (+0x10, s16[+0xE]) and each listed CB3TrackMaterial's [0x10, 0x20)        [C CB3StaticTrack::Update 0x3329A0]
//   effects (version 3, fx1; docs/native-renderer.md §14, docs/effects-survey.md §3.1-3.2), grab_effects below:
//     shadows (CB3ShadowMgr::RenderShadows 0x3033F0 -> RenderAmbientShadow 0x303170 -> CB3ShadowRenderer::
//     RenderAmbientVehicleShadow 0x303A40): race car +0x510 (ground normal xyz, distance w); traffic: the view's list
//     (s = gWorld+0x12AD50+0x19AC, count gWorld+0x4DE0+0x36350+s, indices +0x35C24+0xFE s), each one's EssentialParams
//     +0x60 (tv+0xD0) and its distance object (gWorld+0x4DE0+0x343F0)[tv+0x176] +0x18; crashed vehicles +0x1870 and
//     +0x13C0; per vehicle data its box +0xE80 / +0xE90 and wheel positions +0xBB0 + 0x40 k; krShadowDarkness
//     D_004E0FC0; the player camera manager's IsPlayerVisible inputs (0x4EE040+0x28E00: +0x9E0..+0xA10, +0xB80, and
//     the behaviour's +0x120 object +0xB88/+0xB8C)        [C drafts of 0x3033F0, 0x303170, 0x303A40, IsPlayerVisible 0x2E7340, GetTimeElapsed 0x373940]
//     skid marks (CB3TrailSystem at gEffectsManager 0x1D9D200 + 0x2600, rendered by func_002BD560 -> CB3TrailRenderer::
//     Render 0x2F3600): per type 0..4 the emitter list +0x13C0C + 0x208 t (count +0x200), each listed emitter [0, 0x14)
//     when a field other than its data pointers changed, and its point data (+0x8 & ~0x30000000 = half +0x14634 x 0x9800
//     + slot x 0x130: 16 x V4 point/time + 16 x 3 s8); mgaDefaultParams 0x49FFF0
//     (5 x 0x50) and the per-type VU params D_01E865E0 (5 x 0x30)   [C func_002BD560, Render 0x2F3600, AddTrailSegment 0x2BD320]
//   coronas (corona1, same version; readers detect them by their presence), grab_coronas below: the inputs of the boost
//     glow and the car / traffic lights (docs/native-renderer.md §16); B3PORT_SCENE_CORONA_CHECK=1 adds the game's own
//     corona slots (proof runs only)
//   post passes (post1, same version; readers detect it by its presence), grab_post below: the bloom / blur choice inputs
//   HUD / 2D (hud1, same version; readers detect it by its presence): the Gt2dRenderer call stream of the frame drawn
//     before the poll and the guest rasters it used, recorded at function entries by hud_capture.cpp (header there)
//     and written here as the host-only region 0x40000000 (outside guest RAM) plus the rasters' guest bytes
//
// Format (little endian): "B3SCENE\0", u32 version (1; 2 = with props; 3 = with effects; same records), u32 0; then per poll
//   u32 'TICK' (0x4B434954), u32 poll, u32 vblank, u32 run count, runs: { u32 address, u32 length, bytes[length] }
// A run is a stretch of 16-byte blocks whose bytes changed since they were last written (or were never written), so the
// file is a delta stream of a sparse guest memory image: a reader that applies every run up to poll k holds exactly the
// bytes this hook read at poll k, and decodes them with the same pointers and layouts (b3replay does).
//
// Read only: nothing here writes guest memory or changes host timing beyond the reads and the file write (replays are
// paced by guest EE cycles, so host time does not reach the guest; docs/native-renderer.md §12).
//
// Live feed (live1, docs/native-renderer.md §13): B3PORT_SCENE_LIVE=<name> also (or instead of the file) appends each
// poll's record to the shared-memory ring Local\<name> (include/b3live/ring.h) that the live renderer (b3replay --live
// <name>, its own process) reads. The ring never makes the game wait: a reader that falls behind loses records and asks
// for a keyframe (want_key), which the next poll sends as every block of the shadow image instead of the delta.
//
// In-process renderer (inproc1, docs/native-renderer.md §23): B3PORT_RENDER=<b3render.dll> loads the native renderer
// into this process (only then: LoadLibrary at the first poll) and hands it the same record every poll through its C
// ABI (b3r_submit: a copy onto a lock-free list; never a wait), with B3PORT_RENDER_ARGS as its command line (b3replay's
// live options). The records are the ones the file and the ring get; nothing else is read. b3scene_finish closes the
// feed and waits (B3PORT_RENDER_WAIT_MS, default 120000) for the renderer to finish its shots and summaries.
#include "b3live/ring.h"
#include "b3gameview/gameview.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

b3gv::Host g_host;   // pic1: the adapter's callbacks (lat1 stamp, extra publishers)

// split2 (docs/native-renderer.md §29.6): view 1's per-vehicle results in a 2-player split screen. RenderTwoPlayer
// 0x276870 runs Render(0) then Render(1) [C B3WorldPS2.cpp matched]; CB3TrafficSystem::Draw(lePlayer) writes each
// listed traffic car's unit +0xCA, frustum +0xCB and camera-inside bit +0x17A and each crashed vehicle's +0x13BA /
// +0x13BB [L draft Draw__16CB3TrafficSystemFi], RenderRaceCars(view) each listed race car's +0x5A / +0x5B / +0x10A2
// [L draft], so the poll sees view 2's. At Render(1)'s entry they are view 1's: copied then (reads only) and published
// at the next poll as a host record at kHostView1Base: {u32 'HSV1', u32 poll, u32 entries, 0}, then per entry {u32
// guest address (traffic vehicle / physics object / race car), u8 unit, s8 frustum, u8 flags +0x17A (traffic) /
// +0x10A2 (race cars), 0}; a poll with no render since the last one republishes the last copy. Only in split frames,
// so one-player records are unchanged.
constexpr std::uint32_t kHostView1Base = 0x41120000U;
std::vector<std::uint8_t> g_view1;   // the copy (header included); empty: none since the last poll

constexpr std::uint32_t kRamEnd = 0x02000000U;   // 32 MiB main RAM
constexpr std::uint32_t kPage = 0x1000U;

struct Scene {
    std::FILE* out = nullptr;
    // shadow image of what was written: page -> bytes + per-16-byte-block "written" bits
    struct Page { std::uint8_t bytes[kPage]; std::uint8_t valid[kPage / 16]; };
    std::map<std::uint32_t, Page> pages;
    std::vector<std::uint8_t> buf;      // this poll's record body
    std::uint32_t runs = 0;
    std::uint64_t records = 0, bytes_written = 0;
    std::uint8_t (*read)(const void*, std::uint32_t) = nullptr;
    const void* memory = nullptr;
    // inproc1: a const view of main RAM (checked by the adapter); null: the per-byte callback
    const std::uint8_t* ram = nullptr;
    std::uint32_t ram_size = 0;
    b3live::Mapping* live = nullptr;    // B3PORT_SCENE_LIVE: the live renderer's ring
    std::vector<std::uint8_t> key;      // keyframe body (live only)
    std::uint64_t live_dropped = 0;
    // B3PORT_RENDER (inproc1): b3render.dll's C ABI
    struct Render {
        int (*want_key)() = nullptr;
        int (*submit)(std::uint32_t, const void*, std::size_t) = nullptr;
        int (*submit_at)(std::uint32_t, const void*, std::size_t, std::uint64_t) = nullptr;   // lat1 (optional)
        int (*close)(std::uint32_t) = nullptr;
        int (*perf_on)() = nullptr;   // perf3 (optional): B3_PERF_LOG's game-thread stages (b3r_perf_on / b3r_perf_add)
        void (*perf_add)(int, std::uint64_t) = nullptr;
    } render;
    bool inproc = false;
    std::uint64_t inproc_dropped = 0;
    // the game thread's cost of the hand-over (body + b3r_submit), host steady clock only (never reaches the guest)
    std::uint64_t inproc_ns = 0, inproc_max_ns = 0, inproc_polls = 0, inproc_keys = 0;
    std::uint64_t hook_ns = 0, hook_max_ns = 0;   // the whole per-poll hook (guest reads + records), same clock
    // B3PORT_SCENE_TIMES=<csv> (inproc1): per poll the whole hook's and the hand-over's time and the record size, kept in
    // memory and written at the end (no I/O on the game thread meanwhile)
    struct Time { std::uint32_t poll, hook_ns, handover_ns, bytes; };
    std::vector<Time> times;
    bool time_polls = false;
    std::uint64_t last_handover_ns = 0;
    std::uint32_t inproc_max_poll = 0;
};

// inproc1: loads B3PORT_RENDER and starts it with B3PORT_RENDER_ARGS. Aborts if it cannot (the run asked for it).
bool open_render(Scene::Render& r, const char* dll) {
#ifdef _WIN32
    const int wn = MultiByteToWideChar(CP_UTF8, 0, dll, -1, nullptr, 0);
    std::wstring w(wn > 0 ? std::size_t(wn) : 1, L'\0');
    if (wn > 0) MultiByteToWideChar(CP_UTF8, 0, dll, -1, w.data(), wn);
    {   // an absolute path: with the System32 search flag a relative one would be looked for in System32 only
        std::wstring full(32768, L'\0');
        const DWORD n = GetFullPathNameW(w.c_str(), DWORD(full.size()), full.data(), nullptr);
        if (n > 0 && n < full.size()) w.assign(full.c_str(), n);
    }
    // its dependencies are system DLLs only (SDL3 and the C++ runtime are static): from System32, never from the game
    // folder (dll1114: a stray version.dll there failed the load with 1114); 87: a Windows without the search flags
    HMODULE m = LoadLibraryExW(w.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (m == nullptr && GetLastError() == ERROR_INVALID_PARAMETER) m = LoadLibraryExW(w.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (m == nullptr) { std::fprintf(stderr, "[B3SCENE] B3PORT_RENDER: cannot load %s (%lu)\n", dll, GetLastError()); return false; }
    auto open = reinterpret_cast<int (*)(const char*)>(reinterpret_cast<void*>(GetProcAddress(m, "b3r_open")));
    r.want_key = reinterpret_cast<int (*)()>(reinterpret_cast<void*>(GetProcAddress(m, "b3r_want_key")));
    r.submit = reinterpret_cast<int (*)(std::uint32_t, const void*, std::size_t)>(reinterpret_cast<void*>(GetProcAddress(m, "b3r_submit")));
    r.close = reinterpret_cast<int (*)(std::uint32_t)>(reinterpret_cast<void*>(GetProcAddress(m, "b3r_close")));
    // lat1: the record with its schedule stamp (b3port_tick_stamp); a DLL without it gets b3r_submit
    r.submit_at = reinterpret_cast<int (*)(std::uint32_t, const void*, std::size_t, std::uint64_t)>(reinterpret_cast<void*>(GetProcAddress(m, "b3r_submit_at")));
    r.perf_on = reinterpret_cast<int (*)()>(reinterpret_cast<void*>(GetProcAddress(m, "b3r_perf_on")));
    r.perf_add = reinterpret_cast<void (*)(int, std::uint64_t)>(reinterpret_cast<void*>(GetProcAddress(m, "b3r_perf_add")));
    if (!open || !r.want_key || !r.submit || !r.close) { std::fprintf(stderr, "[B3SCENE] B3PORT_RENDER: %s lacks the b3r_ functions\n", dll); return false; }
    const char* args = std::getenv("B3PORT_RENDER_ARGS");
    if (open(args ? args : "") != 0) { std::fprintf(stderr, "[B3SCENE] B3PORT_RENDER: b3r_open failed\n"); return false; }
    std::fprintf(stderr, "[B3SCENE] in-process renderer %s\n", dll);
    return true;
#else
    // pic1: the same C ABI from libb3render.so (src/b3replay/inproc.cpp's ELF exports; the native backend on Linux)
    void* m = dlopen(dll, RTLD_NOW | RTLD_LOCAL);
    if (m == nullptr) { std::fprintf(stderr, "[B3SCENE] B3PORT_RENDER: cannot load %s (%s)\n", dll, dlerror()); return false; }
    auto open = reinterpret_cast<int (*)(const char*)>(dlsym(m, "b3r_open"));
    r.want_key = reinterpret_cast<int (*)()>(dlsym(m, "b3r_want_key"));
    r.submit = reinterpret_cast<int (*)(std::uint32_t, const void*, std::size_t)>(dlsym(m, "b3r_submit"));
    r.close = reinterpret_cast<int (*)(std::uint32_t)>(dlsym(m, "b3r_close"));
    r.submit_at = reinterpret_cast<int (*)(std::uint32_t, const void*, std::size_t, std::uint64_t)>(dlsym(m, "b3r_submit_at"));
    r.perf_on = reinterpret_cast<int (*)()>(dlsym(m, "b3r_perf_on"));
    r.perf_add = reinterpret_cast<void (*)(int, std::uint64_t)>(dlsym(m, "b3r_perf_add"));
    if (!open || !r.want_key || !r.submit || !r.close) { std::fprintf(stderr, "[B3SCENE] B3PORT_RENDER: %s lacks the b3r_ functions\n", dll); return false; }
    const char* args = std::getenv("B3PORT_RENDER_ARGS");
    if (open(args ? args : "") != 0) { std::fprintf(stderr, "[B3SCENE] B3PORT_RENDER: b3r_open failed\n"); return false; }
    std::fprintf(stderr, "[B3SCENE] in-process renderer %s\n", dll);
    return true;
#endif
}

// gsoff1: never destroyed. A static object is destroyed before an atexit handler registered while it was being built (or
// before it was built) runs, and finish_at_exit (atexit) still reads it at a normal exit (the game closed by Quit).
Scene& scene() {
    static Scene& s = *new Scene([] {
        Scene t;
        if (const char* path = std::getenv("B3PORT_SCENE_OUT"); path != nullptr && *path) {
            t.out = std::fopen(path, "wb");
            if (t.out == nullptr) {
                std::fprintf(stderr, "[B3SCENE] cannot write %s\n", path);
                std::fflush(stderr);
                std::abort();
            }
            static char buffer[1 << 20];
            std::setvbuf(t.out, buffer, _IOFBF, sizeof buffer);
            const char magic[8] = {'B', '3', 'S', 'C', 'E', 'N', 'E', 0};
            const std::uint32_t head[2] = {3U, 0U};   // version 3: props and effects (the record format is version 1's)
            std::fwrite(magic, 1, 8, t.out);
            std::fwrite(head, 4, 2, t.out);
            std::fprintf(stderr, "[B3SCENE] capturing to %s\n", path);
        }
        if (const char* name = std::getenv("B3PORT_SCENE_LIVE"); name != nullptr && *name) {
            static b3live::Mapping mapping;
            std::string why;
            if (!mapping.create(name, b3live::kDefaultCapacity, &why)) {
                std::fprintf(stderr, "[B3SCENE] live feed %s: %s\n", name, why.c_str());
                std::fflush(stderr);
                std::abort();
            }
            t.live = &mapping;
            std::fprintf(stderr, "[B3SCENE] live feed %s (%llu MB ring)\n", name,
                         static_cast<unsigned long long>(b3live::kDefaultCapacity >> 20));
        }
        if (const char* tp = std::getenv("B3PORT_SCENE_TIMES"); tp != nullptr && *tp) { t.time_polls = true; t.times.reserve(1 << 18); }
        if (const char* dll = std::getenv("B3PORT_RENDER"); dll != nullptr && *dll) {
            if (!open_render(t.render, dll)) { std::fflush(stderr); std::abort(); }
            t.inproc = true;
        }
        return t;
    }());
    return s;
}

void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    const std::size_t n = b.size();
    b.resize(n + 4);
    std::memcpy(b.data() + n, &v, 4);
}

bool ram(std::uint32_t a, std::uint32_t n) { return a >= 0x00100000U && a < kRamEnd && n <= kRamEnd - a; }

std::uint32_t u32(std::uint32_t a) {
    Scene& s = scene();
    if (!ram(a, 4)) return 0;
    std::uint32_t v = 0;
    if (s.ram) { std::memcpy(&v, s.ram + a, 4); return v; }   // (little-endian host, as the guest)
    for (std::uint32_t k = 0; k < 4; ++k) v |= std::uint32_t(s.read(s.memory, a + k)) << (8 * k);
    return v;
}
std::uint8_t u8(std::uint32_t a) { Scene& s = scene(); return ram(a, 1) ? (s.ram ? s.ram[a] : s.read(s.memory, a)) : 0; }
std::uint16_t u16(std::uint32_t a) { return std::uint16_t(u8(a) | (u8(a + 1) << 8)); }

// Captures [a, a+n) (widened to 16-byte blocks): appends the runs of blocks that differ from the shadow. `host`: the
// bytes come from a host buffer (16-aligned a and n) instead of guest memory (the HUD region, hud_capture.cpp).
void grab(std::uint32_t a, std::uint32_t n, const std::uint8_t* host = nullptr) {
    Scene& s = scene();
    const std::uint32_t lo = a & ~15U, hi = (a + n + 15U) & ~15U;
    if (n == 0 || (host == nullptr && !ram(lo, hi - lo))) return;
    std::uint32_t run_start = 0, run_len = 0;
    std::size_t run_at = 0;
    auto close = [&]() {
        if (run_len == 0) return;
        std::memcpy(s.buf.data() + run_at + 4, &run_len, 4);
        ++s.runs;
        run_len = 0;
    };
    Scene::Page* page = nullptr;   // inproc1: one map lookup per page, not per block (map references stay valid)
    std::uint32_t page_base = 1;
    for (std::uint32_t b = lo; b < hi; b += 16) {
        if ((b & ~(kPage - 1)) != page_base) { page_base = b & ~(kPage - 1); page = &s.pages[page_base]; }
        Scene::Page& pg = *page;
        const std::uint32_t off = b & (kPage - 1);
        std::uint8_t now[16];
        if (host != nullptr) std::memcpy(now, host + (b - lo), 16);
        else if (s.ram) std::memcpy(now, s.ram + b, 16);
        else for (std::uint32_t k = 0; k < 16; ++k) now[k] = s.read(s.memory, b + k);
        const bool same = pg.valid[off / 16] && std::memcmp(pg.bytes + off, now, 16) == 0;
        if (same) { close(); continue; }
        std::memcpy(pg.bytes + off, now, 16);
        pg.valid[off / 16] = 1;
        if (run_len == 0 || run_start + run_len != b) {
            close();
            run_start = b;
            run_at = s.buf.size();
            put32(s.buf, b);
            put32(s.buf, 0);
        }
        s.buf.insert(s.buf.end(), now, now + 16);
        run_len += 16;
    }
    close();
}

// True if every byte of [a, a+n) outside [a+skip_lo, a+skip_hi) equals the shadow image (and was captured before).
bool same_except(std::uint32_t a, std::uint32_t n, std::uint32_t skip_lo, std::uint32_t skip_hi) {
    Scene& s = scene();
    for (std::uint32_t k = 0; k < n; ++k) {
        if (k >= skip_lo && k < skip_hi) continue;
        const std::uint32_t b = a + k;
        const auto it = s.pages.find(b & ~(kPage - 1));
        if (it == s.pages.end() || !it->second.valid[(b & (kPage - 1)) / 16]) return false;
        if (it->second.bytes[b & (kPage - 1)] != (s.ram ? s.ram[b] : s.read(s.memory, b))) return false;
    }
    return true;
}

// The vehicle renderer's inputs for one EssentialParams + VehicleParams pair.
void grab_vehicle(std::uint32_t ep, std::uint32_t vp) {
    grab(ep, 0x80);   // [0x5C, 0x80): the 9 SH coefficients (render6; race cars had them, traffic / crashed only L00)
    const std::uint32_t vd = u32(ep + 0x40);
    if (ram(vd, 0x70)) {
        grab(vd, 0x70);
        const std::uint32_t raster = u32(vd + 0x60);
        if (ram(raster, 0xC8)) grab(raster + 0xA0, 0x28);
    }
    if (vp == 0) return;
    grab(vp, 0x380);
    grab(vp + 0x4A0, 0x60);
    grab(vp + 0x1000, 0x30);
    bool deformed = u8(vp + 0x101B) != 0;
    for (std::uint32_t p = 0; p < 6; ++p) deformed = deformed || u8(vp + 0x101C + p) != 0;
    if (deformed) grab(vp + 0x500, 0x780);
}

// The poll's record into the live ring: the delta (head + s.buf), or a keyframe when the ring has none yet or the
// reader asked for one: the same head with every valid block of the shadow image as runs (address order, coalesced).
void build_body(Scene& s, const std::uint32_t head[4], bool key) {
    std::vector<std::uint8_t>& body = s.key;
    body.clear();
    if (!key) {
        body.resize(16);
        std::memcpy(body.data(), head, 16);
        body.insert(body.end(), s.buf.begin(), s.buf.end());
    } else {
        body.resize(16);
        std::uint32_t runs = 0, run_start = 0, run_end = 0;
        std::size_t run_at = 0;
        for (const auto& [base, pg] : s.pages) {   // std::map: address order
            for (std::uint32_t k = 0; k < kPage / 16; ++k) {
                if (!pg.valid[k]) continue;
                const std::uint32_t b = base + 16 * k;
                if (runs == 0 || b != run_end) {
                    run_at = body.size();
                    run_start = b;
                    put32(body, b);
                    put32(body, 0);
                    ++runs;
                }
                body.insert(body.end(), pg.bytes + 16 * k, pg.bytes + 16 * k + 16);
                run_end = b + 16;
                const std::uint32_t len = run_end - run_start;
                std::memcpy(body.data() + run_at + 4, &len, 4);
            }
        }
        const std::uint32_t kh[4] = {head[0], head[1], head[2], runs};
        std::memcpy(body.data(), kh, 16);
    }
}

void live_publish(Scene& s, const std::uint32_t head[4]) {
    b3live::Header* h = s.live->header();
    h->polls.fetch_add(1, std::memory_order_relaxed);
    const bool key = h->key_at.load(std::memory_order_relaxed) == b3live::kNoKey || h->want_key.exchange(0) != 0;
    build_body(s, head, key);
    if (!b3live::publish(*s.live, key ? b3live::kKeyframe : b3live::kDelta, s.key.data(), s.key.size())) ++s.live_dropped;
}

// inproc1: the same record to the in-process renderer (a keyframe only first, or after a dropped record).
void inproc_publish(Scene& s, const std::uint32_t head[4]) {
    const std::uint64_t t0 = b3live::now_ns();
    const bool key = s.render.want_key() != 0;
    build_body(s, head, key);
    const std::uint32_t kind = key ? b3live::kKeyframe : b3live::kDelta;
    // lat1: the host time the input of the tick this record shows was latched (the renderer's schedule clock)
    const bool ok = s.render.submit_at ? s.render.submit_at(kind, s.key.data(), s.key.size(), g_host.tick_stamp ? g_host.tick_stamp(head[1]) : 0) != 0
                                       : s.render.submit(kind, s.key.data(), s.key.size()) != 0;
    if (!ok) ++s.inproc_dropped;
    const std::uint64_t dt = b3live::now_ns() - t0;
    s.inproc_ns += dt;
    ++s.inproc_polls;
    s.inproc_keys += key;
    s.last_handover_ns = dt;
    if (dt > s.inproc_max_ns) { s.inproc_max_ns = dt; s.inproc_max_poll = head[1]; }
    if (s.render.perf_add && s.render.perf_on && s.render.perf_on()) s.render.perf_add(3, dt);   // perf3
}


// Effects (version 3, fx1): the inputs of the car shadows and the skid marks (header comment).
void grab_effects() {
    constexpr std::uint32_t kWorld = 0x522660U, kTraffic = kWorld + 0x4DE0U;
    std::uint32_t datas[80];
    std::uint32_t ndatas = 0;
    auto note_data = [&](std::uint32_t vd) {
        if (!ram(vd, 0xEA0) || ndatas == 80) return;
        for (std::uint32_t k = 0; k < ndatas; ++k) if (datas[k] == vd) return;
        datas[ndatas++] = vd;
    };
    // shadows: race cars (+0x510 ground normal / distance; +0x60 is in the car's [0x60, 0x80) grab)
    grab(0x4E0FC0U, 4);                                     // krShadowDarkness
    const std::uint32_t ncars = u32(kWorld + 0x12AD1CU);
    for (std::uint32_t i = 0; i < ncars && i < 6; ++i) {
        const std::uint32_t car = u32(kWorld + 0x12AD28U + 4 * i);
        if (!ram(car, 0x27D0)) continue;
        grab(car + 0x510, 0x10);
        note_data(u32(car + 0x40));
    }
    // IsPlayerVisible of view 0's player camera manager (D_004EE040[0] + 0x28E00)
    constexpr std::uint32_t kPcm = 0x4EE040U + 0x28E00U;
    grab(kPcm + 0x9E0, 0x30);
    grab(kPcm + 0xB80, 0x10);
    if (const std::uint32_t o = u32(kPcm + 0x9E0); ram(o, 0xB90)) grab(o + 0xB80, 0x10);
    // traffic: view 0's list
    grab(kWorld + 0x12AD50U + 0x19ACU, 1);
    const std::uint32_t s = u8(kWorld + 0x12AD50U + 0x19ACU);
    if (s < 0xFE) {
        grab(kTraffic + 0x36350U + s, 1);
        const std::uint32_t n = u8(kTraffic + 0x36350U + s), list = kTraffic + 0x35C24U + 0xFEU * s;
        grab(list, n);
        for (std::uint32_t j = 0; j < n; ++j) {
            const std::uint32_t k = u8(list + j);
            if (k >= 0xFE) continue;
            const std::uint32_t tv = kTraffic + 0x127D0U + 0x180U * k;
            grab(tv + 0x70, 0x70);                              // EssentialParams [0, 0x70) (+0x60 darkness)
            const std::uint32_t slot = kTraffic + 0x343F0U + 4 * std::uint32_t(u8(tv + 0x176));
            grab(slot, 4);
            if (const std::uint32_t q = u32(slot); ram(q, 0x1C)) grab(q + 0x18, 4);
            note_data(u32(tv + 0xB0));
        }
    }
    // crashed vehicles
    const std::uint32_t nph = u32(0x64517CU);
    for (std::uint32_t k = 0; k < nph && k < 67; ++k) {
        const std::uint32_t ph = u32(kWorld + 0x122A10U + 4 * k);
        if (!ram(ph, 0x2410)) continue;
        grab(ph + 0x13C0, 0x10);
        grab(ph + 0x1870, 0x10);
        note_data(u32(ph + 0x13A0));
    }
    for (std::uint32_t k = 0; k < ndatas; ++k) {
        const std::uint32_t vd = datas[k];
        grab(vd + 0xE80, 0x20);
        const std::uint32_t nw = u8(vd + 0x0D);
        for (std::uint32_t w = 0; w < nw && w < 6; ++w) grab(vd + 0xBB0 + 0x40 * w, 0x10);
    }
    // skid marks: the trail system's per-type emitter lists, the emitters and their point data
    constexpr std::uint32_t kTrails = 0x1D9D200U + 0x2600U;
    grab(0x49FFF0U, 5 * 0x50);                              // CB3TrailSystem::mgaDefaultParams
    grab(0x1E865E0U, 5 * 0x30);                             // the per-type VU parameters
    grab(kTrails + 0x14630U, 0x10);                         // +0x14634 half index
    for (std::uint32_t t = 0; t < 5; ++t) {
        const std::uint32_t list = kTrails + 0x13C0CU + 0x208U * t;
        grab(list + 0x200, 4);
        const std::uint32_t n = u32(list + 0x200);
        if (n == 0 || n > 0x80) continue;
        grab(list, 4 * n);
        for (std::uint32_t j = 0; j < n; ++j) {
            const std::uint32_t e = u32(list + 4 * j);
            if (!ram(e, 0x14)) continue;
            // func_002BD560 swaps every emitter's two data pointers (+0x8 / +0xC) each rendered frame and copies the
            // drawn half into the other; +0x8 is always half (+0x14634) + the emitter's slot x 0x130 [C race2 v3: 177,412
            // of 177,412 listed emitters], which the reader derives. So the emitter is captured only when a field other
            // than the two pointers changed (the swap alone cost roadrage1 25.7 MB).
            if (!same_except(e, 0x14, 0x8, 0x10)) grab(e, 0x14);
            if (const std::uint32_t d = u32(e + 8) & ~0x30000000U; ram(d, 0x130)) grab(d, 0x130);
        }
    }
}

// Coronas (corona1, docs/native-renderer.md §16; same version, readers detect them by their presence): the inputs of the
// three CB3CoronaDynamicArray the effects manager fills while the cars draw, so the reader rebuilds every slot
//   race cars: CB3RaceCar::Draw 0x151A40 -> ActuallyRenderRaceCarImmediateEffects 0x2621D0: RenderLightCoronas 0x256690
//     (light bits +0x58, +0x18F0) and func_00257230 (the boost glow: CB3BoostEffectParams +0x118C [0, 0x20), the red
//     flag +0x18F1 / car id +0x1960 in the Draw fields, the 8 body zones +0x580 it interpolates through, the view
//     index +0x27C0 that RenderRaceCars 0x199A20 compares with IsPlayerVisible's view)
//   traffic / crashed vehicles: RenderTrafficImmediateEffects 0x2621A0 (+0x58 is in their EssentialParams grabs)
//   per vehicle data its light groups +0x1664[7] / counts +0x16AC[7] and exhaust items +0x1684 / count +0x16B4 (0x30
//     each), the traffic system's lights (track traffic data +0x38 / +0xAC / +0xB0, lights +0x117CC, +0x36320 mask,
//     +0x363BA), the effects manager's +0x1F810 (camera point, func_002622A0) and +0x1F820 (the seed EndUpdate 0x2623F0
//     writes), its randomiser state D_01D9D190, the corona textures +0x1F830, and the constant tables
//                                                   [C asm 0x256690, 0x257080, 0x257230, 0x30AAE0, 0x3078F0; L drafts]
// B3PORT_SCENE_CORONA_CHECK=1 (proof runs only) also captures what the game wrote: the three arrays' headers and, for
// player 0, the used slots of the half the last EndCoronaSubmit closed (array +0x10 + 4 half, count +0x2A, 32 bytes each)
//                                                   [C BeginUpdate 0x307500, EndUpdate 0x307440; L ConstructDMAData 0x3076D0]
void grab_corona_tables(std::uint32_t vd) {
    grab(vd + 0x1660, 0x60);
    for (std::uint32_t k = 0; k < 7; ++k)
        if (const std::uint32_t t = u32(vd + 0x1664 + 4 * k), n = u8(vd + 0x16AC + k); n > 0 && ram(t, 0x30 * n)) grab(t, 0x30 * n);
    if (const std::uint32_t t = u32(vd + 0x1684), n = u8(vd + 0x16B4); n > 0 && ram(t, 0x30 * n)) grab(t, 0x30 * n);
}

void grab_coronas() {
    constexpr std::uint32_t kWorld = 0x522660U, kFx = 0x1D9D200U, kTraffic = kWorld + 0x4DE0U;
    static const bool check = [] { const char* e = std::getenv("B3PORT_SCENE_CORONA_CHECK"); return e != nullptr && *e == '1'; }();
    grab(kFx + 0x1F810U, 0x30);        // camera point, seed, RNG, the three CGtTexture*
    grab(0x1D9D190U, 0x10);            // gEffectsRandomiserScales (B3EffectsManager.cpp's)
    grab(0x4BC8D0U, 0x60);             // the texture names and _gaCoronaParams 0x4BC900
    grab(0x4BBFE0U, 0x60);             // light colours D_004BBFE0..D_004BC030
    grab(0x4C4980U, 0x50);             // traffic-light colours / offsets D_004C4980..D_004C49C0
    grab(0x4C4A10U, 0x20);             // boost colours (static init 0x4D6DF0)
    grab(0x4E09E0U, 0x10);             // D_004E09E4
    grab(0x4E0C60U, 0x10);             // D_004E0C6C
    grab(0x4E0E90U, 0x10);             // D_004E0E94
    grab(0x4E0F40U, 0x20);             // D_004E0F44..D_004E0F54
    for (std::uint32_t k = 0; k < 3; ++k)
        if (const std::uint32_t t = u32(kFx + 0x1F830U + 4 * k); ram(t, 0x50)) grab(t, 0x50);
    std::uint32_t datas[96];
    std::uint32_t ndatas = 0;
    auto note_data = [&](std::uint32_t vd) {
        if (!ram(vd, 0x16C0) || ndatas == 96) return;
        for (std::uint32_t k = 0; k < ndatas; ++k) if (datas[k] == vd) return;
        datas[ndatas++] = vd;
    };
    const std::uint32_t ncars = u32(kWorld + 0x12AD1CU);
    for (std::uint32_t i = 0; i < ncars && i < 6; ++i) {
        const std::uint32_t car = u32(kWorld + 0x12AD28U + 4 * i);
        if (!ram(car, 0x27D0)) continue;
        grab(car + 0x118C, 0x20);
        grab(car + 0x580, 0x200);
        grab(car + 0x27C0, 4);
        note_data(u32(car + 0x40));
    }
    for (std::uint32_t v = 0; v < 0xFE; ++v) {
        const std::uint32_t tv = kTraffic + 0x127D0U + 0x180U * v;
        if ((u8(tv + 0x174) & 1U) != 0) note_data(u32(tv + 0xB0));
    }
    const std::uint32_t nph = u32(0x64517CU);
    for (std::uint32_t k = 0; k < nph && k < 67; ++k)
        if (const std::uint32_t ph = u32(kWorld + 0x122A10U + 4 * k); ram(ph, 0x2410)) note_data(u32(ph + 0x13A0));
    for (std::uint32_t k = 0; k < ndatas; ++k) grab_corona_tables(datas[k]);
    grab(kTraffic, 4);
    if (const std::uint32_t td = u32(kTraffic); ram(td, 0xB4)) {
        grab(td + 0x38, 4);
        grab(td + 0xAC, 8);
        const std::uint32_t nl = u32(td + 0x38), nc = u32(td + 0xB0), co = u32(td + 0xAC);
        if (nc > 0 && nc < 0x1000 && ram(co, 0x20 * nc)) grab(co, 0x20 * nc);
        if (nl > 0 && nl <= 0xA9) {
            grab(kTraffic + 0x117CCU, 0x18 * nl);
            for (std::uint32_t j = 0; j < nl; ++j)
                if (const std::uint32_t lt = u32(kTraffic + 0x117CCU + 0x18 * j); ram(lt, 0x18)) grab(lt + 0x10, 8);
        }
        if (nc > 0 && nc < 0x1000) grab(kTraffic + 0x36320U, 4 * ((nc + 30) / 31));
        grab(kTraffic + 0x363BAU, 1);
    }
    if (!check) return;
    for (std::uint32_t a = 0; a < 3; ++a) {
        const std::uint32_t arr = kFx + 0x0CU + 0x34U * a;
        grab(arr, 0x34);
        const std::uint32_t half = u8(arr + 0x2E) & 1U, n = u16(arr + 0x2A);
        const std::uint32_t buf = u32(arr + 0x10 + 4 * half) & ~0x30000000U;
        if (n > 0 && n <= 126 && ram(buf, 0x20 * n)) grab(buf, 0x20 * n);
    }
}

// Contact effects (fx2, docs/native-renderer.md §19; same version, readers detect each part by its presence):
//   scrapes: CB3ScrapeSystem (gEffectsManager 0x1D9D200 + 0x16C80), drawn by CB3EffectsManager::RenderPreFogEffects
//     0x2620C0 -> CB3ScrapeSystem::Render 0x375AA0 -> CB3ScrapeRenderer::Render 0x375F90 while the view's race car
//     +0x18EA is set (in the car's [0x18E0, 0x19A0) grab): the type-0 emitter list +0x8A8C (32 pointers, count
//     +0x8B0C), and while the gate is set each listed emitter [0, 0x20) (+0x0 UV index, +0x4 last time, +0x8 type, +0x10 flags, +0x12 buffer
//     index), its two segment-buffer pointers +0x440 / +0x444, the used part of the buffer the last render drew (+0x444:
//     +0x0 count, 0x20 per segment from +0x10: position, side vector) and the other one's count; CB3ScrapeSystem::mgaDefaultParams D_01EA4910 [0, 0x50) (colours
//     +0x0 / +0x10, texture name +0x20, widths +0x34 / +0x38, lifetime +0x3C) and karScrapeUVs 0x4C8FF0
//                                       [C asm 0x3755E0, 0x375840 (matched), 0x375AA0 (matched), 0x375BF0, 0x375F90, 0x375E90]
//   sparks: the six CB3SparkArray gEffectsManager + 0x21F0 (0xA8 each: normal bank +0x0, crash bank +0x4C, each 4 ring
//     slots {base, capacity, write index, lifetime} + round-robin +0x40 + last spawn +0x44 + allocation +0x48; params
//     +0x98), each one's CB3SparkArrayParams (0x7C, CB3SparkArrayBase::mgaArrayParams 0x4C7710), each bank's allocation
//     (the static DMA head [+0x48, slot 0 base) and every ring's record blocks: 0x90 per 4 sparks, 4 spawn times +
//     4 x {position, velocity}); CB3SparkRenderer's statics 0x4E2A00 (mnNumRenderableArrays, mnRenderPlayer,
//     mrMaxMotionBlurTime, mbIsCrash), D_01E31A20, D_00667050, and player 0's camera history D_01E31A30 (18 x
//     CSparkFrameData 0x60, shifted by every render: so only on ticks where a spark array is renderable by
//     RenderSparkArray 0x2637F0's rule, and its newest record otherwise)
//                                       [C SpawnBouncySpark 0x263C40, CSparkBank::Construct 0x264170, BeginRender 0x2638C0,
//                                        RenderSparkArray 0x2637F0 (matched), UploadStaticDataAndCallCode 0x263420]
//   popcorn: the four CB3PopcornArray gEffectsManager + 0x20F0 (0x40 each: records +0x1C, capacity +0x24, write
//     index +0x28) and their record blocks (0x90 per 4), CB3PopcornArrayBase::maStandardParams 0x4A4DB0 (4 x 0x50)
//                                       [C SpawnBouncyPopcorn 0x365E80 draft, RenderPostFogEffects 0x261E00]
// B3PORT_SCENE_SPARK_CHECK=1 (proof runs only) captures the camera history on every tick.
void grab_contact_effects() {
    constexpr std::uint32_t kFx = 0x1D9D200U;
    static const bool check = [] { const char* e = std::getenv("B3PORT_SCENE_SPARK_CHECK"); return e != nullptr && *e == '1'; }();
    auto strip = [](std::uint32_t a) { return a & ~0x30000000U; };
    auto f32 = [](std::uint32_t a) { const std::uint32_t w = u32(a); float f; std::memcpy(&f, &w, 4); return f; };
    // scrapes
    constexpr std::uint32_t kScrape = kFx + 0x16C80U;
    grab(0x1EA4910U, 0x50);
    grab(0x4C8FF0U, 0x20);
    grab(kScrape + 0x8A80U, 0x90);                          // +0x8A8C list, +0x8B0C count
    // the emitters only while the view's race car +0x18EA is set (player 0; RenderPreFogEffects draws them only then):
    // each one's header, its buffer pointers, the buffer Render drew last (+0x444) and the other's count
    if (const std::uint32_t n = u32(kScrape + 0x8B0CU); n <= 32 && u8(0x522660U + 0x12AD50U + 0x18EAU) != 0) {
        for (std::uint32_t j = 0; j < n; ++j) {
            const std::uint32_t e = strip(u32(kScrape + 0x8A8CU + 4 * j));
            if (!ram(e, 0x448)) continue;
            grab(e, 0x20);
            grab(e + 0x440, 8);
            if (const std::uint32_t b = strip(u32(e + 0x440)); ram(b, 0x10)) grab(b, 0x10);
            if (const std::uint32_t b = strip(u32(e + 0x444)); ram(b, 0x210)) {
                const std::uint32_t c = u32(b);
                grab(b, 0x10 + 0x20 * (c <= 16 ? c : 0));
            }
        }
    }
    // sparks
    grab(0x4E2A00U, 0x10);
    grab(0x1E31A20U, 0x10);
    grab(0x667050U, 4);
    const float now = f32(0x1E31A30U + 0x50U);   // the last render's time (CSparkFrameData f[0] row 5.x), RenderSparkArray's "now"
    bool live = false;
    for (std::uint32_t a = 0; a < 6; ++a) {
        const std::uint32_t arr = kFx + 0x21F0U + 0xA8U * a;
        grab(arr, 0xA8);
        const std::uint32_t prm = strip(u32(arr + 0x98));
        if (!ram(prm, 0x7C)) continue;
        grab(prm, 0x7C);
        float life = 0;
        for (std::uint32_t k = 0; k < 4; ++k) life = std::max(life, f32(prm + 0x40 + 4 * k));
        if (f32(arr + 0x44) > now - life || f32(arr + 0x90) > now - life) live = true;   // RenderSparkArray's test
        for (std::uint32_t bank : {0x0U, 0x4CU}) {
            const std::uint32_t bk = arr + bank, alloc = strip(u32(bk + 0x48)), base0 = strip(u32(bk));
            if (ram(alloc, 0x10) && base0 > alloc && base0 - alloc <= 0x1000) grab(alloc, base0 - alloc);
            for (std::uint32_t k = 0; k < 4; ++k) {
                const std::uint32_t base = strip(u32(bk + 0x10 * k)), cap = u32(bk + 0x10 * k + 4);
                if (cap > 0 && cap <= 0x1000 && ram(base, cap / 4 * 0x90)) grab(base, cap / 4 * 0x90);
            }
        }
    }
    grab(0x1E31A30U, (live || check) ? 18 * 0x60 : 0x60);
    // popcorn
    grab(0x4A4DB0U, 4 * 0x50);
    for (std::uint32_t a = 0; a < 4; ++a) {
        const std::uint32_t arr = kFx + 0x20F0U + 0x40U * a;
        grab(arr, 0x40);
        const std::uint32_t base = strip(u32(arr + 0x1C)), cap = u32(arr + 0x24);
        if (cap > 0 && cap <= 0x1000 && ram(base, (cap + 3) / 4 * 0x90)) grab(base, (cap + 3) / 4 * 0x90);
    }
    // particles (effects3, docs/native-renderer.md §19.10): the 24 CB3ParticleArray gEffectsManager + 0x170 (0x150 each: the
    // row data DispatchArray sends, banks +0xE8 / +0x114 with record pointer +0x8, capacity +0x14, lifetime +0x24, live mask
    // +0x28), each bank's record allocation (capacity x 28 + (capacity + 59) / 60 x 4: positions + spawn times, V4-16,
    // V4-8, newest times), maStandardParams 0x49C570 (24 x 0x80; the texture name pointer +0x0), the names, the program
    // image vu_game_0046D750 and RenderParticleArray's D_004E29E4 [C Construct 0x255240, CB3ParticleBank::Construct 0x2558A0,
    // DispatchArray 0x254340, EndRender 0x254580, RenderParticleArray 0x254970]
    grab(0x49C570U, 24 * 0x80);
    grab(0x4E29E0U, 0x10);
    grab(0x46D750U, 0x1980);
    for (std::uint32_t a = 0; a < 24; ++a) {
        const std::uint32_t arr = kFx + 0x170U + 0x150U * a;
        grab(arr, 0x150);
        if (const std::uint32_t prm = strip(u32(arr + 0xE4U)); ram(prm, 0x80))
            if (const std::uint32_t nm = strip(u32(prm)); ram(nm, 16)) grab(nm, 16);
        for (std::uint32_t bank : {0xE8U, 0x114U}) {
            const std::uint32_t rec = strip(u32(arr + bank + 8U)), cap = u32(arr + bank + 0x14U);
            const std::uint32_t bytes = cap * 28U + (cap + 59U) / 60U * 4U;
            if (cap > 0 && cap <= 0x400 && ram(rec, bytes)) grab(rec, bytes);
        }
    }
}

}  // namespace

// The HUD capture (hud1, hud_capture.cpp) writes through these, from b3hud_publish inside b3scene_poll.
// Post passes (post1, docs/native-renderer.md §25; same version, readers detect each part by its presence):
//   CB3World::Render 0x276300 ends with CB3Bloom::Render(D_00666274 + 0x50, D_0051BA94 != -1, D_005179E6[idx x 0xBB0])
//   and `if (CB3DirectionalBlurBase::UpdateCamera(D_00666274 + 0x60, idx)) CB3DirectionalBlur::Render ...; else if (the
//   view's car +0x18EA == 0) CB3RadialBlur::Render`. D_00666274 points at the open viewport's params (graphics manager
//   +0x1D0 for view 1), so the bloom's params (+0x220) and UpdateCamera's state and outputs (+0x230..+0x257) are in the
//   graphics manager block above. Added here, UpdateCamera's other inputs, per view idx 0 / 1:
//     D_004F50A0 [0, 0x10) (the time it stamps into +0x24 and differentiates the camera point by), D_004D1540 /
//     D_004D1550 (the crash pulse's CosFast4 scale and weights: 0 in the ELF, set at run time),
//     D_005179C0 + 0xBB0 idx [0, 0x30) (flags +0x0: 0x2000 no crash pulse, 0x400 motion blur without boost, 4 / 0x20
//     amount x func_0030A990; +0x26 = D_005179E6, the bloom's reset), the player block gWorld + 0x12AD50 + 0x27D0 idx:
//     its CB3BoostEffectParams +0x118C [0, 0x60) (+0x1C / +0x20 func_0030A990's terms, +0x52 the boost switch) and the
//     pointer +0x2430 with that object's +0x3534 (the crash time of the pulse)
//                                       [C asm 0x4256C0 (loads at 0x4256F4, 0x425A24..0x425A68), func_0030A990 matched]
//   the bloom's screen size: ourGlobals 0x670BD0 -> +0x60 (the camera's frame raster) [0, 0x20): width +0xC, height
//     +0x10, offsets +0x1C / +0x1E                                 [C CB3Bloom::Render 0x2659C0 matched; L RW layouts]
void grab_post() {
    constexpr std::uint32_t kWorld = 0x522660U;
    grab(0x4F50A0U, 0x10);
    grab(0x4D1540U, 0x20);
    for (std::uint32_t idx = 0; idx < 2; ++idx) {
        grab(0x5179C0U + 0xBB0U * idx, 0x30);
        const std::uint32_t pl = kWorld + 0x12AD50U + 0x27D0U * idx;
        grab(pl + 0x118C, 0x60);
        grab(pl + 0x2430, 4);
        if (const std::uint32_t c = u32(pl + 0x2430); ram(c, 0x3540)) grab(c + 0x3530, 0x10);
    }
    grab(0x670BD0U, 4);
    if (const std::uint32_t g = u32(0x670BD0U); ram(g, 0x64))
        if (const std::uint32_t r = u32(g + 0x60); ram(r, 0x20)) { grab(g + 0x60, 4); grab(r, 0x20); }
}

void b3hud_publish();
void b3hud_report();
void b3scene_grab(std::uint32_t a, std::uint32_t n) { grab(a, n); }
void b3scene_grab_host(std::uint32_t a, const std::uint8_t* p, std::uint32_t n) { grab(a, n, p); }
bool b3scene_unchanged(std::uint32_t a, std::uint32_t n) { return same_except(a, n, 0, 0); }
std::uint32_t b3scene_u32(std::uint32_t a) { return u32(a); }
// menu1 (docs/native-renderer.md §22): the front end's car (hud_capture.cpp's FECAR event), with the race cars' grab
void b3scene_grab_vehicle(std::uint32_t ep, std::uint32_t vp) { grab_vehicle(ep, vp); }

void b3gv::on_world_render(int idx, const b3gv::Memory& m) {
    if (idx != 1 || !b3gv::active()) return;
    auto rd = [&](std::uint32_t a, std::uint32_t n) -> const std::uint8_t* {
        return a >= 0x00100000U && a < kRamEnd && n <= kRamEnd - a ? m.bytes(a, n) : nullptr;
    };
    auto r32 = [&](std::uint32_t a) { std::uint32_t v = 0; if (const std::uint8_t* p = rd(a, 4)) std::memcpy(&v, p, 4); return v; };
    auto r8 = [&](std::uint32_t a) { const std::uint8_t* p = rd(a, 1); return p ? *p : std::uint8_t(0); };
    if (r32(0x51BA88U) != 0x51AEC0U) return;   // Is2PlayerSplitScreen
    constexpr std::uint32_t kWorld = 0x522660U, kTraffic = kWorld + 0x4DE0U;
    std::vector<std::uint8_t> out(16);
    std::uint32_t n = 0;
    auto put = [&](std::uint32_t a, std::uint8_t u, std::uint8_t f, std::uint8_t x) {
        const std::uint8_t e[8] = {std::uint8_t(a), std::uint8_t(a >> 8), std::uint8_t(a >> 16), std::uint8_t(a >> 24), u, f, x, 0};
        out.insert(out.end(), e, e + 8);
        ++n;
    };
    const std::uint32_t ncars = r32(kWorld + 0x12AD1CU);
    for (std::uint32_t i = 0; i < ncars && i < 6; ++i)
        if (const std::uint32_t car = r32(kWorld + 0x12AD28U + 4 * i); rd(car, 0x27D0)) put(car, r8(car + 0x5A), r8(car + 0x5B), r8(car + 0x10A2));
    for (std::uint32_t k = 0; k < 0xFE; ++k) {
        const std::uint32_t tv = kTraffic + 0x127D0U + 0x180U * k;
        if ((r8(tv + 0x174) & 1) != 0) put(tv, r8(tv + 0xCA), r8(tv + 0xCB), r8(tv + 0x17A));
    }
    const std::uint32_t nph = r32(0x64517CU);
    for (std::uint32_t k = 0; k < nph && k < 67; ++k)
        if (const std::uint32_t ph = r32(kWorld + 0x122A10U + 4 * k); rd(ph, 0x2410)) put(ph, r8(ph + 0x13BA), r8(ph + 0x13BB), 0);
    const std::uint32_t head[4] = {0x31565348U, 0U, n, 0U};   // 'HSV1'; the poll is set when published
    std::memcpy(out.data(), head, 16);
    g_view1 = std::move(out);
}

void b3gv::set_host(const b3gv::Host& h) { g_host = h; }
const b3gv::Host& b3gv::host() { return g_host; }
const std::uint8_t* b3gv::record_so_far(std::size_t* bytes, std::uint32_t* runs) {
    const Scene& s = scene();
    *bytes = s.buf.size();
    *runs = s.runs;
    return s.buf.data();
}
bool b3gv::active() { const Scene& s = scene(); return s.out != nullptr || s.live != nullptr || s.inproc; }

// Called once per retail poll with the guest memory (pic1: the adapter checked any RAM view it passes).
void b3gv::on_poll(std::uint64_t poll, std::uint64_t vblank, const b3gv::Memory& m) {
    Scene& s = scene();
    if (s.out == nullptr && s.live == nullptr && !s.inproc) return;
    const std::uint64_t hook_t0 = s.inproc || s.time_polls ? b3live::now_ns() : 0;
    s.last_handover_ns = 0;
    s.read = m.read_u8;
    s.memory = m.ctx;
    s.ram = m.ram != nullptr && m.ram_size >= kRamEnd ? m.ram : nullptr;
    s.ram_size = s.ram ? kRamEnd : 0;
    if (s.ram == nullptr && s.read == nullptr) {
        std::fprintf(stderr, "[B3SCENE] on_poll without guest memory\n");
        std::fflush(stderr);
        std::abort();
    }
    s.buf.clear();
    s.runs = 0;

    constexpr std::uint32_t kWorld = 0x522660U, kGm = 0x665EC0U;
    grab(0x51BA80U, 0x50);                  // CB3Game frame fields
    grab(kWorld, 0x60);                     // world timer
    grab(0x4B9D80U, 0x20);                  // kRightAxis, kAtAxis
    grab(0x4C4950U, 0x20);                  // kXAxisVector, kYAxisVector (traffic wheel spin / steer axes)
    grab(kGm, 0x3C0);                       // graphics manager: viewports, params, open viewport
    grab(kGm + 0x470U, 4 * 0x230);          // camera records
    for (std::uint32_t vp : {0x90U, 0x130U, 0x310U}) {
        const std::uint32_t cam = u32(kGm + vp + 0x68U);
        if (ram(cam, 0x40)) grab(cam, 0x40);
        const std::uint32_t rwcam = u32(kGm + vp + 0x58U);   // RwCamera; its RwFrame (+0x4) holds the matrix CGtViewport::Update wrote
        if (ram(rwcam, 0xA0)) {
            grab(rwcam, 0xA0);
            if (const std::uint32_t frame = u32(rwcam + 4); ram(frame, 0x90)) grab(frame, 0x90);
        }
    }
    // race cars
    grab(kWorld + 0x12AD1CU, 0x24);
    const std::uint32_t ncars = u32(kWorld + 0x12AD1CU);
    for (std::uint32_t i = 0; i < ncars && i < 6; ++i) {
        const std::uint32_t car = u32(kWorld + 0x12AD28U + 4 * i);
        if (!ram(car, 0x27D0)) continue;
        grab_vehicle(car, car + 0x80);
        grab(car + 0x18E0, 0xC0);
    }
    // streamed track 0 and its resident blocks; split1 (docs/native-renderer.md §29.5): in a 2-player split screen
    // (Is2PlayerSplitScreen: *0x51BA88 == 0x51AEC0) also streamed track 1, gWorld+0x12A514, the set CB3World::Render(1)
    // draws player 2's view from (trk = gWorld + 0x12A348 + 0x1CC idx) [C B3WorldPS2_00276300 Render__8CB3WorldFi;
    // Construct / Update of both at +0x12A348 / +0x12A514: func_0019B380, func_0019A550], with its blocks and player
    // 2's race car block +0x18E0 (func_0021C010's +0x18EA test, the view index at track +0x1C)
    const bool split = u32(0x51BA88U) == 0x51AEC0U;
    for (std::uint32_t idx = 0; idx < (split ? 2U : 1U); ++idx) {
        const std::uint32_t st = kWorld + 0x12A348U + 0x1CCU * idx;
        grab(st, 0x1CC);
        for (std::uint32_t j = 0; j < 17; ++j) {
            for (std::uint32_t slot : {0x7CU, 0xC4U}) {
                const std::uint32_t blk = u32(st + slot + 4 * j);
                if (ram(blk, 0x60)) grab(blk, 0x60);   // +0x04 unit index, +0x58 0 = high / 1 = LOD block
            }
        }
        if (const std::uint32_t cur = u32(st + 0x1C4); ram(cur, 0x60)) grab(cur, 0x60);
        if (const std::uint32_t bk = u32(st + 0x1C8); ram(bk, 0x70)) grab(bk + 0x60, 0x10);
        if (idx == 1)
            if (const std::uint32_t view = u32(st + 0x1C); view < 2) grab(kWorld + 0x12AD50U + 0x27D0U * view + 0x18E0U, 0x10);
    }
    // static track
    grab(0x64C990U, 0x10);
    if (const std::uint32_t g = u32(0x64C994U); ram(g, 0x60)) grab(g, 0x60);
    // traffic: per race car the list of traffic vehicles Draw walks (u8 indices, 0xFE per car from +0x35C24, counts
    // u8 at +0x36350 + car), and the player's race-car index (gWorld + 0x12AD50 + 0x19AC) that picks the list
    //                                                                                    [L Draw__16CB3TrafficSystem 0x3078F0]
    grab(kWorld + 0x4DE0U + 0x35C20U, 0x740);
    grab(kWorld + 0x12AD50U + 0x19ACU, 4);
    if (split) grab(kWorld + 0x12AD50U + 0x27D0U + 0x19ACU, 4);   // split1: player 2's (CB3TrafficSystem::Draw(1))
    if (split) {   // split2: player 2's camera manager's IsPlayerVisible inputs (as player 1's in grab_effects)
        constexpr std::uint32_t kPcm2 = 0x4EE040U + 0x28E00U + 0xBB0U;
        grab(kPcm2 + 0x9E0, 0x30);
        grab(kPcm2 + 0xB80, 0x10);
        if (const std::uint32_t o = u32(kPcm2 + 0x9E0); ram(o, 0xB90)) grab(o + 0xB80, 0x10);
    }
    // split2: view 1's results from the last frame drawn before this poll. The split screen renders every second tick
    // (two polls a frame), and the per-vehicle bytes change only in a render, so a poll without a render since the
    // last one republishes that render's copy with its own poll number.
    if (split && !g_view1.empty()) {
        const std::uint32_t p = std::uint32_t(poll);
        std::memcpy(g_view1.data() + 4, &p, 4);
        grab(kHostView1Base, std::uint32_t(g_view1.size()), g_view1.data());
    }
    if (!split) g_view1.clear();
    const std::uint32_t tv0 = kWorld + 0x4DE0U + 0x127D0U;
    for (std::uint32_t v = 0; v < 0xFE; ++v) {
        const std::uint32_t tv = tv0 + 0x180U * v;
        grab(tv + 0x170, 0x10);             // every vehicle's flags block, so the reader sees vehicles go inactive
        if ((u8(tv + 0x174) & 1U) == 0) continue;
        grab(tv + 0x70, 0x80);              // EssentialParams with the 9 SH coefficients (+0xCC, GetIrradiance in Draw)
        grab(tv + 0x120, 0x50);
        const std::uint32_t vd = u32(tv + 0xB0);
        if (ram(vd, 0x70)) grab(vd, 0x70);
    }
    // crashed (physics) vehicles
    grab(0x64517CU, 4);
    const std::uint32_t nph = u32(0x64517CU);
    grab(kWorld + 0x122A10U, 4 * (nph < 67 ? nph : 67));
    for (std::uint32_t k = 0; k < nph && k < 67; ++k) {
        const std::uint32_t ph = u32(kWorld + 0x122A10U + 4 * k);
        if (!ram(ph, 0x2410)) continue;
        grab(ph + 0x200, 0x10);             // +0x204: the matrix Draw measures the 275 m cull from
        grab_vehicle(ph + 0x1360, ph + 0x13E0);
        grab(ph + 0x2400, 0x10);
    }

    // props: the prop manager, its per-unit CB3Prop lists (state 2 = not drawn), the dynamic props' placements, and
    // the prop bodies in use
    constexpr std::uint32_t kProps = 0x64A854U, kPhys = 0x55EF70U;
    grab(kProps, 0x9C);
    {
        const std::uint32_t place = u32(kProps + 4), counts = u32(kProps + 0x14), lists = u32(kProps + 0x18);
        const std::uint32_t nplace = u16(kProps + 0x10), ntypes = u8(kProps + 0x12), ndyn = u32(kProps + 0xC);
        const std::uint32_t g = u32(0x64C994U);
        const std::uint32_t nunits = ram(g, 0x60) ? u16(g + 0x54) : 0;
        if (nunits > 0 && nunits < 0x400 && ntypes > 0 && ram(counts, 4 * nunits) && ram(lists, 4 * nunits)) {
            grab(counts, 4 * nunits);
            grab(lists, 4 * nunits);
            for (std::uint32_t u = 0; u < nunits; ++u) {
                const std::uint32_t cu = u32(counts + 4 * u), lu = u32(lists + 4 * u);
                if (!ram(cu, ntypes) || !ram(lu, 4 * ntypes)) continue;
                grab(cu, ntypes);
                grab(lu, 4 * ntypes);
                for (std::uint32_t t = 0; t < ntypes; ++t)
                    if (const std::uint32_t c = u8(cu + t), e = u32(lu + 4 * t); c > 0 && ram(e, 4 * c)) grab(e, 4 * c);
            }
        }
        for (std::uint32_t q = 0; q < ndyn && q < 32; ++q)
            if (const std::uint32_t idx = u16(kProps + 0x1C + 4 * q); idx < nplace && ram(place, 0x40 * nplace)) grab(place + 0x40 * idx, 0x40);
    }
    grab(kPhys + 0xE9218U, 0x10);
    if (const std::uint32_t game = u32(0x51BA88U); ram(game, 0x1B8)) {
        grab(game + 0x1B4, 4);
        if (const std::uint32_t logic = u32(game + 0x1B4); ram(logic, 4)) {
            grab(logic, 4);
            if (const std::uint32_t vt = u32(logic); ram(vt, 0xBC)) {
                grab(vt + 0xB8, 4);
                if (const std::uint32_t fn = u32(vt + 0xB8); ram(fn, 8)) grab(fn, 8);
            }
        }
    }
    // 32 CB3PropPhysics; the u64 mask's low word covers them (DynamisePropBody takes slots 0..15)
    for (std::uint32_t k = 0, mask = u32(kPhys + 0xE9220U); k < 32; ++k)
        if ((mask >> k) & 1U) grab(kPhys + 0xC3900U + 0x780U * k + 0x200U, 0x30);
    grab_effects();
    grab_coronas();
    grab_contact_effects();   // fx2: scrapes, sparks, popcorn
    grab_post();              // post1: the blur choice and the bloom's inputs

    // crash-mode game data (crash2): CB3GameData gWorld+0x125B40 [0, 0x590) (CB3CrashPickup[20] +0x0, 0x10 each;
    // CB3CrashRamp[5] +0x140, 0x70 each: six corners + the CB3CrashModeRampStruct pointer +0x60; stage id +0x558; track
    // data +0x56C), the track data's counts [0x3E0, 0x400) (+0x3F8 pickups, +0x3FC ramps), each pickup's and ramp's data
    // struct, the B3Im3d ramp switch D_00665EA1, and player 1's camera manager CGtQuatPos (gGame 0x4EE040 + 0x29010) that
    // the pickups face           [C CB3GameData::Draw 0x14E8C0, CB3CrashRamp::Draw 0x3A1800, CB3CrashPickup::Draw 0x3A0AA0 asm]
    {
        constexpr std::uint32_t kGameData = kWorld + 0x125B40U;
        grab(kGameData, 0x590);
        grab(0x665EA0U, 0x10);
        grab(0x4EE040U + 0x29010U, 0x20);
        if (const std::uint32_t td = u32(kGameData + 0x56C); ram(td, 0x400)) {
            grab(td + 0x3E0, 0x20);
            const std::uint32_t npick = u32(td + 0x3F8), nramp = u32(td + 0x3FC);
            for (std::uint32_t j = 0; j < npick && j < 20; ++j)
                if (const std::uint32_t p = u32(kGameData + 0x10U * j); ram(p, 0x20)) grab(p, 0x20);
            for (std::uint32_t j = 0; j < nramp && j < 5; ++j)
                if (const std::uint32_t r = u32(kGameData + 0x140U + 0x70U * j + 0x60U); ram(r, 0x50)) grab(r, 0x50);
        }
        // perf1 (docs/native-renderer.md §33): the start / finish lines and split checkpoints CB3GameData::Draw draws
        // through CB3StartFinishLine::Draw 0x15E720 / CB3SplitTimeCheckpoint::DrawCheckpoint 0x15E340: each line's
        // CB3PlanarTriggerStruct (its +0x0 pointer; position +0x00, direction +0x10), DrawCheckpoint's half height / half
        // width statics D_004E1568 / D_004E156C, and Draw's per-view inputs of the race car block gWorld+0x12AD50 + 0x27D0
        // idx: +0x10D4, +0x1384, +0x139C (the logic's GetCheckpointMarkerFlags, vtable slot 0x44, is grabbed below)
        //                                  [C asm 0x14E8C0 (matched), 0x15E720, 0x15E340; Prepare 0x14F050 draft]
        for (std::uint32_t j = 0; j < 4; ++j)   // start lines +0x388, finish lines +0x3A8 (0x10 each)
            if (const std::uint32_t o = u32(kGameData + 0x388U + 0x10U * j); ram(o, 0x20)) grab(o, 0x20);
        for (std::uint32_t j = 0; j < 12; ++j)  // split checkpoints +0x3C8 (8 each)
            if (const std::uint32_t o = u32(kGameData + 0x3C8U + 8U * j); ram(o, 0x20)) grab(o, 0x20);
        grab(0x4E1568U, 8);
        for (std::uint32_t idx = 0; idx < 2; ++idx) {
            const std::uint32_t pl = kWorld + 0x12AD50U + 0x27D0U * idx;
            grab(pl + 0x10D4U, 4);
            grab(pl + 0x1384U, 4);
            grab(pl + 0x139CU, 4);
            grab(pl + 0x18E0U, 0x10);
        }
        // speedrun1 (src/b3replay/speedrun.h): view 0's CB3Score (race car block +0x10C0) timing fields -- [0, 0x40): the
        // finish state +0x8, race time +0xC, lap +0x14, laps completed +0x18, lap times +0x1C[8], best lap +0x3C;
        // [0x270, 0x290): the race state +0x27C, start time +0x280 (the lap count / lap timer block +0x2C0 is perf1's above)
        //                                  [C CB3Score::Update 0x15B000, IncrementLapCount 0x15A1F0, func_00155A40]
        grab(kWorld + 0x12AD50U + 0x10C0U, 0x40);
        grab(kWorld + 0x12AD50U + 0x10C0U + 0x270U, 0x20);
        // func_0021C010's tests (the STATIC +0x28 barriers after the game data): D_00665E53, the view's race car +0x18EA
        // (gWorld+0x12AD50 + 0x27D0 x streamed track +0x1C), and IsCrashMode / the logic's 0xAC: vtable slots 0x98, 0x9C,
        // 0xAC, each function's first 0x24 bytes (constant returns, or 0x1342F0: logic +0x40 == 1 or 4), logic +0x40
        //                                                      [C asm func_0021C010, func_00131E40, func_001342F0]
        grab(0x665E50U, 0x10);
        if (const std::uint32_t view = u32(kWorld + 0x12A348U + 0x1C); view < 2) grab(kWorld + 0x12AD50U + 0x27D0U * view + 0x18E0U, 0x10);
        if (const std::uint32_t game = u32(0x51BA88U); ram(game, 0x1B8)) {
            if (const std::uint32_t logic = u32(game + 0x1B4); ram(logic, 0x50)) {
                grab(logic + 0x40, 4);
                if (const std::uint32_t vt = u32(logic); ram(vt, 0xB0)) {
                    grab(vt + 0x98, 0x18);
                    grab(vt + 0x44, 4);   // perf1: GetCheckpointMarkerFlags (constant returns: 3, 0x12, 9, 0 [C ELF])
                    for (std::uint32_t slot : {0x44U, 0x98U, 0x9CU, 0xACU})
                        if (const std::uint32_t fn = u32(vt + slot); ram(fn, 0x24)) grab(fn, 0x24);
                }
            }
        }
    }

    // animated track materials (anim1; same version, readers detect it by its presence): the static track data's update
    // list (data +0x10, s16[+0xE] material indices) and each listed CB3TrackMaterial's runtime fields [0x10, 0x20)
    // (+0x10 current texture, +0x12/+0x13 frame number / direction, +0x18 period, +0x1C time), which
    // CB3StaticTrack::Update 0x3329A0 advances every tick from the world time   [C asm 0x3329A0, CB3World::Update 0x19A5D8]
    if (const std::uint32_t g = u32(0x64C994U); ram(g, 0x60)) {
        const std::uint32_t mats = u32(g + 0x8), list = u32(g + 0x10), nmat = u16(g + 0xC), n = u16(g + 0xE);
        if (n > 0 && n < 0x100 && ram(list, 2 * n)) {
            grab(list, 2 * n);
            for (std::uint32_t k = 0; k < n; ++k)
                if (const std::uint32_t i = u16(list + 2 * k); i < nmat && ram(mats + 0x28 * i, 0x28)) grab(mats + 0x28 * i + 0x10, 0x10);
        }
    }
    b3hud_publish();                        // hud1: the 2D call stream of the frame drawn before this poll

    const std::uint32_t head[4] = {0x4B434954U, std::uint32_t(poll), std::uint32_t(vblank), s.runs};
    if (s.out != nullptr) {
        std::fwrite(head, 4, 4, s.out);
        std::fwrite(s.buf.data(), 1, s.buf.size(), s.out);
        ++s.records;
        s.bytes_written += 16 + s.buf.size();
        if (poll % 600U == 0U) std::fflush(s.out);
    }
    if (s.live != nullptr) live_publish(s, head);
    if (s.inproc) inproc_publish(s, head);
    if (s.inproc || s.time_polls) {
        const std::uint64_t dt = b3live::now_ns() - hook_t0;
        s.hook_ns += dt;
        if (dt > s.hook_max_ns) s.hook_max_ns = dt;
        if (s.render.perf_add && s.render.perf_on && s.render.perf_on()) s.render.perf_add(2, dt);   // perf3
        if (s.time_polls)
            s.times.push_back({std::uint32_t(poll), std::uint32_t(std::min<std::uint64_t>(dt, 0xFFFFFFFFu)),
                               std::uint32_t(std::min<std::uint64_t>(s.last_handover_ns, 0xFFFFFFFFu)),
                               std::uint32_t(16 + s.buf.size())});
    }
}

void b3gv::finish() {
    Scene& s = scene();
    b3hud_report();
    if (s.time_polls) {   // inproc1: B3PORT_SCENE_TIMES
        s.time_polls = false;
        if (std::FILE* f = std::fopen(std::getenv("B3PORT_SCENE_TIMES"), "w")) {
            std::fprintf(f, "poll,hook_ns,handover_ns,record_bytes\n");
            for (const Scene::Time& t : s.times) std::fprintf(f, "%u,%u,%u,%u\n", t.poll, t.hook_ns, t.handover_ns, t.bytes);
            std::fclose(f);
            std::fprintf(stderr, "[B3SCENE] per-poll hook times: %zu polls -> %s\n", s.times.size(), std::getenv("B3PORT_SCENE_TIMES"));
        }
    }
    if (s.live != nullptr) {
        b3live::Header* h = s.live->header();
        h->closed.store(1, std::memory_order_release);
        std::fprintf(stderr, "[B3SCENE] live feed: %llu polls, %llu records (%llu keyframes), %llu dropped; reader at %llu of %llu bytes,"
                     " %llu resyncs\n", static_cast<unsigned long long>(h->polls.load()), static_cast<unsigned long long>(h->records.load()),
                     static_cast<unsigned long long>(h->keyframes.load()), static_cast<unsigned long long>(s.live_dropped),
                     static_cast<unsigned long long>(h->reader_pos.load()), static_cast<unsigned long long>(h->head.load()),
                     static_cast<unsigned long long>(h->reader_resyncs.load()));
        std::fflush(stderr);
        s.live = nullptr;   // the mapping stays until the process ends, so a reader can drain the tail
    }
    if (s.inproc) {   // inproc1: the end of the feed; the renderer drains it and writes its shots / summaries
        s.inproc = false;
        const char* w = std::getenv("B3PORT_RENDER_WAIT_MS");
        const std::uint32_t ms = w && *w ? std::uint32_t(std::strtoul(w, nullptr, 10)) : 120000U;
        std::fprintf(stderr, "[B3SCENE] in-process renderer: %llu polls handed over (%llu keyframes, %llu dropped), game-thread cost"
                     " mean %.1f us, max %.1f us (poll %u); closing the feed (wait up to %u ms)\n",
                     static_cast<unsigned long long>(s.inproc_polls), static_cast<unsigned long long>(s.inproc_keys),
                     static_cast<unsigned long long>(s.inproc_dropped), s.inproc_polls ? double(s.inproc_ns) / double(s.inproc_polls) / 1e3 : 0.0,
                     double(s.inproc_max_ns) / 1e3, s.inproc_max_poll, ms);
        std::fprintf(stderr, "[B3SCENE] the whole per-poll hook (guest reads, record, hand-over): mean %.1f us, max %.1f us\n",
                     s.inproc_polls ? double(s.hook_ns) / double(s.inproc_polls) / 1e3 : 0.0, double(s.hook_max_ns) / 1e3);
        std::fflush(stderr);
        const int rc = s.render.close(ms);
        std::fprintf(stderr, "[B3SCENE] in-process renderer: rc %d\n", rc);
        std::fflush(stderr);
    }
    if (s.out == nullptr) return;
    std::fclose(s.out);
    s.out = nullptr;
    std::fprintf(stderr, "[B3SCENE] %llu records, %llu bytes\n", static_cast<unsigned long long>(s.records),
                 static_cast<unsigned long long>(s.bytes_written));
    std::fflush(stderr);
}
