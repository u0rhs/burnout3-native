#include "b3asset/vif.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace b3asset {

float VuMemory::f(std::uint32_t a, int lane) const {
    float v;
    std::memcpy(&v, &w[(a & 1023) * 4 + lane], 4);
    return v;
}

namespace {
const char* kFormat[16] = {"S-32", "S-16", "S-8", "S-?", "V2-32", "V2-16", "V2-8", "V2-?",
                           "V3-32", "V3-16", "V3-8", "V3-?", "V4-32", "V4-16", "V4-8", "V4-5"};
bool fail(std::string* why, const char* what) { if (why) *why = what; return false; }
}  // namespace

void VifInterpreter::flush(const KickFn& on_kick) {
    if (!dirty) return;
    dirty = false;
    ++stats.kicks;
    if (on_kick) on_kick(mem, itop, kEnd, 0);
    batch_unpacks.clear();
}

bool VifInterpreter::run(const std::uint8_t* p, std::size_t bytes, const KickFn& on_kick, std::string* why,
                         std::size_t phase) {
    std::size_t o = 0;
    auto word = [&](std::size_t at) { std::uint32_t v; std::memcpy(&v, p + at, 4); return v; };
    while (o + 4 <= bytes) {
        const std::uint32_t code = word(o);
        o += 4;
        const std::uint32_t cmd = (code >> 24) & 0x7F, imm = code & 0xFFFF, num = (code >> 16) & 0xFF;
        if (cmd >= 0x60) {  // UNPACK
            const int vn = (cmd >> 2) & 3, vl = cmd & 3;
            const bool masked = cmd & 0x10, usn = imm & 0x4000;
            const std::uint32_t addr = imm & 0x3FF;
            const std::uint32_t n = num ? num : 256;
            const int comps = vn + 1;
            const int bits = (vn == 3 && vl == 3) ? 16 : (32 >> vl);  // V4-5 packs a whole RGBA5551 in 16 bits
            const std::size_t elem_bits = (vn == 3 && vl == 3) ? 16 : std::size_t(comps) * bits;
            // Elements read from the stream: all n in skipping mode, CL of every WL in filling mode.
            std::size_t reads = n;
            if (wl > cl) reads = (n / wl) * cl + std::min<std::uint32_t>(n % wl, cl);
            const std::size_t data_bytes = ((reads * elem_bits + 7) / 8 + 3) & ~std::size_t(3);
            if (o + data_bytes > bytes) return fail(why, "UNPACK data past the end of the packet");
            std::string key = std::string(kFormat[cmd & 0xF]) + (masked ? " (mask)" : "");
            ++stats.unpacks[key];
            batch_unpacks[addr] = int(cmd & 0xF);
            std::size_t bitpos = 0;
            const std::uint8_t* d = p + o;
            auto take = [&](int nbits) -> std::uint32_t {
                std::uint32_t v = 0;
                for (int b = 0; b < nbits; b += 8) v |= std::uint32_t(d[(bitpos + b) / 8]) << b;
                bitpos += nbits;
                if (nbits == 32) return v;
                const std::uint32_t m = (1u << nbits) - 1;
                v &= m;
                if (!usn && (v >> (nbits - 1))) v |= ~m;  // sign extend
                return v;
            };
            for (std::uint32_t k = 0; k < n; ++k) {
                // Destination: skipping (CL >= WL) writes WL of every CL quadwords; filling writes every quadword.
                const std::uint32_t c = wl <= cl ? (k / wl) * cl + (k % wl) : k;
                const std::uint32_t pos = k % wl;  // cycle position (selects the mask row)
                const bool from_data = wl <= cl || pos < cl;
                std::uint32_t lanes[4] = {0, 0, 0, 0};
                if (from_data) {
                    if (vn == 3 && vl == 3) {
                        const std::uint32_t v = take(16);
                        lanes[0] = (v & 0x1F) << 3; lanes[1] = ((v >> 5) & 0x1F) << 3;
                        lanes[2] = ((v >> 10) & 0x1F) << 3; lanes[3] = (v >> 15) << 7;
                    } else {
                        for (int i = 0; i < comps; ++i) lanes[i] = take(bits);
                        if (vn == 0) lanes[1] = lanes[2] = lanes[3] = lanes[0];
                    }
                }
                std::uint32_t* q = &mem.w[((addr + c) & 1023) * 4];
                for (int i = 0; i < 4; ++i) {
                    // Without data (filling) lanes come from ROW/COL per mask; V2/V3 leave their missing lanes alone.
                    const bool present = from_data && (vn == 3 || i <= vn || vn == 0);
                    std::uint32_t m = masked ? (mask >> ((std::min<std::uint32_t>(pos, 3) * 4 + i) * 2)) & 3 : 0;
                    if (!from_data && !masked) m = 1;
                    if (m == 3) continue;
                    if (m == 1) { q[i] = row[i]; continue; }
                    if (m == 2) { q[i] = col[i]; continue; }
                    if (!present) continue;
                    std::uint32_t v = lanes[i];
                    if (mode == 1) v += row[i];
                    else if (mode == 2) { v += row[i]; row[i] = v; }
                    q[i] = v;
                }
            }
            o += data_bytes;
            dirty = true;
            continue;
        }
        switch (cmd) {
        case 0x00: case 0x02: case 0x03: case 0x06: case 0x07: case 0x10: case 0x11: case 0x13: break;
        case 0x01: cl = imm & 0xFF; wl = (imm >> 8) & 0xFF; if (!wl) wl = 256; if (!cl) cl = 256; break;
        case 0x04: itop = imm & 0x3FF; break;
        case 0x05: mode = imm & 3; break;
        case 0x14: case 0x15: case 0x17:
            ++stats.kicks;
            dirty = false;
            if (on_kick) on_kick(mem, itop, cmd == 0x14 ? kMscal : cmd == 0x15 ? kMscalf : kMscnt, imm);
            batch_unpacks.clear();
            break;
        case 0x20:
            if (o + 4 > bytes) return fail(why, "STMASK past the end");
            mask = word(o); o += 4; break;
        case 0x30: case 0x31:
            if (o + 16 > bytes) return fail(why, "STROW/STCOL past the end");
            for (int i = 0; i < 4; ++i) (cmd == 0x30 ? row : col)[i] = word(o + 4 * i);
            o += 16; break;
        case 0x4A: {
            const std::size_t words = (num ? num : 256) * 2;
            ++stats.mpg;
            if (on_mpg && o + words * 4 <= bytes) on_mpg(imm, p + o, static_cast<std::uint32_t>(words / 2));
            o += words * 4; break;
        }
        case 0x50: case 0x51: {
            // DIRECT data starts on a quadword boundary of the stream.
            o = ((o + phase + 15) & ~std::size_t(15)) - phase;
            const std::size_t qw = imm ? imm : 65536;
            stats.direct_qw += qw;
            if (on_direct && o + qw * 16 <= bytes) on_direct(p + o, qw);
            o += qw * 16; break;
        }
        default:
            ++stats.unknown;
            {
                char b[96];
                std::snprintf(b, sizeof b, "unknown VIF command %08X at byte %zu", code, o - 4);
                return fail(why, b);
            }
        }
    }
    return true;
}

}  // namespace b3asset
