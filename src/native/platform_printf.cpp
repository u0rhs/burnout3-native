// The native backend's platform tier, printf family (boot1; src/native/platform.h): retail's newlib snprintf /
// sprintf / vsprintf on guest memory, and newlib's stdio start-up (__sinit, std) that their first call runs.
//
// Retail [C decomp runtime/libc.s]: snprintf 0x126A18 / sprintf 0x126B40 / vsprintf 0x12BC70 build a string FILE on
// their stack (_flags 0x208 = __SWR | __SSTR, _p = _bf._base = buf, _w = _bf._size = size - 1 or 0 for size 0
// (sprintf / vsprintf: 0x7FFFFFFF), _data = _impure_ptr) and call vfprintf 0x129558, which runs __sinit(_impure_ptr)
// once (reent + 0x38 = __sdidinit is 0), then _vfprintf_r; afterwards a NUL goes at _p (snprintf: only if size != 0).
// The va_list is a pointer to 8-byte slots: the callee spills its unnamed register arguments (a3 / a2 .. t3) as slots
// that continue into the caller's stack arguments [C the sd sequences above; docs/abi-mwcc-ee.md §5].
//
// The conversions follow newlib's vfprintf for integers, characters and strings: flags - + space # 0, width and
// precision (also *), h / l / ll / q length, d i o u x X c s p n %, D O U, and an unknown conversion printed as
// itself. `l` is MWCC/ee-gcc's 64-bit long; `ll` and `q` set newlib's QUADINT, which this newlib was built without
// (no long-long I/O): the value is read as an int [C retail in the mini: snprintf "%llu", 0x123456789 -> "591751049",
// "%lu" -> "4886718345"; both take one slot]. Floating-point conversions (e E f g G) run newlib's _dtoa_r, which allocates
// Bigints from the heap in retail (guest state): the text is made here (mac4), the heap allocation not modelled. Every case is checked
// against the retail functions in tests/r5900_mini.h on random formats (tests/test_native_platform.cpp).
#include "platform.h"

#include "b3host/call.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace b3native::platform {
namespace {

using b3host::gaddr;
using b3host::CallArgs;
using b3host::CallResult;

constexpr gaddr kImpurePtr = 0x004876E8u;   // _impure_ptr [C symbol_addrs.txt]
constexpr gaddr kCleanupR = 0x00121998u;    // _cleanup_r [C symbol_addrs_b3names.txt]
constexpr gaddr kSread = 0x00126C80u, kSwrite = 0x00126CE8u, kSseek = 0x00126D68u, kSclose = 0x00126DD0u;

inline std::uint8_t u8(gaddr p) { return b3host::load<std::uint8_t>(p); }

// std(fp, flags, file, reent) [C libc.s 0x1217C8]
void std_stream(gaddr fp, std::uint16_t flags, std::uint16_t file, gaddr reent) {
    b3host::store<std::uint32_t>(fp + 0x54, reent);
    b3host::store<std::uint16_t>(fp + 0xC, flags);
    b3host::store<std::uint16_t>(fp + 0xE, file);
    b3host::store<std::uint32_t>(fp + 0x20, kSread);
    b3host::store<std::uint32_t>(fp + 0x24, kSwrite);
    b3host::store<std::uint32_t>(fp + 0x28, kSseek);
    b3host::store<std::uint32_t>(fp + 0x2C, kSclose);
    for (gaddr o : {0x0u, 0x4u, 0x8u, 0x10u, 0x14u, 0x18u}) b3host::store<std::uint32_t>(fp + o, 0);
    b3host::store<std::uint32_t>(fp + 0x1C, fp);
}

// __sinit(reent) [C libc.s 0x1219D8]
void sinit(gaddr s) {
    b3host::store<std::uint32_t>(s + 0x3C, kCleanupR);
    b3host::store<std::uint32_t>(s + 0x38, 1);
    std_stream(s + 0x1E4, 4, 0, s);
    std_stream(s + 0x23C, 9, 1, s);
    std_stream(s + 0x294, 10, 2, s);
    b3host::store<std::uint32_t>(s + 0x1D8, 0);
    b3host::store<std::uint32_t>(s + 0x1E0, s + 0x1E4);
    b3host::store<std::uint32_t>(s + 0x1DC, 3);
}

// vfprintf's start [C libc.s 0x129578-0x1295A0]: the FILE's _data is _impure_ptr here, so __sinit(_impure_ptr) once.
void check_init() {
    const gaddr reent = b3host::load<std::uint32_t>(kImpurePtr);
    if (b3host::load<std::uint32_t>(reent + 0x38) == 0) sinit(reent);
}

// The va_list: 8-byte slots; the first `nreg` come from registers a[first..7], the rest from guest memory at `mem`.
struct Slots {
    const CallArgs* a = nullptr;
    unsigned next = 8;   // next register slot (8: none left)
    gaddr mem = 0;
    std::uint64_t take() {
        if (a && next < 8) return b3host::detail::low64(a->gpr[next++]);
        const std::uint64_t v = b3host::load<std::uint64_t>(mem);
        mem += 8;
        return v;
    }
};

enum : unsigned { ALT = 1, LADJUST = 4, LONGINT = 0x10, QUADINT = 0x20, SHORTINT = 0x40, ZEROPAD = 0x80, HEXPREFIX = 0x100 };

// newlib's _vfprintf_r for integer / character / string conversions; returns the output (all of it: the callers
// truncate) and the count retail returns.
std::string format(const char* who, gaddr fmt, Slots& ap) {
    std::string out;
    auto pad = [&](int n, char c) { if (n > 0) out.append(static_cast<std::size_t>(n), c); };
    for (;;) {
        std::uint8_t ch;
        while ((ch = u8(fmt)) != 0 && ch != '%') { out.push_back(static_cast<char>(ch)); ++fmt; }
        if (!ch) break;
        ++fmt;   // the '%'
        unsigned flags = 0;
        char sign = 0;
        int width = 0, prec = -1;
        std::string body;   // the digits / characters
        bool is_number = false;
        for (;;) {
            ch = u8(fmt++);
            switch (ch) {
            case ' ': if (!sign) sign = ' '; continue;
            case '#': flags |= ALT; continue;
            case '*': {
                const int w = static_cast<std::int32_t>(ap.take());
                if (w >= 0) { width = w; continue; }
                width = -w;
                flags |= LADJUST;
                continue;
            }
            case '-': flags |= LADJUST; continue;
            case '+': sign = '+'; continue;
            case '.': {
                ch = u8(fmt++);
                if (ch == '*') {
                    const int n = static_cast<std::int32_t>(ap.take());
                    prec = n < 0 ? -1 : n;
                    continue;
                }
                int n = 0;
                while (ch >= '0' && ch <= '9') { n = 10 * n + (ch - '0'); ch = u8(fmt++); }
                prec = n < 0 ? -1 : n;
                --fmt;   // reswitch on ch
                continue;
            }
            case '0': flags |= ZEROPAD; continue;
            case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': {
                int n = 0;
                do { n = 10 * n + (ch - '0'); ch = u8(fmt++); } while (ch >= '0' && ch <= '9');
                width = n;
                --fmt;
                continue;
            }
            case 'h': flags |= SHORTINT; continue;
            case 'l':
                if (u8(fmt) == 'l') { ++fmt; flags |= QUADINT; } else flags |= LONGINT;
                continue;
            case 'q': flags |= QUADINT; continue;
            case 'L':
                stub_unexpected(who, "the L length modifier (long double)");
            default: break;
            }
            break;
        }
        auto sarg = [&]() -> std::int64_t {
            const std::uint64_t v = ap.take();
            if (flags & LONGINT) return static_cast<std::int64_t>(v);
            if (flags & SHORTINT) return static_cast<std::int16_t>(v);
            return static_cast<std::int32_t>(v);
        };
        auto uarg = [&]() -> std::uint64_t {
            const std::uint64_t v = ap.take();
            if (flags & LONGINT) return v;
            if (flags & SHORTINT) return static_cast<std::uint16_t>(v);
            return static_cast<std::uint32_t>(v);
        };
        unsigned base = 10;
        const char* xdigs = "0123456789abcdef";
        std::uint64_t uq = 0;
        switch (ch) {
        case 'c':
            body.assign(1, static_cast<char>(static_cast<std::uint8_t>(ap.take())));
            sign = 0;
            break;
        case 'D': flags |= LONGINT; [[fallthrough]];
        case 'd': case 'i': {
            const std::int64_t v = sarg();
            uq = static_cast<std::uint64_t>(v);
            if (v < 0) { uq = 0 - uq; sign = '-'; }
            is_number = true;
            break;
        }
        case 'e': case 'E': case 'f': case 'g': case 'G': {
            // mac4 (2026-11-08): formatted here (the race HUD's crash combo text, GetComboItemDescription 0x364300:
            // sprintf "%.1f" -- the takedown run ended on the stub's fatal before). The double comes as its 64 bits in
            // one slot (soft-float, docs/abi-mwcc-ee.md §5). The digits: the host's printf, correctly rounded as newlib's
            // _dtoa_r mode 3 [L]; Inf / NaN as newlib spells them ("Inf" / "NaN", "INF" / "NAN" for E / G [L newlib
            // vfprintf]). Not modelled [G, guest state only]: _dtoa_r's Bigints from the newlib heap (_REENT freelist)
            // -- only the heap's later layout can differ from retail, no game value.
            const double v = std::bit_cast<double>(ap.take());
            if (!std::isfinite(v)) {
                body = std::isnan(v) ? (ch <= 'G' ? "NAN" : "NaN") : (ch <= 'G' ? "INF" : "Inf");
                if (std::signbit(v) && !std::isnan(v)) sign = '-';
                flags &= ~ZEROPAD;
                break;
            }
            std::string spec = "%";
            if (flags & ALT) spec += '#';
            if (flags & LADJUST) spec += '-';
            if (sign) spec += sign;
            if (flags & ZEROPAD) spec += '0';
            spec += std::to_string(width);
            if (prec >= 0) spec += "." + std::to_string(prec);
            spec += static_cast<char>(ch);
            char buf[512];
            const int n = std::snprintf(buf, sizeof buf, spec.c_str(), v);
            if (n < 0 || n >= static_cast<int>(sizeof buf)) stub_unexpected(who, "a floating-point field over 511 characters");
            out.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        case 'n': {
            const gaddr p = static_cast<gaddr>(ap.take());
            const std::uint32_t n = static_cast<std::uint32_t>(out.size());
            if (flags & LONGINT) b3host::store<std::uint64_t>(p, static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(n))));
            else if (flags & SHORTINT) b3host::store<std::uint16_t>(p, static_cast<std::uint16_t>(n));
            else b3host::store<std::uint32_t>(p, n);
            continue;
        }
        case 'O': flags |= LONGINT; [[fallthrough]];
        case 'o': uq = uarg(); base = 8; sign = 0; is_number = true; break;
        case 'p':
            // (u_long)(void *): the 32-bit pointer sign-extended to the 64-bit long [C retail in the mini: %p of
            // 0x80001234 -> "0xffffffff80001234", of 0x123456789abc -> "0x56789abc"]
            uq = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(ap.take())));
            base = 16;
            flags |= HEXPREFIX;
            ch = 'x';
            sign = 0;
            is_number = true;
            break;
        case 's': {
            gaddr cp = static_cast<gaddr>(ap.take());
            std::string s;
            if (cp == 0) s = "(null)";
            else for (gaddr p = cp; u8(p) && (prec < 0 || static_cast<int>(s.size()) < prec); ++p) s.push_back(static_cast<char>(u8(p)));
            if (prec >= 0 && static_cast<int>(s.size()) > prec) s.resize(static_cast<std::size_t>(prec));
            body = s;
            sign = 0;
            break;
        }
        case 'U': flags |= LONGINT; [[fallthrough]];
        case 'u': uq = uarg(); base = 10; sign = 0; is_number = true; break;
        case 'X': xdigs = "0123456789ABCDEF"; [[fallthrough]];
        case 'x':
            uq = uarg();
            base = 16;
            if ((flags & ALT) && uq != 0) flags |= HEXPREFIX;
            sign = 0;
            is_number = true;
            break;
        case 0:
            goto done;   // newlib: a format ending in '%...' stops there
        default:
            body.assign(1, static_cast<char>(ch));
            sign = 0;
            break;
        }
        {
            int dprec = 0;
            if (is_number) {
                dprec = prec;
                if (dprec >= 0) flags &= ~ZEROPAD;
                std::string d;
                if (uq != 0 || prec != 0) {
                    do { d.insert(d.begin(), xdigs[uq % base]); uq /= base; } while (uq);
                    if (base == 8 && (flags & ALT) && d[0] != '0') d.insert(d.begin(), '0');
                } else if (base == 8 && (flags & ALT)) {
                    d = "0";
                }
                body = d;
            }
            const int size = static_cast<int>(body.size());
            int realsz = dprec > size ? dprec : size;
            if (sign) ++realsz;
            else if (flags & HEXPREFIX) realsz += 2;
            if ((flags & (LADJUST | ZEROPAD)) == 0) pad(width - realsz, ' ');
            if (sign) out.push_back(sign);
            else if (flags & HEXPREFIX) { out.push_back('0'); out.push_back(static_cast<char>(ch)); }
            if ((flags & (LADJUST | ZEROPAD)) == ZEROPAD) pad(width - realsz, '0');
            pad(dprec - size, '0');
            out += body;
            if (flags & LADJUST) pad(width - realsz, ' ');
        }
    }
done:
    return out;
}

// Writes the first `room` bytes of `s` at buf (the string FILE's _w), then the NUL retail stores at _p.
void emit(gaddr buf, const std::string& s, std::uint32_t room, bool nul) {
    const std::uint32_t n = s.size() < room ? static_cast<std::uint32_t>(s.size()) : room;
    for (std::uint32_t k = 0; k < n; ++k) b3host::store<std::uint8_t>(buf + k, static_cast<std::uint8_t>(s[k]));
    if (nul) b3host::store<std::uint8_t>(buf + n, 0);
}

}  // namespace

// snprintf(buf, size, fmt, ...) [C libc.s 0x126A18]
void snprintf_(const CallArgs& a, CallResult& r) {
    const gaddr buf = a.gpr[0].w[0], fmt = a.gpr[2].w[0];
    const std::uint32_t size = a.gpr[1].w[0];
    Slots ap{&a, 3, a.sp};
    check_init();
    const std::string s = format("snprintf", fmt, ap);
    emit(buf, s, size ? size - 1 : 0, size != 0);
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(s.size())));
}
// sprintf(buf, fmt, ...) [C libc.s 0x126B40]
void sprintf_(const CallArgs& a, CallResult& r) {
    const gaddr buf = a.gpr[0].w[0], fmt = a.gpr[1].w[0];
    Slots ap{&a, 2, a.sp};
    check_init();
    const std::string s = format("sprintf", fmt, ap);
    emit(buf, s, 0x7FFFFFFFu, true);
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(s.size())));
}
// printf(fmt, ...) [C libc.s 0x1252C8] / scePrintf(fmt, ...) [C libkernl 0x10E048] (boot2): formatted as retail's
// vfprintf would (the same engine), written to the host's stdout with a "[guest]" prefix; returns the length.
// Retail's printf goes through newlib's stdout FILE (its buffer, flushed to the IOP's tty by a write syscall): that
// buffer and the FILE's counters are not written here (logged); scePrintf
// writes the IOP's tty directly (no guest state) [L].
// boot2 (movie2's poll-1 finding: the heap shifted by 0x400 against a recorded retail run): newlib's first write to stdout
// makes its buffer -- __swsetup -> __smakebuf: _malloc_r(reent, 1024) (BUFSIZ; the tty a line-buffered stream) -- the
// block every later allocation sits behind. Done here at the first printf, on the same heap path; the FILE
// (_impure_ptr->_stdout, reent +8) as the recorded retail run leaves it after a line has been flushed [C navproof14s1 poll 1,
// crt 0x487634..: _p = _bf._base = the block, _flags 0x0889 (__SWR | __SLBF | __SMBF | 0x800), _bf._size 0x400,
// _lbfsize -0x400, _r and _w 0; L: the order of the fields' writes, newlib's own code not run]
void stdout_first_write() {
    static bool done = false;
    if (done) return;
    done = true;
    const gaddr fp = b3host::load<std::uint32_t>(b3host::load<std::uint32_t>(kImpurePtr) + 8u);   // _impure_ptr->_stdout
    if (!fp || b3host::load<std::uint32_t>(fp + 0x10u)) return;          // no FILE, or already buffered
    const gaddr buf = platform_malloc_r(0x400u);
    if (!buf) return;
    b3host::store<std::uint32_t>(fp + 0x00u, buf);
    b3host::store<std::uint32_t>(fp + 0x04u, 0u);
    b3host::store<std::uint32_t>(fp + 0x08u, 0u);
    b3host::store<std::uint16_t>(fp + 0x0Cu, 0x0889u);
    b3host::store<std::uint32_t>(fp + 0x10u, buf);
    b3host::store<std::uint32_t>(fp + 0x14u, 0x400u);
    b3host::store<std::uint32_t>(fp + 0x18u, 0xFFFFFC00u);
}
void printf_(const CallArgs& a, CallResult& r) {
    const gaddr fmt = a.gpr[0].w[0];
    Slots ap{&a, 1, a.sp};
    check_init();
    stdout_first_write();
    const std::string s = format("printf", fmt, ap);
    std::fprintf(stdout, "[guest] %s", s.c_str());
    if (!s.empty() && s.back() != '\n') std::fputc('\n', stdout);
    stub_note("printf", "to the host's stdout; newlib's stdout buffer made at the first write as newlib does (1 KB, the FILE as after a flush)");
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(s.size())));
}
void sce_printf_(const CallArgs& a, CallResult& r) {
    const gaddr fmt = a.gpr[0].w[0];
    Slots ap{&a, 1, a.sp};
    const std::string s = format("scePrintf", fmt, ap);
    std::fprintf(stdout, "[guest] %s", s.c_str());
    if (!s.empty() && s.back() != '\n') std::fputc('\n', stdout);
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(s.size())));
}
// vsprintf(buf, fmt, ap) [C libc.s 0x12BC70]
void vsprintf_(const CallArgs& a, CallResult& r) {
    const gaddr buf = a.gpr[0].w[0], fmt = a.gpr[1].w[0];
    Slots ap{nullptr, 8, a.gpr[2].w[0]};
    check_init();
    const std::string s = format("vsprintf", fmt, ap);
    emit(buf, s, 0x7FFFFFFFu, true);
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(s.size())));
}
// __sinit(reent) [C libc.s 0x1219D8] and std(fp, flags, file, reent) [C libc.s 0x1217C8] as table entries too
void sinit_(const CallArgs& a, CallResult& r) { sinit(a.gpr[0].w[0]); r = CallResult{}; }
void std_(const CallArgs& a, CallResult& r) {
    std_stream(a.gpr[0].w[0], static_cast<std::uint16_t>(a.gpr[1].w[0]), static_cast<std::uint16_t>(a.gpr[2].w[0]), a.gpr[3].w[0]);
    r = CallResult{};
}

}  // namespace b3native::platform
