// b3libm/ps2libm.h -- the PS2 libm functions retail game code calls, for native code (docs/ps2-libm.md).
//
//   b3libm::ps2::*   bit-exact ports of retail's newlib/fdlibm build on the F type (always available; the PS2 float
//                    model of b3host/ps2float.h, docs/specs/ps2-float.md). Verified against the retail code itself
//                    (tests/test_libm*.cpp: retail functions run in an R5900 interpreter from orig/SLUS_210.50).
//   b3libm::fast::*  host libm. Where the PS2 build returns a finite value and the host would give Inf/NaN (domain
//                    errors, overflow), the result is clamped to +-FLT_MAX / +FLT_MAX for NaN [G policy, as the rest of
//                    fast mode: game code never sees Inf/NaN on the PS2]. Not bit-exact.
//   b3libm::sinf(float) etc.   the one game code calls: ps2:: when B3_PS2_FLOAT=1, fast:: otherwise.
//
// The game calls (call sites, research/port/game_library_calls.tsv in the decomp): sinf 54, cosf 43, acosf 37
// (func_0011E328), atan2f 24 (func_0011E358), powf 18 (func_0011E388), fabsf 18, asinf 17 (func_0011E340), tanf 7,
// floorf 5, atanf 5, ceil 3, ceilf 2, logf 1 (func_0011E370), fabs 1.
#ifndef B3LIBM_PS2LIBM_H
#define B3LIBM_PS2LIBM_H

#include "b3libm/ps2_basic.h"
#include "b3libm/ps2_trig.h"
#if __has_include("b3libm/ps2_atan.h")
#include "b3libm/ps2_atan.h"
#define B3LIBM_HAVE_ATAN 1
#endif
#if __has_include("b3libm/ps2_tan.h")
#include "b3libm/ps2_tan.h"
#define B3LIBM_HAVE_TAN 1
#endif
#if __has_include("b3libm/ps2_exp.h")
#include "b3libm/ps2_exp.h"
#define B3LIBM_HAVE_EXP 1
#endif

#include <cfloat>
#include <cmath>

#ifndef B3_PS2_FLOAT
#define B3_PS2_FLOAT 0
#endif

namespace b3libm {

namespace fast {
inline float safe(float r) {
    if (std::isnan(r)) return FLT_MAX;
    if (std::isinf(r)) return r > 0 ? FLT_MAX : -FLT_MAX;
    return r;
}
inline float sinf(float x) { return safe(std::sin(x)); }
inline float cosf(float x) { return safe(std::cos(x)); }
inline float tanf(float x) { return safe(std::tan(x)); }
inline float acosf(float x) { return safe(std::acos(x)); }
inline float asinf(float x) { return safe(std::asin(x)); }
inline float atanf(float x) { return safe(std::atan(x)); }
inline float atan2f(float y, float x) { return safe(std::atan2(y, x)); }
inline float powf(float x, float y) { return safe(std::pow(x, y)); }
inline float logf(float x) { return safe(std::log(x)); }
inline float sqrtf(float x) { return safe(std::sqrt(x)); }
inline float floorf(float x) { return std::floor(x); }
inline float ceilf(float x) { return std::ceil(x); }
inline float fabsf(float x) { return std::fabs(x); }
inline double ceil(double x) { return std::ceil(x); }
inline double fabs(double x) { return std::fabs(x); }
}  // namespace fast

#if B3_PS2_FLOAT
#define B3LIBM_F1(name) inline float name(float x) { return ps2::to_host(ps2::name(ps2::from_float(x))); }
#define B3LIBM_F2(name) inline float name(float a, float b) { return ps2::to_host(ps2::name(ps2::from_float(a), ps2::from_float(b))); }
B3LIBM_F1(sinf)
B3LIBM_F1(cosf)
B3LIBM_F1(acosf)
B3LIBM_F1(asinf)
B3LIBM_F1(floorf)
B3LIBM_F1(ceilf)
B3LIBM_F1(fabsf)
B3LIBM_F1(sqrtf)
#ifdef B3LIBM_HAVE_TAN
B3LIBM_F1(tanf)
#else
inline float tanf(float x) { return fast::tanf(x); }   // not ported yet: host fallback (not bit-exact)
#endif
#ifdef B3LIBM_HAVE_ATAN
B3LIBM_F1(atanf)
B3LIBM_F2(atan2f)
#else
inline float atanf(float x) { return fast::atanf(x); }
inline float atan2f(float y, float x) { return fast::atan2f(y, x); }
#endif
#ifdef B3LIBM_HAVE_EXP
B3LIBM_F1(logf)
B3LIBM_F2(powf)
#else
inline float logf(float x) { return fast::logf(x); }
inline float powf(float x, float y) { return fast::powf(x, y); }
#endif
inline double ceil(double x) { return ps2::ceil(x); }
inline double fabs(double x) { return ps2::fabs(x); }
#undef B3LIBM_F1
#undef B3LIBM_F2
#else
using fast::sinf, fast::cosf, fast::tanf, fast::acosf, fast::asinf, fast::atanf, fast::atan2f, fast::powf, fast::logf,
    fast::sqrtf, fast::floorf, fast::ceilf, fast::fabsf, fast::ceil, fast::fabs;
#endif

}  // namespace b3libm

#endif
