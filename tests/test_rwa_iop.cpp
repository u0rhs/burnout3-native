// The native RWA IOP executor (src/audio/rwa_iop.cpp) on synthetic command sequences: replies, the 0x14 snapshot's
// write-back and register writes, uploads with the VAG flag patch, the spurious 0x0D, remote file ops, virtual time.
// No disc data needed; the logged-stream test is test_rwa_iop_b0 (`make test_rwa_iop_b0`). Every case runs twice: with
// the stub mixer and with the SPU2-equivalent mixer (spu2_mixer_impl.h); mixer state is read through the libsd-level
// read-backs both implement.
#include "test.h"

#include "../src/audio/spu2_mixer_impl.h"
#include "../src/audio/spu2_stub_mixer.h"
#include "../src/native/rwa_iop.h"

#include <map>
#include <string>
#include <vector>

namespace {

using b3native::RwaIop;
namespace sd = b3audio::sd;

struct Reply { uint32_t op, p5, a0, a1; uint64_t t; };

struct Fake final : b3native::RwaIopPlatform {
    std::map<uint32_t, uint8_t> ee;
    std::vector<Reply> replies;
    RwaIop* iop = nullptr;
    std::string file = std::string(0x3000, '\0');
    std::map<int, uint32_t> pos;
    std::vector<std::string> opened;
    void read_ee(uint32_t a, void* d, uint32_t n) override {
        for (uint32_t k = 0; k < n; ++k) static_cast<uint8_t*>(d)[k] = ee[a + k];
    }
    void write_ee(uint32_t a, const void* s, uint32_t n) override {
        for (uint32_t k = 0; k < n; ++k) ee[a + k] = static_cast<const uint8_t*>(s)[k];
    }
    void reply(const uint32_t p[12]) override { replies.push_back({p[4], p[5], p[9], p[10], iop ? iop->now() : 0}); }
    int file_open(const char* name) override {
        opened.emplace_back(name);
        if (std::string(name) != "sound\\x.awd") return 0;
        pos[7] = 0;
        return 7;
    }
    int file_read(int h, void* d, uint32_t n) override {
        uint32_t& p = pos[h];
        const uint32_t got = p >= file.size() ? 0 : std::min<uint32_t>(n, static_cast<uint32_t>(file.size()) - p);
        std::memcpy(d, file.data() + p, got);
        p += got;
        return static_cast<int>(got);
    }
    int file_seek(int h, uint32_t p) override { pos[h] = p; return 0; }
    void file_close(int h) override { pos.erase(h); }
    void put32(uint32_t a, uint32_t v) { for (int k = 0; k < 4; ++k) ee[a + k] = static_cast<uint8_t>(v >> (8 * k)); }
    uint32_t get32(uint32_t a) { uint32_t v = 0; for (int k = 3; k >= 0; --k) v = v << 8 | ee[a + k]; return v; }
};

template <class M>
struct Rig {
    Fake pf;
    M mix;
    RwaIop iop;
    explicit Rig(b3native::RwaIopOptions o = {}) : iop(pf, mix, o) { pf.iop = &iop; }
    void cmd(uint32_t op, uint32_t p5 = 0, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t src = 0, uint32_t dst = 0, uint32_t size = 0,
             uint32_t f18 = 0, uint32_t f1c = 0, uint32_t f20 = 0) {
        uint32_t p[12] = {};
        p[4] = op; p[5] = p5; p[6] = f18; p[7] = f1c; p[8] = f20 ? f20 : size; p[9] = a0; p[10] = a1; p[11] = 1;
        iop.send_cmd(p, src, dst, size);
    }
    uint32_t base() const { return iop.options().module_base; }
    void open() {
        cmd(1); cmd(2); cmd(0x11, 0, 48, 1); cmd(0x13); cmd(0x15, 0, kReply);
    }
    static constexpr uint32_t kReply = 0x1EE1780, kSnap = 0x1E00000;
    // VoiceState n in the EE-side snapshot buffer
    void voice(uint32_t n, uint32_t play, uint32_t start, uint32_t loop, uint32_t freq, uint32_t vols, uint32_t trig, uint32_t flags) {
        const uint32_t v = kSnap + n * 0x20;
        pf.put32(v, play); pf.put32(v + 4, start); pf.put32(v + 8, loop); pf.put32(v + 12, freq);
        pf.put32(v + 16, vols); pf.put32(v + 20, 0); pf.put32(v + 24, trig); pf.put32(v + 28, flags);
    }
    void snapshot(uint32_t id) {
        pf.put32(kSnap + 0x658, 1); pf.put32(kSnap + 0x660, 0xFFFF); pf.put32(kSnap + 0x664, 1); pf.put32(kSnap + 0x670, id);
        cmd(0x14, 0, id, 0, kSnap, base() + 0xE100, 0x680, kSnap, base() + 0xE100);
    }
};


// Mixer state through the read-backs (hardware voice n).
template <class R> uint16_t vparam(R& r, uint16_t p, int n) { return r.mix.get_param(r.iop.now(), static_cast<uint16_t>(p | sd::voice(n))); }
template <class R> uint32_t vaddr(R& r, uint16_t p, int n) { return r.mix.get_addr(r.iop.now(), static_cast<uint16_t>(p | sd::voice(n))); }

template <class M>
void handshake_replies() {
    Rig<M> r;
    r.cmd(0x49, 0, 0x100000, 0x2000);
    r.open();
    const auto& R = r.pf.replies;
    CHECK(R.size() == 5);
    CHECK(R[0].op == 9 && R[0].a0 == 0x115 && R[0].a1 == 0);
    CHECK(R[1].op == 0xA && R[1].a0 == 0x75700 && R[1].a1 == r.base() + 0xB1A0);   // work area = the IRX arena's first block
    CHECK(R[2].op == 0x1A && R[2].a0 == 0 && R[2].a1 == 0);
    CHECK(R[3].op == 0x1B && R[3].a0 == 0x96200 && R[3].a1 == 0x100000);
    CHECK(R[4].op == 0x1C && R[4].a0 == r.base() + 0xE100);
    // op 0x11: voice defaults (pitch from 22050 = 0x759), silent blocks at 0x5010
    CHECK_BITS(vparam(r, sd::VP_PITCH, 0), 0x759);
    CHECK_BITS(vaddr(r, sd::VA_SSA, 47), 0x5000);
    CHECK_BITS(r.mix.sound_ram()[0x5011], 1);
    CHECK_BITS(r.mix.sound_ram()[0x5021], 7);
    r.cmd(0x4E, 3, 1);
    CHECK(R.back().op == 0x4F && R.back().p5 == 3);
    r.cmd(0x2C, 5, 0x1234, 9);
    CHECK(R.back().op == 0x2D && R.back().p5 == 5 && R.back().a0 == 0x1234 && R.back().a1 == 9);
    const size_t n = R.size();
    r.cmd(0x16, 0, 0x123);   // not a multiple of 0x400: ignored, no reply
    r.cmd(0x36); r.cmd(0x21); r.cmd(0x43, 4);
    CHECK(R.size() == n);
    r.cmd(0x17);             // answered anyway (always_answer_0x17)
    CHECK(R.size() == n + 1 && R.back().op == 0x1D);
}

template <class M>
void snapshot_keyon_writeback_and_end() {
    Rig<M> r;
    r.open();
    // two ADPCM blocks at sound RAM 0x6000 via op 4 (data into IOP RAM) + op 5 (IOP -> sound RAM)
    const uint32_t buf = 0x100000;
    std::vector<uint8_t> blocks(0x40, 0);
    blocks[1] = 0;    // block 0: continue
    blocks[0x11] = 1; // block 1: end, no repeat
    for (uint32_t k = 0; k < blocks.size(); ++k) r.pf.ee[0x1F00000 + k] = blocks[k];
    r.cmd(4, 0, 0, 0, 0x1F00000, buf, 0x40);
    r.cmd(5, 0, 0, 0, 0, 0, 0, buf, 0x6000, 0x40);
    CHECK(r.pf.replies.back().op == 0xD);
    CHECK_BITS(r.mix.sound_ram()[0x6011], 1);
    // key on voice 0 at 48 kHz (pitch 0x1000), trigger 0, not looping; voice 25 idle in state 1
    r.voice(0, 2, 0x6000, 0x6000, 48000, 0x7FFF7FFF, 0, 0);
    r.voice(25, 1, 0, 0, 22050, 0, 0, 0);
    const size_t n = r.pf.replies.size();
    r.snapshot(0x42);
    CHECK(r.pf.replies.size() == n);            // the 5800 us key-off -> key-on gap holds the reply back
    CHECK(vparam(r, sd::VP_ENVX, 0) == 0);
    r.iop.advance(279);
    CHECK(r.pf.replies.size() == n + 1);
    CHECK(r.pf.replies.back().op == 0x14);
    CHECK(r.pf.replies.back().t == r.pf.replies[n - 1].t + 279);
    CHECK_BITS(vparam(r, sd::VP_PITCH, 0), 0x1000);
    CHECK_BITS(vaddr(r, sd::VA_SSA, 0), 0x6000);
    CHECK_BITS(vparam(r, sd::VP_ADSR1, 0), 0x00FF);
    CHECK_BITS(vparam(r, sd::VP_ADSR2, 0), 0x0007);
    CHECK_BITS(vparam(r, sd::VP_VOLL, 0), 0x3FFF);
    CHECK_BITS(r.pf.get32(Rig<M>::kReply + 0), 4);         // PlayState 2 -> 4
    CHECK_BITS(r.pf.get32(Rig<M>::kReply + 0x14), 0);      // CurrentPosition = TriggerPosition
    CHECK_BITS(r.pf.get32(Rig<M>::kReply + 8), 0x5000);    // LoopStartAddress for a non-looping key-on
    CHECK_BITS(r.pf.get32(Rig<M>::kReply + 0x670), 0x42);  // PacketID echoed
    CHECK_BITS(r.pf.get32(Rig<M>::kReply + 25 * 0x20), 1);
    CHECK_BITS(r.pf.get32(Rig<M>::kReply + 25 * 0x20 + 8), 0x5000);
    r.iop.advance(4);
    CHECK_BITS(vaddr(r, sd::VA_LSAX, 0), 0x5010);            // 83 us later: the silent end block
    CHECK(vparam(r, sd::VP_ENVX, 0) != 0);                    // attacking (the real envelope starts at 0 on key-on)
    // 30 samples on: 1 block played; then the end block (flag 1, no repeat) ends the voice
    r.iop.advance(30);
    r.voice(0, 4, 0x6000, 0x5000, 48000, 0x7FFF7FFF, 0, 0);
    r.snapshot(0x43);
    CHECK_BITS(r.pf.get32(Rig<M>::kReply + 0), 4);
    CHECK(r.pf.get32(Rig<M>::kReply + 0x14) >= 28);        // (NAX - Start) * 28 / 16
    r.iop.advance(60);
    r.snapshot(0x44);
    CHECK_BITS(r.pf.get32(Rig<M>::kReply + 0), 1);         // ENVX 0 -> the voice ended
    CHECK_BITS(r.pf.get32(Rig<M>::kReply + 0x670), 0x44);
}

template <class M>
void upload_46_flag_patch_and_spurious_0d() {
    Rig<M> r;
    r.open();
    r.cmd(5, 0, 0, 0, 0, 0, 0, 0x100000, 0x7000, 0x10);   // an op 5 installs the channel-1 callback
    CHECK(r.pf.replies.back().op == 0xD);
    // a 0x40-byte piece at offset 0 of a 0x40-byte ring, from IOP 0x101000 to sound RAM 0x8000
    const uint32_t wa = 0x75700;   // the work area (the IRX arena's first block)
    for (uint32_t k = 0; k < 0x40; ++k) r.pf.ee[0x1F10000 + k] = 0x55;
    r.pf.ee[0x1F10001] = 0;   // the piece's first flag byte clear: "first" (also patch the block before the last)
    r.cmd(4, 0, 0, 0, 0x1F10000, 0x101000, 0x40);
    const uint32_t prm[5] = {0, 0x8000, 0x101000, 0x40, 0x40};
    for (int k = 0; k < 5; ++k) r.pf.put32(0x1F20000 + 4 * k, prm[k]);
    const size_t n = r.pf.replies.size();
    r.cmd(0x46, 0x2B52C0, 0, 0, 0x1F20000, wa + 0x400, 0x14);
    CHECK(r.pf.replies.size() == n + 2);
    CHECK(r.pf.replies[n].op == 0xD);                    // the stale op-5 callback
    CHECK(r.pf.replies[n + 1].op == 0x47 && r.pf.replies[n + 1].p5 == 0x2B52C0);
    const uint8_t* s = r.mix.sound_ram() + 0x8000;
    CHECK_BITS(s[1], 6);       // loop start + repeat
    CHECK_BITS(s[0x31], 3);    // last block of the ring: end + repeat
    CHECK_BITS(s[0x21], 3);    // the block before it (first piece, s[1] was 0)
    CHECK_BITS(s[0x11], 0x55);
    CHECK(r.iop.stats().spurious_0d == 1);
    b3native::RwaIopOptions o;
    o.spurious_0d = false;
    Rig<M> q(o);
    q.open();
    q.cmd(5, 0, 0, 0, 0, 0, 0, 0x100000, 0x7000, 0x10);
    const size_t m = q.pf.replies.size();
    for (int k = 0; k < 5; ++k) q.pf.put32(0x1F20000 + 4 * k, prm[k]);
    q.cmd(0x46, 1, 0, 0, 0x1F20000, wa + 0x400, 0x14);
    CHECK(q.pf.replies.size() == m + 1 && q.pf.replies.back().op == 0x47);
}

template <class M>
void remote_file_ops() {
    Rig<M> r;
    r.open();
    for (size_t k = 0; k < r.pf.file.size(); ++k) r.pf.file[k] = static_cast<char>(k * 7);
    r.cmd(0x30, 1, 0x81F30);
    CHECK(r.pf.replies.back().op == 0x31 && r.pf.replies.back().p5 == 1);
    const uint32_t slots = r.base() + 0xB1A0;
    auto request = [&](uint32_t idx, uint32_t op, uint32_t buf, uint32_t size, uint32_t fpos, uint32_t flags) {
        const uint32_t e = 0x1F30000;
        for (uint32_t k = 0; k < 0x70; ++k) r.pf.ee[e + k] = 0;
        r.pf.put32(e, idx); r.pf.put32(e + 4, buf); r.pf.put32(e + 8, size); r.pf.put32(e + 12, fpos);
        r.pf.put32(e + 16, op); r.pf.put32(e + 20, 0x2B4FC0); r.pf.put32(e + 28, flags);
        const char* name = "sound\\x.awd";
        for (uint32_t k = 0; name[k]; ++k) r.pf.ee[e + 0x24 + k] = static_cast<uint8_t>(name[k]);
        r.cmd(0x32, 0, idx, 0, e, slots + idx * 0x70, 0x70);
        return r.pf.replies.back();
    };
    Reply a = request(1, 2, 0x1F40000, 0x100, 0, 0x16);   // read to EE at the current position (0)
    CHECK(a.op == 0x33 && a.a0 == 0x100 && a.a1 == 1 && a.p5 == 0x2B4FC0);
    CHECK(r.pf.opened.size() == 1);
    CHECK(r.pf.ee[0x1F40000 + 5] == static_cast<uint8_t>(5 * 7));
    a = request(1, 4, 0, 0, 0x800, 0x16);                 // seek
    CHECK(a.op == 0x33 && a.a0 == 0x800);
    a = request(1, 3, 0x102000, 0x10, 0x1234, 0x16);     // read into IOP memory: +0x0C ignored, size up to 0x800
    CHECK(a.op == 0x33 && a.a0 == 0x10);
    CHECK(r.iop.iop_ram()[0x102000] == static_cast<uint8_t>(0x800 * 7));
    CHECK(r.iop.iop_ram()[0x102000 + 0x7FF] == static_cast<uint8_t>((0x800 + 0x7FF) * 7));
    CHECK(r.pf.pos[7] == 0x1000);                         // the file advanced by the rounded size
    a = request(1, 3, 0x102000, 0x10, 0x2000, 0x16 | 8);  // flag 8: seek to +0x0C first
    CHECK(a.op == 0x33 && a.a0 == 0x10);
    CHECK(r.iop.iop_ram()[0x102000] == static_cast<uint8_t>(0x2000 * 7));
    a = request(1, 1, 0, 0, 0, 0x16);                     // close
    CHECK(a.op == 0x33 && a.a0 == 0);
    CHECK(r.pf.pos.empty());
    a = request(2, 4, 0, 0, 0x2FF8, 0x16);                // another slot: opens again; seek near the end
    CHECK(a.op == 0x33 && a.a0 == 0x2FF8 && a.a1 == 2);
    a = request(2, 2, 0x1F40000, 0x10, 0, 0x16);          // a short read still reports the requested size
    CHECK(a.op == 0x33 && a.a0 == 0x10);
    CHECK(r.iop.stats().file_short_reads == 1);
    CHECK(r.pf.opened.size() == 2);
}

template <class M>
void sweep_and_irq_clock() {
    Rig<M> r;
    r.open();
    r.voice(3, 2, 0x6000, 0x6000, 22050, 0x10001000, 0, 1);
    r.pf.ee[Rig<M>::kSnap + 0x600 + 3] = 1;   // UsedVoices[3]
    r.snapshot(1);
    r.iop.advance(300);
    CHECK_BITS(vparam(r, sd::VP_VOLL, 3), 0x0800);   // keyed on at the snapshot's volume, fixed
    r.voice(3, 4, 0x6000, 0x6000, 22050, 0x7FFF7FFF, 0, 1);
    r.snapshot(2);
    const uint16_t before = vparam(r, sd::VP_VOLL, 3);
    r.iop.advance(512);                               // a sweep pass (every second 256-sample IRQ)
    const uint16_t after = vparam(r, sd::VP_VOLL, 3);
    CHECK(before == 0x0800);
    CHECK((after & 0xE000) == 0x8000);                // linear increase toward the new target
    CHECK_BITS(after & 0x7F, 0x20);                   // d = (0x7FFF - 0x1000) >> 7 = 223 -> R[223] = 0x20
}

template <class M>
void mixer_stream_ticks() {
    Rig<M> r;
    r.open();
    const size_t n = r.pf.replies.size();
    r.cmd(0x16, 0, 0x800);   // ring of 0x800 bytes (MixerBufferLength 0x100 << 3)
    CHECK(r.pf.replies.size() == n);
    const uint32_t ring = 0x75C00;   // the arena's second block (after the 0x414-byte work area, 0x100 granules), 0x80-aligned
    for (uint32_t k = 0; k < 0x200; ++k) r.pf.ee[0x1F50000 + k] = static_cast<uint8_t>(k);
    r.cmd(4, 0, 0, 0, 0x1F50000, ring, 0x200);   // the EE's mixer data for the first half's left channel
    CHECK(r.pf.replies.back().op == 0xC);
    r.iop.advance(256 * 4);
    std::vector<Reply> ticks;
    for (size_t k = n; k < r.pf.replies.size(); ++k)
        if (r.pf.replies[k].op == 0x19) ticks.push_back(r.pf.replies[k]);
    CHECK(ticks.size() == 4);   // one per N/2 bytes = one per IRQ here
    CHECK(ticks.size() >= 2 && ticks[0].a0 == ring + 0x400 && ticks[0].a1 == 0x400 && ticks[1].a0 == ring);
    CHECK(ticks.size() >= 2 && ticks[1].t - ticks[0].t == 256);
    CHECK_BITS(r.mix.sound_ram()[0x4000 + 5], 5);   // MEMIN core 0 left, first slot
    r.cmd(0x17);
    CHECK(r.pf.replies.back().op == 0x1D);
    const size_t m = r.pf.replies.size();
    r.iop.advance(256 * 4);
    CHECK(r.pf.replies.size() == m);   // no more ticks
    CHECK_BITS(r.mix.sound_ram()[0x4000 + 5], 0);
}

TEST(handshake_replies_stub) { handshake_replies<b3audio::Spu2StubMixer>(); }
TEST(handshake_replies_spu2) { handshake_replies<b3audio::Spu2MixerImpl>(); }
TEST(snapshot_keyon_writeback_and_end_stub) { snapshot_keyon_writeback_and_end<b3audio::Spu2StubMixer>(); }
TEST(snapshot_keyon_writeback_and_end_spu2) { snapshot_keyon_writeback_and_end<b3audio::Spu2MixerImpl>(); }
TEST(upload_46_flag_patch_and_spurious_0d_stub) { upload_46_flag_patch_and_spurious_0d<b3audio::Spu2StubMixer>(); }
TEST(upload_46_flag_patch_and_spurious_0d_spu2) { upload_46_flag_patch_and_spurious_0d<b3audio::Spu2MixerImpl>(); }
TEST(remote_file_ops_stub) { remote_file_ops<b3audio::Spu2StubMixer>(); }
TEST(remote_file_ops_spu2) { remote_file_ops<b3audio::Spu2MixerImpl>(); }
TEST(sweep_and_irq_clock_stub) { sweep_and_irq_clock<b3audio::Spu2StubMixer>(); }
TEST(sweep_and_irq_clock_spu2) { sweep_and_irq_clock<b3audio::Spu2MixerImpl>(); }
TEST(mixer_stream_ticks_stub) { mixer_stream_ticks<b3audio::Spu2StubMixer>(); }
TEST(mixer_stream_ticks_spu2) { mixer_stream_ticks<b3audio::Spu2MixerImpl>(); }

}  // namespace

TEST_MAIN("rwa_iop (native RWA IOP executor; stub and SPU2-equivalent mixers)")
