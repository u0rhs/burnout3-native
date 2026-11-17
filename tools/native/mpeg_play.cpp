// mpeg_play -- retail libmpeg in tests/r5900_mini.h on the dev IPU model (movie1, 2026-11-06).
//
//   mpeg_play ELF M2V [--max-pictures N] [--trace FILE] [--dump-picture N FILE.ppm]
//
// Runs Sony's own libmpeg (the retail ELF's code) the way the game does: sceMpegInit, sceMpegCreate on a work area,
// the whole elementary stream in guest RAM behind a toIPU source chain, then sceMpegGetPicture until sceMpegIsEnd.
// The hardware is the dev model (src/native/hw_model_dev.h: the platform's with the IPU of ipu_model.h, through
// tools/native/boot_oracle.h's MiniPlatform), so this checks the IPU model against retail's decoder: a VLC table or
// bit count that is off desynchronises the stream and libmpeg reports an error ([MPEG ERROR], counted here) or loses
// pictures. The pictures returned are compared with an independent count of the stream's picture start codes.
// Prints the IPU command counts and, with --trace, every IPU command with BP / IFC / FP and the result after it.
// The stream is the user's disc file (read from the path given; nothing is written anywhere but the trace).
#include "boot_oracle.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kInit = 0x00109BF8u, kCreate = 0x00109CA8u, kGetPicture = 0x00109F00u, kIsEnd = 0x0010A028u;
constexpr std::uint32_t kScePrintf = 0x0010E048u, kErrMessage = 0x0010A9F8u;
constexpr std::uint32_t kMp = 0x00F00000u, kWork = 0x00F00100u, kWorkSize = 0x00500000u;
// The stream goes through a guest ring after the work area (movie2: movie1 had the whole stream at 0x01000000, inside
// the work area, which sceMpegCreate clears -- libmpeg then searched 1 MB of zeros for a start code, one FDEC per byte,
// and the IPU stalled at the end), below the mini's stack (r5900_mini.h kStackTop 0x01FF0000): two halves of kSeg
// qwords, a segment per half in turn, and below each half kPad bytes holding the previous segment's tail, so
// sceIpuRestartDMA's rewind by the qwords inside the IPU (at most 10) finds the bytes it re-sends.
constexpr std::uint32_t kOut = 0x00600000u, kRing = 0x01410000u, kSeg = 0x8000u, kPad = 0x100u;
constexpr std::uint32_t kHalf[2] = {kRing + kPad, kRing + kPad + kSeg * 16 + kPad};
static_assert(kWork + kWorkSize <= kRing, "the stream must not overlap the libmpeg work area");
static_assert(kHalf[1] + kSeg * 16 <= 0x01F00000u, "the ring must stay below the stack");

std::string guest_string(mini::R5900& c, std::uint32_t a) {
    std::string s;
    for (std::uint32_t i = 0; i < 512; ++i) {
        const char ch = static_cast<char>(c.ram()[(a + i) & 0x01FFFFFFu]);
        if (!ch) break;
        s += ch;
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: mpeg_play ELF M2V [--max-pictures N] [--trace FILE]\n");
        return 2;
    }
    long max_pictures = 1L << 30;
    const char* trace_path = nullptr;
    long dump_at = 0;
    const char* dump_path = nullptr;
    const bool dump_raster = std::getenv("MPEG_DUMP_RASTER") != nullptr;   // the other macroblock order, to compare
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--max-pictures" && i + 1 < argc) max_pictures = std::atol(argv[++i]);
        else if (a == "--trace" && i + 1 < argc) trace_path = argv[++i];
        else if (a == "--dump-picture" && i + 2 < argc) { dump_at = std::atol(argv[++i]); dump_path = argv[++i]; }
    }
    mini::R5900 cpu;
    if (!cpu.load_elf(argv[1])) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    std::FILE* f = std::fopen(argv[2], "rb");
    if (!f) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
    std::vector<std::uint8_t> m2v;
    {
        std::uint8_t buf[65536];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) m2v.insert(m2v.end(), buf, buf + n);
        std::fclose(f);
    }
    // an independent count: picture start codes (00 00 01 00), and the size from the first sequence header
    long start_codes = 0;
    unsigned width = 0, height = 0;
    for (std::size_t i = 0; i + 7 < m2v.size(); ++i) {
        if (m2v[i] || m2v[i + 1] || m2v[i + 2] != 1) continue;
        if (m2v[i + 3] == 0x00) ++start_codes;
        if (m2v[i + 3] == 0xB3 && !width) {
            width = (m2v[i + 4] << 4) | (m2v[i + 5] >> 4);
            height = ((m2v[i + 5] & 15) << 8) | m2v[i + 6];
        }
    }
    std::printf("stream %s: %zu bytes, %ux%u, %ld picture start codes\n", argv[2], m2v.size(), width, height, start_codes);

    b3native::MiniPlatform plat(cpu);
    plat.install();
    // movie2: no access log (it grew without bound here: 32 bytes per access while libmpeg polls; see boot_oracle.h),
    // and a stalled IPU ends the run: BUSY with nothing in the IPU and toIPU stopped cannot progress, however long
    // libmpeg polls IPU_CTRL (movie1's run polled for up to 4e9 steps).
    plat.keep_trace = false;
    // mac4: the scratchpad for fromSPR / toSPR (libmpeg's reference fetches, dmaRefImage): without it no data moves and
    // every predicted macroblock is zero (the native platform sets it: platform_hw.cpp)
    plat.hw.scratchpad = [&cpu] { return cpu.spr().data(); };
    // + 2 qwords of zeros after the stream: libmpeg's last FDEC (FB 32, past the sequence end code) needs 32 bits beyond
    // it [C the manual's FDEC rule; seen on 9 of 51 disc movies, whose end code fills their last qword]; the game's
    // feed goes on past the end the same way (its viBuf ring) [L].
    const std::uint32_t qwc_total = static_cast<std::uint32_t>((m2v.size() + 15) / 16) + 2;
    m2v.resize(static_cast<std::size_t>(qwc_total) * 16, 0);
    std::uint32_t qwc_sent = 0, segments = 0;
    auto w32 = [&](std::uint32_t a, std::uint32_t v) { std::uint8_t d[16] = {}; std::memcpy(d, &v, 4); plat.hw.write(a, 4, d); };
    auto feed = [&]() {
        const std::uint32_t q = std::min<std::uint32_t>(kSeg, qwc_total - qwc_sent);
        const std::uint32_t at = kHalf[segments++ & 1], tail = std::min<std::uint32_t>(kPad, qwc_sent * 16);
        std::memcpy(&cpu.ram()[at - tail], m2v.data() + qwc_sent * 16 - tail, tail);
        std::memcpy(&cpu.ram()[at], m2v.data() + qwc_sent * 16, q * 16);
        w32(0x1000B410u, at);
        w32(0x1000B420u, q);
        qwc_sent += q;
        w32(0x1000B400u, 0x101);   // normal mode, from memory, STR
    };
    std::uint64_t stalled_polls = 0;
    {
        auto inner = cpu.on_mmio;
        cpu.on_mmio = [&, inner](std::uint32_t p, unsigned w, bool wr, std::uint8_t* d) {
            inner(p, w, wr, d);
            const auto& ipu = plat.hw.ipu();
            if (!wr && b3native::IpuModel::owns(p) && ipu.busy() && qwc_sent < qwc_total &&
                !(plat.hw.chan(4, 0) & 0x100u) && (plat.hw.chan(4, 2) & 0xFFFFu) == 0)
                feed();
            const bool stuck = !wr && b3native::IpuModel::owns(p) && ipu.busy() && ipu.ifc() == 0 &&
                               !(plat.hw.chan(4, 0) & 0x100u);
            stalled_polls = stuck ? stalled_polls + 1 : 0;
            if (stalled_polls > 100000)
                throw std::runtime_error("IPU stalled: busy, no input, toIPU stopped (after " +
                                         std::to_string(ipu.commands()) + " commands)");
        };
    }
    // movie2: the platform's deferred DMAC completions (vu0: ready 32 cycles after the kick) are delivered by MiniPlatform::tick
    // on the mini's clock: between every two instructions here (libmpeg's _doCSC2 waits for its fromIPU handler)
    cpu.on_tick = [&](std::uint64_t) { plat.tick(); };
    cpu.tick_every = 1;
    cpu.cop0_status |= 0x10000u;
    cpu.max_steps = 400000000ull;   // per libmpeg call [G: a 640x480 picture takes far fewer]
    long errors = 0;
    std::vector<b3native::IpuModel::Trace> trace;
    if (trace_path) plat.hw.ipu().trace = &trace;
    plat.hw.ipu().hashing = true;   // rt1: the output / command hashes (the identity check of IPU speed work)
    // scePrintf / _ErrMessage: to the host (retail's printf would go to the IOP)
    cpu.hooks[kScePrintf] = [&](mini::R5900& c) {
        std::printf("[guest] %s", guest_string(c, static_cast<std::uint32_t>(c.gpr_lo(4))).c_str());
        c.set_gpr64(2, 0);
    };
    cpu.hooks[kErrMessage] = [&](mini::R5900& c) {
        ++errors;
        std::printf("[MPEG ERROR] %s\n", guest_string(c, static_cast<std::uint32_t>(c.gpr_lo(4))).c_str());
    };

    cpu.call_i(kInit, {}, {});
    if (cpu.call_i(kCreate, {}, {kMp, kWork, kWorkSize}) != 1) { std::printf("sceMpegCreate failed\n"); return 1; }
    // toIPU in normal mode, one segment of at most 0xFFF0 qwords at a time, the next one started when the channel has
    // stopped with QWC 0 while the IPU waits (as an application's ring buffer refills). Not a source chain: libmpeg's
    // default stop-DMA callback (sceIpuStopDMA 0x10B488) writes CHCR = 1 (normal mode) and sceIpuRestartDMA restores
    // that value with STR, so a chain does not survive a stop / restart [C the asm]; the game registers its own
    // callbacks (CGtVideoDecoder StopDMA / RestartDMA, viBuf) [C unit_00325030.s 0x325780-0x3257C4].
    feed();
    const unsigned mbs = (width / 16) * ((height + 15) / 16);
    long pictures = 0, ends = 0;
    const auto t0 = std::chrono::steady_clock::now();
    std::uint64_t instructions = 0;
    while (pictures < max_pictures) {
        std::int64_t r;
        try {
            r = static_cast<std::int32_t>(cpu.call_i(kGetPicture, {}, {kMp, kOut, mbs}));
        } catch (const std::exception& e) {
            const auto& ipu = plat.hw.ipu();
            std::printf("stopped after %ld pictures: %s\n  IPU commands %llu, last cmd %08X, CTRL %08X, BP %u IFC %u FP %u; "
                        "toIPU CHCR %08X MADR %08X QWC %u TADR %08X; fromIPU CHCR %08X QWC %u; pc %08X\n",
                        pictures, e.what(), static_cast<unsigned long long>(ipu.commands()), ipu.last_command(), ipu.ctrl(),
                        ipu.bp(), ipu.ifc(), ipu.fp(), plat.hw.chan(4, 0), plat.hw.chan(4, 1), plat.hw.chan(4, 2),
                        plat.hw.chan(4, 3), plat.hw.chan(3, 0), plat.hw.chan(3, 2), cpu.pc());
            break;
        }
        instructions += cpu.retired;
        if (r < 0) { std::printf("sceMpegGetPicture returned %lld after %ld pictures\n", static_cast<long long>(r), pictures); break; }
        ++pictures;
        if (dump_path && pictures == dump_at) {
            // mac4: the picture as a PPM (RGB32 macroblocks, 1024 bytes each, column by column as CGtVideoImage reads
            // them: hud_capture.cpp's layout), to look at the IPU model's pixels
            const unsigned cols = width / 16, rows = (height + 15) / 16;
            std::vector<std::uint8_t> img(static_cast<std::size_t>(cols) * 16 * rows * 16 * 3);
            for (unsigned mx = 0; mx < cols; ++mx)
                for (unsigned my = 0; my < rows; ++my)
                    for (unsigned y = 0; y < 16; ++y)
                        for (unsigned x = 0; x < 16; ++x) {
                            const std::uint32_t a = kOut + ((dump_raster ? my * cols + mx : mx * rows + my) * 1024u) + y * 64 + x * 4;
                            const std::size_t o = ((static_cast<std::size_t>(my) * 16 + y) * cols * 16 + mx * 16 + x) * 3;
                            for (int k = 0; k < 3; ++k) img[o + k] = cpu.ram()[(a + k) & 0x01FFFFFFu];
                        }
            if (std::FILE* d = std::fopen(dump_path, "wb")) {
                std::fprintf(d, "P6\n%u %u\n255\n", cols * 16, rows * 16);
                std::fwrite(img.data(), 1, img.size(), d);
                std::fclose(d);
                std::printf("picture %ld written to %s\n", pictures, dump_path);
            }
        }
        if (cpu.call_i(kIsEnd, {}, {kMp})) { ++ends; break; }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const auto& ipu = plat.hw.ipu();
    std::printf("pictures %ld (start codes %ld), end %ld, MPEG errors %ld, IPU commands %llu, %.1f M instructions, %.1f s\n",
                pictures, start_codes, ends, errors, static_cast<unsigned long long>(ipu.commands()),
                instructions / 1e6, secs);
    std::printf("IPU output hash %016llx, command hash %016llx\n", static_cast<unsigned long long>(ipu.out_hash()),
                static_cast<unsigned long long>(ipu.cmd_hash()));   // rt1: the identity check of IPU speed work
    std::printf("frame count (mp+8) %u, IPU BP %u IFC %u FP %u, toIPU QWC %u CHCR %08X\n", cpu.peek32(kMp + 8), ipu.bp(),
                ipu.ifc(), ipu.fp(), plat.hw.chan(4, 2), plat.hw.chan(4, 0));
    if (trace_path) {
        std::FILE* t = std::fopen(trace_path, "w");
        if (t) {
            std::fprintf(t, "# cmd bp|ifc<<8|fp<<16 result ctrl\n");
            for (const auto& e : trace)
                std::fprintf(t, "%08X %06X %08llX %08X\n", e.cmd, e.bp, static_cast<unsigned long long>(e.result), e.ctrl);
            std::fclose(t);
        }
    }
    return errors == 0 && pictures > 0 ? 0 : 1;
}
