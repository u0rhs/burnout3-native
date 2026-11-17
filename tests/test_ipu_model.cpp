// tests/test_ipu_model.cpp -- the native IPU model (src/native/ipu_model.h, movie1): hand-made bitstreams only (no
// disc data). Bit counts and results are checked against values worked out by hand from the EE User's Manual 8.4 /
// 8.5 and 13818-2 Annex B; the tables' completeness by their Kraft sums.
#include "test.h"

#include "hw_model_dev.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

using b3native::HwModelDev;
using b3native::IpuModel;

namespace {

// MSB-first bit writer -> the bytes of a bitstream (memory order = stream order).
struct Bits {
    std::vector<std::uint8_t> b;
    unsigned n = 0;
    Bits& put(const char* s) {
        for (; *s; ++s) if (*s == '0' || *s == '1') bit(*s == '1');
        return *this;
    }
    Bits& val(std::uint32_t v, unsigned len) {
        for (unsigned i = len; i-- > 0;) bit((v >> i) & 1u);
        return *this;
    }
    void bit(bool x) {
        if ((n & 7) == 0) b.push_back(0);
        if (x) b.back() |= static_cast<std::uint8_t>(0x80u >> (n & 7));
        ++n;
    }
    void pad_qwords(unsigned extra = 0) { while (b.size() % 16) b.push_back(0); for (unsigned i = 0; i < extra * 16; ++i) b.push_back(0); }
};

// A test rig: HwModelDev (the dev model, hw_model_dev.h: the IPU) with 1 MB of fake RAM; helpers for the registers.
struct Rig {
    HwModelDev m;
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(1u << 20);
    Rig() {
        m.read_ram = [this](std::uint32_t a, void* out, unsigned n) {
            if (a + n > ram.size()) return false;
            std::memcpy(out, ram.data() + a, n);
            return true;
        };
        m.write_ram = [this](std::uint32_t a, const void* in, unsigned n) {
            if (a + n > ram.size()) return false;
            std::memcpy(ram.data() + a, in, n);
            return true;
        };
    }
    void w32(std::uint32_t a, std::uint32_t v) { std::uint8_t d[16] = {}; std::memcpy(d, &v, 4); m.write(a, 4, d); }
    std::uint32_t r32(std::uint32_t a) { std::uint8_t d[16] = {}; m.read(a, 4, d); std::uint32_t v; std::memcpy(&v, d, 4); return v; }
    std::uint64_t r64(std::uint32_t a) { std::uint8_t d[16] = {}; m.read(a, 8, d); std::uint64_t v; std::memcpy(&v, d, 8); return v; }
    void fifo(const std::uint8_t* q) { m.write(0x10007010u, 16, q); }
    void push(const Bits& s) { for (std::size_t i = 0; i < s.b.size(); i += 16) fifo(&s.b[i]); }
    void cmd(std::uint32_t c) { w32(0x10002000u, c); }
    std::uint32_t ctrl() { return r32(0x10002010u); }
    std::uint32_t bp() { return r32(0x10002020u) & 0x7F; }
    std::uint32_t ifc() { return (r32(0x10002020u) >> 8) & 0xF; }
    std::uint32_t fp() { return (r32(0x10002020u) >> 16) & 3; }
    std::uint32_t data() { return static_cast<std::uint32_t>(r64(0x10002000u)); }
    // absolute bit position of the decoder: qwords already dropped are counted by the caller; here BP plus the
    // qwords consumed out of `pushed`
    void reset() { w32(0x10002010u, 0x40000000u); cmd(0); }
};

double kraft(const b3native::IpuVlcCode* c, unsigned n, unsigned extra_bits = 0) {
    double s = 0;
    for (unsigned i = 0; i < n; ++i) s += std::ldexp(1.0, -static_cast<int>(std::strlen(c[i].bits) + extra_bits));
    return s;
}

}  // namespace

TEST(ipu_registers_reset_bclr) {
    Rig r;
    r.reset();
    CHECK_BITS(r.ctrl(), 0);
    Bits s;
    s.val(0x12345678u, 32).val(0x9ABCDEF0u, 32);
    s.pad_qwords(1);
    r.push(s);
    CHECK_BITS(r.ifc(), 2);
    CHECK_BITS(r.ctrl() & 0xF, 2);   // IFC also in IPU_CTRL 3:0 [C manual 8.3]
    r.cmd(0x00000008u);              // BCLR 8: FIFO cleared, BP = 8
    CHECK_BITS(r.ifc(), 0);
    CHECK_BITS(r.bp(), 8);
    r.push(s);
    r.cmd(0x40000000u);              // FDEC FB 0: the 32 bits after BP 8
    CHECK_BITS(r.data(), 0x3456789Au);
    CHECK_BITS(r.bp(), 8);           // FDEC consumes FB only
    CHECK_BITS(r.fp(), 1);           // the internal buffer took one qword [L]
    CHECK_BITS(r.ifc(), 1);
    r.cmd(0x40000000u | 24);         // FDEC FB 24: 32 bits from bit 32
    CHECK_BITS(r.data(), 0x9ABCDEF0u);
    CHECK_BITS(r.bp(), 32);
    // IPU_TOP: the same 32 bits, not consumed
    CHECK_BITS(static_cast<std::uint32_t>(r.r64(0x10002030u)), 0x9ABCDEF0u);
    CHECK(!(r.r64(0x10002030u) >> 63));
    r.cmd(0x40000000u | 32);         // flush 32 more (bit 64..95: zeros), crossing nothing
    r.cmd(0x40000000u | 32);         // to bit 96: 32 bits left in qword 0 -> FP 1
    CHECK_BITS(r.bp(), 96);
    r.cmd(0x40000000u | 32);         // bit 128: qword 0 dropped
    CHECK_BITS(r.bp(), 0);
    CHECK_BITS(r.fp() + r.ifc(), 1);
    // reset: everything abandoned, r/w fields from the written value
    r.w32(0x10002010u, 0x40800000u);
    CHECK_BITS(r.ctrl(), 0x00800000u);   // MP1 kept from the write, IFC 0
    CHECK_BITS(r.ifc() + r.fp(), 0);
}

TEST(ipu_sceIpuInit_sequence) {
    // what libipu's sceIpuInit does (libipu.cpp): RST, BCLR, 8 qwords, SETIQ intra / non-intra, 2 qwords, SETVQ,
    // SETTH, RST, BCLR
    Rig r;
    r.w32(0x10002010u, 0x40000000u);
    CHECK(!(r.ctrl() >> 31));
    r.cmd(0);
    std::uint8_t q[16];
    for (unsigned k = 0; k < 4; ++k) { for (unsigned i = 0; i < 16; ++i) q[i] = static_cast<std::uint8_t>(k * 16 + i + 1); r.fifo(q); }
    for (unsigned k = 0; k < 4; ++k) { std::memset(q, 16, 16); r.fifo(q); }
    CHECK_BITS(r.ifc(), 8);
    r.cmd(0x50000000u);
    CHECK(!(r.ctrl() >> 31));
    CHECK_BITS(r.m.ipu().iq_intra()[0], 1);
    CHECK_BITS(r.m.ipu().iq_intra()[63], 64);
    r.cmd(0x58000000u);
    CHECK_BITS(r.m.ipu().iq_nonintra()[0], 16);
    CHECK_BITS(r.m.ipu().iq_nonintra()[63], 16);
    CHECK_BITS(r.ifc() + r.fp(), 0);
    CHECK_BITS(r.bp(), 0);
    std::memset(q, 0, 16); r.fifo(q); r.fifo(q);
    r.cmd(0x60000000u);
    CHECK_BITS(r.ifc() + r.fp(), 0);
    r.cmd(0x90000000u);
    r.w32(0x10002010u, 0x40000000u);
    r.cmd(0);
    CHECK_BITS(r.ctrl(), 0);
    CHECK(r.m.ipu().commands() == 6);
}

TEST(ipu_vlc_tables_kraft) {
    using namespace b3native;
    // a prefix code that covers everything sums to 1; the unused prefixes are named
    // MBAI: the manual's error rows 00000010, 00000001111 (MPEG-2; stuffing in MPEG-1), 00000001110, 0000000110,
    // 0000000101, 00000001001, 00000000
    CHECK_NEAR(kraft(kIpuMbai, ipu_count(kIpuMbai)),
               1.0 - 2 * std::ldexp(1.0, -8) - 2 * std::ldexp(1.0, -10) - 3 * std::ldexp(1.0, -11), 1e-12);
    CHECK_NEAR(kraft(kIpuMbTypeI, 2), 0.75, 1e-12);                         // "00" error
    CHECK_NEAR(kraft(kIpuMbTypeP, ipu_count(kIpuMbTypeP)), 1.0 - 1.0 / 64, 1e-12);   // "000000" error
    CHECK_NEAR(kraft(kIpuMbTypeB, ipu_count(kIpuMbTypeB)), 1.0 - 1.0 / 64, 1e-12);
    // motion code: "1" + each magnitude code (its sign bit makes two codewords: mass 2^-len) + the errors 00000010,
    // 0000000 = 1
    CHECK_NEAR(0.5 + kraft(kIpuMotionMagnitude, ipu_count(kIpuMotionMagnitude)) + std::ldexp(1.0, -8) + std::ldexp(1.0, -7),
               1.0, 1e-12);
    CHECK_NEAR(kraft(kIpuDmv, 3), 1.0, 1e-12);
    CHECK_NEAR(kraft(kIpuCbp, ipu_count(kIpuCbp)), 1.0 - 1.0 / 512, 1e-12);   // "000000000" unused
    CHECK_NEAR(kraft(kIpuDcSizeLuma, ipu_count(kIpuDcSizeLuma)), 1.0, 1e-12);
    CHECK_NEAR(kraft(kIpuDcSizeChroma, ipu_count(kIpuDcSizeChroma)), 1.0, 1e-12);
    // DCT tables (a code with its sign bit is two codewords: mass 2^-len): table zero leaves only the prefix of 12
    // zeros unused (no B-14 code starts with it)
    auto dct = [](const IpuVlcCode* a, unsigned n) { return kraft(a, n); };
    const double longs = dct(kIpuDctLong, ipu_count(kIpuDctLong));
    const double z = dct(kIpuDctZero, ipu_count(kIpuDctZero)) + longs, o = dct(kIpuDctOne, ipu_count(kIpuDctOne)) + longs;
    CHECK_NEAR(z, 1.0 - std::ldexp(1.0, -12), 1e-12);
    // table one: B-14's 12/13-bit codes of (0,8..15), (1,5), (2,4) are unused in B-15 (their levels have short codes)
    CHECK_NEAR(o, 1.0 - std::ldexp(1.0, -12) - 6 * std::ldexp(1.0, -12) - 4 * std::ldexp(1.0, -13), 1e-12);
}

TEST(ipu_vdec_every_code) {
    using namespace b3native;
    struct T { const IpuVlcCode* c; unsigned n; unsigned tbl; std::uint32_t pct; };
    const T tables[] = {
        {kIpuMbai, ipu_count(kIpuMbai), 0, 0}, {kIpuMbTypeI, 2, 1, 1}, {kIpuMbTypeP, ipu_count(kIpuMbTypeP), 1, 2},
        {kIpuMbTypeB, ipu_count(kIpuMbTypeB), 1, 3}, {kIpuMbTypeD, 1, 1, 4}, {kIpuDmv, 3, 3, 0},
    };
    for (const T& t : tables)
        for (unsigned i = 0; i < t.n; ++i) {
            Rig r;
            r.reset();
            r.w32(0x10002010u, t.pct << 24);
            Bits s;
            s.put("101").put(t.c[i].bits).put("1100");   // 3 bits skipped by FB, the code, then something else
            s.pad_qwords(1);
            r.push(s);
            r.cmd(0x30000000u | (t.tbl << 26) | 3);
            const std::uint32_t len = static_cast<std::uint32_t>(std::strlen(t.c[i].bits));
            CHECK_BITS(r.data(), (len << 16) | (static_cast<std::uint32_t>(t.c[i].value) & 0xFFFFu));
            CHECK_BITS(r.bp(), 3 + len);
            CHECK(!(r.ctrl() & 0x4000u));
        }
    // motion codes: the manual's signed list (8.5.3) [C]
    const std::pair<const char*, std::int32_t> mc[] = {
        {"00000011001", -16}, {"00000011011", -15}, {"00000011101", -14}, {"00000011111", -13}, {"00000100001", -12},
        {"00000100011", -11}, {"0000010011", -10}, {"0000010101", -9}, {"0000010111", -8}, {"00000111", -7},
        {"00001001", -6}, {"00001011", -5}, {"0000111", -4}, {"00011", -3}, {"0011", -2}, {"011", -1}, {"1", 0},
        {"010", 1}, {"0010", 2}, {"00010", 3}, {"0000110", 4}, {"00001010", 5}, {"00001000", 6}, {"00000110", 7},
        {"0000010110", 8}, {"0000010100", 9}, {"0000010010", 10}, {"00000100010", 11}, {"00000100000", 12},
        {"00000011110", 13}, {"00000011100", 14}, {"00000011010", 15}, {"00000011000", 16},
    };
    for (const auto& e : mc) {
        Rig r;
        r.reset();
        Bits s;
        s.put(e.first).put("0101");
        s.pad_qwords(1);
        r.push(s);
        r.cmd(0x30000000u | (2u << 26));
        const std::uint32_t len = static_cast<std::uint32_t>(std::strlen(e.first));
        CHECK_BITS(r.data(), (len << 16) | (static_cast<std::uint32_t>(e.second) & 0xFFFFu));
        CHECK_BITS(r.bp(), len);
    }
    // errors: DATA 0, ECD; the MBAI stuffing code only with MP1
    {
        Rig r;
        r.reset();
        Bits s;
        s.put("00000010 0000 0000");
        s.pad_qwords(1);
        r.push(s);
        r.cmd(0x30000000u);
        CHECK_BITS(r.data(), 0);
        CHECK(r.ctrl() & 0x4000u);
        CHECK_BITS(r.bp(), 0);
    }
    for (int mp1 = 0; mp1 < 2; ++mp1) {
        Rig r;
        r.reset();
        r.w32(0x10002010u, mp1 ? 0x00800000u : 0);
        Bits s;
        s.put("00000001111 1");
        s.pad_qwords(1);
        r.push(s);
        r.cmd(0x30000000u);
        CHECK_BITS(r.data(), mp1 ? 0x000B0022u : 0);
    }
}

TEST(ipu_command_waits_for_data) {
    Rig r;
    r.reset();
    Bits s;
    s.val(0xCAFEBABEu, 32);
    s.pad_qwords();
    // FDEC FB 100 needs 132 bits: one qword is not enough -> busy, nothing consumed
    r.push(s);
    r.cmd(0x40000000u | 0x3F);   // FB 63: 95 bits needed, here 128: completes
    CHECK(!(r.ctrl() >> 31));
    r.cmd(0x40000000u | 0x3F);   // to bit 126: needs 158 > 128 -> waits
    CHECK(r.ctrl() >> 31);
    CHECK(r.r64(0x10002000u) >> 63);
    CHECK(r.r64(0x10002030u) >> 63);
    CHECK_BITS(r.bp(), 63);
    Bits t;
    t.val(0x0123456Fu, 32);
    t.pad_qwords();
    r.push(t);
    CHECK(!(r.ctrl() >> 31));
    CHECK_BITS(r.bp(), 126);
    // bits 126..157: the last 2 bits of qword 0 (0) then 0x0123456F's first 30 bits
    CHECK_BITS(r.data(), 0x0123456Fu >> 2);
}

TEST(ipu_bdec_hand_made_macroblocks) {
    // An intra macroblock, MPEG-2, table zero, then the start of the next macroblock ("1" = MBAI 1):
    //   Y0: dc size 3 "101" + "110"; AC (0,1) "11"+"0"; (1,1) "011"+"1"; EOB "10"     = 6 + 3 + 4 + 2 = 15
    //   Y1: dc size 0 "100"; EOB "10"                                               = 5
    //   Y2: dc size 1 "00" + "1"; escape "000001" run 5 "000101" level -3 "111111111101"; EOB "10" = 3 + 24 + 2 = 29
    //   Y3: dc size 0 "100"; (0,2) "0100"+"0"; EOB "10"                             = 3 + 5 + 2 = 10
    //   Cb: dc size 0 "00"; EOB "10"                                                = 4
    //   Cr: dc size 2 "10" + "01"; (0,40) "000000000010000"+"1"; EOB "10"           = 4 + 16 + 2 = 22
    // total 85 bits; then "1" (not 8 zeros: macroblock follows)
    Bits s;
    s.put("101 110 11 0 011 1 10");
    s.put("100 10");
    s.put("00 1 000001 000101 111111111101 10");
    s.put("100 0100 0 10");
    s.put("00 10");
    s.put("10 01 000000000010000 1 10");
    s.put("1 0000000");
    s.pad_qwords(1);
    for (int dt = 0; dt < 2; ++dt) {
        Rig r;
        r.reset();
        r.push(s);
        r.cmd(0x20000000u | (1u << 27) | (1u << 26) | (static_cast<std::uint32_t>(dt) << 25) | (5u << 16));
        CHECK(!(r.ctrl() & 0x4000u));
        CHECK(!(r.ctrl() & 0x8000u));
        CHECK_BITS(r.bp(), 85);
        CHECK_BITS((r.ctrl() >> 8) & 0x3F, 0x3F);
        CHECK_BITS((r.ctrl() >> 4) & 0xF, 8);   // 48 qwords of RAW16, no fromIPU DMA: 8 in the FIFO, BUSY until drained
        CHECK(r.ctrl() >> 31);
    }
    // Table one (IVF) for the same intra block shape: Y0 dc "101"+"110", (0,1) "10"+"0", (0,15) "11111111"+"1", EOB
    // "0110"; Y1..Cr: dc size 0, EOB "0110"
    {
        Bits u;
        u.put("101 110 10 0 11111111 1 0110");
        u.put("100 0110").put("100 0110").put("100 0110").put("00 0110").put("00 0110");
        // 6 + 3 + 9 + 4 = 22; + 3 * 7 + 2 * 6 = 33 -> 55; then 8 zeros: a start code follows (SCD, BP at 56)
        u.put("0 00000000 00000000 00000001 10110011");
        u.pad_qwords(1);
        Rig r;
        r.reset();
        r.w32(0x10002010u, 0x00200000u);   // IVF
        r.push(u);
        r.cmd(0x20000000u | (1u << 27) | (1u << 26));
        CHECK(!(r.ctrl() & 0x4000u));
        CHECK(r.ctrl() & 0x8000u);
        CHECK_BITS(r.bp(), 56);
    }
    // A non-intra macroblock with cbp: "1101" = cbp 4 (block 3, Y3 only... cbp bit 0x04 = Y3? 0x20 Y0, 0x10 Y1,
    // 0x08 Y2, 0x04 Y3, 0x02 Cb, 0x01 Cr); its block: first "1"+"1" = (0,-1), then "011"+"0" (1,1), EOB "10"
    {
        Bits u;
        u.put("1101 1 1 011 0 10");   // 4 + 2 + 4 + 2 = 12
        u.put("1");
        u.pad_qwords(1);
        Rig r;
        r.reset();
        r.push(u);
        r.cmd(0x20000000u | 7u);   // FB 7 skips nothing useful: put the MB at bit 7
        CHECK(r.ctrl() & 0x4000u);   // the 7 skipped bits were the MB's start: an error code here
        Rig r2;
        r2.reset();
        r2.push(u);
        r2.cmd(0x20000000u);
        CHECK(!(r2.ctrl() & 0x4000u));
        CHECK_BITS(r2.bp(), 12);
        CHECK_BITS((r2.ctrl() >> 8) & 0x3F, 4);
    }
    // MPEG-1 escape: run 2 "000010", level 8 bits 0x00 then 8 bits 200 = +200; then EOB
    {
        Bits u;
        u.put("01001");                                       // cbp 2 (Cb)
        u.put("000001 000010 00000000 11001000 10");          // 6 + 6 + 16 + 2
        u.put("1");
        u.pad_qwords(1);
        Rig r;
        r.reset();
        r.w32(0x10002010u, 0x00800000u);
        r.push(u);
        r.cmd(0x20000000u);
        CHECK(!(r.ctrl() & 0x4000u));
        CHECK_BITS(r.bp(), 5 + 30);
    }
    // a run past 63: error code
    {
        Bits u;
        u.put("01001 000001 111111 000000000001 000001 111111 000000000001 10 1");
        u.pad_qwords(1);
        Rig r;
        r.reset();
        r.push(u);
        r.cmd(0x20000000u);
        CHECK(r.ctrl() & 0x4000u);
    }
}

TEST(ipu_bdec_waits_then_completes) {
    Bits s;
    s.put("101 110 11 0 011 1 10").put("100 10").put("100 10").put("100 10").put("00 10").put("00 10").put("1");
    // 15 + 4*5 ... = 15 + 5 + 5 + 5 + 4 + 4 = 38 bits, +1
    s.pad_qwords(1);
    Rig r;
    r.reset();
    // put the macroblock across a qword boundary: 120 bits of FB first
    Bits pre;
    pre.val(0, 32).val(0, 32).val(0, 32).val(0, 24);
    for (std::size_t i = 0; i < s.b.size() * 8 - 0; ++i) pre.bit((s.b[i / 8] >> (7 - (i & 7))) & 1u);
    pre.pad_qwords();
    // only the first qword
    r.fifo(&pre.b[0]);
    r.cmd(0x40000000u | 60);
    r.cmd(0x40000000u | 60);   // at bit 120: FDEC needs 152 bits -> waits
    CHECK(r.ctrl() >> 31);
    for (std::size_t i = 16; i < pre.b.size(); i += 16) r.fifo(&pre.b[i]);
    CHECK(!(r.ctrl() >> 31));
    r.cmd(0x20000000u | (1u << 27));
    CHECK(!(r.ctrl() & 0x4000u));
    CHECK_BITS(r.bp(), (120 + 38) - 128);
}

TEST(ipu_bdec_macroblock_larger_than_the_ipu) {
    // intra, MPEG-2, table zero: every block = dc size 0 + 63 escapes (run 0, level 1 .. 63) + EOB; then "1"
    // per block 3 (luma) / 2 (chroma) + 63 * 24 + 2 bits; the whole macroblock 4 * 1517 + 2 * 1516 = 9100 bits
    Bits s;
    unsigned bits = 0;
    for (int b = 0; b < 6; ++b) {
        if (b < 4) { s.put("100"); bits += 3; } else { s.put("00"); bits += 2; }
        for (int c = 1; c <= 63; ++c) { s.put("000001 000000").val(static_cast<std::uint32_t>(c), 12); bits += 24; }
        s.put("10");
        bits += 2;
    }
    s.put("1");
    s.pad_qwords(1);
    CHECK_BITS(bits, 9100);
    Rig r;
    r.reset();
    std::memcpy(&r.ram[0x10000], s.b.data(), s.b.size());
    r.w32(0x1000B410u, 0x10000);
    r.w32(0x1000B420u, static_cast<std::uint32_t>(s.b.size() / 16));
    r.w32(0x1000B400u, 0x101);
    r.w32(0x1000B010u, 0x40000);
    r.w32(0x1000B020u, 48);
    r.w32(0x1000B000u, 0x100);
    r.cmd(0x20000000u | (1u << 27) | (1u << 26));
    CHECK(!(r.ctrl() >> 31));
    CHECK(!(r.ctrl() & 0x4000u));
    CHECK_BITS(r.bp(), 9100 % 128);
    // what is left in the IPU + what the DMA has not sent = the qwords after the one holding bit 9100
    const std::uint32_t left = r.ifc() + r.fp() + r.r32(0x1000B420u);
    CHECK_BITS(left, static_cast<std::uint32_t>(s.b.size() / 16) - 9100 / 128);
    // an escape with level 0 is an error code (13818-2 B-16): ECD, BP at the code
    Bits u;
    u.put("100 000001 000011 000000000000 10 1");
    u.pad_qwords(1);
    Rig q;
    q.reset();
    q.push(u);
    q.cmd(0x20000000u | (1u << 27));
    CHECK(q.ctrl() & 0x4000u);
    CHECK_BITS(q.bp(), 3);
}

TEST(ipu_dma_chain_and_fromipu) {
    Rig r;
    r.reset();
    // a stream of 12 qwords at 0x1000: qword i holds 32-bit words i*4.. (big-endian bytes so FDEC reads them back)
    for (unsigned i = 0; i < 12 * 4; ++i) {
        const std::uint32_t v = 0xA0000000u + i;
        r.ram[0x1000 + i * 4 + 0] = static_cast<std::uint8_t>(v >> 24);
        r.ram[0x1000 + i * 4 + 1] = static_cast<std::uint8_t>(v >> 16);
        r.ram[0x1000 + i * 4 + 2] = static_cast<std::uint8_t>(v >> 8);
        r.ram[0x1000 + i * 4 + 3] = static_cast<std::uint8_t>(v);
    }
    // chain at 0x8000: ref 5 qwords at 0x1000, then refe 7 qwords at 0x1050
    auto tag = [&](std::uint32_t at, std::uint32_t id, std::uint32_t qwc, std::uint32_t addr) {
        const std::uint32_t w0 = qwc | (id << 28);
        std::memcpy(&r.ram[at], &w0, 4);
        std::memcpy(&r.ram[at + 4], &addr, 4);
    };
    tag(0x8000, 3, 5, 0x1000);
    tag(0x8010, 0, 7, 0x1050);
    r.w32(0x1000B430u, 0x8000);   // TADR
    r.w32(0x1000B420u, 0);        // QWC
    r.w32(0x1000B400u, 0x105);    // chain, DIR 1, STR
    CHECK_BITS(r.ifc(), 8);       // the FIFO is full; the transfer waits
    CHECK(r.r32(0x1000B400u) & 0x100u);
    CHECK_BITS(r.r32(0x1000B430u), 0x8020);
    CHECK_BITS(r.r32(0x1000B420u), 4);   // 3 of the refe's 7 qwords sent
    r.cmd(0x40000000u);
    CHECK_BITS(r.data(), 0xA0000000u);
    // consume 7 qwords (4 per qword) by FDEC 32 x 28 -> the rest comes in
    for (int i = 0; i < 28; ++i) r.cmd(0x40000000u | 32);
    CHECK_BITS(r.data(), 0xA000001Cu);
    CHECK(!(r.r32(0x1000B400u) & 0x100u));   // the chain ended (refe)
    CHECK(r.m.d_stat() & (1u << 4));
    CHECK_BITS(r.ifc() + r.fp(), 5);
    // fromIPU: 48 qwords of a BDEC (all-zero cbp: "000000001" in MPEG-2) drained to 0x20000
    r.reset();
    Bits s;
    s.put("000000001 1");
    s.pad_qwords(1);
    r.push(s);
    r.w32(0x1000B010u, 0x20000);
    r.w32(0x1000B020u, 48);
    r.w32(0x1000B000u, 0x100);
    r.cmd(0x20000000u);
    CHECK(!(r.ctrl() >> 31));
    CHECK_BITS(r.r32(0x1000B020u), 0);
    CHECK_BITS(r.r32(0x1000B010u), 0x20000 + 48 * 16);
    CHECK(!(r.r32(0x1000B000u) & 0x100u));
    CHECK(r.m.d_stat() & (1u << 3));
    CHECK_BITS(r.bp(), 9);
}

// The manual's CSC steps 1-10 written out literally (8.6.1), independent of the model's code.
static void csc_ref(int Y, int Cb, int Cr, int th0, int th1, int out[4]) {
    const int Cr1 = Cr - 128, Cb1 = Cb - 128;
    const int Cr2 = 0x0cc * Cr1, Cr3 = 0x068 * Cr1, Cb2 = 0x032 * Cb1, Cb3 = 0x102 * Cb1;   // s17.7
    const int Cr4 = Cr2 & ~0x003f, Cr5 = Cr3 & ~0x003f, Cb4 = Cb2 & ~0x003f, Cb5 = Cb3 & ~0x003f;   // s11.1 (in .7 units)
    const int Y1 = Y - 16, Y2 = 0x095 * Y1, Y3 = Y2 & ~0x003f;
    const int r0 = Y3 + Cr4, g0 = Y3 - Cb4 - Cr5, b0 = Y3 + Cb5;   // s12.1 in .7 units: the low 6 bits are zero
    auto rnd = [](int v) { const int half = v / 64; return (half + 1) >> 1; };   // .1 -> .0, rounding off bit 0
    auto clip = [](int v) { return v < 0 ? 0 : v > 255 ? 255 : v; };
    int R = clip(rnd(r0)), G = clip(rnd(g0)), B = clip(rnd(b0)), A;
    if (R < th0 && G < th0 && B < th0) { R = G = B = 0; A = 0; }
    else if (R < th1 && G < th1 && B < th1) A = 0x40;
    else A = 0x80;
    out[0] = R; out[1] = G; out[2] = B; out[3] = A;
}

TEST(ipu_csc_pixels) {
    // one macroblock of RAW8 through toIPU DMA (24 qwords at 0x2000), CSC to RGB32 through fromIPU (64 qwords to
    // 0x3000); thresholds 20 / 60
    Rig r;
    r.reset();
    std::uint8_t* raw = &r.ram[0x2000];
    for (unsigned y = 0; y < 16; ++y) for (unsigned x = 0; x < 16; ++x) raw[y * 16 + x] = static_cast<std::uint8_t>(16 + x * 14 + y);
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
        raw[256 + y * 8 + x] = static_cast<std::uint8_t>(128 + 30 * (static_cast<int>(x) - 4));
        raw[320 + y * 8 + x] = static_cast<std::uint8_t>(128 - 25 * (static_cast<int>(y) - 3));
    }
    r.cmd(0x90000000u | 20u | (60u << 16));
    r.w32(0x1000B010u, 0x3000);
    r.w32(0x1000B020u, 64);
    r.w32(0x1000B000u, 0x100);
    r.w32(0x1000B410u, 0x2000);
    r.w32(0x1000B420u, 24);
    r.w32(0x1000B400u, 0x101);
    r.cmd(0x70000001u);
    CHECK(!(r.ctrl() >> 31));
    CHECK_BITS(r.r32(0x1000B020u), 0);
    CHECK(!(r.r32(0x1000B400u) & 0x100u));
    int bad = 0;
    for (unsigned y = 0; y < 16; ++y) for (unsigned x = 0; x < 16; ++x) {
        int e[4];
        csc_ref(raw[y * 16 + x], raw[256 + (y / 2) * 8 + x / 2], raw[320 + (y / 2) * 8 + x / 2], 20, 60, e);
        const std::uint8_t* p = &r.ram[0x3000 + (y * 16 + x) * 4];
        if (p[0] != e[0] || p[1] != e[1] || p[2] != e[2] || p[3] != e[3]) ++bad;
    }
    CHECK_BITS(static_cast<std::uint32_t>(bad), 0);
    // RGB16 without dither: R5 = R8 >> 3 ..., A1 = (A8 == 0x40)
    r.reset();
    r.w32(0x1000B010u, 0x4000);
    r.w32(0x1000B020u, 32);
    r.w32(0x1000B000u, 0x100);
    r.w32(0x1000B410u, 0x2000);
    r.w32(0x1000B420u, 24);
    r.w32(0x1000B400u, 0x101);
    r.cmd(0x70000001u | (1u << 27));
    bad = 0;
    for (unsigned p = 0; p < 256; ++p) {
        const std::uint8_t* c = &r.ram[0x3000 + p * 4];
        const std::uint16_t want = static_cast<std::uint16_t>((c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10) | (c[3] == 0x40 ? 0x8000 : 0));
        std::uint16_t got;
        std::memcpy(&got, &r.ram[0x4000 + p * 2], 2);
        if (got != want) ++bad;
    }
    CHECK_BITS(static_cast<std::uint32_t>(bad), 0);
}

// mac4: the IDCT against the double-precision definition (13818-2 Annex A), IEEE 1180's ranges: random blocks of
// spatial values in [-L, H], their exact DCT rounded, back through the model's IDCT; peak error <= 1, the mean
// per-pixel error and mean square error within 1180's bounds
TEST(ipu_idct_ieee1180) {
    const double kPi = 3.14159265358979323846;
    double c[8][8];
    for (int u = 0; u < 8; ++u)
        for (int x = 0; x < 8; ++x) c[u][x] = (u == 0 ? std::sqrt(0.125) : 0.5) * std::cos((2 * x + 1) * u * kPi / 16);
    const int ranges[][2] = {{256, 255}, {5, 5}, {300, 300}};
    std::uint32_t seed = 1;
    auto rnd = [&seed](int lo, int hi) {   // 1180's generator form (an LCG; any uniform source does here)
        seed = seed * 1103515245u + 12345u;
        return lo + static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(hi - lo + 1));
    };
    for (const auto& rg : ranges) {
        int peak = 0;
        double sum_err = 0, sum_sq = 0;
        const int kBlocks = 2000;
        for (int n = 0; n < kBlocks; ++n) {
            int px[64];
            for (int i = 0; i < 64; ++i) px[i] = rnd(-rg[0], rg[1]);
            std::int16_t blk[64];
            double ref[64];
            for (int v = 0; v < 8; ++v)
                for (int u = 0; u < 8; ++u) {
                    double a = 0;
                    for (int y = 0; y < 8; ++y)
                        for (int x = 0; x < 8; ++x) a += c[v][y] * c[u][x] * px[8 * y + x];
                    blk[8 * v + u] = static_cast<std::int16_t>(std::clamp(static_cast<int>(std::floor(a + 0.5)), -2048, 2047));
                }
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    double a = 0;
                    for (int v = 0; v < 8; ++v)
                        for (int u = 0; u < 8; ++u) a += c[v][y] * c[u][x] * blk[8 * v + u];
                    ref[8 * y + x] = std::clamp(std::floor(a + 0.5), -256.0, 255.0);
                }
            b3native::mpeg_dsp::idct(blk);
            for (int i = 0; i < 64; ++i) {
                const int e = blk[i] - static_cast<int>(ref[i]);
                peak = std::max(peak, std::abs(e));
                sum_err += e;
                sum_sq += e * e;
            }
        }
        CHECK(peak <= 1);
        CHECK(std::fabs(sum_err) / (64.0 * kBlocks) <= 0.0015);
        CHECK(sum_sq / (64.0 * kBlocks) <= 0.02);
    }
    // all zero in, all zero out; a DC of 1024 is a flat 128
    std::int16_t z[64] = {};
    b3native::mpeg_dsp::idct(z);
    for (int i = 0; i < 64; ++i) CHECK_BITS(z[i], 0);
    std::int16_t d[64] = {};
    d[0] = 1024;
    b3native::mpeg_dsp::idct(d);
    for (int i = 0; i < 64; ++i) CHECK_BITS(d[i], 128);
}

// mac4: BDEC's pixels (RAW16 after the IDCT; before mac4 all zeros = a green movie): an intra macroblock, MPEG-2,
// DC only: Y0 dc +8 (size 4 "110" + "1000": 136), Y1 dc -8 ("110" + "0111": 128), Y2 / Y3 / Cb / Cr dc size 0
// (128, 128, 128, 128), each EOB "10". Frame DCT: the top-left quarter 136, the rest 128; field DCT (DT): block 0 is
// the left half's even lines. Values within 1 of the flat level (mismatch control sets F[7][7]'s low bit).
TEST(ipu_bdec_pixels) {
    for (int dt = 0; dt < 2; ++dt) {
        Bits s;
        s.put("110 1000 10").put("110 0111 10").put("100 10").put("100 10").put("00 10").put("00 10");
        s.put("1 0000000");
        s.pad_qwords(1);
        Rig r;
        r.reset();
        r.push(s);
        r.w32(0x1000B010u, 0x20000);   // fromIPU: the 48 qwords to 0x20000
        r.w32(0x1000B020u, 48);
        r.w32(0x1000B000u, 0x100);
        r.cmd(0x20000000u | (1u << 27) | (1u << 26) | (static_cast<std::uint32_t>(dt) << 25) | (5u << 16));
        CHECK(!(r.ctrl() & 0x4000u));   // no error
        CHECK(!(r.ctrl() >> 31));
        std::int16_t mb[384];
        std::memcpy(mb, r.ram.data() + 0x20000, sizeof mb);
        int bad = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const bool hi = dt == 0 ? (y < 8 && x < 8) : (x < 8 && (y & 1) == 0);
                if (std::abs(mb[16 * y + x] - (hi ? 136 : 128)) > 1) ++bad;
            }
        for (int i = 256; i < 384; ++i) if (std::abs(mb[i] - 128) > 1) ++bad;
        CHECK_BITS(bad, 0);
        CHECK_BITS(mb[0], 136);
        CHECK_BITS(mb[15], 128);
        CHECK_BITS(mb[300], 128);
    }
}

// The dev model's toSPR chain mode (retail libmpeg's dmaRefImage; hw_model_dev.h): the chain's payload to the
// scratchpad from SADR on, SADR advanced, the transfer complete. The product model refuses it (test_native_platform).
TEST(dev_to_spr_chain) {
    Rig r;
    std::vector<std::uint8_t> spr(0x4000, 0);
    r.m.scratchpad = [&spr] { return spr.data(); };
    auto put = [&](std::uint32_t a, std::uint32_t v) { std::memcpy(&r.ram[a], &v, 4); };
    put(0x8000, 0x10000001u);                                     // cnt, 1 qword
    for (unsigned k = 0; k < 4; ++k) put(0x8010 + 4 * k, 0x11111111u * (k + 1));
    put(0x8020, 0x70000001u);                                     // end, 1 qword
    for (unsigned k = 0; k < 4; ++k) put(0x8030 + 4 * k, 0xA0000000u + k);
    r.w32(0x1000D420u, 0);
    r.w32(0x1000D430u, 0x8000);
    r.w32(0x1000D480u, 0x100);
    r.w32(0x1000D400u, 0x105);
    CHECK(std::memcmp(&spr[0x100], &r.ram[0x8010], 16) == 0);
    CHECK(std::memcmp(&spr[0x110], &r.ram[0x8030], 16) == 0);
    CHECK_BITS(r.r32(0x1000D480u), 0x120);
    CHECK(!(r.r32(0x1000D400u) & 0x100u));
    CHECK(r.m.d_stat() & (1u << 9));
}

TEST_MAIN("test_ipu_model")
