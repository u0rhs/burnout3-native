// code_mods.h -- the code-mod loader and the B3ModApi behind it (mod kit Phase 2b, docs/modding/DESIGN.md §2,
// sdk/include/b3mod_api.h; docs/modding/sdk-reference.md).
//
// native_play builds one CodeMods after the port's own wrappers are on the hook table and before hooks.seal(), and
// load()s the active code mods in load order: each library is opened (dlopen RTLD_NOW | RTLD_LOCAL / LoadLibraryExW
// with its absolute path), b3mod_built_for() is checked, and b3mod_main(api, self) registers the mod's hooks, settings
// rows, callbacks and arena memory. Then, all or nothing, its hooks go on the table as "mod:<id>" (replace -> kReplace,
// wrap -> kWrap). A refusal anywhere (b3mod_main returning non-zero, an address without a native function, a second
// replacement of an address, a platform-tier address without Developer mods, any registration call refused) leaves the
// mod out entirely: its arena memory is given back, its rows, callbacks and deferred writes dropped, the library closed,
// and the reason returned (the log, the launcher's Mods page through mods-state.ini).
//
// At run time, on the game's thread: vblank() at each virtual VBlank (after native_play applied the settings changes
// for it: settings().boundary), frame() at each game frame (the overlay's items, then overlay_record()), shutdown() at a
// normal end. Everything the platform gives (the VBlank count, disc files, the renderer's textures, the start values of
// settings) comes through Host, so the tests run the loader on a synthetic hook table (tests/test_code_mods.cpp).
#ifndef B3NATIVE_CODE_MODS_H
#define B3NATIVE_CODE_MODS_H

#include "hook_table.h"

#include "b3mod/codemod.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct B3ModSelf;   // sdk/include/b3mod_api.h (defined in code_mods.cpp)
struct B3ModApi;    // sdk/include/b3mod_api.h

namespace b3native {

class CodeMods {
public:
    struct Host {
        std::string port_version;
        std::function<std::uint64_t()> vblank;
        // a disc file through the data overlay ("DATA/GLOBALUS.BIN"): its size (-1 none) / bytes from offset (-1 none)
        std::function<std::int64_t(const std::string& disc_path)> disc_size;
        std::function<std::int64_t(const std::string& disc_path, std::uint64_t offset, void* dst, std::uint64_t n)> disc_read;
        // a mod's texture for the renderer (id unique in the process)
        std::function<void(std::uint32_t id, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba)> texture;
        // modsec: the renderer may drop that texture (its mod did not load, or its script stopped or reloaded)
        std::function<void(std::uint32_t id)> texture_free;
        // a mod's settings values as saved (key -> text; window runs: mod-settings/<id>.ini; else none)
        std::function<std::map<std::string, std::string>(const std::string& id)> saved_values;
        // one line of the log (default: stdout)
        std::function<void(const std::string& line)> log;
    };
    // lua6: a mod whose entry point is in the program itself rather than in a library (a script mod: script_mods.h).
    // main() is what b3mod_main is to a library: it registers through `api`, 0: loaded. The loader keeps the object as
    // long as the mod (so a mod that is not loaded drops it at once), and treats the mod as it treats a library's.
    struct Builtin {
        virtual ~Builtin() = default;
        virtual int main(const B3ModApi* api, B3ModSelf* self) = 0;
        virtual const char* kind() const = 0;   // the log's word for what loaded ("script")
    };
    struct Candidate {
        std::string id, version;
        std::filesystem::path dir, library;   // library: absolute ("" : none for this system)
        bool platform = false;                // [code] platform = yes
        std::shared_ptr<Builtin> builtin;     // lua6: set instead of a library (script mods)
        // the set of files the user allowed (b3mod::trust_digest over dir with `code`): when given, the loader hashes
        // the set again right before it loads the library and refuses it when it differs (a file changed after the
        // check); on Windows the files are held open without write or delete sharing from that hash to the load
        b3mod::CodeSpec code;
        std::string trust_digest;
    };
    struct Result {
        std::string id;
        bool loaded = false;
        std::string reason;   // not loaded: why (one line)
        unsigned hooks = 0, rows = 0;
    };

    CodeMods(HookTable& table, GuestArena& arena, Host host);
    ~CodeMods();
    CodeMods(const CodeMods&) = delete;
    CodeMods& operator=(const CodeMods&) = delete;

    // Loads `mods` in load order (the first wins a replace / replace conflict). developer: the user's Developer mods
    // (a mod with [code] platform = yes may then hook the platform tier). One result per candidate.
    std::vector<Result> load(const std::vector<Candidate>& mods, bool developer);

    bool any() const { return !loaded_.empty(); }
    std::vector<std::string> loaded_ids() const;
    b3mod::LiveSettings& settings() { return settings_; }

    // ---- the game's thread, after load ----
    void vblank();                   // every on_vblank callback, in load order
    void frame();                    // every on_frame callback, in load order (the mods queue this frame's overlay)
    // The overlay record when this frame's items differ from the last one published (else empty); after frame()
    std::vector<std::uint8_t> overlay_record();
    void shutdown();                 // every on_shutdown callback, in reverse load order (once)

    // modsec: a mod's textures (B3ModApi.texture) are capped per mod: kModTextures of them, kModTextureBytes in all
    // (RGBA); beyond that texture() refuses (0, one log line). release_textures drops a mod's own (the renderer's
    // copies too): the loader does it for a mod that did not load, a script mod when it stops or reloads.
    static constexpr std::size_t kModTextures = 64;
    static constexpr std::uint64_t kModTextureBytes = std::uint64_t(16) << 20;
    static void release_textures(B3ModSelf* s);

    // the API's implementation (sdk/include/b3mod_api.h); public for the C trampolines in code_mods.cpp
    struct Impl;

private:
    HookTable& table_;
    GuestArena& arena_;
    Host host_;
    std::vector<std::unique_ptr<B3ModSelf>> loaded_;
    b3mod::LiveSettings settings_;
    std::vector<b3mod::OverlayItem> last_overlay_;
    std::uint32_t overlay_serial_ = 0;
    bool overlay_published_ = false, shut_ = false;
    friend struct Impl;
};

// The program's code range (never runs natively: a write there is refused) [C config/b3_retail.yaml: .text from file
// offset 0 at 0x100000 to the first data slice at 0x383F00]
constexpr b3host::gaddr kProgramCodeBegin = 0x00100000u, kProgramCodeEnd = 0x00483F00u;

}  // namespace b3native

#endif
