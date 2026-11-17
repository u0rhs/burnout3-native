/* b3mod_api.h -- the code-mod API of the Burnout 3 PC port (mod kit part 2, docs/modding/DESIGN.md §2;
 * reference: docs/modding/sdk-reference.md).
 *
 * A code mod is a shared library (.dll on Windows, .so on Linux, .dylib on macOS) named by the [code] section of its
 * mod.ini (with [mod] api = 2). native_play loads it before the game's first instruction and calls its one entry point:
 *
 *     B3MOD_EXPORT int b3mod_main(const B3ModApi* api, B3ModSelf* self);   0: loaded; anything else: refused
 *     B3MOD_DECLARE_VERSION()                                              the API version this header has (once per mod)
 *
 * Everything a mod registers (hooks, settings rows, event callbacks, arena memory) is registered inside b3mod_main;
 * afterwards those calls are refused. A mod's registrations go in all or nothing: when one of them is refused (a hook on
 * an address without a native function, a second replacement of an address another mod replaces), the mod is not
 * loaded at all and the log says why.
 *
 * The ABI is C only: one struct of function pointers, `size` and `version` first. New members are only ever added at
 * the end (a mod checks api->size before using a member newer than the version it was built for); a change to an
 * existing member makes a new B3MOD_API_VERSION. Every string is UTF-8; every guest address is a 32-bit EE address of
 * the retail program (SLUS-21050), as the decomp, gen/native/manifest.tsv and config/symbol_addrs.txt name them.
 *
 * Threads: every function here runs on the game's thread, and every callback a mod registers is called on it (hooks,
 * on_vblank, on_frame, on_shutdown), so a mod needs no locks for its own state.
 *
 * Determinism: a mod that bases what the game does on api->vblank(), guest memory and its settings (never on the
 * wall clock, never on host randomness) plays back exactly from a replay. Settings values reach the mod at VBlank
 * boundaries only and are recorded in pad recordings and replay files.
 */
#ifndef B3MOD_API_H
#define B3MOD_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The version of the B3ModApi struct this header describes (ABI version). Mods declare it with B3MOD_DECLARE_VERSION. */
#define B3MOD_API_VERSION 1u
/* The [mod] api a code mod's mod.ini gives (the mod API of the port; data-only mods may still say 1). */
#define B3MOD_MANIFEST_API 2

#if defined(_WIN32)
#define B3MOD_EXPORT __declspec(dllexport)
#else
#define B3MOD_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
#define B3MOD_EXTERN_C extern "C"
#else
#define B3MOD_EXTERN_C
#endif
/* Put once in one source file of the mod: exports b3mod_built_for(), which tells the loader this header's version. */
#define B3MOD_DECLARE_VERSION() \
    B3MOD_EXTERN_C B3MOD_EXPORT unsigned b3mod_built_for(void) { return B3MOD_API_VERSION; }

/* A colour: red in the low byte, then green, blue, alpha (0 transparent .. 255 opaque). */
#define B3MOD_RGBA(r, g, b, a) \
    ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

/* The loaded mod, as the port knows it (opaque). Passed to every call that acts for the mod. */
typedef struct B3ModSelf B3ModSelf;
/* One running hook: what B3ModApi.next needs to run the layer below (opaque, valid during the hook's call only). */
typedef struct B3ModHookCtx B3ModHookCtx;

/* A call's argument registers, as the EE ABI passes them (b3host::CallArgs). Integer and pointer arguments go in
 * a0..a3, t0..t3 in order (32-bit values sign-extended to 64 bits); a 128-bit value takes one whole register (its
 * upper half in gpr_hi); float arguments go in f12..f19 as raw bits; more arguments are on the guest stack from sp. */
typedef struct B3ModRegs {
    uint64_t gpr[8];    /* a0, a1, a2, a3, t0, t1, t2, t3: the low 64 bits */
    uint64_t gpr_hi[8]; /* their upper 64 bits (128-bit arguments only) */
    uint32_t fpr[8];    /* f12 .. f19, raw IEEE bits */
    uint32_t sp;        /* the guest stack pointer at the call (0 in B3ModApi.call: the game thread's current one) */
    uint32_t reserved;
} B3ModRegs;

/* A call's results: v0 (all 128 bits), v1, f0 (raw bits). */
typedef struct B3ModResult {
    uint64_t v0;
    uint64_t v0_hi;
    uint64_t v1;
    uint32_t f0;
    uint32_t reserved;
} B3ModResult;

/* A hook: runs instead of the function at its address. ctx runs the layer below it through api->next (the game's own
 * function, or another hook); a hook that does not call next replaces the call entirely. */
typedef void (*B3ModHookFn)(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result, void* user);
/* An event callback (on_vblank, on_frame, on_shutdown). */
typedef void (*B3ModEventFn)(void* user);

enum { B3MOD_LOG_INFO = 0, B3MOD_LOG_WARN = 1, B3MOD_LOG_ERROR = 2 };
enum { B3MOD_SETTING_TOGGLE = 0, B3MOD_SETTING_CHOICE = 1, B3MOD_SETTING_SLIDER = 2 };

/* A settings row (B3ModApi.add_setting). Its value is an index: a toggle 0 Off / 1 On, a choice 0..count-1, a slider
 * 0..count-1 (count levels). */
typedef struct B3ModSetting {
    uint32_t size;               /* sizeof(B3ModSetting) */
    uint32_t kind;               /* B3MOD_SETTING_* */
    const char* key;             /* a-z 0-9 _ - . ; 1..32 characters, unique in the mod */
    const char* label;           /* the row's name (the game's page shows it in upper case); up to 40 characters */
    const char* description;     /* one or two sentences (may be NULL) */
    const char* const* values;   /* a choice's value names (count of them); NULL for a toggle or a slider */
    uint32_t count;              /* a choice: its values (2..16); a slider: its levels (2..101); a toggle: 0 or 2 */
    uint32_t default_value;      /* the index used until the player changes it */
} B3ModSetting;

typedef struct B3ModApi {
    uint32_t size;      /* sizeof(B3ModApi) as the port has it: members past this size are not there */
    uint32_t version;   /* B3MOD_API_VERSION of the port */

    /* ---- info ---- */
    /* The port's version text, e.g. "0.9.3 (abc1234)". */
    const char* (*port_version)(void);
    /* The virtual VBlank count (60 per second of game time): the clock a mod uses. */
    uint64_t (*vblank)(void);
    /* The mod's id (mod.ini [mod] id) and its folder (an absolute path). */
    const char* (*mod_id)(const B3ModSelf* self);
    const char* (*mod_dir)(const B3ModSelf* self);

    /* ---- log ---- */
    /* One line into the game's log, as "mod <id>: <text>" (level B3MOD_LOG_*). */
    void (*log)(B3ModSelf* self, int level, const char* text);

    /* ---- hooks (inside b3mod_main only) ---- */
    /* Replace the function at `addr`: at most one mod per address (a second one is a conflict and is not loaded). The
     * port's own wrappers stay above a replacement. 1: registered; 0: refused (the log says why). */
    int (*replace)(B3ModSelf* self, uint32_t addr, B3ModHookFn fn, void* user);
    /* Wrap the function at `addr`: any number per address; the first mod in the load order is on top. 1 / 0 as above. */
    int (*wrap)(B3ModSelf* self, uint32_t addr, B3ModHookFn fn, void* user);
    /* Inside a hook: run the layer below with `regs` (the hook's own or changed ones), its results into `result`. */
    void (*next)(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result);
    /* Call the game function at `addr` with `regs` (regs->sp 0: the current guest stack), through every hook on it.
     * 1: called; 0: no native function at that address (nothing was called). */
    int (*call)(B3ModSelf* self, uint32_t addr, const B3ModRegs* regs, B3ModResult* result);
    /* 1 when the game function at `addr` is native (hookable and callable), else 0. */
    int (*has_function)(B3ModSelf* self, uint32_t addr);

    /* ---- guest memory ---- */
    /* Copy n bytes from / to guest memory: EE RAM (32 MB) and the scratchpad. 1: done; 0: a byte outside them, or (write)
     * in the program's code, which never runs natively (use a hook). Writes made inside b3mod_main are applied when the
     * mod has loaded (all or nothing). */
    int (*read)(B3ModSelf* self, uint32_t addr, void* dst, uint32_t n);
    int (*write)(B3ModSelf* self, uint32_t addr, const void* src, uint32_t n);
    /* Guest memory of the mod's own (inside b3mod_main only): `bytes` zeroed bytes at a multiple of `align` (a power of
     * two up to 4096) in the mod arena, the same address in every run with the same mods. 0: none left. */
    uint32_t (*alloc)(B3ModSelf* self, uint32_t bytes, uint32_t align);

    /* ---- settings ---- */
    /* A row on PC SETTINGS > MODS and on the launcher's Mods page (inside b3mod_main only). 1: added; 0: refused. */
    int (*add_setting)(B3ModSelf* self, const B3ModSetting* setting);
    /* The row's value now (an index); -1 for a key the mod did not add. A change the player makes reaches the mod at
     * the next VBlank boundary (and is recorded, so a replay changes it on the same VBlank). */
    int (*get_setting)(B3ModSelf* self, const char* key);

    /* ---- overlays: 2D drawn over the game, after its HUD (never touching guest state) ---- */
    /* A texture from w x h RGBA pixels (straight alpha, rows top first; up to 1024 x 1024). Returns its id (0: refused). */
    uint32_t (*texture)(B3ModSelf* self, uint32_t w, uint32_t h, const uint8_t* rgba);
    /* A rectangle in the game's 640 x 448 screen (the 4:3 picture, top left 0, 0), filled with `rgba`, or with the
     * texture tinted by it (texture 0: none). */
    void (*quad)(B3ModSelf* self, float x, float y, float w, float h, uint32_t rgba, uint32_t texture);
    /* Text from (x, y) (its top left), letters `size` high, in the port's stroke font (A-Z, 0-9, space and + - / . : ;
     * lower case drawn as upper case). */
    void (*text)(B3ModSelf* self, float x, float y, float size, uint32_t rgba, const char* utf8);
    /* What a mod draws between two game frames is shown from the next frame until the frame after: draw it again in
     * every on_frame. Up to 256 items per mod and frame. */

    /* ---- events (inside b3mod_main only) ---- */
    /* At every virtual VBlank, after the port's own VBlank work (settings changes for this VBlank are in by then). */
    int (*on_vblank)(B3ModSelf* self, B3ModEventFn fn, void* user);
    /* At every game frame (the game's poll of the pad), before the frame goes to the renderer: draw overlays here. */
    int (*on_frame)(B3ModSelf* self, B3ModEventFn fn, void* user);
    /* When the run ends normally (not after a crash), in the reverse of the load order. */
    int (*on_shutdown)(B3ModSelf* self, B3ModEventFn fn, void* user);

    /* ---- files (read only) ---- */
    /* `path`: "disc:DATA/GLOBALUS.BIN" -- a disc file as the game gets it (through the active mods' files); any other
     * path -- a file in the mod's own folder ("data/table.bin"; no "..", no absolute path). The size, or -1: no file. */
    int64_t (*data_size)(B3ModSelf* self, const char* path);
    /* Up to n bytes from `offset` into dst: the bytes read, or -1 (no file). */
    int64_t (*read_data)(B3ModSelf* self, const char* path, uint64_t offset, void* dst, uint64_t n);
} B3ModApi;

/* The entry point a mod defines. */
B3MOD_EXTERN_C B3MOD_EXPORT int b3mod_main(const B3ModApi* api, B3ModSelf* self);

#ifdef __cplusplus
}
#endif

#endif /* B3MOD_API_H */
