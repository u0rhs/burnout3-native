// pic1 (docs/native-renderer.md §32): the game view's native hook path without the generated table or a renderer. Calls
// go through b3host::traced_entry as the native table's entries do; the watch hook must hand the HUD's entries (with
// the argument registers) and UpdatePort(0)'s returns (the poll) to the capture, in this order (the latch's oracle hook
// first, then the capture), and the capture must write them into its record.
#include "test.h"

#include "b3gameview/gameview.h"
#include "b3host/native_backend.h"
#include "gameview_native.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr b3host::gaddr kUpdatePort = 0x00214610u, kBegin = 0x0021A0F0u, kSetTexture = 0x00219A20u, kOther = 0x00214A80u;

std::vector<std::string> g_order;   // hook and function events, in order
std::uint64_t g_vb = 100;
std::uint64_t vblank() { return g_vb; }

// play1: UpdatePort is a member: $a0 = this (the input manager), $a1 = the port
constexpr std::uint32_t kManager = 0x004F4D10u;   // [G] any guest address: the test only needs a non-zero `this`
void fake_update_port(const b3host::CallArgs& a, b3host::CallResult&) { g_order.push_back("UpdatePort(" + std::to_string(a.gpr[1].w[0]) + ")"); }
void fake_begin(const b3host::CallArgs&, b3host::CallResult&) { g_order.push_back("Begin"); }
void fake_set_texture(const b3host::CallArgs&, b3host::CallResult&) { g_order.push_back("SetTexture"); }
void fake_other(const b3host::CallArgs&, b3host::CallResult&) { g_order.push_back("other"); }
void oracle(b3host::gaddr, const b3host::CallArgs&, const b3host::CallResult* r) { g_order.push_back(r ? "oracle after" : "oracle before"); }

b3host::CallArgs args(std::uint32_t a0, std::uint32_t a1 = 0) {
    b3host::CallArgs a;
    a.gpr[0].w[0] = a0;
    a.gpr[1].w[0] = a1;
    a.n_gpr = 2;
    return a;
}

std::uint32_t le32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }

}  // namespace

TEST(native_watch_path_to_records) {
    const std::string out = "build/test_gameview_native.b3scene";
    setenv("B3PORT_SCENE_OUT", out.c_str(), 1);
    setenv("B3PORT_RENDER", "", 1);   // no renderer here (gameview_replay proves that part)
    unsetenv("B3PORT_SCENE_LIVE");
    unsetenv("B3PORT_SCENE_HUD");
    unsetenv("B3PORT_SCENE_HUD_OBJECTS");
    b3host::NativeBackend nb;
    b3host::BackendScope scope(nb);
    b3gv_native::Options o;
    o.mode = "offscreen";
    o.vblank = vblank;
    CHECK(b3gv_native::install(o, "."));
    CHECK(b3gv::hud_on());
    std::size_t np = 0;
    b3gv::entry_points(&np);
    CHECK(np > 40);   // the six Gt2d entries, the scene walk, markers, front end and the object callbacks
    CHECK(b3host::watched<kBegin>() && b3host::watched<kSetTexture>() && b3host::watched<kUpdatePort>());
    CHECK(!b3host::watched<kOther>());
    // the oracle hook (native_boot's poll point) on UpdatePort as well: it must stay first at the return
    b3host::g_oracle_hook = oracle;
    b3host::g_oracle_target = kUpdatePort;

    b3host::CallResult r;
    // poll 1: a 2D pass with a texture bind, then UpdatePort(0) and (1)
    nb.ram()[0x00300000u + 0x8C] = 0x55;   // the raster's +0x8C (texture-cache flag) as the capture records it
    b3host::traced_entry<kBegin, fake_begin>(args(0), r);
    b3host::traced_entry<kSetTexture, fake_set_texture>(args(0x00300000u, 7), r);
    b3host::traced_entry<kOther, fake_other>(args(0), r);
    // decomp2host's prologue (option A, b3host/watch.h): an object render callback reporting itself, with arena
    // pointers (guest addresses in the event) -- CGt2dObjectImage::_Render(space, data), a static member
    b3host::watch_self<0x001D5FB0u>(nb.ram().data() + 0x00310000u, nb.ram().data() + 0x00320028u);
    b3host::watch_self<kOther>(nb.ram().data() + 0x00310000u);   // not watched: nothing
    b3host::traced_entry<kUpdatePort, fake_update_port>(args(kManager, 0), r);
    b3host::traced_entry<kUpdatePort, fake_update_port>(args(kManager, 1), r);
    g_vb = 102;
    b3host::traced_entry<kUpdatePort, fake_update_port>(args(kManager, 0), r);   // poll 2 (no HUD events)
    b3host::g_oracle_hook = nullptr;
    b3host::g_oracle_target = 0;
    b3gv_native::finish();
    CHECK(!b3host::g_watch_hook);

    const std::vector<std::string> want = {"Begin", "SetTexture", "other", "oracle before", "UpdatePort(0)", "oracle after",
                                           "oracle before", "UpdatePort(1)", "oracle after", "oracle before", "UpdatePort(0)", "oracle after"};
    CHECK(g_order == want);

    // the records: two polls (port 0 only), their VBlanks, and poll 1's HUD region with Begin / State / Texture
    std::FILE* f = std::fopen(out.c_str(), "rb");
    CHECK(f != nullptr);
    if (!f) return;
    std::vector<std::uint8_t> b(1 << 20);
    b.resize(std::fread(b.data(), 1, b.size(), f));
    std::fclose(f);
    CHECK(b.size() > 16 && std::memcmp(b.data(), "B3SCENE", 8) == 0);
    std::size_t at = 16;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> polls;
    bool hud = false, tex_event = false, begin_event = false, object_event = false;
    while (at + 16 <= b.size()) {
        CHECK_BITS(le32(&b[at]), 0x4B434954u);
        polls.emplace_back(le32(&b[at + 4]), le32(&b[at + 8]));
        const std::uint32_t nruns = le32(&b[at + 12]);
        at += 16;
        for (std::uint32_t j = 0; j < nruns && at + 8 <= b.size(); ++j) {
            const std::uint32_t a = le32(&b[at]), n = le32(&b[at + 4]);
            const std::uint8_t* p = &b[at + 8];
            if (a == 0x40000000u && n >= 16 && le32(p) == 0x31445548u) {   // 'HUD1' {frames, event bytes, missed}
                hud = polls.back().first == 1;
                const std::uint32_t ev = le32(p + 8);
                for (std::uint32_t q = 16; q + 8 <= 16 + ev && q + 8 <= n;) {
                    const std::uint32_t kind = le32(p + q), len = le32(p + q + 4);
                    if (kind == 1) begin_event = true;
                    if (kind == 10 && len == 64) object_event = le32(p + q + 8) == 0x001D5FB0u && le32(p + q + 12) == 0x00320000u;
                    if (kind == 3 && len == 16) tex_event = le32(p + q + 8) == 0x00300000u && le32(p + q + 12) == 7 && le32(p + q + 16) == 0x55u;
                    q += 8 + ((len + 3) & ~3u);
                }
            }
            at += 8 + n;
        }
    }
    CHECK(polls.size() == 2);
    if (polls.size() == 2) {
        CHECK(polls[0].first == 1 && polls[0].second == 100);
        CHECK(polls[1].first == 2 && polls[1].second == 102);
    }
    CHECK(hud);
    CHECK(begin_event);
    CHECK(object_event);   // {pc, object = data - 0x28, ...} from the prologue's arguments
    CHECK(tex_event);   // {a0 = the texture, a1 = the palette, the raster's +0x8C from the arena, flush}
}

TEST_MAIN("test_gameview_native (pic1: the game view's native hook path)")
