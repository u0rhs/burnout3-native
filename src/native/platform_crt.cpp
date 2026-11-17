// The native backend's platform tier, C runtime part (src/native/platform.h): retail newlib / libgcc / MWCC runtime
// functions that game code reaches through dispatch (a call stub, a function pointer). They work on guest memory
// (b3host::load/store), so the bytes they write are the bytes retail writes. Each is checked against the retail
// function run in tests/r5900_mini.h on random inputs (tests/test_native_platform.cpp); returns as retail returns
// them (e.g. strcmp's byte difference, not only its sign).
#include "platform.h"

#include <algorithm>
#include <cstring>

#include "b3host/call.h"

#include <cstdint>

namespace b3native::platform {
namespace {

using b3host::gaddr;
using b3host::CallArgs;
using b3host::CallResult;

inline std::uint32_t a32(const CallArgs& a, unsigned i) { return a.gpr[i].w[0]; }
inline std::uint64_t a64(const CallArgs& a, unsigned i) { return b3host::detail::low64(a.gpr[i]); }
inline void ret32(CallResult& r, std::uint32_t v) { r.v0 = b3host::detail::gpr64(b3host::detail::sext32(v)); }
inline void ret64(CallResult& r, std::uint64_t v) { r.v0 = b3host::detail::gpr64(v); }
inline std::uint8_t u8(gaddr p) { return b3host::load<std::uint8_t>(p); }
inline void put8(gaddr p, std::uint8_t v) { b3host::store<std::uint8_t>(p, v); }

}  // namespace

// double fabs(double) [C decomp runtime fabs 0x11D630]: the double's 64-bit image in a0, its sign bit (bit 63) cleared, in v0;
// v1 = 0x7FFFFFFF as retail leaves it (the mask), a0 / a1 caller-saved
void fabs_(const CallArgs& a, CallResult& r) {
    const std::uint64_t x = std::uint64_t(a.gpr[0].w[0]) | std::uint64_t(a.gpr[0].w[1]) << 32;
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(x & 0x7FFFFFFFFFFFFFFFull);
    r.v1 = 0x7FFFFFFFull;
}
void memset_(const CallArgs& a, CallResult& r) {
    b3host::fill_guest(a32(a, 0), static_cast<std::uint8_t>(a32(a, 1)), a32(a, 2));
    ret32(r, a32(a, 0));
}
void memcpy_(const CallArgs& a, CallResult& r) {
    const gaddr d = a32(a, 0), s = a32(a, 1);
    const std::uint32_t n = a32(a, 2);
    for (std::uint32_t i = 0; i < n; ++i) put8(d + i, u8(s + i));   // forward, byte order as any overlap-free copy
    ret32(r, d);
}
void memmove_(const CallArgs& a, CallResult& r) {
    const gaddr d = a32(a, 0), s = a32(a, 1);
    const std::uint32_t n = a32(a, 2);
    if (d <= s || d >= s + n) for (std::uint32_t i = 0; i < n; ++i) put8(d + i, u8(s + i));
    else for (std::uint32_t i = n; i-- > 0;) put8(d + i, u8(s + i));
    ret32(r, d);
}
void memcmp_(const CallArgs& a, CallResult& r) {
    const gaddr x = a32(a, 0), y = a32(a, 1);
    const std::uint32_t n = a32(a, 2);
    for (std::uint32_t i = 0; i < n; ++i)
        if (u8(x + i) != u8(y + i)) { ret32(r, static_cast<std::uint32_t>(int(u8(x + i)) - int(u8(y + i)))); return; }
    ret32(r, 0);
}
void strlen_(const CallArgs& a, CallResult& r) {
    const gaddr s = a32(a, 0);
    std::uint32_t n = 0;
    while (u8(s + n)) ++n;
    ret32(r, n);
}
void strcpy_(const CallArgs& a, CallResult& r) {
    const gaddr d = a32(a, 0), s = a32(a, 1);
    std::uint32_t i = 0;
    for (;; ++i) { const std::uint8_t c = u8(s + i); put8(d + i, c); if (!c) break; }
    ret32(r, d);
}
void strncpy_(const CallArgs& a, CallResult& r) {
    const gaddr d = a32(a, 0), s = a32(a, 1);
    const std::uint32_t n = a32(a, 2);
    std::uint32_t i = 0;
    for (; i < n; ++i) { const std::uint8_t c = u8(s + i); put8(d + i, c); if (!c) { ++i; break; } }
    for (; i < n; ++i) put8(d + i, 0);
    ret32(r, d);
}
void strcat_(const CallArgs& a, CallResult& r) {
    const gaddr d = a32(a, 0), s = a32(a, 1);
    gaddr e = d;
    while (u8(e)) ++e;
    for (std::uint32_t i = 0;; ++i) { const std::uint8_t c = u8(s + i); put8(e + i, c); if (!c) break; }
    ret32(r, d);
}
void strncat_(const CallArgs& a, CallResult& r) {
    const gaddr d = a32(a, 0), s = a32(a, 1);
    const std::uint32_t n = a32(a, 2);
    gaddr e = d;
    while (u8(e)) ++e;
    std::uint32_t i = 0;
    for (; i < n; ++i) { const std::uint8_t c = u8(s + i); if (!c) break; put8(e + i, c); }
    put8(e + i, 0);
    ret32(r, d);
}
void strcmp_(const CallArgs& a, CallResult& r) {
    gaddr x = a32(a, 0), y = a32(a, 1);
    while (u8(x) && u8(x) == u8(y)) { ++x; ++y; }
    ret32(r, static_cast<std::uint32_t>(int(u8(x)) - int(u8(y))));
}
void strncmp_(const CallArgs& a, CallResult& r) {
    gaddr x = a32(a, 0), y = a32(a, 1);
    std::uint32_t n = a32(a, 2);
    if (n == 0) { ret32(r, 0); return; }
    while (--n && u8(x) && u8(x) == u8(y)) { ++x; ++y; }
    ret32(r, static_cast<std::uint32_t>(int(u8(x)) - int(u8(y))));
}
// [C decomp libc.s 0x126DF0] tolower through retail's ctype table at 0x4B3459 (bit 0 = upper case), indexed by the
// character as a signed byte in the loop (lb) and as an unsigned byte for the final difference (lbu); the table is
// guest data (the ELF image), read as retail reads it.
void strcasecmp_(const CallArgs& a, CallResult& r) {
    constexpr gaddr kCtype = 0x004B3459u;
    gaddr x = a32(a, 0), y = a32(a, 1);
    auto low_s = [&](gaddr p) {
        const int c = static_cast<std::int8_t>(u8(p));
        return (u8(kCtype + static_cast<std::uint32_t>(c)) & 1) ? c + 0x20 : c;
    };
    while (static_cast<std::int8_t>(u8(x)) != 0 && low_s(x) == low_s(y)) { ++x; ++y; }
    const int cx = u8(x), cy = u8(y);
    const int lx = (u8(kCtype + cx) & 1) ? cx + 0x20 : cx;
    const int ly = (u8(kCtype + cy) & 1) ? cy + 0x20 : cy;
    ret32(r, static_cast<std::uint32_t>(lx - ly));
}
// fb1: [C decomp libc.s 0x127520, libg.a strncasecmp.o (research/retail_names.csv, high)] strcasecmp's comparison
// with a count: n == 0 is 0; the loop compares the case-folded signed bytes and stops on a difference, on the n-th
// character, or when either byte is 0 (both are tested, as retail does); the result is the same unsigned-byte folded
// difference as strcasecmp's, of the bytes where it stopped
void strncasecmp_(const CallArgs& a, CallResult& r) {
    constexpr gaddr kCtype = 0x004B3459u;
    gaddr x = a32(a, 0), y = a32(a, 1);
    std::uint32_t n = a32(a, 2);
    if (n == 0) { ret32(r, 0); return; }
    --n;
    auto low_s = [&](int c) { return (u8(kCtype + static_cast<std::uint32_t>(c)) & 1) ? c + 0x20 : c; };
    for (;;) {
        const int cx = static_cast<std::int8_t>(u8(x)), cy = static_cast<std::int8_t>(u8(y));
        if (low_s(cx) != low_s(cy) || n == 0 || cx == 0 || cy == 0) break;
        --n; ++x; ++y;
    }
    const int cx = u8(x), cy = u8(y);
    const int lx = (u8(kCtype + cx) & 1) ? cx + 0x20 : cx;
    const int ly = (u8(kCtype + cy) & 1) ? cy + 0x20 : cy;
    ret32(r, static_cast<std::uint32_t>(lx - ly));
}
void strchr_(const CallArgs& a, CallResult& r) {
    gaddr s = a32(a, 0);
    const std::uint8_t c = static_cast<std::uint8_t>(a32(a, 1));
    for (;; ++s) {
        if (u8(s) == c) { ret32(r, s); return; }
        if (!u8(s)) { ret32(r, 0); return; }
    }
}
void strrchr_(const CallArgs& a, CallResult& r) {
    gaddr s = a32(a, 0), last = 0;
    const std::uint8_t c = static_cast<std::uint8_t>(a32(a, 1));
    for (;; ++s) {
        if (u8(s) == c) last = s;
        if (!u8(s)) break;
    }
    ret32(r, last);
}
void strpbrk_(const CallArgs& a, CallResult& r) {
    for (gaddr s = a32(a, 0); u8(s); ++s)
        for (gaddr k = a32(a, 1); u8(k); ++k)
            if (u8(k) == u8(s)) { ret32(r, s); return; }
    ret32(r, 0);
}
// [C decomp mwrt.s 0x1007F0] node = {next = chain, dtor, object}; chain = node; returns the object.
void register_global_object(const CallArgs& a, CallResult& r) {
    constexpr gaddr kChain = 0x004E3000u;   // __global_destructor_chain [C symbol_addrs]
    const gaddr node = a32(a, 2);
    b3host::store<std::uint32_t>(node, b3host::load<std::uint32_t>(kChain));
    b3host::store<std::uint32_t>(node + 4, a32(a, 1));
    b3host::store<std::uint32_t>(node + 8, a32(a, 0));
    b3host::store<std::uint32_t>(kChain, node);
    r.v0 = a.gpr[0];   // daddu v0, a0, zero: the whole 64-bit register
}
// [C decomp mwrt.s 0x100270] MWCC's array constructor (array, ctor, dtor, element size, count): ctor(element, 1)
// for each element in order. The rest of retail's code is the exception-unwind path (destroy what was built), which
// the game, built without exceptions thrown, never takes; v0 is whatever the last ctor left (no caller reads it [L]).
void construct_array(const CallArgs& a, CallResult& r) {
    const gaddr base = a32(a, 0), ctor = a32(a, 1);
    const std::uint32_t size = a32(a, 3), n = a32(a, 4);
    for (std::uint32_t i = 0; i < n; ++i)
        b3host::call_guest<void>(ctor, b3host::gp<void>::from_raw(base + i * size), 1);
    (void)r;
}
// libgcc 64-bit integer helpers (MWCC long = 64 bits in one GPR). Division by zero is not defined; callers guard.
void muldi3(const CallArgs& a, CallResult& r) { ret64(r, a64(a, 0) * a64(a, 1)); }
void divdi3(const CallArgs& a, CallResult& r) {
    const auto x = static_cast<std::int64_t>(a64(a, 0)), y = static_cast<std::int64_t>(a64(a, 1));
    ret64(r, static_cast<std::uint64_t>(y == -1 ? -static_cast<std::uint64_t>(x) : static_cast<std::uint64_t>(x / y)));
}
void moddi3(const CallArgs& a, CallResult& r) {
    const auto x = static_cast<std::int64_t>(a64(a, 0)), y = static_cast<std::int64_t>(a64(a, 1));
    ret64(r, y == -1 ? 0 : static_cast<std::uint64_t>(x % y));
}
void udivdi3(const CallArgs& a, CallResult& r) { ret64(r, a64(a, 0) / a64(a, 1)); }
void umoddi3(const CallArgs& a, CallResult& r) { ret64(r, a64(a, 0) % a64(a, 1)); }

// boot1: strstr [C decomp libc.s 0x127BF8; name: E map via symbol_addrs_b3names.txt]: the first position where the
// needle matches (an empty needle matches at once; an empty haystack gives itself for an empty needle, else 0).
// rt1: qsort(base, n, es, cmp) [C decomp libc.s 0x125338, 2540 bytes: newlib's BSD qsort (Bentley & McIlroy) -- the
// insertion sort under 7 elements, med3, the pseudo-median of 9 over 40, the 3-way partition with swap_cnt and its
// insertion-sort fallback, recursion on the left part (jal qsort 0x125CC4) and a loop on the right]. The comparator is
// guest code (call_guest), called in retail's order on the same element addresses, so its results and every byte it or
// the sort writes are retail's. Swaps move es bytes: retail moves 64-bit words when base and es are 8-aligned, bytes
// otherwise -- the same bytes either way.
namespace {
int q_cmp(gaddr cmp, gaddr x, gaddr y) { return b3host::call_guest<int>(cmp, x, y); }
void q_swap(gaddr x, gaddr y, std::uint32_t n) {
    for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint8_t t = u8(x + k);
        put8(x + k, u8(y + k));
        put8(y + k, t);
    }
}
gaddr q_med3(gaddr a, gaddr b, gaddr c, gaddr cmp) {
    return q_cmp(cmp, a, b) < 0 ? (q_cmp(cmp, b, c) < 0 ? b : (q_cmp(cmp, a, c) < 0 ? c : a))
                                : (q_cmp(cmp, b, c) > 0 ? b : (q_cmp(cmp, a, c) < 0 ? a : c));
}
void q_insertion(gaddr a, std::uint32_t n, std::uint32_t es, gaddr cmp) {
    for (gaddr pm = a + es; pm < a + n * es; pm += es)
        for (gaddr pl = pm; pl > a && q_cmp(cmp, pl - es, pl) > 0; pl -= es) q_swap(pl, pl - es, es);
}
void q_sort(gaddr a, std::uint32_t n, std::uint32_t es, gaddr cmp) {
    for (;;) {
        if (n < 7) { q_insertion(a, n, es, cmp); return; }
        gaddr pm = a + (n / 2) * es;
        if (n > 7) {
            gaddr pl = a, pn = a + (n - 1) * es;
            if (n > 40) {
                const std::uint32_t d = (n / 8) * es;
                pl = q_med3(pl, pl + d, pl + 2 * d, cmp);
                pm = q_med3(pm - d, pm, pm + d, cmp);
                pn = q_med3(pn - 2 * d, pn - d, pn, cmp);
            }
            pm = q_med3(pl, pm, pn, cmp);
        }
        q_swap(a, pm, es);
        gaddr pa = a + es, pb = pa, pc = a + (n - 1) * es, pd = pc;
        bool swapped = false;
        for (;;) {
            int r;
            while (pb <= pc && (r = q_cmp(cmp, pb, a)) <= 0) {
                if (r == 0) { swapped = true; q_swap(pa, pb, es); pa += es; }
                pb += es;
            }
            while (pb <= pc && (r = q_cmp(cmp, pc, a)) >= 0) {
                if (r == 0) { swapped = true; q_swap(pc, pd, es); pd -= es; }
                pc -= es;
            }
            if (pb > pc) break;
            q_swap(pb, pc, es);
            swapped = true;
            pb += es;
            pc -= es;
        }
        if (!swapped) { q_insertion(a, n, es, cmp); return; }
        const gaddr pn = a + n * es;
        std::uint32_t r = std::min(pa - a, pb - pa);
        if (r > 0) q_swap(a, pb - r, r);
        r = std::min(pd - pc, pn - pd - es);
        if (r > 0) q_swap(pb, pn - r, r);
        if ((r = pb - pa) > es) q_sort(a, r / es, es, cmp);
        if ((r = pd - pc) > es) { a = pn - r; n = r / es; continue; }
        return;
    }
}
}  // namespace
// rt1: strtol (0x128F08) = _strtol_r(_impure_ptr, ...) (func_00128CD0) [C decomp libc.s: newlib's, with the EE's 64-bit
// long]: spaces / digits / letters through retail's ctype table at 0x4B3459 indexed by the signed byte (bit 3 space,
// bit 2 digit, bits 0 / 1 upper / lower case); sign; "0x" / "0X" for base 0 or 16; base 0 -> 8 after a '0', else 10;
// cutoff = LONG_MAX or -LONG_MIN by the sign, cutlim = cutoff % base, then / base; a digit past the cutoff makes `any`
// -1 for good (the rest are still read); overflow: LONG_MIN / LONG_MAX and errno (the reent's first word) = 34
// (ERANGE); *endptr = the first unused character if any digit was seen, else nptr. Returns the 64-bit long.
// atoi (0x11FD30) = (int) strtol(s, NULL, 10), sign-extended. rand (0x125D40): the reent's 64-bit _rand_next (+0xA8)
// = next * 0x5851F42D4C957F2D + 1, returns (next >> 32) & 0x7FFFFFFF.
namespace {
constexpr gaddr kImpurePtr = 0x004876E8u, kCtypeTab = 0x004B3459u;
std::uint64_t strtol_r(gaddr nptr, gaddr endptr, std::uint32_t base_in) {
    const gaddr reent = b3host::load<std::uint32_t>(kImpurePtr);
    auto cls = [](int c) { return u8(kCtypeTab + static_cast<std::uint32_t>(c)); };
    gaddr s = nptr;
    int c;
    do { c = static_cast<std::int8_t>(u8(s)); ++s; } while (cls(c) & 8);
    bool neg = false;
    if (c == '-') { neg = true; c = static_cast<std::int8_t>(u8(s)); ++s; }
    else if (c == '+') { c = static_cast<std::int8_t>(u8(s)); ++s; }
    std::int32_t base = static_cast<std::int32_t>(base_in);
    if ((base == 0 || base == 16) && c == '0') {
        const int x = static_cast<std::int8_t>(u8(s));
        if (x == 'x' || x == 'X') { c = static_cast<std::int8_t>(u8(s + 1)); s += 2; base = 16; }
    }
    if (base == 0) base = c == '0' ? 8 : 10;
    const std::uint64_t ub = static_cast<std::uint64_t>(static_cast<std::int64_t>(base));
    std::uint64_t cutoff = neg ? 0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFull;
    const std::int64_t cutlim = static_cast<std::int32_t>(static_cast<std::uint32_t>(cutoff % ub));
    cutoff /= ub;
    std::uint64_t acc = 0;
    int any = 0;
    for (;;) {
        const std::uint8_t f = cls(c);
        if (f & 4) c -= '0';
        else if (f & 3) c -= (f & 1) ? 0x37 : 0x57;
        else break;
        if (!(c < base)) break;
        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) any = -1;
        else { any = 1; acc = acc * ub + static_cast<std::uint64_t>(static_cast<std::int64_t>(c)); }
        c = static_cast<std::int8_t>(u8(s));
        ++s;
    }
    if (any < 0) {
        acc = neg ? 0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFull;
        b3host::store<std::uint32_t>(reent, 34u);
    } else if (neg) {
        acc = 0 - acc;
    }
    if (endptr) b3host::store<std::uint32_t>(endptr, any ? s - 1 : nptr);
    return acc;
}
}  // namespace
void strtol_(const CallArgs& a, CallResult& r) {
    r.v0 = b3host::detail::gpr64(strtol_r(a32(a, 0), a32(a, 1), a32(a, 2)));
}
void atoi_(const CallArgs& a, CallResult& r) {
    ret32(r, static_cast<std::uint32_t>(strtol_r(a32(a, 0), 0, 10)));
}
void rand_(const CallArgs&, CallResult& r) {
    const gaddr st = b3host::load<std::uint32_t>(kImpurePtr) + 0xA8u;
    const std::uint64_t next = b3host::load<std::uint64_t>(st) * 0x5851F42D4C957F2Dull + 1u;
    b3host::store<std::uint64_t>(st, next);
    ret32(r, static_cast<std::uint32_t>(next >> 32) & 0x7FFFFFFFu);
}
void qsort_(const CallArgs& a, CallResult& r) {
    q_sort(a32(a, 0), a32(a, 1), a32(a, 2), a32(a, 3));
    r = CallResult{};
}
void strstr_(const CallArgs& a, CallResult& r) {
    gaddr h = a32(a, 0);
    const gaddr n = a32(a, 1);
    if (!u8(h)) { ret32(r, u8(n) ? 0u : h); return; }
    for (; u8(h); ++h) {
        std::uint32_t k = 0;
        while (u8(n + k) && u8(h + k) == u8(n + k)) ++k;
        if (!u8(n + k)) { ret32(r, h); return; }
    }
    ret32(r, 0);
}
// boot1: func_00129198 [C decomp libc.s 0x129198; G: newlib's strupr]: in place, every byte whose newlib _ctype_ entry
// has _L (2) set loses 0x20; the table is read from the image (0x4B3459 = _ctype_ + 1, indexed by the signed byte, as
// retail's `lb` + `addu`), so the result is retail's for every byte value. Returns the string.
void strupr_(const CallArgs& a, CallResult& r) {
    const gaddr s = a32(a, 0);
    for (gaddr p = s; u8(p); ++p) {
        const std::int32_t c = static_cast<std::int8_t>(u8(p));
        if (u8(0x004B3459u + static_cast<gaddr>(c)) & 2u) put8(p, static_cast<std::uint8_t>(c - 0x20));
    }
    ret32(r, s);
}

}  // namespace b3native::platform

namespace b3native::platform {
// boot2: two libgcc soft-float conversions setUpFSAA0 calls (retail fp-bit, decomp asm/runtime/libgcc.s).
// float __floatdisf(long long) [C 0x12D8D0]: beyond 2^53 the low 11 bits are folded into a sticky bit, then
// high * 2^32 + (unsigned) low in double (litodp, dpmul, dpadd: exact below 2^53) and dptofp's round to nearest even
// [C the asm; L fp-bit's rounding]: one correct rounding of the integer, as the host's conversion gives.
void floatdisf_(const CallArgs& a, CallResult& r) {
    const std::int64_t x = static_cast<std::int64_t>(a64(a, 0));
    const float f = static_cast<float>(x);
    r = CallResult{};
    std::memcpy(&r.f0, &f, 4);
}
// unsigned fptoui(float) [C 0x12DE38]: fp-bit's __unpack_f, then: zero or negative -> 0; exponent < 0 -> 0, >= 32 ->
// 0xFFFFFFFF; else the fraction (implicit bit at 30) shifted: truncation. NaN / infinity / PS2 out-of-range bit
// patterns (exponent field 0xFF) take fp-bit's other classes: fatal here until met.
void fptoui_(const CallArgs& a, CallResult& r) {
    const std::uint32_t bits = a.fpr[0];
    const std::uint32_t ef = (bits >> 23) & 0xFFu, frac = bits & 0x7FFFFFu;
    std::uint32_t v = 0;
    if (ef == 0xFF) stub_unexpected("fptoui", "an infinity / NaN bit pattern (fp-bit's other classes)");
    if (ef != 0 || frac != 0) {   // zero: 0
        if (!(bits >> 31)) {
            const int e = static_cast<int>(ef) - 127;   // denormals: fp-bit normalises them (exponent < -126): 0 below
            const std::uint32_t m = ef ? ((frac | 0x800000u) << 7) : 0;
            if (ef == 0) v = 0;
            else if (e < 0) v = 0;
            else if (e >= 32) v = 0xFFFFFFFFu;
            else if (e < 31) v = m >> (30 - e);
            else v = m << (e - 30);
        }
    }
    r = CallResult{};
    ret32(r, v);
}
}  // namespace b3native::platform
