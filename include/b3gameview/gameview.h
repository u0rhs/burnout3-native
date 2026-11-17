// b3gameview/gameview.h -- the game view (pic1, docs/native-renderer.md §32): the scene and HUD capture that feeds the
// native renderer, independent of the backend that runs the game.
//
// The capture code (src/gameview/scene_capture.cpp, src/gameview/hud_capture.cpp) reads guest memory at retail
// addresses and builds the per-poll records of docs/native-renderer.md §12.1 / §17.2 (the .b3scene format). It depends
// only on what this header declares. A backend adapter supplies:
//   - the guest memory, as read-only views of main RAM and the scratchpad (Memory), or callbacks where the backend has
//     no plain view;
//   - the entry observations (on_entry): before the first instruction of each function entry_points() lists, the pc
//     and the argument registers a0..t3;
//   - the poll (on_poll): once per retail controller poll (the input update of the 1/60 s tick loop), with the poll
//     number and the backend's VBlank count;
//   - two host callbacks (Host): the record's schedule stamp, and extra publishers that run where the HUD publishes.
// Adapters: tools/native/gameview_native.cpp (the native backend: the arena, b3host's watch hook) and
// tools/native/gameview_replay.cpp (a .b3scene played into an arena: the plumbing proof).
//
// Outputs and switches are the capture's own (unchanged from inproc1 / lat1): B3PORT_SCENE_OUT=<file>,
// B3PORT_SCENE_LIVE=<ring>, B3PORT_RENDER=<b3render library> with B3PORT_RENDER_ARGS, B3PORT_SCENE_TIMES, and the
// B3PORT_SCENE_HUD* / _MOVIE / _FRONTEND switches. Nothing here writes guest memory or changes guest timing.
#ifndef B3GAMEVIEW_GAMEVIEW_H
#define B3GAMEVIEW_GAMEVIEW_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace b3gv {

inline constexpr std::uint32_t kRamLo = 0x00100000U, kRamEnd = 0x02000000U;   // the capture reads main RAM above 1 MB
inline constexpr std::uint32_t kSprBase = 0x70000000U, kSprSize = 0x4000U;

// Guest memory, read only. A view (ram / spr) is used where set; else the callbacks; else the read gives 0.
struct Memory {
    const std::uint8_t* ram = nullptr;   // guest physical 0 .. ram_size (little-endian host, as the guest)
    std::uint32_t ram_size = 0;
    const std::uint8_t* spr = nullptr;   // 0x70000000 .. +16 KB
    const void* ctx = nullptr;
    std::uint8_t (*read_u8)(const void* ctx, std::uint32_t a) = nullptr;
    std::uint32_t (*read_u32)(const void* ctx, std::uint32_t a) = nullptr;
    const std::uint8_t* (*view)(const void* ctx, std::uint32_t a, std::uint32_t n) = nullptr;   // n bytes of RAM at a

    bool in_ram(std::uint32_t a, std::uint32_t n) const { return ram && a < ram_size && n <= ram_size - a; }
    bool in_spr(std::uint32_t a, std::uint32_t n) const {
        return spr && a >= kSprBase && a < kSprBase + kSprSize && n <= kSprBase + kSprSize - a;
    }
    std::uint8_t u8(std::uint32_t a) const {
        if (in_ram(a, 1)) return ram[a];
        if (in_spr(a, 1)) return spr[a - kSprBase];
        return read_u8 ? read_u8(ctx, a) : 0;
    }
    std::uint32_t u32(std::uint32_t a) const {
        std::uint32_t v = 0;
        if (in_ram(a, 4)) { std::memcpy(&v, ram + a, 4); return v; }
        if (in_spr(a, 4)) { std::memcpy(&v, spr + (a - kSprBase), 4); return v; }
        if (read_u32) return read_u32(ctx, a);
        if (read_u8) for (std::uint32_t k = 0; k < 4; ++k) v |= std::uint32_t(read_u8(ctx, a + k)) << (8 * k);
        return v;
    }
    // n bytes of main RAM at a (null if the backend cannot give a view)
    const std::uint8_t* bytes(std::uint32_t a, std::uint32_t n) const {
        if (in_ram(a, n)) return ram + a;
        return view ? view(ctx, a, n) : nullptr;
    }
};

struct Host {
    // lat1: the host steady-clock time (ns) the input of the tick this poll's record shows was latched; 0 = none.
    std::uint64_t (*tick_stamp)(std::uint64_t poll) = nullptr;
    // the backend's host records (the native capture: the game's fonts, the video skip hint, the menu record), published at
    // the HUD's publish point.
    void (*extra_publish)() = nullptr;
    void (*extra_report)() = nullptr;   // lines after the HUD's summary at the end
};
void set_host(const Host& h);
const Host& host();

// True when any output is configured (file, ring, in-process renderer). Cheap after the first call.
bool active();

// ---- the HUD side's entries (hud_capture.cpp) ----
// The function entries the HUD capture observes, under the current switches (B3PORT_SCENE_HUD_OBJECTS / _MARKERS /
// _FRONTEND); empty when the HUD capture is off (no output, or B3PORT_SCENE_HUD=0). Stable for the process.
const std::uint32_t* entry_points(std::size_t* count);
// The adapter says how many of entry_points() it could hook; the HUD capture is on only if all of them. Returns the
// movie-picture period (B3PORT_SCENE_MOVIE; 0 = off).
std::uint32_t hud_enable(std::size_t hooked, std::size_t wanted, const char* who);
bool hud_on();
// One observation before the first instruction of the function at pc: arg = a0, a1, a2, a3, t0, t1, t2, t3 (GPR
// 4..11, low words). Idempotent for the same call observed twice.
void on_entry(std::uint32_t pc, const std::uint32_t arg[8], const Memory& m);

// ---- split2: CB3World::Render(idx)'s entry (scene_capture.cpp; docs/native-renderer.md §29.6) ----
// The adapter calls this at the entry of 0x276300 with a1 (the view index); reads only. In a 2-player split screen,
// idx 1 copies view 1's per-vehicle Draw results for the next poll's record.
constexpr std::uint32_t kWorldRender = 0x00276300U;
void on_world_render(int idx, const Memory& m);

// ---- the poll (scene_capture.cpp) ----
void on_poll(std::uint64_t poll, std::uint64_t vblank, const Memory& m);
// The session's end: closes the outputs (the in-process renderer gets B3PORT_RENDER_WAIT_MS to finish).
void finish();

// ---- replay support (gameview_replay) ----
// While set, the HUD publish point calls this instead of publishing the HUD (the replay passes the original
// record's HUD runs through). Null: the HUD publishes as usual.
using PublishHook = void (*)(void* ctx);
void set_hud_publish_override(PublishHook hook, void* ctx);
// The poll's record body so far (the runs after the 16-byte TICK head) and its run count; valid inside on_poll.
const std::uint8_t* record_so_far(std::size_t* bytes, std::uint32_t* runs);

}  // namespace b3gv

// The capture writer, valid only inside on_poll (the HUD publish and the extra publishers write through these).
void b3scene_grab(std::uint32_t a, std::uint32_t n);
void b3scene_grab_host(std::uint32_t a, const std::uint8_t* p, std::uint32_t n);
bool b3scene_unchanged(std::uint32_t a, std::uint32_t n);
std::uint32_t b3scene_u32(std::uint32_t a);
void b3scene_grab_vehicle(std::uint32_t ep, std::uint32_t vp);

#endif
