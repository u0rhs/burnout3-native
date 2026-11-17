#include "b3asset/vu1.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "b3host/ps2float.h"

namespace b3asset {

namespace {
namespace fr = b3fp::ref;
constexpr std::uint32_t kOne = 0x3F800000u;
bool fail(std::string* why, const std::string& what) { if (why) *why = what; return false; }
std::uint32_t le32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
int s11(std::uint32_t w) { int v = int(w & 0x7FF); return (v & 0x400) ? v - 0x800 : v; }
// MAC flag bits for one lane: Z (bit 3 - lane), S (bit 7 - lane). Overflow/underflow are not modelled (no game
// program reads them; results already clamp/flush in b3fp::ref).
std::uint32_t lane_mac(std::uint32_t v, int lane) {
    std::uint32_t m = 0;
    if ((v & 0x7FFFFFFFu) == 0) m |= 1u << (3 - lane);
    if (v & 0x80000000u) m |= 1u << (7 - lane);
    return m;
}
std::uint32_t mac_to_status(std::uint32_t mac) {
    std::uint32_t s = 0;
    if (mac & 0x000F) s |= 1;
    if (mac & 0x00F0) s |= 2;
    if (mac & 0x0F00) s |= 4;
    if (mac & 0xF000) s |= 8;
    return s;
}
std::uint32_t ftoi(std::uint32_t b, int frac) {
    const double v = double(b3fp::flt(fr::clamp(b))) * double(1 << frac);
    if (v >= 2147483647.0) return 0x7FFFFFFFu;
    if (v <= -2147483648.0) return 0x80000000u;
    return std::uint32_t(std::int32_t(std::trunc(v)));
}
std::uint32_t itof(std::uint32_t b, int frac) {
    return fr::chop(double(std::int32_t(b)) / double(1 << frac));
}
}  // namespace

bool VuMicroprogram::load_textbin(const std::vector<std::uint8_t>& d, std::string* why) {
    loaded = 0;
    std::size_t o = 0;
    while (o + 16 <= d.size()) {
        const std::uint32_t tag = le32(&d[o]);
        const std::size_t end = std::min(d.size(), o + 16 + std::size_t(tag & 0xFFFF) * 16);
        const int id = (tag >> 28) & 7;
        std::size_t v = o + 8;
        while (v + 4 <= end) {
            const std::uint32_t code = le32(&d[v]);
            v += 4;
            const std::uint32_t cmd = (code >> 24) & 0x7F;
            if (cmd == 0x4A) {  // MPG
                const std::uint32_t num = ((code >> 16) & 0xFF) ? ((code >> 16) & 0xFF) : 256, addr = code & 0xFFFF;
                v = (v + 7) & ~std::size_t(7);
                if (v + 8 * num > d.size()) return fail(why, "MPG past the end");
                for (std::uint32_t k = 0; k < num; ++k) {
                    const std::uint32_t a = (addr + k) & 2047;
                    lower[a] = le32(&d[v + 8 * k]);
                    upper[a] = le32(&d[v + 8 * k + 4]);
                }
                loaded += num;
                v += 8 * num;
            } else if (cmd == 0 || cmd == 0x10 || cmd == 0x11 || cmd == 0x13 || (cmd >= 1 && cmd <= 7)) {
                // NOP, FLUSHE, FLUSH, FLUSHA, STCYCL..ITOP: no effect on the image
            } else {
                return fail(why, "unexpected VIF code in a microprogram image");
            }
        }
        o = o + 16 + std::size_t(tag & 0xFFFF) * 16;
        if (id == 6 || id == 7) break;
    }
    return loaded ? true : fail(why, "no MPG in image");
}

bool read_gif_packet(const VuMemory& mem, std::uint32_t addr, GifPacket& out, std::string* why) {
    GifVertex cur;
    std::uint32_t qa = addr & 1023;
    for (int guard = 0; guard < 1024; ++guard) {
        const std::uint32_t* t = mem.qw(qa);
        const std::uint64_t lo = std::uint64_t(t[0]) | std::uint64_t(t[1]) << 32;
        const std::uint64_t regs = std::uint64_t(t[2]) | std::uint64_t(t[3]) << 32;
        const std::uint32_t nloop = std::uint32_t(lo & 0x7FFF);
        const bool eop = (lo >> 15) & 1, pre = (lo >> 46) & 1;
        const int flg = int(lo >> 58) & 3;
        int nreg = int(lo >> 60) & 0xF;
        if (!nreg) nreg = 16;
        qa = (qa + 1) & 1023;
        ++out.qwords;
        if (pre) cur.prim = std::uint32_t(lo >> 47) & 0x7FF;
        if (flg == 2) {  // IMAGE
            qa = (qa + nloop) & 1023;
            out.qwords += nloop;
        } else {
            for (std::uint32_t l = 0; l < nloop; ++l) {
                for (int r = 0; r < nreg; ++r) {
                    const int reg = int(regs >> (4 * r)) & 0xF;
                    const std::uint32_t* d;
                    std::uint64_t d64;
                    if (flg == 0) {  // PACKED: one qword per register
                        d = mem.qw(qa);
                        qa = (qa + 1) & 1023;
                        ++out.qwords;
                    } else {
                        return fail(why, "REGLIST GIF tag (not used by the track programs)");
                    }
                    d64 = std::uint64_t(d[0]) | std::uint64_t(d[1]) << 32;
                    float f0, f1, f2;
                    std::memcpy(&f0, &d[0], 4);
                    std::memcpy(&f1, &d[1], 4);
                    std::memcpy(&f2, &d[2], 4);
                    switch (reg) {
                    case 0x0: cur.prim = d[0] & 0x7FF; break;
                    case 0x1: cur.r = d[0] & 0xFF; cur.g = d[1] & 0xFF; cur.b = d[2] & 0xFF; cur.a = d[3] & 0xFF; break;
                    case 0x2: cur.s = f0; cur.t = f1; cur.q = f2; break;  // PACKED ST writes Q too
                    case 0x3: cur.u = d[0] & 0x3FFF; cur.v = d[1] & 0x3FFF; break;
                    case 0x4:
                    case 0xC:  // XYZF2 / XYZF3
                        cur.x = d[0] & 0xFFFF; cur.y = d[1] & 0xFFFF; cur.z = (d[2] >> 4) & 0xFFFFFF;
                        cur.f = (d[3] >> 4) & 0xFF;
                        cur.adc = reg == 0xC || ((d[3] >> 15) & 1);
                        out.verts.push_back(cur);
                        break;
                    case 0x5:
                    case 0xD:  // XYZ2 / XYZ3
                        cur.x = d[0] & 0xFFFF; cur.y = d[1] & 0xFFFF; cur.z = d[2];
                        cur.adc = reg == 0xD || ((d[3] >> 15) & 1);
                        out.verts.push_back(cur);
                        break;
                    case 0xA: cur.f = (d[3] >> 4) & 0xFF; break;
                    case 0xE: out.ad.emplace_back(std::uint8_t(d[2] & 0xFF), d64); break;
                    case 0xF: break;
                    default: out.ad.emplace_back(std::uint8_t(0x80 | reg), d64); break;
                    }
                }
            }
        }
        if (eop) return true;
    }
    return fail(why, "GIF packet without EOP");
}

void Vu1::reset() {
    std::memset(vf, 0, sizeof vf);
    std::memset(vi, 0, sizeof vi);
    std::memset(acc, 0, sizeof acc);
    vf[0][3] = kOne;
    I = Q = P = 0;
    R = 0x3F800000u;
    mac = status = clip = 0;
    cycle_ = 0;
    for (auto& r : vf_ready_) for (auto& l : r) l = 0;
    pflags_.clear();
    pclip_.clear();
    q_pending_ = p_pending_ = false;
    q_ready_ = p_ready_ = fdiv_busy_ = efu_busy_ = 0;
    vi_hist_.clear();
    tpc = 0;
}

void Vu1::commit(std::uint64_t now) {
    std::size_t k = 0;
    for (; k < pflags_.size() && pflags_[k].ready <= now; ++k) {
        mac = pflags_[k].mac;
        status = (status & ~0xFu) | pflags_[k].status | (pflags_[k].status << 6);
        ++pflag_commits_;
    }
    pflags_.erase(pflags_.begin(), pflags_.begin() + std::ptrdiff_t(k));
    for (k = 0; k < pclip_.size() && pclip_[k].ready <= now; ++k) clip = ((clip << 6) | pclip_[k].bits) & 0xFFFFFF;
    pclip_.erase(pclip_.begin(), pclip_.begin() + std::ptrdiff_t(k));
    if (q_pending_ && q_ready_ <= now) commit_q();
    if (p_pending_ && p_ready_ <= now) { P = p_next_; p_pending_ = false; }
}

void Vu1::commit_q() {
    Q = q_next_;
    status = (status & ~0x30u) | q_status_ | (q_status_ << 6);
    q_pending_ = false;
}

void Vu1::settle() {
    commit(~std::uint64_t(0));
    for (auto& r : vf_ready_) for (auto& l : r) l = 0;
    fdiv_busy_ = efu_busy_ = 0;
    vi_hist_.clear();
}

bool Vu1::run(const VuMicroprogram& prog, std::uint32_t pc, std::uint32_t top, std::uint32_t itop, const KickFn& on_kick,
              std::string* why, std::uint64_t max_cycles) {
    const std::uint64_t start = cycle_;
    bool ending = false;          // E bit seen: one more pair, then stop
    int branch_pending = 0;       // 1 = the next pair is a delay slot, jump after it
    std::uint32_t branch_target = 0;
    const std::uint64_t flag_base = pflag_commits_;
    pc &= pc_mask;
    for (;;) {
        if (cycle_ - start > max_cycles) return fail(why, "cycle limit");
        const std::uint32_t up = prog.upper[pc], lo = prog.lower[pc];
        const bool in_delay = branch_pending == 1;
        const bool ibit = (up >> 31) & 1, ebit = (up >> 30) & 1;
        const int udest = int(up >> 21) & 0xF;  // bit 3 = x
        auto has = [](int dest, int lane) { return (dest >> (3 - lane)) & 1; };
        const int uft = int(up >> 16) & 31, ufs = int(up >> 11) & 31, ufd = int(up >> 6) & 31;
        const int uop = int(up & 0x3F);
        const int uop2 = ((int(up >> 6) & 0x1F) << 2) | int(up & 3);

        // ---- decode the lower op enough to know its reads/waits
        const bool ltype1 = !ibit && (lo >> 25) == 0x40;
        const int lop = ltype1 ? int(lo & 0x3F) : int(lo >> 25);
        const int lop2 = ((int(lo >> 6) & 0x1F) << 2) | int(lo & 3);
        const int ldest = int(lo >> 21) & 0xF;
        const int lit = int(lo >> 16) & 31, lis = int(lo >> 11) & 31, lid = int(lo >> 6) & 31;
        const int lfsf = int(lo >> 21) & 3, lftf = int(lo >> 23) & 3;

        // ---- stalls: VF lanes read by either slot, WAITQ/WAITP, busy FDIV/EFU
        std::uint64_t need = cycle_;
        auto read_lanes = [&](int reg, int mask) {
            if (reg == 0) return;
            for (int l = 0; l < 4; ++l)
                if ((mask >> (3 - l)) & 1) need = std::max(need, vf_ready_[reg][l]);
        };
        if (uop < 0x3C) {
            if (uop < 0x1C) { read_lanes(ufs, udest); read_lanes(uft, 8 >> (uop & 3)); }
            else if (uop == 0x2E) { read_lanes(ufs, 0xE); read_lanes(uft, 0xE); }
            else {
                read_lanes(ufs, udest);
                if (uop >= 0x28) read_lanes(uft, udest);
            }
        } else if (uop2 != 0x2F) {
            if (uop2 < 0x10 || (uop2 >= 0x18 && uop2 <= 0x1B)) { read_lanes(ufs, udest); read_lanes(uft, 8 >> (uop2 & 3)); }
            else if (uop2 == 0x1F) { read_lanes(ufs, 0xE); read_lanes(uft, 0x1); }
            else if (uop2 == 0x2E) { read_lanes(ufs, 0xE); read_lanes(uft, 0xE); }
            else {
                read_lanes(ufs, udest);
                if (uop2 >= 0x28) read_lanes(uft, udest);
            }
        }
        if (!ibit) {
            if (ltype1 && lop >= 0x3C) {
                switch (lop2) {
                case 0x30: case 0x31: read_lanes(lis, 0xF); break;               // MOVE, MR32
                case 0x35: case 0x37: read_lanes(lis, ldest); break;             // SQI, SQD
                case 0x38: read_lanes(lis, 8 >> lfsf); read_lanes(lit, 8 >> lftf);
                           need = std::max(need, fdiv_busy_); break;              // DIV
                case 0x39: read_lanes(lit, 8 >> lftf); need = std::max(need, fdiv_busy_); break;
                case 0x3A: read_lanes(lis, 8 >> lfsf); read_lanes(lit, 8 >> lftf); need = std::max(need, fdiv_busy_); break;
                case 0x3B: if (q_pending_) need = std::max(need, q_ready_); break; // WAITQ
                case 0x3C: read_lanes(lis, 8 >> lfsf); break;                     // MTIR
                case 0x7B: if (p_pending_) need = std::max(need, p_ready_); break; // WAITP
                default:
                    if (lop2 >= 0x70 && lop2 <= 0x7E) { read_lanes(lis, 0xF); need = std::max(need, efu_busy_); }
                    break;
                }
            } else if (!ltype1 && lop == 0x01) {
                read_lanes(lis, ldest);  // SQ
            }
        }
        if (pair_timing) need = cycle_;  // VU0 pair timing: no stalls
        const bool stalled = need > cycle_;
        if (stalled) {
            stats.stall_cycles += need - cycle_;
            cycle_ = need;
        }
        commit(cycle_);
        const std::uint64_t now = cycle_;
        ++stats.pairs;

        // ---- upper slot (computed against the pre-pair state)
        std::uint32_t ures[4];
        int uwrite = -1;             // VF index written, 32 = ACC
        bool uflags = false;
        std::uint32_t uclip = 0;
        bool uclip_valid = false;
        auto qread = [&]() { if (q_pending_) ++stats.q_read_pending; return Q; };
        auto bcv = [&](int reg, int bc) { return vf[reg][bc]; };
        auto fmac = [&](int kind, int lane, std::uint32_t a, std::uint32_t b) -> std::uint32_t {
            switch (kind) {
            case 0: return fr::add(a, b);
            case 1: return fr::sub(a, b);
            case 2: return fr::madd(acc[lane], a, b);
            case 3: return fr::msub(acc[lane], a, b);
            // MAX/MINI compare the raw sign-magnitude encodings: the game's programs pass integer words through them
            // (e.g. 0x46F0D0's alternate 0x4745C0 `maxx.w` on the ADC flag word, 0x470650 `maxz.w` on an integer
            // alpha), so no denormal flush here.
            case 4: return fr::max(a, b);
            case 5: return fr::min(a, b);
            default: return fr::mul(a, b);
            }
        };
        if (uop < 0x3C) {
            int kind = -1;
            bool to_acc = false;
            std::uint32_t scalar = 0;
            int bc = -1;
            bool use_scalar = false;
            if (uop < 0x1C) { kind = uop >> 2; bc = uop & 3; }
            else {
                switch (uop) {
                case 0x1C: kind = 6; scalar = qread(); use_scalar = true; break;  // MULq
                case 0x1D: kind = 4; scalar = I; use_scalar = true; break;        // MAXi
                case 0x1E: kind = 6; scalar = I; use_scalar = true; break;        // MULi
                case 0x1F: kind = 5; scalar = I; use_scalar = true; break;        // MINIi
                case 0x20: kind = 0; scalar = qread(); use_scalar = true; break;
                case 0x21: kind = 2; scalar = qread(); use_scalar = true; break;
                case 0x22: kind = 0; scalar = I; use_scalar = true; break;
                case 0x23: kind = 2; scalar = I; use_scalar = true; break;
                case 0x24: kind = 1; scalar = qread(); use_scalar = true; break;
                case 0x25: kind = 3; scalar = qread(); use_scalar = true; break;
                case 0x26: kind = 1; scalar = I; use_scalar = true; break;
                case 0x27: kind = 3; scalar = I; use_scalar = true; break;
                case 0x28: kind = 0; break;
                case 0x29: kind = 2; break;
                case 0x2A: kind = 6; break;
                case 0x2B: kind = 4; break;
                case 0x2C: kind = 1; break;
                case 0x2D: kind = 3; break;
                case 0x2F: kind = 5; break;
                case 0x2E: {  // OPMSUB fd.xyz = ACC - fs.yzx * ft.zxy
                    for (int l = 0; l < 4; ++l) ures[l] = vf[ufd][l];
                    const int a1[3] = {1, 2, 0}, a2[3] = {2, 0, 1};
                    for (int l = 0; l < 3; ++l)
                        if (has(udest, l)) ures[l] = fr::msub(acc[l], vf[ufs][a1[l]], vf[uft][a2[l]]);
                    uwrite = ufd;
                    uflags = true;
                    break;
                }
                default: return fail(why, "unknown upper op");
                }
            }
            if (kind >= 0 && uop != 0x2E) {
                for (int l = 0; l < 4; ++l) {
                    const std::uint32_t b = use_scalar ? scalar : (bc >= 0 ? bcv(uft, bc) : vf[uft][l]);
                    ures[l] = has(udest, l) ? fmac(kind, l, vf[ufs][l], b) : (to_acc ? acc[l] : vf[ufd][l]);
                }
                uwrite = ufd;
                uflags = kind != 4 && kind != 5;
            }
        } else if (uop2 != 0x2F) {
            int kind = -1;
            std::uint32_t scalar = 0;
            int bc = -1;
            bool use_scalar = false;
            if (uop2 < 0x10) { kind = uop2 >> 2; bc = uop2 & 3; }
            else if (uop2 >= 0x18 && uop2 <= 0x1B) { kind = 6; bc = uop2 & 3; }
            else {
                switch (uop2) {
                case 0x10: case 0x11: case 0x12: case 0x13: {
                    static const int fb[4] = {0, 4, 12, 15};
                    for (int l = 0; l < 4; ++l) ures[l] = has(udest, l) ? itof(vf[ufs][l], fb[uop2 & 3]) : vf[uft][l];
                    uwrite = uft;
                    break;
                }
                case 0x14: case 0x15: case 0x16: case 0x17: {
                    static const int fb[4] = {0, 4, 12, 15};
                    for (int l = 0; l < 4; ++l) ures[l] = has(udest, l) ? ftoi(vf[ufs][l], fb[uop2 & 3]) : vf[uft][l];
                    uwrite = uft;
                    break;
                }
                case 0x1D:
                    for (int l = 0; l < 4; ++l) ures[l] = has(udest, l) ? (fr::clamp(vf[ufs][l]) & 0x7FFFFFFFu) : vf[uft][l];
                    uwrite = uft;
                    break;
                case 0x1F: {  // CLIP
                    const std::uint32_t wv = fr::clamp(vf[uft][3]) & 0x7FFFFFFFu;
                    const float aw = b3fp::flt(wv);
                    std::uint32_t bits = 0;
                    for (int l = 0; l < 3; ++l) {
                        const float x = b3fp::flt(fr::clamp(vf[ufs][l]));
                        if (x > aw) bits |= 1u << (2 * l);
                        if (x < -aw) bits |= 2u << (2 * l);
                    }
                    uclip = bits;
                    uclip_valid = true;
                    break;
                }
                case 0x1C: kind = 6; scalar = qread(); use_scalar = true; break;
                case 0x1E: kind = 6; scalar = I; use_scalar = true; break;
                case 0x20: kind = 0; scalar = qread(); use_scalar = true; break;
                case 0x21: kind = 2; scalar = qread(); use_scalar = true; break;
                case 0x22: kind = 0; scalar = I; use_scalar = true; break;
                case 0x23: kind = 2; scalar = I; use_scalar = true; break;
                case 0x24: kind = 1; scalar = qread(); use_scalar = true; break;
                case 0x25: kind = 3; scalar = qread(); use_scalar = true; break;
                case 0x26: kind = 1; scalar = I; use_scalar = true; break;
                case 0x27: kind = 3; scalar = I; use_scalar = true; break;
                case 0x28: kind = 0; break;
                case 0x29: kind = 2; break;
                case 0x2A: kind = 6; break;
                case 0x2C: kind = 1; break;
                case 0x2D: kind = 3; break;
                case 0x2E: {  // OPMULA ACC.xyz = fs.yzx * ft.zxy
                    for (int l = 0; l < 4; ++l) ures[l] = acc[l];
                    const int a1[3] = {1, 2, 0}, a2[3] = {2, 0, 1};
                    for (int l = 0; l < 3; ++l)
                        if (has(udest, l)) ures[l] = fr::mul(vf[ufs][a1[l]], vf[uft][a2[l]]);
                    uwrite = 32;
                    uflags = true;
                    break;
                }
                default: return fail(why, "unknown upper op2");
                }
            }
            if (kind >= 0) {
                for (int l = 0; l < 4; ++l) {
                    const std::uint32_t b = use_scalar ? scalar : (bc >= 0 ? bcv(uft, bc) : vf[uft][l]);
                    ures[l] = has(udest, l) ? fmac(kind, l, vf[ufs][l], b) : acc[l];
                }
                uwrite = 32;
                uflags = true;
            }
        }

        // ---- lower slot
        std::uint32_t lres[4];
        int lwrite = -1;   // VF written by the lower op
        int lvi = -1;      // VI written
        std::uint16_t lvi_val = 0;
        bool lvi_eligible = true;
        int lvi2 = -1;     // VI post-increment/pre-decrement
        std::uint16_t lvi2_val = 0;
        bool xgkick = false;
        std::uint32_t xg_addr = 0;
        bool do_branch = false;
        std::uint32_t target = 0;
        auto vis = [&](int r) -> std::uint16_t { return vi[r & 15]; };
        auto branch_vi = [&](int r) -> std::uint16_t {
            r &= 15;
            std::uint16_t v = vi[r];
            if (r == 0 || stalled || in_delay || vi_hist_.empty()) return v;
            const ViWrite& n = vi_hist_.back();
            if (n.cycle + 1 != now || n.reg != r || !n.eligible) return v;
            for (auto it = vi_hist_.rbegin(); it != vi_hist_.rend(); ++it) {
                if (now - it->cycle > 4) break;
                if (it->reg == r) {
                    if (!it->eligible) break;
                    v = it->before;
                }
            }
            if (v != vi[r]) ++stats.branch_vi_backup;
            return v;
        };
        auto mem_qw = [&](std::uint32_t a) { return mem_.qw(a & qw_mask); };
        auto flag_read = [&]() { if (pflag_commits_ == flag_base) ++stats.flag_reads_prelaunch; };
        if (ibit) {
            I = lo;
        } else if (ltype1) {
            if (lop < 0x3C) {
                switch (lop) {
                case 0x30: lvi = lid; lvi_val = std::uint16_t(vis(lis) + vis(lit)); break;
                case 0x31: lvi = lid; lvi_val = std::uint16_t(vis(lis) - vis(lit)); break;
                case 0x32: { int imm = int(lo >> 6) & 31; if (imm & 16) imm -= 32;
                             lvi = lit; lvi_val = std::uint16_t(vis(lis) + imm); break; }
                case 0x34: lvi = lid; lvi_val = std::uint16_t(vis(lis) & vis(lit)); break;
                case 0x35: lvi = lid; lvi_val = std::uint16_t(vis(lis) | vis(lit)); break;
                default: return fail(why, "unknown lower op1");
                }
            } else {
                switch (lop2) {
                case 0x30:  // MOVE
                    for (int l = 0; l < 4; ++l) lres[l] = has(ldest, l) ? vf[lis][l] : vf[lit][l];
                    lwrite = lit; break;
                case 0x31:  // MR32
                    for (int l = 0; l < 4; ++l) lres[l] = has(ldest, l) ? vf[lis][(l + 1) & 3] : vf[lit][l];
                    lwrite = lit; break;
                case 0x34: { const std::uint32_t* m = mem_qw(vis(lis));  // LQI
                    for (int l = 0; l < 4; ++l) lres[l] = has(ldest, l) ? m[l] : vf[lit][l];
                    lwrite = lit; lvi2 = lis; lvi2_val = std::uint16_t(vis(lis) + 1); break; }
                case 0x36: { const std::uint16_t a = std::uint16_t(vis(lis) - 1);  // LQD
                    const std::uint32_t* m = mem_qw(a);
                    for (int l = 0; l < 4; ++l) lres[l] = has(ldest, l) ? m[l] : vf[lit][l];
                    lwrite = lit; lvi2 = lis; lvi2_val = a; break; }
                case 0x35: {  // SQI
                    for (int l = 0; l < 4; ++l) if (has(ldest, l)) w(vis(lit), l) = vf[lis][l];
                    lvi2 = lit; lvi2_val = std::uint16_t(vis(lit) + 1); break; }
                case 0x37: { const std::uint16_t a = std::uint16_t(vis(lit) - 1);  // SQD
                    for (int l = 0; l < 4; ++l) if (has(ldest, l)) w(a, l) = vf[lis][l];
                    lvi2 = lit; lvi2_val = a; break; }
                case 0x38: case 0x39: case 0x3A: {  // DIV, SQRT, RSQRT
                    const std::uint32_t n = vf[lis][lfsf], d = vf[lit][lftf];
                    std::uint32_t st = 0;
                    if (pair_timing && q_pending_) commit_q();  // the FDIV unit's previous result reaches Q first
                    if (lop2 == 0x38) {
                        q_next_ = fr::vu_div(n, d);
                        if ((fr::clamp(d) & 0x7FFFFFFF) == 0) st = (fr::clamp(n) & 0x7FFFFFFF) ? 0x20 : 0x10;
                    } else if (lop2 == 0x39) {
                        q_next_ = fr::vu_sqrt(d);
                        if (d & 0x80000000u && (d & 0x7FFFFFFF)) st = 0x10;
                    } else {
                        q_next_ = fr::vu_rsqrt(n, d);
                        if ((fr::clamp(d) & 0x7FFFFFFF) == 0) st = 0x20;
                        else if (d & 0x80000000u) st = 0x10;
                    }
                    if (q_pending_) { Q = Q; }  // the busy check above already waited for the previous one
                    q_status_ = st;
                    q_pending_ = true;
                    q_ready_ = now + (lop2 == 0x3A ? 13 : 7);
                    fdiv_busy_ = q_ready_;
                    break;
                }
                case 0x3B: if (pair_timing && q_pending_) commit_q(); break;  // WAITQ (hw: the stall above)
                case 0x3C: lvi = lit; lvi_val = std::uint16_t(vf[lis][lfsf]); break;  // MTIR
                case 0x3D:  // MFIR
                    for (int l = 0; l < 4; ++l)
                        lres[l] = has(ldest, l) ? std::uint32_t(std::int32_t(std::int16_t(vis(lis)))) : vf[lit][l];
                    lwrite = lit; break;
                case 0x3E: {  // ILWR
                    const std::uint32_t* m = mem_qw(vis(lis));
                    for (int l = 0; l < 4; ++l) if (has(ldest, l)) { lvi = lit; lvi_val = std::uint16_t(m[l]); }
                    break; }
                case 0x3F:  // ISWR
                    for (int l = 0; l < 4; ++l) if (has(ldest, l)) w(vis(lis), l) = vis(lit);
                    break;
                case 0x64:  // MFP
                    if (p_pending_) ++stats.p_read_pending;
                    for (int l = 0; l < 4; ++l) lres[l] = has(ldest, l) ? P : vf[lit][l];
                    lwrite = lit; break;
                case 0x68: lvi = lit; lvi_val = std::uint16_t(top & 0x3FF); break;   // XTOP
                case 0x69: lvi = lit; lvi_val = std::uint16_t(itop & 0x3FF); break;  // XITOP
                case 0x6C: xgkick = true; xg_addr = vis(lis); break;
                case 0x7B: break;  // WAITP
                case 0x78: case 0x79: case 0x7A: {  // ESQRT, ERSQRT, ERCPR
                    const std::uint32_t x = vf[lis][lfsf];
                    if (lop2 == 0x78) { p_next_ = fr::vu_sqrt(x); p_ready_ = now + 12; }
                    else if (lop2 == 0x79) { p_next_ = fr::vu_rsqrt(kOne, x); p_ready_ = now + 18; }
                    else { p_next_ = fr::vu_div(kOne, x); p_ready_ = now + 12; }
                    p_pending_ = true;
                    efu_busy_ = p_ready_;
                    break;
                }
                default: return fail(why, "unknown lower op2 0x" + std::to_string(lop2));
                }
            }
        } else {
            switch (lop) {
            case 0x00: { const std::uint32_t* m = mem_qw(std::uint32_t(vis(lis) + s11(lo)));  // LQ
                for (int l = 0; l < 4; ++l) lres[l] = has(ldest, l) ? m[l] : vf[lit][l];
                lwrite = lit; break; }
            case 0x01: { const std::uint32_t a = std::uint32_t(vis(lit) + s11(lo));  // SQ
                for (int l = 0; l < 4; ++l) if (has(ldest, l)) w(a, l) = vf[lis][l];
                break; }
            case 0x04: { const std::uint32_t* m = mem_qw(std::uint32_t(vis(lis) + s11(lo)));  // ILW
                for (int l = 0; l < 4; ++l) if (has(ldest, l)) { lvi = lit; lvi_val = std::uint16_t(m[l]); }
                break; }
            case 0x05: { const std::uint32_t a = std::uint32_t(vis(lis) + s11(lo));  // ISW
                for (int l = 0; l < 4; ++l) if (has(ldest, l)) w(a, l) = vis(lit);
                break; }
            case 0x08: case 0x09: {  // IADDIU, ISUBIU
                const std::uint32_t imm = ((lo >> 10) & 0x7800) | (lo & 0x7FF);
                lvi = lit; lvi_val = std::uint16_t(lop == 0x08 ? vis(lis) + imm : vis(lis) - imm); break; }
            case 0x10: lvi = 1; lvi_val = clip == (lo & 0xFFFFFF); lvi_eligible = false; break;                  // FCEQ
            case 0x11: clip = lo & 0xFFFFFF; pclip_.clear(); break;                                            // FCSET
            case 0x12: lvi = 1; lvi_val = (clip & lo & 0xFFFFFF) != 0; lvi_eligible = false; break;             // FCAND
            case 0x13: lvi = 1; lvi_val = ((clip | (lo & 0xFFFFFF)) & 0xFFFFFF) == 0xFFFFFF; lvi_eligible = false; break;  // FCOR
            case 0x14: case 0x15: case 0x16: case 0x17: {
                const std::uint32_t imm = ((lo >> 10) & 0x800) | (lo & 0x7FF);
                if (lop == 0x15) { status = (status & 0x3F) | (imm & 0xFC0); break; }
                flag_read();
                lvi = lit; lvi_eligible = false;
                lvi_val = lop == 0x14 ? std::uint16_t(status == imm) : lop == 0x16 ? std::uint16_t(status & imm)
                                                                                     : std::uint16_t(status | imm);
                break; }
            case 0x18: flag_read(); lvi = lit; lvi_val = mac == vis(lis); lvi_eligible = false; break;                        // FMEQ
            case 0x1A: flag_read(); lvi = lit; lvi_val = std::uint16_t(mac & vis(lis)); lvi_eligible = false; break;          // FMAND
            case 0x1B: flag_read(); lvi = lit; lvi_val = std::uint16_t(mac | vis(lis)); lvi_eligible = false; break;          // FMOR
            case 0x1C: lvi = lit; lvi_val = std::uint16_t(clip & 0xFFF); lvi_eligible = false; break;            // FCGET
            case 0x20: do_branch = true; target = (pc + 1 + s11(lo)) & pc_mask; break;                            // B
            case 0x21: do_branch = true; target = (pc + 1 + s11(lo)) & pc_mask; lvi = lit; lvi_val = std::uint16_t(pc + 2); break;
            case 0x24: do_branch = true; target = vis(lis) & pc_mask; break;                                       // JR
            case 0x25: do_branch = true; target = vis(lis) & pc_mask; lvi = lit; lvi_val = std::uint16_t(pc + 2); break;
            case 0x28: case 0x29: {
                const bool eq = branch_vi(lit) == branch_vi(lis);
                if (eq == (lop == 0x28)) { do_branch = true; target = (pc + 1 + s11(lo)) & pc_mask; }
                break; }
            case 0x2C: case 0x2D: case 0x2E: case 0x2F: {
                const std::int16_t v = std::int16_t(branch_vi(lis));
                const bool c = lop == 0x2C ? v < 0 : lop == 0x2D ? v > 0 : lop == 0x2E ? v <= 0 : v >= 0;
                if (c) { do_branch = true; target = (pc + 1 + s11(lo)) & pc_mask; }
                break; }
            default: return fail(why, "unknown lower op0 0x" + std::to_string(lop));
            }
        }

        // ---- write back: lower, then upper (upper wins a same-register conflict)
        if (lwrite > 0) {
            for (int l = 0; l < 4; ++l) if (has(ldest, l)) { vf[lwrite][l] = lres[l]; vf_ready_[lwrite][l] = now + 4; }
        }
        auto write_vi = [&](int r, std::uint16_t v, bool eligible) {
            r &= 15;
            if (r == 0) return;
            vi_hist_.push_back({now, std::uint8_t(r), vi[r], eligible});
            if (vi_hist_.size() > 8) vi_hist_.erase(vi_hist_.begin());
            vi[r] = v;
        };
        if (lvi2 >= 0) write_vi(lvi2, lvi2_val, true);
        if (lvi >= 0) write_vi(lvi, lvi_val, lvi_eligible);
        if (uwrite == 32) {
            for (int l = 0; l < 4; ++l) acc[l] = ures[l];
        } else if (uwrite > 0) {
            int dmask = udest;
            for (int l = 0; l < 4; ++l) if (has(dmask, l)) { vf[uwrite][l] = ures[l]; vf_ready_[uwrite][l] = now + 4; }
        }
        if (uflags) {
            std::uint32_t m = 0;
            for (int l = 0; l < 4; ++l) if (has(udest, l)) m |= lane_mac(ures[l], l);
            pflags_.push_back({now + 4, m, mac_to_status(m)});
        }
        if (uclip_valid) pclip_.push_back({now + 4, uclip});
        if (xgkick) {
            ++stats.xgkicks;
            if (on_kick) on_kick(xg_addr & 1023);
        }

        ++cycle_;
        ++stats.cycles;
        const std::uint32_t next = (pc + 1) & pc_mask;
        if (ending) { tpc = branch_pending == 1 ? branch_target : next; return true; }
        if (ebit) ending = true;
        if (branch_pending == 1) {
            branch_pending = 0;
            if (do_branch) ++stats.notes["branch in a delay slot"];
            pc = branch_target;
            continue;
        }
        if (do_branch) {
            branch_pending = 1;
            branch_target = target;
        }
        pc = next;
    }
}

}  // namespace b3asset
