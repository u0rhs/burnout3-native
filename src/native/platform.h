// platform.h -- the native backend's hand-written tier (docs/standalone.md §8): retail library and runtime functions
// that have a native replacement here rather than in gen/decomp or b3port libm.
// X(name, address, entry) with entry a void(const b3host::CallArgs&, b3host::CallResult&).
#ifndef B3NATIVE_PLATFORM_H
#define B3NATIVE_PLATFORM_H

#include "b3host/host.h"
#include "b3host/native_backend.h"
#include "hw_model.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace b3audio { struct AudioSink; }
namespace b3native::platform {

// The stub log: every platform stub that leaves guest state the game could read different from retail (state retail's
// code would have written, or a value the game reads back) notes it here, so a RAM difference can be traced
// to a stub. `effect` says what retail would have done and where. Printed by the tools; B3NATIVE_STUBLOG=<file> also
// appends it there at exit.
struct StubEvent {
    const char* stub;
    std::string effect;
    unsigned calls;
};
void stub_note(const char* stub, const std::string& effect);
const std::vector<StubEvent>& stub_log();
// A platform stub met a call it does not model (an argument or state retail would handle and the stub cannot): fatal.
[[noreturn]] void stub_unexpected(const char* stub, const std::string& what);
// boot2: B3NATIVE_MAX_VBLANKS reached (the run is ended on purpose after that many virtual VBlanks)
struct VBlankLimit : std::runtime_error {
    std::uint64_t vblanks;
    explicit VBlankLimit(std::uint64_t n) : std::runtime_error("VBlank limit reached after " + std::to_string(n) + " virtual VBlanks"), vblanks(n) {}
};
// exit() reached: the program ends (thrown through native code to the tool that started it).
struct NativeExit : std::runtime_error {
    int code;
    explicit NativeExit(int c) : std::runtime_error("exit(" + std::to_string(c) + ")"), code(c) {}
};

void initialize_cpp_rts(const b3host::CallArgs& a, b3host::CallResult& r);
void crt0_init_cpp(const b3host::CallArgs& a, b3host::CallResult& r);
void flush_cache(const b3host::CallArgs& a, b3host::CallResult& r);
void init_sys(const b3host::CallArgs& a, b3host::CallResult& r);
void exit_(const b3host::CallArgs& a, b3host::CallResult& r);
void load_exec_ps2(const b3host::CallArgs& a, b3host::CallResult& r);   // fe1
// C runtime (src/native/platform_crt.cpp)
void memset_(const b3host::CallArgs&, b3host::CallResult&);
void fabs_(const b3host::CallArgs&, b3host::CallResult&);
void memcpy_(const b3host::CallArgs&, b3host::CallResult&);
void memmove_(const b3host::CallArgs&, b3host::CallResult&);
void memcmp_(const b3host::CallArgs&, b3host::CallResult&);
void strlen_(const b3host::CallArgs&, b3host::CallResult&);
void strcpy_(const b3host::CallArgs&, b3host::CallResult&);
void strncpy_(const b3host::CallArgs&, b3host::CallResult&);
void strcat_(const b3host::CallArgs&, b3host::CallResult&);
void strncat_(const b3host::CallArgs&, b3host::CallResult&);
void strcmp_(const b3host::CallArgs&, b3host::CallResult&);
void strncmp_(const b3host::CallArgs&, b3host::CallResult&);
void strcasecmp_(const b3host::CallArgs&, b3host::CallResult&);
void strncasecmp_(const b3host::CallArgs&, b3host::CallResult&);   // fb1
void strchr_(const b3host::CallArgs&, b3host::CallResult&);
void strrchr_(const b3host::CallArgs&, b3host::CallResult&);
void strpbrk_(const b3host::CallArgs&, b3host::CallResult&);
void register_global_object(const b3host::CallArgs&, b3host::CallResult&);
void construct_array(const b3host::CallArgs&, b3host::CallResult&);
void muldi3(const b3host::CallArgs&, b3host::CallResult&);
void divdi3(const b3host::CallArgs&, b3host::CallResult&);
void moddi3(const b3host::CallArgs&, b3host::CallResult&);
void udivdi3(const b3host::CallArgs&, b3host::CallResult&);
void umoddi3(const b3host::CallArgs&, b3host::CallResult&);
void strstr_(const b3host::CallArgs&, b3host::CallResult&);
void qsort_(const b3host::CallArgs&, b3host::CallResult&);   // rt1
void strtol_(const b3host::CallArgs&, b3host::CallResult&);   // rt1
void atoi_(const b3host::CallArgs&, b3host::CallResult&);     // rt1
void rand_(const b3host::CallArgs&, b3host::CallResult&);     // rt1
void strupr_(const b3host::CallArgs&, b3host::CallResult&);
// platform_printf.cpp (boot1)
void snprintf_(const b3host::CallArgs&, b3host::CallResult&);
void sprintf_(const b3host::CallArgs&, b3host::CallResult&);
void vsprintf_(const b3host::CallArgs&, b3host::CallResult&);
void printf_(const b3host::CallArgs&, b3host::CallResult&);
void sce_printf_(const b3host::CallArgs&, b3host::CallResult&);
void sinit_(const b3host::CallArgs&, b3host::CallResult&);
void std_(const b3host::CallArgs&, b3host::CallResult&);
// platform_iop.cpp (boot1)
void sif_init_rpc(const b3host::CallArgs&, b3host::CallResult&);
void sif_reboot_iop(const b3host::CallArgs&, b3host::CallResult&);
void sif_sync_iop(const b3host::CallArgs&, b3host::CallResult&);
void sif_load_file_reset(const b3host::CallArgs&, b3host::CallResult&);
// the power-off handler the game registered (sceCdPOffCallback; platform_devices.cpp), for the platform layer; 0 if none
void power_off_handler(b3host::gaddr& func, b3host::gaddr& arg);
void get_thread_id(const b3host::CallArgs&, b3host::CallResult&);
void change_thread_priority(const b3host::CallArgs&, b3host::CallResult&);
// the kernel's INTC handler list as the platform keeps it (for the S5 frame scheduler)
struct IntcHandler {
    std::int32_t id;
    std::uint32_t cause;
    b3host::gaddr handler, argument;
};
std::vector<IntcHandler> intc_handlers();
std::uint32_t intc_mask();
void note_libkernl_timer_handler();   // _InitSys: libkernl's InitTimer registration (id 1)
void add_intc_handler(const b3host::CallArgs&, b3host::CallResult&);
void enable_intc(const b3host::CallArgs&, b3host::CallResult&);
void disable_intc(const b3host::CallArgs&, b3host::CallResult&);
std::uint32_t osd_config_param();
void scf_get_aspect(const b3host::CallArgs&, b3host::CallResult&);
void get_osd_config_param(const b3host::CallArgs&, b3host::CallResult&);
void get_osd_config_param2(const b3host::CallArgs&, b3host::CallResult&);
// the hardware model behind b3hw_* (platform_hw.cpp, boot1)
void install_hw_model(b3host::NativeBackend& nb);
HwModel& hw_model();
// layer_check: B3NATIVE_DMA_LOG's events (platform_hw.cpp): 'd' a DMA completion, 'i' a DMAC callback delivered
void dma_log(char kind, unsigned channel);
KernelModel& kernel();
// boot2: the kernel's interrupt delivery (platform_iop.cpp): one virtual VBlank, one DMAC channel interrupt
void vblank();
void time_check();                 // boot2: deliver the VBlanks virtual EE time has reached
void gate_latched();               // H3: a GIF transfer under the PATH3 mask: the gate's wake-up (platform_iop.cpp)
std::uint64_t virtual_cycles();
std::uint64_t next_vblank_start(std::uint64_t t);   // boot2: the first virtual VBlank start at or after t
void arm_virtual_time(b3host::NativeBackend& nb);
void rearm_virtual_time();   // boot2: a new timed event (a delayed SIF completion)
void idle_step();   // boot2: the idle loop's next event (an alarm before the next VBlank, else the VBlank)
// boot2: one extra timed event source on the virtual clock (RW Audio's IOP executor): next_cycles() its next event in EE
// cycles (~0 none), due() run when the clock reaches it
void set_extra_time_source(std::function<std::uint64_t()> next_cycles, std::function<void()> due);
bool skip_to_next_event();   // boot2: native_play's idle hook (rt1): jump to the next pending event; never in proof runs
// boot2: work that runs at every virtual VBlank, after the INTC handlers (RW Audio's IOP time)
std::vector<std::function<void()>>& vblank_hooks();
void add_vblank_hook(std::function<void()> h);
// boot2: RW Audio's IOP executor behind SIF command 0 (platform_rwa.cpp; not in the platform unit tests)
// boot2: the mixer is the SPU2-equivalent one by default (B3NATIVE_MIXER=stub: the old stub, for comparisons), with
// libsd's reverb presets from the install's IOP/LIBSD.IRX; `sink` (audio_sink.h) receives each VBlank's 48 kHz frames
void install_rwa_iop(b3audio::AudioSink* sink = nullptr);
// custom soundtrack (2026-11-10, remaster-features §6; src/native/custom_music.h): a host source mixed into each
// VBlank's rendered 48 kHz frames before they go to the sink (s16 stereo, added in place); null: none (the default)
void set_music_overlay(void (*mix)(std::int16_t* lr, std::size_t frames));
// boot2 diagnostics: B3NATIVE_RWA_LOG=<file> -- RW Audio's commands and replies in order, `S|U <poll> <op>` (the poll
// as native_boot's poll point counts it: log_poll)
inline std::uint64_t log_poll = 0;
// boot2: the kernel's threads (platform_threads.cpp): interrupt context marks and the preemption point
void isr_enter();
void isr_exit();
int isr_depth();   // boot2: >0 inside a platform-delivered handler (interrupt or alarm)
// vu0: guest instructions retired but not yet charged to the cost counter (the native fallback's running mini); the
// DMAC model's clock is virtual_cycles() + this. Default: none.
std::function<std::uint64_t()>& guest_pending_cycles();
bool dmac_handler_exists(unsigned channel);   // vu0: the kernel's DMAC handler list has one for the channel
// boot2: the kernel's alarms (platform_time.cpp, docs/specs/ee-kernel.md §6.4)
void set_alarm(const b3host::CallArgs&, b3host::CallResult&);
void release_alarm(const b3host::CallArgs&, b3host::CallResult&);
void maybe_preempt();
std::int32_t current_thread_id();
bool kernel_signal_sema(std::int32_t id);   // SignalSema for platform code (IOP completions)
void terminate_thread(const b3host::CallArgs&, b3host::CallResult&);
void delete_thread(const b3host::CallArgs&, b3host::CallResult&);
void exit_thread(const b3host::CallArgs&, b3host::CallResult&);
void refer_thread_status(const b3host::CallArgs&, b3host::CallResult&);
bool dmac_interrupt(unsigned channel);
void remove_intc_handler(const b3host::CallArgs&, b3host::CallResult&);
void add_dmac_handler(const b3host::CallArgs&, b3host::CallResult&);
void remove_dmac_handler(const b3host::CallArgs&, b3host::CallResult&);
void enable_dmac(const b3host::CallArgs&, b3host::CallResult&);
void disable_dmac(const b3host::CallArgs&, b3host::CallResult&);
void set_gs_crt(const b3host::CallArgs&, b3host::CallResult&);
void gs_put_imr(const b3host::CallArgs&, b3host::CallResult&);
void sync_dcache(const b3host::CallArgs&, b3host::CallResult&);
void create_sema(const b3host::CallArgs&, b3host::CallResult&);
void vib_set_act_param(const b3host::CallArgs&, b3host::CallResult&);
void lg_kbd_get_led_state(const b3host::CallArgs&, b3host::CallResult&);
void race_car_physics_attribs_release(const b3host::CallArgs&, b3host::CallResult&);
void create_thread(const b3host::CallArgs&, b3host::CallResult&);
void start_thread(const b3host::CallArgs&, b3host::CallResult&);
void floatdisf_(const b3host::CallArgs&, b3host::CallResult&);
void fptoui_(const b3host::CallArgs&, b3host::CallResult&);
// the frame scheduler's first piece (platform_iop.cpp, boot1)
std::uint64_t vblank_count();
void dintr(const b3host::CallArgs&, b3host::CallResult&);
void eintr(const b3host::CallArgs&, b3host::CallResult&);
void wakeup_thread(const b3host::CallArgs&, b3host::CallResult&);
void sleep_thread(const b3host::CallArgs&, b3host::CallResult&);
// platform_malloc.cpp (boot1): newlib's allocator on the guest heap
b3host::gaddr heap_end();
void malloc_(const b3host::CallArgs&, b3host::CallResult&);
void free_(const b3host::CallArgs&, b3host::CallResult&);
void realloc_(const b3host::CallArgs&, b3host::CallResult&);
void calloc_(const b3host::CallArgs&, b3host::CallResult&);
void memalign_(const b3host::CallArgs&, b3host::CallResult&);
void malloc_r_(const b3host::CallArgs&, b3host::CallResult&);
b3host::gaddr platform_malloc_r(std::uint32_t bytes);   // boot2: newlib's _malloc_r on the guest's reent
void free_r_(const b3host::CallArgs&, b3host::CallResult&);
void realloc_r_(const b3host::CallArgs&, b3host::CallResult&);
void calloc_r_(const b3host::CallArgs&, b3host::CallResult&);
void memalign_r_(const b3host::CallArgs&, b3host::CallResult&);
void malloc_trim_r_(const b3host::CallArgs&, b3host::CallResult&);
void malloc_extend_top_(const b3host::CallArgs&, b3host::CallResult&);
void sbrk_(const b3host::CallArgs&, b3host::CallResult&);
void sbrk_r_(const b3host::CallArgs&, b3host::CallResult&);
void malloc_lock_(const b3host::CallArgs&, b3host::CallResult&);
void malloc_unlock_(const b3host::CallArgs&, b3host::CallResult&);
void end_of_heap(const b3host::CallArgs&, b3host::CallResult&);
void errno_(const b3host::CallArgs&, b3host::CallResult&);
// platform_sif.cpp: the SIF command channel (RW Audio's, and the completions the platform delivers), the IOP heap, the module
// loader's answers; the RPC entry points are fatal (no IOP RPC server: S4)
void sif_bind_rpc(const b3host::CallArgs&, b3host::CallResult&);
void sif_call_rpc(const b3host::CallArgs&, b3host::CallResult&);
void sif_check_stat_rpc(const b3host::CallArgs&, b3host::CallResult&);
void sif_init_iop_heap(const b3host::CallArgs&, b3host::CallResult&);
void sif_alloc_sys_memory(const b3host::CallArgs&, b3host::CallResult&);
void sif_free_sys_memory(const b3host::CallArgs&, b3host::CallResult&);
void sif_load_module(const b3host::CallArgs&, b3host::CallResult&);
void sif_search_module_by_name(const b3host::CallArgs&, b3host::CallResult&);
void sif_stop_module(const b3host::CallArgs&, b3host::CallResult&);
void sif_unload_module(const b3host::CallArgs&, b3host::CallResult&);
void sif_add_cmd_handler(const b3host::CallArgs&, b3host::CallResult&);
void sif_remove_cmd_handler(const b3host::CallArgs&, b3host::CallResult&);
void sif_set_cmd_buffer(const b3host::CallArgs&, b3host::CallResult&);
void sif_init_cmd(const b3host::CallArgs&, b3host::CallResult&);
void sif_exit_cmd(const b3host::CallArgs&, b3host::CallResult&);
void sif_send_cmd(const b3host::CallArgs&, b3host::CallResult&);
void sif_dma_stat(const b3host::CallArgs&, b3host::CallResult&);
void wait_sema(const b3host::CallArgs&, b3host::CallResult&);
void poll_sema(const b3host::CallArgs&, b3host::CallResult&);
void signal_sema(const b3host::CallArgs&, b3host::CallResult&);
void delete_sema(const b3host::CallArgs&, b3host::CallResult&);
// movie3: the movies' libraries natively (src/native/platform_movie.cpp)
void movie_vibuf_create(const b3host::CallArgs&, b3host::CallResult&);
void movie_vibuf_reset(const b3host::CallArgs&, b3host::CallResult&);
void movie_vibuf_begin_put(const b3host::CallArgs&, b3host::CallResult&);
void movie_vibuf_end_put(const b3host::CallArgs&, b3host::CallResult&);
void movie_vibuf_count(const b3host::CallArgs&, b3host::CallResult&);
void movie_vibuf_add_dma(const b3host::CallArgs&, b3host::CallResult&);
void movie_vibuf_nop(const b3host::CallArgs&, b3host::CallResult&);
void movie_vibuf_delete(const b3host::CallArgs&, b3host::CallResult&);
void movie_mpeg_init(const b3host::CallArgs&, b3host::CallResult&);
void movie_mpeg_create(const b3host::CallArgs&, b3host::CallResult&);
void movie_mpeg_delete(const b3host::CallArgs&, b3host::CallResult&);
void movie_mpeg_reset(const b3host::CallArgs&, b3host::CallResult&);
void movie_mpeg_clear_ref(const b3host::CallArgs&, b3host::CallResult&);
void movie_mpeg_add_callback(const b3host::CallArgs&, b3host::CallResult&);
void movie_mpeg_is_end(const b3host::CallArgs&, b3host::CallResult&);
void movie_mpeg_get_picture(const b3host::CallArgs&, b3host::CallResult&);
// sif-lane: the console's devices at the libraries' entry points (src/native/platform_devices.cpp)
void dev_cd_init(const b3host::CallArgs&, b3host::CallResult&);
void dev_cd_mmode(const b3host::CallArgs&, b3host::CallResult&);
void dev_cd_disk_ready(const b3host::CallArgs&, b3host::CallResult&);
void dev_cd_read_clock(const b3host::CallArgs&, b3host::CallResult&);
void dev_cd_poff_callback(const b3host::CallArgs&, b3host::CallResult&);
void dev_fs_reset(const b3host::CallArgs&, b3host::CallResult&);
void dev_open(const b3host::CallArgs&, b3host::CallResult&);
void dev_read(const b3host::CallArgs&, b3host::CallResult&);
void dev_close(const b3host::CallArgs&, b3host::CallResult&);
void dev_devctl(const b3host::CallArgs&, b3host::CallResult&);
void dev_lg_init(const b3host::CallArgs&, b3host::CallResult&);
void dev_lg_enumerate(const b3host::CallArgs&, b3host::CallResult&);
// sif-lane: libdbc, the pads' and memory cards' device library (src/native/platform_dbc.cpp)
void dbc_row_init(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_get_mod_version(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_end(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_set_work_addr(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_create_socket(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_delete_socket(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_get_dep_number(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_get_connection(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_init_socket(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_reset_socket(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_get_device_status(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_search_port_specific(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_sr_data(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_send_data(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_send_data2(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_send_data3(const b3host::CallArgs&, b3host::CallResult&);
void dbc_row_receive_data(const b3host::CallArgs&, b3host::CallResult&);
// S4 of the SIF lane: the game's file system on the disc (src/native/platform_gtfs.cpp)
void gtfs_init(const b3host::CallArgs&, b3host::CallResult&);
void gtfs_open(const b3host::CallArgs&, b3host::CallResult&);
void gtfs_close(const b3host::CallArgs&, b3host::CallResult&);
void gtfs_read(const b3host::CallArgs&, b3host::CallResult&);
void gtfs_set_position(const b3host::CallArgs&, b3host::CallResult&);
void gtfs_abort(const b3host::CallArgs&, b3host::CallResult&);
// S4: the sound distortion's IOP side (src/native/platform_distortion.cpp)
void dist_construct(const b3host::CallArgs&, b3host::CallResult&);
void dist_start(const b3host::CallArgs&, b3host::CallResult&);
void dist_stop(const b3host::CallArgs&, b3host::CallResult&);
void dist_update(const b3host::CallArgs&, b3host::CallResult&);
// engpan: the IOP -> EE half -- SIF command 6's handler (func_002FC530), its mono mix _FinalMix (func_002FC820), the DSP
// block's distortion CB3SoundDSPDataBlock::ProcessB2Distortion (all three hand-written asm in retail), and libsif's
// sceSifSetDma / isceSifSetDma (the handler's DMA back into B3ROUTE's Fuzzy buffer)
void dist_cmd6_handler(const b3host::CallArgs&, b3host::CallResult&);
void dist_final_mix(const b3host::CallArgs&, b3host::CallResult&);
void dist_b2_distortion(const b3host::CallArgs&, b3host::CallResult&);
void sif_set_dma(const b3host::CallArgs&, b3host::CallResult&);
// engpan: B3ROUTE.IRX beside RW Audio's executor (platform_rwa.cpp, src/audio/b3route.h): the distortion's RPC command
// list (`bytes` from its count word, server 0x150276), applied at the current IOP time; an EE -> IOP SIF DMA into the
// module's image (false: not B3ROUTE's). B3_B3ROUTE=0 leaves the module out (the port before engpan: the list has no
// receiver, the engine voices play on the dry bus with their channel pans).
void b3route_rpc(const void* list, std::uint32_t bytes);
bool b3route_sif_dma(std::uint32_t iop_addr, const void* src, std::uint32_t n);
}  // namespace b3native::platform

// sif-lane: library functions whose IOP side the platform answers at the library's own entry point (no SIF RPC), ahead
// of the generated rows at the same addresses (native_table.cpp kFirst). X(name, address, entry, result register:
// TableEntry::ret). Each row charges the retail code's virtual time (platform_charge.h). S4: the game's own back ends
// of its IOP modules too -- the file system (GTFSCDVD) and the sound distortion (B3ROUTE).
#define B3NATIVE_FIRST(X) \
    X("Init__19CGtFileSystemPS2DVDFUiP13CGtFilePS2DVDP19CGtFileBufferPS2DVDPCciiiPc", 0x001D3850u, b3native::platform::gtfs_init, 'i') \
    X("Open__13CGtFilePS2DVDFP13CGTFileSystemPCcUi", 0x001D36E0u, b3native::platform::gtfs_open, 'i') \
    X("Close__13CGtFilePS2DVDFv", 0x001D3670u, b3native::platform::gtfs_close, 'n') \
    X("Read__13CGtFilePS2DVDFPvUi", 0x001D3280u, b3native::platform::gtfs_read, 'i') \
    X("SetPosition__13CGtFilePS2DVDFl15EFileMoveMethod", 0x001D30B0u, b3native::platform::gtfs_set_position, 'l') \
    X("Abort__13CGtFilePS2DVDFv", 0x001D2FA0u, b3native::platform::gtfs_abort, 'i') \
    X("Construct__18CB3SoundDistortionFP13RwaSPU2Outputi", 0x002FD0E0u, b3native::platform::dist_construct, 'n') \
    X("StartDistortion__18CB3SoundDistortionFv", 0x002FCFC0u, b3native::platform::dist_start, 'n') \
    X("StopDistortion__18CB3SoundDistortionFv", 0x002FCF00u, b3native::platform::dist_stop, 'n') \
    X("Update__18CB3SoundDistortionFv", 0x002FCA60u, b3native::platform::dist_update, 'n') \
    X("func_002FC530", 0x002FC530u, b3native::platform::dist_cmd6_handler, 'n') \
    X("func_002FC820", 0x002FC820u, b3native::platform::dist_final_mix, 'n') \
    X("ProcessB2Distortion__20CB3SoundDSPDataBlockFPsPs", 0x002FBE20u, b3native::platform::dist_b2_distortion, 'n') \
    X("sceSifSetDma", 0x0010C1E0u, b3native::platform::sif_set_dma, 'i') \
    X("isceSifSetDma", 0x0010C1F0u, b3native::platform::sif_set_dma, 'i') \
    X("sceCdPOffCallback", 0x0011E858u, b3native::platform::dev_cd_poff_callback, 'i') \
    X("sceCdInit", 0x0011F218u, b3native::platform::dev_cd_init, 'i') \
    X("sceCdDiskReady", 0x0011F6F0u, b3native::platform::dev_cd_disk_ready, 'i') \
    X("sceCdMmode", 0x0011FA48u, b3native::platform::dev_cd_mmode, 'i') \
    X("sceCdReadClock", 0x0011FB18u, b3native::platform::dev_cd_read_clock, 'i') \
    X("sceFsReset", 0x00110238u, b3native::platform::dev_fs_reset, 'i') \
    X("sceOpen", 0x00110270u, b3native::platform::dev_open, 'i') \
    X("sceClose", 0x001104F8u, b3native::platform::dev_close, 'i') \
    X("sceRead", 0x001108B0u, b3native::platform::dev_read, 'i') \
    X("sceDevctl", 0x00112A48u, b3native::platform::dev_devctl, 'i') \
    X("lgDevInit", 0x004438E0u, b3native::platform::dev_lg_init, 'i') \
    X("lgDevEnumerate", 0x00443CC8u, b3native::platform::dev_lg_enumerate, 'i') \
    X("sceDbcGetModVersion", 0x00215F18u, b3native::platform::dbc_row_get_mod_version, 'i') \
    X("sceDbcInit", 0x00215F70u, b3native::platform::dbc_row_init, 'i') \
    X("sceDbcEnd", 0x002161B0u, b3native::platform::dbc_row_end, 'i') \
    X("sceDbcSetWorkAddr", 0x002161F8u, b3native::platform::dbc_row_set_work_addr, 'i') \
    X("sceDbcCreateSocket", 0x00216268u, b3native::platform::dbc_row_create_socket, 'i') \
    X("sceDbcDeleteSocket", 0x00216340u, b3native::platform::dbc_row_delete_socket, 'i') \
    X("sceDbcGetDepNumber", 0x002163C8u, b3native::platform::dbc_row_get_dep_number, 'i') \
    X("sceDbcGetConnection", 0x00216510u, b3native::platform::dbc_row_get_connection, 'i') \
    X("sceDbcInitSocket", 0x00216608u, b3native::platform::dbc_row_init_socket, 'i') \
    X("sceDbcResetSocket", 0x00216678u, b3native::platform::dbc_row_reset_socket, 'i') \
    X("sceDbcGetDeviceStatus", 0x002166E8u, b3native::platform::dbc_row_get_device_status, 'i') \
    X("sceDbcSearchPortSpecific", 0x00216758u, b3native::platform::dbc_row_search_port_specific, 'i') \
    X("sceDbcSRData", 0x002167E0u, b3native::platform::dbc_row_sr_data, 'i') \
    X("sceDbcSendData", 0x00216928u, b3native::platform::dbc_row_send_data, 'i') \
    X("sceDbcSendData2", 0x00216A58u, b3native::platform::dbc_row_send_data2, 'i') \
    X("sceDbcSendData3", 0x00216BB8u, b3native::platform::dbc_row_send_data3, 'i') \
    X("sceDbcReceiveData", 0x00216D20u, b3native::platform::dbc_row_receive_data, 'i')

// movie3: libmpeg (src/libs/sce/libmpeg*.cpp, addresses from the manifest) and ViBuf (src/b3/_unclassified/vibuf_*.cpp,
// matched game code) replaced natively; they take precedence over the generated rows (native_table.cpp kFirst), and
// B3NATIVE_MOVIE_IPU=1 keeps the generated libraries on the emulated IPU instead.
#define B3NATIVE_MOVIE(X) \
    X("sceMpegInit", 0x00109BF8u, b3native::platform::movie_mpeg_init) \
    X("sceMpegCreate", 0x00109CA8u, b3native::platform::movie_mpeg_create) \
    X("sceMpegDelete", 0x00109EB8u, b3native::platform::movie_mpeg_delete) \
    X("sceMpegGetPicture", 0x00109F00u, b3native::platform::movie_mpeg_get_picture) \
    X("sceMpegIsEnd", 0x0010A028u, b3native::platform::movie_mpeg_is_end) \
    X("sceMpegReset", 0x0010A048u, b3native::platform::movie_mpeg_reset) \
    X("sceMpegClearRefBuff", 0x0010A0A8u, b3native::platform::movie_mpeg_clear_ref) \
    X("sceMpegAddCallback", 0x0010A100u, b3native::platform::movie_mpeg_add_callback) \
    X("viBufCount", 0x00325DC0u, b3native::platform::movie_vibuf_count) \
    X("viBufDelete", 0x00325E10u, b3native::platform::movie_vibuf_delete) \
    X("viBufRestartDMA", 0x00325E90u, b3native::platform::movie_vibuf_nop) \
    X("viBufStopDMA", 0x00326220u, b3native::platform::movie_vibuf_nop) \
    X("viBufAddDMA", 0x00326340u, b3native::platform::movie_vibuf_add_dma) \
    X("viBufEndPut", 0x00326610u, b3native::platform::movie_vibuf_end_put) \
    X("viBufBeginPut", 0x00326670u, b3native::platform::movie_vibuf_begin_put) \
    X("viBufReset", 0x00326770u, b3native::platform::movie_vibuf_reset) \
    X("viBufCreate", 0x003268E0u, b3native::platform::movie_vibuf_create)

// [C decomp assembly/asm/runtime/mwrt.s, crt0.s func_00100240] __initialize_cpp_rts(ctor_begin, ctor_end, ...)
// [C decomp sce/libkernl.s 0x10C0A0] FlushCache(mode): syscall 0x64; host memory is coherent, so a no-op.
// [C decomp sce/libkernl.s 0x115108] _InitSys: libkernl's own start-up (crt0); a logged no-op (platform.cpp).
// [C decomp runtime/libc.s 0x1213B8] exit: ends the program (NativeExit); fatal if atexit handlers are registered.
// [C decomp sce/libkernl.s; PS2 SDK sifcmd.h / sifrpc.h] the SIF rows (sceSif*; the semaphores: platform_threads.cpp):
// src/native/platform_sif.cpp, evidence there.
#define B3NATIVE_PLATFORM(X) \
    X("AddIntcHandler", 0x0010BB00u, b3native::platform::add_intc_handler) \
    X("ChangeThreadPriority", 0x0010BCB0u, b3native::platform::change_thread_priority) \
    X("DisableIntc", 0x0010C900u, b3native::platform::disable_intc) \
    X("RemoveIntcHandler", 0x0010BB20u, b3native::platform::remove_intc_handler) \
    X("AddDmacHandler", 0x0010BB30u, b3native::platform::add_dmac_handler) \
    X("AddDmacHandler2", 0x0010BB40u, b3native::platform::add_dmac_handler) /* movie2: the same syscall 0x12 stub [C libkernl.s 0x10BB40]; libmpeg 0x109770 / 0x109A74 */ \
    X("RemoveDmacHandler", 0x0010BB50u, b3native::platform::remove_dmac_handler) \
    X("EnableDmac", 0x0010CA38u, b3native::platform::enable_dmac) \
    X("DisableDmac", 0x0010C9D0u, b3native::platform::disable_dmac) \
    X("SetGsCrt", 0x0010BA20u, b3native::platform::set_gs_crt) \
    X("GsPutIMR", 0x0010C160u, b3native::platform::gs_put_imr) \
    X("SyncDCache", 0x0010C718u, b3native::platform::sync_dcache) \
    X("CreateSema", 0x0010BE20u, b3native::platform::create_sema) \
    X("Release__24CB3RaceCarPhysicsAttribsFv", 0x00254090u, b3native::platform::race_car_physics_attribs_release) /* mac5: j 0x252800 */ \
    X("lgKbdGetLedState", 0x00306668u, b3native::platform::lg_kbd_get_led_state) /* mac5: the profile name keyboard */ \
    X("sceVibSetActParam", 0x00217520u, b3native::platform::vib_set_act_param) /* mac5: the pad's vibration (UpdateVibration) */ \
    X("CreateThread", 0x0010BC20u, b3native::platform::create_thread) \
    X("_StartThread", 0x0010BC40u, b3native::platform::start_thread) \
    X("DeleteThread", 0x0010BC30u, b3native::platform::delete_thread) \
    X("ExitThread", 0x0010BC50u, b3native::platform::exit_thread) \
    X("ExitDeleteThread", 0x0010BC60u, b3native::platform::exit_thread) \
    X("TerminateThread", 0x0010BC70u, b3native::platform::terminate_thread) \
    X("ReferThreadStatus", 0x0010BD20u, b3native::platform::refer_thread_status) \
    X("iReferThreadStatus", 0x0010BD30u, b3native::platform::refer_thread_status) \
    X("iTerminateThread", 0x0010BC80u, b3native::platform::terminate_thread) \
    X("InvalidDCache", 0x0010C858u, b3native::platform::sync_dcache) \
    X("__floatdisf", 0x0012D8D0u, b3native::platform::floatdisf_) \
    X("fptoui", 0x0012DE38u, b3native::platform::fptoui_) \
    X("EnableIntc", 0x0010C968u, b3native::platform::enable_intc) \
    X("GetThreadId", 0x0010BD10u, b3native::platform::get_thread_id) \
    X("SleepThread", 0x0010BD40u, b3native::platform::sleep_thread) \
    X("WakeupThread", 0x0010BD50u, b3native::platform::wakeup_thread) \
    X("SetAlarm", 0x0010BBA0u, b3native::platform::set_alarm) \
    X("ReleaseAlarm", 0x0010BBB0u, b3native::platform::release_alarm) \
    X("iSetAlarm", 0x0010BC00u, b3native::platform::set_alarm) \
    X("iReleaseAlarm", 0x0010BC10u, b3native::platform::release_alarm) \
    X("iWakeupThread", 0x0010CCD0u, b3native::platform::wakeup_thread) \
    X("func_00114E60", 0x00114E60u, b3native::platform::dintr) \
    X("EIntr", 0x00114EB8u, b3native::platform::eintr) \
    X("func_002372F0", 0x002372F0u, b3native::platform::get_thread_id) \
    X("EndOfHeap", 0x0010BE00u, b3native::platform::end_of_heap) \
    X("sbrk", 0x0010C520u, b3native::platform::sbrk_) \
    X("calloc", 0x0011FD98u, b3native::platform::calloc_) \
    X("_calloc_r", 0x0011FE00u, b3native::platform::calloc_r_) \
    X("__errno", 0x001213A8u, b3native::platform::errno_) \
    X("_free_r", 0x00121E88u, b3native::platform::free_r_) \
    X("_malloc_trim_r", 0x00122180u, b3native::platform::malloc_trim_r_) \
    X("memalign", 0x001230A8u, b3native::platform::memalign_) \
    X("_memalign_r", 0x00123110u, b3native::platform::memalign_r_) \
    X("malloc", 0x001232D8u, b3native::platform::malloc_) \
    X("free", 0x00123330u, b3native::platform::free_) \
    X("malloc_extend_top", 0x00123378u, b3native::platform::malloc_extend_top_) \
    X("_malloc_r", 0x001235D0u, b3native::platform::malloc_r_) \
    X("__malloc_lock", 0x00124120u, b3native::platform::malloc_lock_) \
    X("__malloc_unlock", 0x001241A0u, b3native::platform::malloc_unlock_) \
    X("realloc", 0x00125E08u, b3native::platform::realloc_) \
    X("_realloc_r", 0x00125E70u, b3native::platform::realloc_r_) \
    X("_sbrk_r", 0x001265C8u, b3native::platform::sbrk_r_) \
    X("FlushCache", 0x0010C0A0u, b3native::platform::flush_cache) \
    X("sceSifInitRpc", 0x0010E880u, b3native::platform::sif_init_rpc) \
    X("sceSifLoadFileReset", 0x00113708u, b3native::platform::sif_load_file_reset) \
    X("sceSifSyncIop", 0x00114458u, b3native::platform::sif_sync_iop) \
    X("sceSifRebootIop", 0x001144A8u, b3native::platform::sif_reboot_iop) \
    X("_InitSys", 0x00115108u, b3native::platform::init_sys) \
    X("exit", 0x001213B8u, b3native::platform::exit_) \
    X("func_001154C0", 0x001154C0u, b3native::platform::load_exec_ps2)   /* fe1: libkernl LoadExecPS2 (EXTRAS > PLAY NFS:UNDERGROUND 2 DEMO) */ \
    X("std", 0x001217C8u, b3native::platform::std_) \
    X("sceScfGetAspect", 0x0024ACE0u, b3native::platform::scf_get_aspect) \
    X("GetOsdConfigParam", 0x0010BED0u, b3native::platform::get_osd_config_param) \
    X("GetOsdConfigParam2", 0x0010C130u, b3native::platform::get_osd_config_param2) \
    X("__sinit", 0x001219D8u, b3native::platform::sinit_) \
    X("snprintf", 0x00126A18u, b3native::platform::snprintf_) \
    X("sprintf", 0x00126B40u, b3native::platform::sprintf_) \
    X("printf", 0x001252C8u, b3native::platform::printf_) \
    X("scePrintf", 0x0010E048u, b3native::platform::sce_printf_) \
    X("func_00100240", 0x00100240u, b3native::platform::crt0_init_cpp) \
    X("func_00100270", 0x00100270u, b3native::platform::construct_array) \
    X("__initialize_cpp_rts", 0x00100810u, b3native::platform::initialize_cpp_rts) \
    X("__register_global_object", 0x001007F0u, b3native::platform::register_global_object) \
    X("memcmp", 0x00123E10u, b3native::platform::memcmp_) \
    X("memcpy", 0x00123EA8u, b3native::platform::memcpy_) \
    X("memmove", 0x00123F58u, b3native::platform::memmove_) \
    X("memset", 0x00124060u, b3native::platform::memset_) \
    X("fabs", 0x0011D630u, b3native::platform::fabs_) /* mac5: the Mac run's sound Doppler (CalculateDoppler) */ \
    X("atoi", 0x0011FD30u, b3native::platform::atoi_) \
    X("qsort", 0x00125338u, b3native::platform::qsort_) \
    X("rand", 0x00125D40u, b3native::platform::rand_) \
    X("strcasecmp", 0x00126DF0u, b3native::platform::strcasecmp_) \
    X("strcat", 0x00126EC8u, b3native::platform::strcat_) \
    X("strchr", 0x00126FF8u, b3native::platform::strchr_) \
    X("strcmp", 0x00127188u, b3native::platform::strcmp_) \
    X("strcpy", 0x001272D0u, b3native::platform::strcpy_) \
    X("strlen", 0x001273E8u, b3native::platform::strlen_) \
    X("strncasecmp", 0x00127520u, b3native::platform::strncasecmp_) \
    X("strncat", 0x001275F8u, b3native::platform::strncat_) \
    X("strncmp", 0x001277A8u, b3native::platform::strncmp_) \
    X("strncpy", 0x00127960u, b3native::platform::strncpy_) \
    X("strpbrk", 0x00127B20u, b3native::platform::strpbrk_) \
    X("strrchr", 0x00127BA8u, b3native::platform::strrchr_) \
    X("strstr", 0x00127BF8u, b3native::platform::strstr_) \
    X("strtol", 0x00128F08u, b3native::platform::strtol_) \
    X("func_00129198", 0x00129198u, b3native::platform::strupr_) \
    X("__muldi3", 0x0012BFA0u, b3native::platform::muldi3) \
    X("__divdi3", 0x0012C018u, b3native::platform::divdi3) \
    X("__moddi3", 0x0012C6B0u, b3native::platform::moddi3) \
    X("__udivdi3", 0x0012CD40u, b3native::platform::udivdi3) \
    X("vsprintf", 0x0012BC70u, b3native::platform::vsprintf_) \
    X("__umoddi3", 0x0012D2C0u, b3native::platform::umoddi3) \
    X("sceSifBindRpc", 0x0010F018u, b3native::platform::sif_bind_rpc) \
    X("sceSifCallRpc", 0x0010F1E8u, b3native::platform::sif_call_rpc) \
    X("sceSifCheckStatRpc", 0x0010F3E0u, b3native::platform::sif_check_stat_rpc) \
    X("sceSifInitIopHeap", 0x00113028u, b3native::platform::sif_init_iop_heap) \
    X("sceSifAllocSysMemory", 0x00113120u, b3native::platform::sif_alloc_sys_memory) \
    X("sceSifFreeSysMemory", 0x001131A0u, b3native::platform::sif_free_sys_memory) \
    X("sceSifLoadModule", 0x00113F78u, b3native::platform::sif_load_module) \
    X("sceSifSearchModuleByName", 0x00113BE0u, b3native::platform::sif_search_module_by_name) \
    X("sceSifStopModule", 0x00113948u, b3native::platform::sif_stop_module) \
    X("sceSifUnloadModule", 0x00113B50u, b3native::platform::sif_unload_module) \
    X("sceSifAddCmdHandler", 0x0010E408u, b3native::platform::sif_add_cmd_handler) \
    X("sceSifRemoveCmdHandler", 0x0010E480u, b3native::platform::sif_remove_cmd_handler) \
    X("func_0010E7D0", 0x0010E7D0u, b3native::platform::sync_dcache)   /* boot2: libkernl data-cache writeback loop (cache 0x18): one coherent memory */ \
    X("sceSifSetCmdBuffer", 0x0010E3D8u, b3native::platform::sif_set_cmd_buffer) \
    X("sceSifInitCmd", 0x0010E120u, b3native::platform::sif_init_cmd) \
    X("sceSifExitCmd", 0x0010E3A0u, b3native::platform::sif_exit_cmd) \
    X("sceSifSendCmd", 0x0010E608u, b3native::platform::sif_send_cmd) \
    X("isceSifSendCmd", 0x0010E648u, b3native::platform::sif_send_cmd) \
    X("sceSifDmaStat", 0x0010C1C0u, b3native::platform::sif_dma_stat) \
    X("isceSifDmaStat", 0x0010C1D0u, b3native::platform::sif_dma_stat) \
    X("DeleteSema", 0x0010BE30u, b3native::platform::delete_sema) \
    X("SignalSema", 0x0010BE40u, b3native::platform::signal_sema) \
    X("iSignalSema", 0x0010BE50u, b3native::platform::signal_sema) \
    X("WaitSema", 0x0010BE60u, b3native::platform::wait_sema) \
    X("PollSema", 0x0010BE70u, b3native::platform::poll_sema) \
    X("iPollSema", 0x0010BE80u, b3native::platform::poll_sema)

#endif
