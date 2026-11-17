// The native backend's platform tier, IOP start-up part (boot1; src/native/platform.h). The EE kernel's objects are in
// platform_kernel.cpp, its virtual time and interrupt delivery in platform_time.cpp.
//
// The EE libraries below talk to the IOP (SIF RPC, IOP reboot, the CD/DVD driver) or the kernel's thread tables. A
// native build has no IOP and no EE kernel: the S5 platform layer (docs/standalone.md §5) replaces what they serve
// (files from the extracted disc, pads, saves, sound) at the game's own library entry points. So these stubs do
// nothing on hardware, return what retail's caller needs, are fatal on any argument they do not model, and log
// that the library's own state in guest memory (libsif / libcdvd / libkernl .data/.bss) is left as the image has it:
// game code reads that state only through library functions, each a platform-tier entry or MISSING [L].
//
// Evidence for the return values: SDK 3.0.3 headers (common/include/libcdvd.h, ee/include/sifdev.h, sifrpc.h) and
// sample (ee/sample/inet/libnet/ball_game/ee/main.c:312-323, the same start-up sequence as CB3Game::
// ConstructHardware 0x1D45B0: `while (!sceSifRebootIop(...)) {}`, `while (!sceSifSyncIop()) {}`).
#include "platform.h"
#include "platform_sif.h"

#include "b3host/call.h"
#include "b3host/native_backend.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace b3native::platform {
namespace {

using b3host::CallArgs;
using b3host::CallResult;

inline std::int32_t arg(const CallArgs& a, unsigned i) { return static_cast<std::int32_t>(a.gpr[i].w[0]); }
inline void ret(CallResult& r, std::int32_t v) {
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(v)));
}

}  // namespace

// void sceSifInitRpc(unsigned int mode) [C sifrpc.h:94]: SIF RPC start-up; mode 0 only. S4: no RPC is made or served
// (platform_sif.h), so libsif's RPC data (its packet pools, the packet-id counter) is not set up.
void sif_init_rpc(const CallArgs& a, CallResult& r) {
    if (arg(a, 0) != 0) stub_unexpected("sceSifInitRpc", "mode " + std::to_string(arg(a, 0)));
    r = CallResult{};
}
// int sceSifRebootIop(const char *img) [C sifdev.h:390]: nonzero = the reboot request went out [C SDK sample loop].
// The image name (retail: "cdrom0:\\IOP\\IOPRP...;1", D_004B80F0) has no meaning without an IOP: logged.
void sif_reboot_iop(const CallArgs& a, CallResult& r) {
    std::string img;
    for (b3host::gaddr p = a.gpr[0].w[0]; p && b3host::load<std::uint8_t>(p) && img.size() < 80; ++p)
        img.push_back(static_cast<char>(b3host::load<std::uint8_t>(p)));
    stub_note("sceSifRebootIop", "no IOP to reboot (image \"" + img + "\"); libsif state not modelled");
    ret(r, 1);
}
// int sceSifSyncIop(void) [C sifdev.h:391]: nonzero = the IOP finished rebooting [C SDK sample loop].
void sif_sync_iop(const CallArgs&, CallResult& r) { ret(r, 1); }
// int sceSifLoadFileReset(void) [C sifdev.h:377]: reset the IOP-side loader binding after the reboot; return 0 [L: the
// SDK's success value; the caller ignores it]. (sceFsReset: platform_devices.cpp.)
void sif_load_file_reset(const CallArgs&, CallResult& r) {
    stub_note("sceSifLoadFileReset", "no IOP: libsif's load-file RPC binding not initialised");
    ret(r, 0);
}
// CB3RaceCarPhysicsAttribs::Release() 0x254090 [C raw bytes 0x08094A00: `j 0x252800`, a tail call; the decomp defines
// it in its proven unit B3RaceCarPhysicsAttribs.cpp but its symbol files carry no type:func line for 0x254090, so the
// generator does not see it]: the call goes on to 0x252800 with the same registers.
void race_car_physics_attribs_release(const CallArgs& a, CallResult& r) {
    b3host::detail::dispatch(0x00252800u, a, r);
}
// int lgKbdGetLedState(int keyboard, u16 *leds) [C asm 0x306668, liblgkbd]: keyboard k = the argument - 1; when the
// opened-keyboard count (D_01ECE348 + 0x40) is less than k, or k < 0: 0x80000004 (retail's check, so k == count passes
// as in retail); else *leds = the device record's LED bits (D_01ECE348 + 0x110 k + 0xA8, low five bits) and 0. The
// on-screen keyboard (CB3OnScreenKeyboardComponent::Show) asks it for the Caps Lock state.
void lg_kbd_get_led_state(const CallArgs& a, CallResult& r) {
    const std::int32_t k = static_cast<std::int32_t>(a.gpr[0].w[0]) - 1;
    constexpr b3host::gaddr kLgKbd = 0x01ECE348u;
    if (b3host::load<std::int32_t>(kLgKbd + 0x40) < k || k < 0) { ret(r, static_cast<std::int32_t>(0x80000004u)); return; }
    const std::uint8_t leds = b3host::load<std::uint8_t>(kLgKbd + 0x110u * static_cast<std::uint32_t>(k) + 0xA8u) & 0x1Fu;
    b3host::store<std::uint16_t>(a.gpr[1].w[0], leds);
    ret(r, 0);
}
// int sceVibSetActParam(int port, int slot, int profile_size, u_char *profile, int data_size?, u_char *data) as retail
// 0x217520 builds it [C asm]: a 0xA0 frame; +0x00 = a1, +0x04 = a3, +0x30 = 0x28; a3 bytes from t0 to +0x08, then a1
// bytes from a2 after them; sceDbcSendData2 0x216A58(a0, 0x0103400B, sp + 0x30, sp), its result returned. The flag
// word's upper 32 bits are the caller's s4 in retail (never read by DBCMAN's 32-bit field [L]); 0 here.
void vib_set_act_param(const CallArgs& a, CallResult& r) {
    const std::uint32_t n1 = a.gpr[1].w[0], n3 = a.gpr[3].w[0];
    if (n1 > 0x28 || n3 > 0x28 || n1 + n3 > 0x28) stub_unexpected("sceVibSetActParam", "parameter sizes " + std::to_string(n1) + " + " + std::to_string(n3));
    b3host::GuestStackFrame f(0xA0);
    const b3host::gaddr sp = f.addr();
    b3host::store<std::uint32_t>(sp + 0x00, n1);
    b3host::store<std::uint32_t>(sp + 0x04, n3);
    b3host::store<std::uint32_t>(sp + 0x30, 0x28u);
    std::uint8_t tmp[0x28];
    b3host::copy_from_guest(tmp, a.gpr[4].w[0], n3);
    b3host::copy_to_guest(sp + 0x08, tmp, n3);
    b3host::copy_from_guest(tmp, a.gpr[2].w[0], n1);
    b3host::copy_to_guest(sp + 0x08 + n3, tmp, n1);
    ret(r, b3host::call_guest<int>(0x00216A58u, a.gpr[0].w[0], 0x0103400Bu, sp + 0x30, sp));
}
// sceMpegInit: Sony libmpeg as C since movie1 (src/libs/sce/libmpeg.cpp, the IPU in src/native/ipu_model.h).
// void SyncDCache(void *start, void *end) / InvalidDCache [C libkernl 0x10C718 / 0x10C858]: the data cache; the host
// has one coherent memory, so nothing to do. Retail's DIntr / EIntr around it leave the interrupt state as it was.
void sync_dcache(const CallArgs&, CallResult& r) { r = CallResult{}; }
// int sceScfGetAspect(void) [C decomp libscf 0x24ACE0]: on a retail console (IsT10K 0) (GetOsdConfigParam >> 1) & 3:
// 0 4:3, 1 full, 2 16:9. libscf's own cache (gConfig) is not written (logged).
void scf_get_aspect(const CallArgs&, CallResult& r) {
    stub_note("sceScfGetAspect", "from the platform's OSD config word; libscf's gConfig cache not written");
    ret(r, static_cast<std::int32_t>((osd_config_param() >> 1) & 3u));
}

}  // namespace b3native::platform
