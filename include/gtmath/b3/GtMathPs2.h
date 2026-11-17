// GtMathPs2 for the PC port: the decomp's include/b3/GtMathPs2.h API (same names, types, layouts) implemented on the
// host. This directory (include/gtmath) goes before the decomp's include/ on the include path, so decomp sources that
// `#include "b3/GtMathPs2.h"` get this file.
//
// Each body follows the decomp header's VU0/COP1 instruction sequence op for op, through b3vu (VU0 lanes) and b3fp
// (COP1 scalar) from b3host/ps2float.h. With -DB3_PS2_FLOAT=1 that is the PS2 float model bit for bit
// (docs/specs/ps2-float.md); with 0 it is plain host float (SSE). Comments cite the decomp asm being mirrored ([C]
// = read from the asm).
//
// Lanes the original leaves undefined: an asm output register ("=j") written under a partial mask (e.g. vsub.xyz)
// leaves its other lanes holding whatever the register held. Here they are taken from the left vector operand (the
// value most likely sharing the register) [G]; tests never rely on them. Lanes of a "+j" (in-place) operand are kept
// [C asm semantics].
//
// Names: all from the decomp headers (which cite the E-build maps / Revenge stabs); none invented here.
#ifndef GTMATHPS2_H
#define GTMATHPS2_H

#include "b3host/host.h"   // lane-noise: b3host::zero_if_host (CGtMatrix3x4's constructor)
#include "b3host/ps2float.h"
#include "b3host/ps2types.h"
#include "b3libm/ps2libm.h"

#include <cmath>

namespace GtMathPs2 {


// ---------------------------------------------------------------------------------------------------------------
// CGtV2d / CGtMatrix2x3 (plain COP1 float code in the decomp header)
// ---------------------------------------------------------------------------------------------------------------
struct alignas(8) CGtV2d {   // [L] N220: 8-byte aligned (ld/sd moves)
    float marValues[2];
    CGtV2d() {}   // user-provided on purpose: `= default` changes GCC's -O2 code of 10 live units (frame layout), gen1
    CGtV2d(float x, float y) { marValues[0] = x; marValues[1] = y; }
    void Set(float x, float y) { marValues[0] = x; marValues[1] = y; }
    void SetX(float x) { marValues[0] = x; }
    void SetY(float y) { marValues[1] = y; }
    CGtV2d& operator*=(float s) { marValues[0] = b3fp::mul(marValues[0], s); marValues[1] = b3fp::mul(marValues[1], s); return *this; }
    CGtV2d& operator+=(const CGtV2d& o) { marValues[0] = b3fp::add(marValues[0], o.marValues[0]); marValues[1] = b3fp::add(marValues[1], o.marValues[1]); return *this; }
    CGtV2d& operator-=(const CGtV2d& o) { marValues[0] = b3fp::sub(marValues[0], o.marValues[0]); marValues[1] = b3fp::sub(marValues[1], o.marValues[1]); return *this; }
    float X() const { return marValues[0]; }
    float Y() const { return marValues[1]; }
    CGtV2d operator+(const CGtV2d& o) const { return CGtV2d(b3fp::add(marValues[0], o.marValues[0]), b3fp::add(marValues[1], o.marValues[1])); }
    CGtV2d operator-(const CGtV2d& o) const { return CGtV2d(b3fp::sub(marValues[0], o.marValues[0]), b3fp::sub(marValues[1], o.marValues[1])); }
    CGtV2d operator*(const CGtV2d& o) const { return CGtV2d(b3fp::mul(marValues[0], o.marValues[0]), b3fp::mul(marValues[1], o.marValues[1])); }
    CGtV2d operator/(const CGtV2d& o) const { return CGtV2d(b3fp::div(marValues[0], o.marValues[0]), b3fp::div(marValues[1], o.marValues[1])); }
    // [C decomp] one div.s 1/s, then a mul.s per lane (func_0031A9C0)
    CGtV2d operator/(float s) const { const float inv = b3fp::div(1.0f, s); return CGtV2d(b3fp::mul(marValues[0], inv), b3fp::mul(marValues[1], inv)); }
    CGtV2d operator*(float s) const { return CGtV2d(b3fp::mul(marValues[0], s), b3fp::mul(marValues[1], s)); }
    CGtV2d operator-() const { return CGtV2d(b3fp::neg(marValues[0]), b3fp::neg(marValues[1])); }
};

struct CGtMatrix2x3 {
    CGtV2d mRight;
    CGtV2d mDown;
    CGtV2d mPos;
    CGtMatrix2x3() {}
};

inline CGtMatrix2x3 CreateRotationMatrix2x3(float angle) {
    const float s = b3libm::sinf(angle);   // retail: libm sinf/cosf (b3libm: bit-exact in PS2-faithful mode)
    const float c = b3libm::cosf(angle);
    CGtMatrix2x3 r;
    r.mDown.Set(s, c);
    r.mRight.Set(c, b3fp::neg(s));
    r.mPos.Set(0.0f, 0.0f);
    return r;
}
inline CGtV2d TransformPoint(const CGtMatrix2x3& m, const CGtV2d& v) {
    CGtV2d r = m.mPos;
    r += m.mRight * v.marValues[0];
    r += m.mDown * v.marValues[1];
    return r;
}
inline void TransformPoints(const CGtMatrix2x3& m, CGtV2d* dst, const CGtV2d* src, int n) {
    for (int i = 0; i < n; i++) dst[i] = TransformPoint(m, src[i]);
}

// ---------------------------------------------------------------------------------------------------------------
// Axis types: one scalar in one lane of a 128-bit value [C layout: Revenge stabs, decomp header]
// ---------------------------------------------------------------------------------------------------------------
struct XAxisType {
    u_long128 lVector;
    XAxisType() = default;   // trivial (uninitialized, as before): a goto may cross it (decomp T8 cases)
    XAxisType(u_long128 v) : lVector(v) {}
    operator float() const { return b3vu::lane(lVector, 0); }
    XAxisType operator-() const;
};
struct YAxisType {
    u_long128 lVector;
    YAxisType() = default;   // trivial (uninitialized, as before): a goto may cross it (decomp T8 cases)
    YAxisType(u_long128 v) : lVector(v) {}
    operator float() const { return b3vu::lane(lVector, 1); }
    YAxisType operator-() const;
};
struct ZAxisType {
    u_long128 lVector;
    ZAxisType() = default;   // trivial (uninitialized, as before): a goto may cross it (decomp T8 cases)
    ZAxisType(u_long128 v) : lVector(v) {}
    operator float() const { return b3vu::lane(lVector, 2); }
    ZAxisType operator-() const;
};
struct WAxisType {
    u_long128 lVector;
    WAxisType() = default;   // trivial (uninitialized, as before): a goto may cross it (decomp T8 cases)
    WAxisType(u_long128 v) : lVector(v) {}
    operator float() const { return b3vu::lane(lVector, 3); }
    XAxisType operator-() const;   // [C decomp] returns XAxisType (vsubw.x)
};

namespace detail {
// lane index of each axis type
template <class T> struct axis;
template <> struct axis<XAxisType> { static constexpr unsigned i = 0, m = b3vu::X; };
template <> struct axis<YAxisType> { static constexpr unsigned i = 1, m = b3vu::Y; };
template <> struct axis<ZAxisType> { static constexpr unsigned i = 2, m = b3vu::Z; };
template <> struct axis<WAxisType> { static constexpr unsigned i = 3, m = b3vu::W; };
template <class T> inline constexpr bool is_lane = std::is_same_v<T, XAxisType> || std::is_same_v<T, YAxisType> ||
                                             std::is_same_v<T, ZAxisType> || std::is_same_v<T, WAxisType>;
// v{op}{B}.{A} r, a, b  ->  r.A = a.A op b.B
template <class A, class B> inline A lmul(const A& a, const B& b) { return A(b3vu::mul<axis<A>::m>(a.lVector, a.lVector, b3vu::bc<axis<B>::i>(b.lVector))); }
template <class A, class B> inline A ladd(const A& a, const B& b) { return A(b3vu::add<axis<A>::m>(a.lVector, a.lVector, b3vu::bc<axis<B>::i>(b.lVector))); }
template <class A, class B> inline A lsub(const A& a, const B& b) { return A(b3vu::sub<axis<A>::m>(a.lVector, a.lVector, b3vu::bc<axis<B>::i>(b.lVector))); }
// qmtc2 f; v{op}x.{A} r, a, s.x
template <class A> inline A lmulf(const A& a, float f) { return A(b3vu::mul<axis<A>::m>(a.lVector, a.lVector, b3vu::splat(f))); }
template <class A> inline A laddf(const A& a, float f) { return A(b3vu::add<axis<A>::m>(a.lVector, a.lVector, b3vu::splat(f))); }
template <class A> inline A lsubf(const A& a, float f) { return A(b3vu::sub<axis<A>::m>(a.lVector, a.lVector, b3vu::splat(f))); }
// qmtc2 f -> s; vsub{B}.x r, s, b.B  ->  r.x = f - b.B
template <class B> inline XAxisType fsubl(float f, const B& b) { const u_long128 s = b3vu::splat(f); return XAxisType(b3vu::sub<b3vu::X>(s, s, b3vu::bc<axis<B>::i>(b.lVector))); }
// vadd{B}.{D} d, vf0, b.B  (the lane setters): d.D = 0 + b.B
// lane-noise (2026-11-11): a setter that writes some lanes keeps the others from the object's own bytes. In a fresh
// trivial local (`CGtV3d v; v.SetX(x); v.SetY(y); v.SetZ(z);`) those bytes are -ftrivial-auto-var-init's zero, but
// GCC optimises as if they were uninitialised (its manual): once SRA splits the local into lanes, the w lane takes
// whatever value suits the optimiser -- another path's constant, a register's leftover -- and that moved with any edit
// of a header every unit includes [C: the ps2float_ref.h rewrite, bit-identical results, turned the w lane of
// IntersectLineNearest's track vertices (GetGtV3d / GetOffset, 0x273BF0) from 0 into 6724.0, which ResetOnTrack
// wrote into the car matrix's mPos.w and on into 145 words of MB 5/6/29/30 by race VBlank 20801]. The empty asm makes
// the object opaque memory: the zero store stands and the kept lanes read it, as on the PS2 the instruction keeps
// what the register held. (-fno-tree-sra did the same for every local but cost ~3 % of the race's speed.)
inline const u_long128& defined(u_long128& v) {
    asm("" : "+m"(v));
    return v;
}
template <unsigned D, unsigned B> inline u_long128 set_add(const u_long128& d, const u_long128& b) { return b3vu::add<D>(d, b3vu::vf0(), b3vu::bc<B>(b)); }
// vmul{B}.w d, vf0, b.B: d.w = 1 * b.B
template <unsigned B> inline u_long128 set_w_mul(const u_long128& d, const u_long128& b) { return b3vu::mul<b3vu::W>(d, b3vu::vf0(), b3vu::bc<B>(b)); }
}  // namespace detail

// ---- lane arithmetic (decomp GtMathPs2.h / GtMathPs2Quat.h free operators; [C] one VU op each) ----
inline XAxisType operator*(XAxisType x, XAxisType y) { return detail::lmul(x, y); }
inline XAxisType operator*(XAxisType x, YAxisType y) { return detail::lmul(x, y); }
inline XAxisType operator*(XAxisType x, ZAxisType y) { return detail::lmul(x, y); }
inline XAxisType operator*(XAxisType x, WAxisType y) { return detail::lmul(x, y); }
inline YAxisType operator*(YAxisType x, XAxisType y) { return detail::lmul(x, y); }
inline YAxisType operator*(YAxisType x, YAxisType y) { return detail::lmul(x, y); }
inline YAxisType operator*(YAxisType x, ZAxisType y) { return detail::lmul(x, y); }
inline YAxisType operator*(YAxisType x, WAxisType y) { return detail::lmul(x, y); }
inline ZAxisType operator*(ZAxisType x, XAxisType y) { return detail::lmul(x, y); }
inline ZAxisType operator*(ZAxisType x, YAxisType y) { return detail::lmul(x, y); }
inline ZAxisType operator*(ZAxisType x, ZAxisType y) { return detail::lmul(x, y); }
inline WAxisType operator*(WAxisType x, XAxisType y) { return detail::lmul(x, y); }
inline WAxisType operator*(WAxisType x, YAxisType y) { return detail::lmul(x, y); }
inline WAxisType operator*(WAxisType x, ZAxisType y) { return detail::lmul(x, y); }
inline WAxisType operator*(WAxisType x, WAxisType y) { return detail::lmul(x, y); }
inline XAxisType operator*(XAxisType a, float f) { return detail::lmulf(a, f); }
inline YAxisType operator*(YAxisType a, float f) { return detail::lmulf(a, f); }
inline ZAxisType operator*(ZAxisType a, float f) { return detail::lmulf(a, f); }
inline WAxisType operator*(WAxisType a, float f) { return detail::lmulf(a, f); }

inline XAxisType operator+(XAxisType a, XAxisType b) { return detail::ladd(a, b); }
inline XAxisType operator+(XAxisType x, YAxisType y) { return detail::ladd(x, y); }
inline XAxisType operator+(XAxisType x, ZAxisType y) { return detail::ladd(x, y); }
inline YAxisType operator+(YAxisType a, YAxisType b) { return detail::ladd(a, b); }
inline YAxisType operator+(YAxisType x, XAxisType y) { return detail::ladd(x, y); }
inline YAxisType operator+(YAxisType x, ZAxisType y) { return detail::ladd(x, y); }
inline ZAxisType operator+(ZAxisType a, ZAxisType b) { return detail::ladd(a, b); }
inline ZAxisType operator+(ZAxisType x, XAxisType y) { return detail::ladd(x, y); }
inline ZAxisType operator+(ZAxisType x, YAxisType y) { return detail::ladd(x, y); }
inline WAxisType operator+(WAxisType a, WAxisType b) { return detail::ladd(a, b); }
inline XAxisType operator+(XAxisType a, float f) { return detail::laddf(a, f); }
inline YAxisType operator+(YAxisType a, float f) { return detail::laddf(a, f); }
inline ZAxisType operator+(ZAxisType a, float f) { return detail::laddf(a, f); }

inline XAxisType operator-(XAxisType a, XAxisType b) { return detail::lsub(a, b); }
inline XAxisType operator-(XAxisType x, YAxisType y) { return detail::lsub(x, y); }
inline XAxisType operator-(XAxisType x, ZAxisType y) { return detail::lsub(x, y); }
inline XAxisType operator-(XAxisType x, WAxisType y) { return detail::lsub(x, y); }
inline YAxisType operator-(YAxisType a, YAxisType b) { return detail::lsub(a, b); }
inline YAxisType operator-(YAxisType x, XAxisType y) { return detail::lsub(x, y); }
inline YAxisType operator-(YAxisType x, ZAxisType y) { return detail::lsub(x, y); }
inline ZAxisType operator-(ZAxisType a, ZAxisType b) { return detail::lsub(a, b); }
inline ZAxisType operator-(ZAxisType x, XAxisType y) { return detail::lsub(x, y); }
inline ZAxisType operator-(ZAxisType x, YAxisType y) { return detail::lsub(x, y); }
inline YAxisType operator-(YAxisType a, float f) { return detail::lsubf(a, f); }
inline ZAxisType operator-(ZAxisType a, float f) { return detail::lsubf(a, f); }
inline XAxisType operator-(float f, XAxisType b) { return detail::fsubl(f, b); }
inline XAxisType operator-(float f, YAxisType b) { return detail::fsubl(f, b); }
inline XAxisType operator-(float f, WAxisType b) { return detail::fsubl(f, b); }
// gen1 [C] retail's GtMathPs2::operator -(float, ZAxisType) (the decomp's docs/gtmathps2_api.md, B3O0PCH:80): qmtc2 f; vsubz.x
// (GetHullPoints 0x3DA234 `vsubz.x $vf7, $vf9, $vf7z`, DrawPositionMarker 0x15391C); it had fallen to host float
inline XAxisType operator-(float f, ZAxisType b) { return detail::fsubl(f, b); }
// gen1: retail's operator +(YAxisType, WAxisType), +(ZAxisType, WAxisType), +(WAxisType, YAxisType) (the decomp's gtmathps2_api.md):
// vaddw.y / vaddw.z [C retail UpdateTrafficWorldCollisionEffects 0x258490 and UpdateRaceCarWorldCollisionEffects 0x2594C0
// vaddw.y, _RenderDistort 0x3CF300 and UpdateCamera 0x4256C0 vaddw.z], vaddy.w [L: the ladd family]; with none here they
// had fallen to host float
inline YAxisType operator+(YAxisType x, WAxisType y) { return detail::ladd(x, y); }
inline ZAxisType operator+(ZAxisType x, WAxisType y) { return detail::ladd(x, y); }
inline WAxisType operator+(WAxisType x, YAxisType y) { return detail::ladd(x, y); }
// gen1: division with a lane operand. Retail's API has no lane operator / (the decomp's gtmathps2_api.md: CGtV2d / V3d / V4d only), so
// MWCC converts the lane to float and divides with div.s [C: div.s and no vdiv in each function with such a site:
// PreBuildSpheremapWarpData 0x2298B0, IntersectLine 0x2BF5F0, func_003D9360, BodyBodyResponse 0x1B4BF0,
// DoDrivingWorldCollision 0x1C5430]: the host's ps2f division, not host IEEE through the float conversion.
#define B3GTMATH_LANE_DIV(L) \
    inline float operator/(L a, float b) { return b3fp::div(float(a), b); } \
    inline float operator/(float a, L b) { return b3fp::div(a, float(b)); } \
    inline float operator/(L a, XAxisType b) { return b3fp::div(float(a), float(b)); } \
    inline float operator/(L a, YAxisType b) { return b3fp::div(float(a), float(b)); } \
    inline float operator/(L a, ZAxisType b) { return b3fp::div(float(a), float(b)); } \
    inline float operator/(L a, WAxisType b) { return b3fp::div(float(a), float(b)); }
B3GTMATH_LANE_DIV(XAxisType)
B3GTMATH_LANE_DIV(YAxisType)
B3GTMATH_LANE_DIV(ZAxisType)
B3GTMATH_LANE_DIV(WAxisType)
#undef B3GTMATH_LANE_DIV
// gen1: float * lane and float + lane: the FPU op on the two values (mul.s / add.s), as MWCC compiles them with the
// decomp's include/b3/GtMathPs2.h, which declares neither [C matched B3EffectsManager_00257C70 `field_1F84C * W()`:
// mul.s only; the 33 FPU-form sites of decomp 4b3318f2ac]. Retail's API lists them (the decomp's gtmathps2_api.md), but no site of a
// VU form is known; one would get its operator here from its retail asm. Before: host IEEE through the float conversion.
#define B3GTMATH_SCALAR_LEFT_FPU(L) \
    inline float operator*(float a, L b) { return b3fp::mul(a, float(b)); } \
    inline float operator+(float a, L b) { return b3fp::add(a, float(b)); }
B3GTMATH_SCALAR_LEFT_FPU(XAxisType)
B3GTMATH_SCALAR_LEFT_FPU(YAxisType)
B3GTMATH_SCALAR_LEFT_FPU(ZAxisType)
B3GTMATH_SCALAR_LEFT_FPU(WAxisType)
#undef B3GTMATH_SCALAR_LEFT_FPU
// gen1: lane -/+ float where nothing was declared (host IEEE before). X - f: qmtc2 f; vsubx.x, W + f: vaddx.w [C per
// site, every one known: X - f ApplyDrivingForces 0x1C1E00, CalcSpeed 0x16290C (2.5f by lui 0x4020),
// CollisionIsCameraInVehicle 0x2F7514 / 0x2F78CC / 0x2F7AB0, DrawPositionMarker 0x153B94 / 0x153DD8 / 0x153F48; W + f
// ApplyDrivingForces 0x1C1F30, CollisionIsCameraInVehicle 0x2F7480 / 0x2F7708; retail's API declares both]. W - f: sub.s [C
// matched B3AIDriver_00145A10 `field_B0.W() - D_004E0EC8`: the FPU op; the decomp header declares no W - float].
inline XAxisType operator-(XAxisType a, float f) { return detail::lsubf(a, f); }
inline WAxisType operator+(WAxisType a, float f) { return detail::laddf(a, f); }
inline float operator-(WAxisType a, float f) { return b3fp::sub(float(a), f); }
// gen1: lane op lane with no operator of its own, pinned now that float * / + / - lane and lane -/+ float all exist (they
// would be ambiguous), each to what it resolved to before [G: retail's form not checked; these sites do not change]:
// - Y / Z / W - lane: the left one's float conversion into float - lane (qmtc2; vsub{B}.x);
// - X + W, Z * W: the right one's float conversion into lane op float (vaddx.x / vmulx.z);
// - W + X, W + Z, W - Z (host IEEE before): the FPU op on the values [L: the decomp header declares none of them].
#define B3GTMATH_FSUB_AS_BEFORE(A, B) inline XAxisType operator-(A a, B b) { return detail::fsubl(float(a), b); }
B3GTMATH_FSUB_AS_BEFORE(YAxisType, WAxisType)
B3GTMATH_FSUB_AS_BEFORE(ZAxisType, WAxisType)
B3GTMATH_FSUB_AS_BEFORE(WAxisType, XAxisType)
B3GTMATH_FSUB_AS_BEFORE(WAxisType, YAxisType)
B3GTMATH_FSUB_AS_BEFORE(WAxisType, WAxisType)
#undef B3GTMATH_FSUB_AS_BEFORE
inline XAxisType operator+(XAxisType a, WAxisType b) { return detail::laddf(a, float(b)); }
inline ZAxisType operator*(ZAxisType a, WAxisType b) { return detail::lmulf(a, float(b)); }
inline float operator+(WAxisType a, XAxisType b) { return b3fp::add(float(a), float(b)); }
inline float operator+(WAxisType a, ZAxisType b) { return b3fp::add(float(a), float(b)); }
inline float operator-(WAxisType a, ZAxisType b) { return b3fp::sub(float(a), float(b)); }
// gen1: an integer (or double) left operand: converted to float, then the FPU op, as MWCC with the decomp header (no
// such operator: the built-in one after cvt.s.w) [L]; host IEEE before; without these it would be ambiguous here
#define B3GTMATH_ARITH_LEFT(op, fn) \
    template <class I, class L> requires(std::is_arithmetic_v<I> && !std::is_same_v<I, float> && !std::is_same_v<I, bool> && \
                                         detail::is_lane<L>) \
    inline float operator op(I a, L b) { return fn(static_cast<float>(a), float(b)); }
B3GTMATH_ARITH_LEFT(+, b3fp::add)
B3GTMATH_ARITH_LEFT(-, b3fp::sub)
B3GTMATH_ARITH_LEFT(*, b3fp::mul)
B3GTMATH_ARITH_LEFT(/, b3fp::div)
#undef B3GTMATH_ARITH_LEFT
// and W + an integer (the built-in operator before W + float existed): the FPU op [L]
template <class I> requires(std::is_arithmetic_v<I> && !std::is_same_v<I, float> && !std::is_same_v<I, bool>)
inline float operator+(WAxisType a, I b) { return b3fp::add(float(a), static_cast<float>(b)); }

// [C] vsub.x r, vf0, a (0 - x: +0 stays +0)
inline XAxisType XAxisType::operator-() const { return XAxisType(b3vu::sub<b3vu::X>(lVector, b3vu::vf0(), lVector)); }
// [C] vsub.y r, vf0, a
inline YAxisType YAxisType::operator-() const { return YAxisType(b3vu::sub<b3vu::Y>(lVector, b3vu::vf0(), lVector)); }
// [C] qmtc2 0xBF800000 -> vf2; vmulx.z t, t, vf2x (times -1: +0 becomes -0)
inline ZAxisType ZAxisType::operator-() const { return ZAxisType(b3vu::mul<b3vu::Z>(lVector, lVector, b3vu::splat(-1.0f))); }
// [C] vsubw.x r, vf0, a.w
inline XAxisType WAxisType::operator-() const { return XAxisType(b3vu::sub<b3vu::X>(lVector, b3vu::vf0(), b3vu::bc<3>(lVector))); }

// ---------------------------------------------------------------------------------------------------------------
// CGtV3d [C layout: Revenge stabs: only `u_long128 mData`]
// ---------------------------------------------------------------------------------------------------------------
struct CGtV3d {
    u_long128 mData;
    CGtV3d() = default;   // trivial (uninitialized, as before): a goto may cross it (decomp T8 cases)
    CGtV3d(u_long128 v) : mData(v) {}
    // [L] three floats through a stack temporary; its w word is undefined in retail -> 0 here [G]
    CGtV3d(float x, float y, float z) : mData(b3vu::make(x, y, z, 0.0f)) {}
    u_long128 GetRawData() const { return mData; }
    // [C] vadd.xyz r, vf0, vf0 (w undefined in retail; kept here [G])
    void Zero() { mData = b3vu::add<b3vu::XYZ>(detail::defined(mData), b3vu::vf0(), b3vu::vf0()); }
    // [C] vsub.xyz t, this, o
    CGtV3d operator-(const CGtV3d& o) const { return CGtV3d(b3vu::sub<b3vu::XYZ>(mData, mData, o.mData)); }
    XAxisType GetSquaredLength() const;
    XAxisType GetSquaredLength(u_long128 one) const;
    void SetX(float x);
    void SetY(float y);
    void SetZ(float z);
    void Set(float x, float y, float z);
    void Set(XAxisType x, YAxisType y, ZAxisType z);
    void Normalise();
    XAxisType NormaliseGetLength();
    XAxisType GetLength() const;
    bool IsZero() const;
    bool IsEffectivelyZero() const;
    XAxisType NormaliseFast();
    CGtV3d operator-() const;
    CGtV3d operator+(const CGtV3d& o) const;
    CGtV3d& operator+=(CGtV3d o);
    CGtV3d& operator-=(CGtV3d o);
    CGtV3d operator*(float s) const;
    XAxisType X() const { return XAxisType(mData); }
    YAxisType Y() const { return YAxisType(mData); }
    ZAxisType Z() const { return ZAxisType(mData); }
    void SetX(XAxisType a) { mData = detail::set_add<b3vu::X, 0>(detail::defined(mData), a.lVector); }
    void SetY(YAxisType a) { mData = detail::set_add<b3vu::Y, 1>(detail::defined(mData), a.lVector); }
    void SetZ(ZAxisType a) { mData = detail::set_add<b3vu::Z, 2>(detail::defined(mData), a.lVector); }
    void SetX(YAxisType a) { mData = detail::set_add<b3vu::X, 1>(detail::defined(mData), a.lVector); }
    void SetX(ZAxisType a) { mData = detail::set_add<b3vu::X, 2>(detail::defined(mData), a.lVector); }
    void SetY(XAxisType a) { mData = detail::set_add<b3vu::Y, 0>(detail::defined(mData), a.lVector); }
    void SetY(ZAxisType a) { mData = detail::set_add<b3vu::Y, 2>(detail::defined(mData), a.lVector); }
    void SetZ(XAxisType a) { mData = detail::set_add<b3vu::Z, 0>(detail::defined(mData), a.lVector); }
    void SetZ(YAxisType a) { mData = detail::set_add<b3vu::Z, 1>(detail::defined(mData), a.lVector); }
    CGtV3d operator*(CGtV3d o) const { return CGtV3d(b3vu::mul<b3vu::XYZ>(mData, mData, o.mData)); }
    CGtV3d operator*(XAxisType a) const { return CGtV3d(b3vu::mul<b3vu::XYZ>(mData, mData, b3vu::bc<0>(a.lVector))); }
    CGtV3d operator*(YAxisType a) const { return CGtV3d(b3vu::mul<b3vu::XYZ>(mData, mData, b3vu::bc<1>(a.lVector))); }
    CGtV3d operator*(ZAxisType a) const { return CGtV3d(b3vu::mul<b3vu::XYZ>(mData, mData, b3vu::bc<2>(a.lVector))); }
    CGtV3d& operator*=(ZAxisType a) { mData = b3vu::mul<b3vu::XYZ>(mData, mData, b3vu::bc<2>(a.lVector)); return *this; }
    CGtV3d& operator+=(XAxisType a) { mData = b3vu::add<b3vu::XYZ>(mData, mData, b3vu::bc<0>(a.lVector)); return *this; }
};

// ---------------------------------------------------------------------------------------------------------------
// CGtV4d
// ---------------------------------------------------------------------------------------------------------------
struct CGtV4d {
    union { float f[4]; u_long128 mData; };
    CGtV4d() = default;   // trivial, as CGtV3d
    CGtV4d(u_long128 d) : mData(d) {}
    CGtV4d(float x, float y, float z, float w) : mData(b3vu::make(x, y, z, w)) {}
    // Defaulted (a byte copy of the 16 bytes, as the explicit mData copy was): trivially copyable, so a CGtV4d can be
    // passed by value to guest code (b3host/call.h; native2: 76 functions needed that).
    CGtV4d(const CGtV4d& o) = default;
    CGtV4d& operator=(const CGtV4d& o) = default;
    // [C] qmtc2 s -> vf2; vmulx.xyzw t, this, vf2x
    CGtV4d operator*(float s) const { return CGtV4d(b3vu::mul<b3vu::XYZW>(mData, mData, b3vu::splat(s))); }
    // [C] vmul.xyzw t, this, o
    CGtV4d operator*(const CGtV4d o) const { return CGtV4d(b3vu::mul<b3vu::XYZW>(mData, mData, o.mData)); }
    // [C] vsub.xyzw t, this, o
    CGtV4d operator-(const CGtV4d o) const { return CGtV4d(b3vu::sub<b3vu::XYZW>(mData, mData, o.mData)); }
    // [C decomp include/b3/GtMathPs2.h gapO: API B3O0PCH map CGtV4d::operator +(CGtV4d), 0x164790] vadd.xyzw t, this, o
    CGtV4d operator+(const CGtV4d o) const { return CGtV4d(b3vu::add<b3vu::XYZW>(mData, mData, o.mData)); }
    u_long128 GetRawData() const { return mData; }
    // [C] vsub.xyzw vf1, vf0, vf0
    void Zero() { mData = b3vu::sub<b3vu::XYZW>(mData, b3vu::vf0(), b3vu::vf0()); }
    // [C] qmtc2 a; vmulx.w this, vf0, s.x  (w = 1 * a)
    void SetA(float a) { mData = b3vu::mul<b3vu::W>(detail::defined(mData), b3vu::vf0(), b3vu::splat(a)); }
    // [C] vsubw.w this, vf0, vf0w (w = 0); vaddx.w this, this, s.x
    void SetW(float w) { u_long128 t = b3vu::sub<b3vu::W>(detail::defined(mData), b3vu::vf0(), b3vu::bc<3>(b3vu::vf0())); mData = b3vu::add<b3vu::W>(t, t, b3vu::splat(w)); }
    // [C] vaddx.{x,y,z} vf1, vf0, vf2x on a copy of this
    void SetX(float v) { mData = b3vu::add<b3vu::X>(detail::defined(mData), b3vu::vf0(), b3vu::splat(v)); }
    void SetY(float v) { mData = b3vu::add<b3vu::Y>(detail::defined(mData), b3vu::vf0(), b3vu::splat(v)); }
    void SetZ(float v) { mData = b3vu::add<b3vu::Z>(detail::defined(mData), b3vu::vf0(), b3vu::splat(v)); }
    XAxisType X() const { return XAxisType(mData); }
    YAxisType Y() const { return YAxisType(mData); }
    ZAxisType Z() const { return ZAxisType(mData); }
    WAxisType W() const { return WAxisType(mData); }
    void SetX(XAxisType a) { mData = detail::set_add<b3vu::X, 0>(detail::defined(mData), a.lVector); }
    void SetY(YAxisType a) { mData = detail::set_add<b3vu::Y, 1>(detail::defined(mData), a.lVector); }
    void SetZ(ZAxisType a) { mData = detail::set_add<b3vu::Z, 2>(detail::defined(mData), a.lVector); }
    void SetW(XAxisType a) { mData = detail::set_w_mul<0>(detail::defined(mData), a.lVector); }   // [C] vmulx.w this, vf0, a.x
    void SetW(YAxisType a) { mData = detail::set_w_mul<1>(detail::defined(mData), a.lVector); }
    void SetW(ZAxisType a) { mData = detail::set_w_mul<2>(detail::defined(mData), a.lVector); }
    void SetW(WAxisType a) { mData = detail::set_w_mul<3>(detail::defined(mData), a.lVector); }
    void SetX(ZAxisType a) { mData = detail::set_add<b3vu::X, 2>(detail::defined(mData), a.lVector); }
    void SetY(XAxisType a) { mData = detail::set_add<b3vu::Y, 0>(detail::defined(mData), a.lVector); }
    void SetZ(YAxisType a) { mData = detail::set_add<b3vu::Z, 1>(detail::defined(mData), a.lVector); }
    CGtV4d& operator*=(float s) { mData = b3vu::mul<b3vu::XYZW>(mData, mData, b3vu::splat(s)); return *this; }
};

// ---------------------------------------------------------------------------------------------------------------
// Matrices [C layout: Revenge stabs: rows of CGtV3d]
// ---------------------------------------------------------------------------------------------------------------
struct CGtMatrix3x3 {
    CGtV3d mRight, mUp, mAt;
};

struct CGtMatrix3x4 {
    CGtV3d mRight, mUp, mAt, mPos;
    // lane-noise (2026-11-11): retail's constructor is empty. In host memory the object starts zeroed instead of
    // holding host stack bytes: a function returning a matrix by name (`CGtMatrix3x4 m; ... return m;`) builds it in
    // its caller's return slot, which -ftrivial-auto-var-init never zeroes, and its rows are mostly written x/y/z only
    // -- func_003090A0's wheel spin / steer matrices left a host address's high half in the w lanes of the traffic
    // wheel matrices (0x55D800 ->) and the vehicle VIF packets (race VBlank 23001, crash 23101). In guest memory (a
    // T31 guest-stack local, a member of a guest object) the bytes stay as found, as on the PS2.
    CGtMatrix3x4() { b3host::zero_if_host(this, sizeof *this); }
    void SetIdentity();
    CGtMatrix3x4 operator*(const CGtMatrix3x4& m) const;
    CGtMatrix3x4& operator*=(const CGtMatrix3x4& m) { *this = *this * m; return *this; }   // gen1 (decomp gapN)
    void OrthoNormalise();
    void Invert();
};

namespace detail {
// ACC.xyz = r * v.x; ACC += u * v.y; then `last` (vmaddz into a register holding `base`, or vmaddaz + vmaddw with
// the position) -- the shared shape of TransformPoint/TransformVector/operator* [C decomp asm]
inline u_long128 rows3(const u_long128& r, const u_long128& u, const u_long128& v) {
    u_long128 acc = b3vu::mul<b3vu::XYZ>(r, r, b3vu::bc<0>(v));
    return b3vu::madd<b3vu::XYZ>(acc, acc, u, b3vu::bc<1>(v));
}
}  // namespace detail

// [C] vmulax.xyz ACC, R, v.x; vmadday.xyz ACC, U, v.y; vmaddaz.xyz ACC, A, v.z; vf1 = Pos; vmaddw.xyz vf1, vf1, vf0w
inline CGtV3d TransformPoint(const CGtMatrix3x4& m, CGtV3d v) {
    u_long128 acc = detail::rows3(m.mRight.mData, m.mUp.mData, v.mData);
    acc = b3vu::madd<b3vu::XYZ>(acc, acc, m.mAt.mData, b3vu::bc<2>(v.mData));
    return CGtV3d(b3vu::madd<b3vu::XYZ>(m.mPos.mData, acc, m.mPos.mData, b3vu::bc<3>(b3vu::vf0())));
}
// [C] vmulax.xyz ACC, R, v.x; vmadday.xyz ACC, U, v.y; vf1 = A; vmaddz.xyz vf1, vf1, v.z  (w = A.w)
inline CGtV3d TransformVector(const CGtMatrix3x4& m, CGtV3d v) {
    const u_long128 acc = detail::rows3(m.mRight.mData, m.mUp.mData, v.mData);
    return CGtV3d(b3vu::madd<b3vu::XYZ>(m.mAt.mData, acc, m.mAt.mData, b3vu::bc<2>(v.mData)));
}
// gen1 (decomp GtMathPs2.h gapN, TransformVector(const CGtMatrix3x3&, CGtV3d) 0x1A6130): the 3x4 form's instructions,
// vmulax.xyz ACC, R, v.x; vf2 = U; vmadday.xyz ACC, vf2, v.y; vf2 = A; vmaddz.xyz vf2, vf2, v.z  (w = A.w)
inline CGtV3d TransformVector(const CGtMatrix3x3& m, CGtV3d v) {
    const u_long128 acc = detail::rows3(m.mRight.mData, m.mUp.mData, v.mData);
    return CGtV3d(b3vu::madd<b3vu::XYZ>(m.mAt.mData, acc, m.mAt.mData, b3vu::bc<2>(v.mData)));
}
// gen1 (decomp gapN, TransformPoints(const CGtMatrix3x4&, CGtV3d*, const CGtV3d*, int) 0x1BF720): one TransformPoint each
inline void TransformPoints(const CGtMatrix3x4& m, CGtV3d* dst, const CGtV3d* src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = TransformPoint(m, src[i]);
}

// [C] each row of this through m's R/U/A (vmulax/vmadday/vmaddz into the register holding the row: w = row.w); the
// position row also adds m.mPos (vmaddaz, then vf1 = m.mPos, vmaddw.xyz vf1, vf1, vf0w: w = m.mPos.w)
inline CGtMatrix3x4 CGtMatrix3x4::operator*(const CGtMatrix3x4& m) const {
    CGtMatrix3x4 r;
    const CGtV3d* rows[3] = {&mRight, &mUp, &mAt};
    CGtV3d* out[3] = {&r.mRight, &r.mUp, &r.mAt};
    for (int i = 0; i < 3; ++i) {
        const u_long128 row = rows[i]->mData;
        const u_long128 acc = detail::rows3(m.mRight.mData, m.mUp.mData, row);
        out[i]->mData = b3vu::madd<b3vu::XYZ>(row, acc, m.mAt.mData, b3vu::bc<2>(row));
    }
    u_long128 acc = detail::rows3(m.mRight.mData, m.mUp.mData, mPos.mData);
    acc = b3vu::madd<b3vu::XYZ>(acc, acc, m.mAt.mData, b3vu::bc<2>(mPos.mData));
    r.mPos.mData = b3vu::madd<b3vu::XYZ>(m.mPos.mData, acc, m.mPos.mData, b3vu::bc<3>(b3vu::vf0()));
    return r;
}
inline CGtMatrix3x4 Multiply(const CGtMatrix3x4& a, const CGtMatrix3x4& b) { return a * b; }

// ---------------------------------------------------------------------------------------------------------------
// CGtV3d bodies
// ---------------------------------------------------------------------------------------------------------------
namespace detail {
// x*x+y*y+z*z on the VU: vmul.xyz sq; one.x = 0 + 1; vadday.x ACC, sq, sq.y; vmaddz.x t, one, sq.z  [C]
inline u_long128 sum_xyz(const u_long128& sq, const u_long128& one) {
    const u_long128 acc = b3vu::add<b3vu::X>(sq, sq, b3vu::bc<1>(sq));
    return b3vu::madd<b3vu::X>(sq, acc, one, b3vu::bc<2>(sq));
}
inline u_long128 one_x() { return b3vu::add<b3vu::X>(b3vu::vf0(), b3vu::vf0(), b3vu::bc<3>(b3vu::vf0())); }   // vaddw.x vf0, vf0w
}  // namespace detail

inline XAxisType CGtV3d::GetSquaredLength() const {
    return XAxisType(detail::sum_xyz(b3vu::mul<b3vu::XYZ>(mData, mData, mData), detail::one_x()));
}
// [C decomp FAKEMATCH variant] the caller supplies the 1.0 vector
inline XAxisType CGtV3d::GetSquaredLength(u_long128 one) const {
    return XAxisType(detail::sum_xyz(b3vu::mul<b3vu::XYZ>(mData, mData, mData), one));
}
inline XAxisType DotProduct(CGtV3d a, CGtV3d b) {
    return XAxisType(detail::sum_xyz(b3vu::mul<b3vu::XYZ>(a.mData, a.mData, b.mData), detail::one_x()));
}

inline void CGtV3d::SetX(float x) { mData = b3vu::add<b3vu::X>(detail::defined(mData), b3vu::vf0(), b3vu::splat(x)); }   // [C] vaddx.x this, vf0, s.x
inline void CGtV3d::SetY(float y) { mData = b3vu::add<b3vu::Y>(detail::defined(mData), b3vu::vf0(), b3vu::splat(y)); }
inline void CGtV3d::SetZ(float z) { mData = b3vu::add<b3vu::Z>(detail::defined(mData), b3vu::vf0(), b3vu::splat(z)); }
inline void CGtV3d::Set(float x, float y, float z) { SetX(x); SetY(y); SetZ(z); }
inline void CGtV3d::Set(XAxisType x, YAxisType y, ZAxisType z) { SetX(x); SetY(y); SetZ(z); }

inline CGtV3d CGtV3d::operator-() const { return CGtV3d(b3vu::sub<b3vu::XYZ>(mData, b3vu::vf0(), mData)); }   // [C] vsub.xyz t, vf0, this
// [C] vf1 = o, vf2 = this; vadd.xyz vf1, vf2, vf1 -> w = o.w
inline CGtV3d CGtV3d::operator+(const CGtV3d& o) const { return CGtV3d(b3vu::add<b3vu::XYZ>(o.mData, mData, o.mData)); }
inline CGtV3d& CGtV3d::operator+=(CGtV3d o) { mData = b3vu::add<b3vu::XYZ>(mData, mData, o.mData); return *this; }
inline CGtV3d& CGtV3d::operator-=(CGtV3d o) { mData = b3vu::sub<b3vu::XYZ>(mData, mData, o.mData); return *this; }
inline CGtV3d CGtV3d::operator*(float s) const { return CGtV3d(b3vu::mul<b3vu::XYZ>(mData, mData, b3vu::splat(s))); }

// [C] Q = vrsqrt(vf0w, l.x); vf1 = this; vmulq.xyz vf1, vf1, Q
inline void CGtV3d::Normalise() {
    const XAxisType l = GetSquaredLength();
    const float q = b3vu::q_rsqrt(1.0f, b3vu::lane(l.lVector, 0));
    mData = b3vu::mul<b3vu::XYZ>(mData, mData, b3vu::splat(q));
}
// [C] Q = vrsqrt(1, l.x) while the FPU computes sqrt.s(l.x); this.xyz *= Q; returns the sqrt.s length in x
inline XAxisType CGtV3d::NormaliseGetLength() {
    XAxisType l = GetSquaredLength();
    const float sq = b3vu::lane(l.lVector, 0);
    const float q = b3vu::q_rsqrt(1.0f, sq);
    l.lVector.w[0] = b3fp::bits(b3fp::sqrt(sq));
    mData = b3vu::mul<b3vu::XYZ>(mData, mData, b3vu::splat(q));
    return l;
}
// [C] Q = vsqrt(l.x); vf1.x = 1; vmulq.x vf1, vf1, Q
inline XAxisType CGtV3d::GetLength() const {
    const XAxisType l = GetSquaredLength();
    const float q = b3vu::q_sqrt(b3vu::lane(l.lVector, 0));
    const u_long128 one = b3vu::add<b3vu::X>(l.lVector, b3vu::vf0(), b3vu::bc<3>(b3vu::vf0()));
    return XAxisType(b3vu::mul<b3vu::X>(one, one, b3vu::splat(q)));
}
// [C] mtsab 4 / qfsrv / or: ((x | y | z) & 0x7FFFFFFF) == 0 -- raw bits, so a denormal lane is NOT zero
inline bool CGtV3d::IsZero() const { return ((mData.w[0] | mData.w[1] | mData.w[2]) & 0x7FFFFFFFu) == 0; }
// [C decomp] squared length < 2^-32 (c.lt.s)
inline bool CGtV3d::IsEffectivelyZero() const { return b3fp::lt(static_cast<float>(GetSquaredLength()), 2.3283064e-10f); }
// [C] Q = vrsqrt(1, l.x); this.xyz *= Q; returns 1 * Q (the reciprocal length) in x
inline XAxisType CGtV3d::NormaliseFast() {
    const XAxisType l = GetSquaredLength();
    const float q = b3vu::q_rsqrt(1.0f, b3vu::lane(l.lVector, 0));
    mData = b3vu::mul<b3vu::XYZ>(mData, mData, b3vu::splat(q));
    const u_long128 one = b3vu::add<b3vu::X>(l.lVector, b3vu::vf0(), b3vu::bc<3>(b3vu::vf0()));
    return XAxisType(b3vu::mul<b3vu::X>(one, one, b3vu::splat(q)));
}

// ---- free vector functions ----
// [C] vopmula.xyz ACC, a, b; vopmsub.xyz b, b, a -> result in b's register (w = b.w)
inline CGtV3d CrossProduct(CGtV3d a, CGtV3d b) {
    const u_long128 acc = b3vu::opmula(b.mData, a.mData, b.mData);
    return CGtV3d(b3vu::opmsub(b.mData, acc, b.mData, a.mData));
}
inline CGtV3d Min(CGtV3d a, CGtV3d b) { return CGtV3d(b3vu::min<b3vu::XYZ>(a.mData, a.mData, b.mData)); }   // [C] vmini.xyz
inline CGtV3d Max(CGtV3d a, CGtV3d b) { return CGtV3d(b3vu::max<b3vu::XYZ>(a.mData, a.mData, b.mData)); }   // [C] vmax.xyz
inline CGtV4d Min(CGtV4d a, CGtV4d b) { return CGtV4d(b3vu::min<b3vu::XYZW>(a.mData, a.mData, b.mData)); } // [C] vmini.xyzw
inline CGtV3d Abs(CGtV3d v) { return CGtV3d(b3vu::abs<b3vu::XYZ>(v.mData, v.mData)); }                      // [C] vabs.xyz
inline CGtV4d Abs(CGtV4d v) { return CGtV4d(b3vu::abs<b3vu::XYZW>(v.mData, v.mData)); }                   // gen1 (decomp gapN) vabs.xyzw
// gen1 (decomp gapN, CGtV3d::operator-(WAxisType) const): vsubw.xyz r, v, a.w (an "=j" output: w from the first source)
inline CGtV3d operator-(CGtV3d v, WAxisType a) { return CGtV3d(b3vu::sub<b3vu::XYZ>(v.mData, v.mData, b3vu::bc<3>(a.lVector))); }

// [C] dot clamped to [-1, 1]: vsubw.x m, vf0, vf0w (-1); vmax.x d, d, m; vminiw.x d, d, vf0w
inline XAxisType CosAngleBetweenNormalisedVectors(CGtV3d a, CGtV3d b) {
    XAxisType d = DotProduct(a, b);
    const u_long128 m = b3vu::sub<b3vu::X>(b3vu::vf0(), b3vu::vf0(), b3vu::bc<3>(b3vu::vf0()));
    d.lVector = b3vu::max<b3vu::X>(d.lVector, d.lVector, m);
    d.lVector = b3vu::min<b3vu::X>(d.lVector, d.lVector, b3vu::bc<3>(b3vu::vf0()));
    return d;
}

// [C] vaddax.xyzw ACC, a, vf0x; vmsubax.xyzw ACC, a, t; vf1 = b; vmaddx.xyzw vf1, vf1, t  (a - a*t + b*t)
inline CGtV4d Interpolate(CGtV4d a, CGtV4d b, float t) {
    const u_long128 tv = b3vu::splat(t);
    u_long128 acc = b3vu::add<b3vu::XYZW>(a.mData, a.mData, b3vu::bc<0>(b3vu::vf0()));
    acc = b3vu::msub<b3vu::XYZW>(acc, acc, a.mData, tv);
    return CGtV4d(b3vu::madd<b3vu::XYZW>(b.mData, acc, b.mData, tv));
}
// [C] as above on xyz; the result register was loaded with b (w = b.w)
inline CGtV3d Interpolate(CGtV3d a, CGtV3d b, float t) {
    const u_long128 tv = b3vu::splat(t);
    u_long128 acc = b3vu::add<b3vu::XYZ>(a.mData, a.mData, b3vu::bc<0>(b3vu::vf0()));
    acc = b3vu::msub<b3vu::XYZ>(acc, acc, a.mData, tv);
    return CGtV3d(b3vu::madd<b3vu::XYZ>(b.mData, acc, b.mData, tv));
}

// [C] decomp placeholder name (inlined normalise of OrthoNormalise/Matrix3x4SetLookAt): in place, w kept
inline void func_002EBED0_inl1(CGtV3d& v) {
    const XAxisType l = v.GetSquaredLength();
    const float q = b3vu::q_rsqrt(1.0f, b3vu::lane(l.lVector, 0));
    v.mData = b3vu::mul<b3vu::XYZ>(v.mData, v.mData, b3vu::splat(q));
}

// ---- CGtMatrix3x4 bodies ----
// [C] each row xyz = 0 (vadd.xyz r, vf0, vf0; w undefined in retail, kept here [G]); then the diagonal = 0 + 1
inline void CGtMatrix3x4::SetIdentity() {
    mRight.Zero(); mUp.Zero(); mAt.Zero(); mPos.Zero();
    mRight.mData = b3vu::add<b3vu::X>(mRight.mData, b3vu::vf0(), b3vu::bc<3>(b3vu::vf0()));
    mUp.mData = b3vu::add<b3vu::Y>(mUp.mData, b3vu::vf0(), b3vu::bc<3>(b3vu::vf0()));
    mAt.mData = b3vu::add<b3vu::Z>(mAt.mData, b3vu::vf0(), b3vu::bc<3>(b3vu::vf0()));
}

// [C decomp body, op for op]
inline void CGtMatrix3x4::OrthoNormalise() {
    CGtV3d lLengths(b3vu::vf0());
    lLengths.SetX(mRight.NormaliseGetLength());
    lLengths.SetY(mUp.NormaliseGetLength());
    lLengths.SetZ(mAt.NormaliseGetLength());
    auto pos = [](float f) { return b3fp::lt(0.0f, f); };
    auto lt = [](float a, float b) { return b3fp::lt(a, b); };
    if (pos(lLengths.X())) {
        if (pos(lLengths.Y())) {
            if (pos(lLengths.Z())) {
                CGtV3d lDots(b3vu::vf0());
                lDots.SetX(DotProduct(mUp, mAt));
                lDots.SetY(DotProduct(mAt, mRight));
                lDots.SetZ(DotProduct(mRight, mUp));
                lDots = Abs(lDots);
                if (lt(lDots.X(), lDots.Y())) {
                    if (lt(lDots.X(), lDots.Z())) {
                        mRight = CrossProduct(mUp, mAt); func_002EBED0_inl1(mRight);
                        mAt = CrossProduct(mRight, mUp); func_002EBED0_inl1(mAt);
                    } else {
                        mAt = CrossProduct(mRight, mUp); func_002EBED0_inl1(mAt);
                        mUp = CrossProduct(mAt, mRight); func_002EBED0_inl1(mUp);
                    }
                } else if (lt(lDots.Y(), lDots.Z())) {
                    mUp = CrossProduct(mAt, mRight); func_002EBED0_inl1(mUp);
                    mRight = CrossProduct(mUp, mAt); func_002EBED0_inl1(mRight);
                } else {
                    mAt = CrossProduct(mRight, mUp); func_002EBED0_inl1(mAt);
                    mUp = CrossProduct(mAt, mRight); func_002EBED0_inl1(mUp);
                }
            } else {
                mAt = CrossProduct(mRight, mUp); func_002EBED0_inl1(mAt);
                mUp = CrossProduct(mAt, mRight); func_002EBED0_inl1(mUp);
            }
        } else {
            mUp = CrossProduct(mAt, mRight); func_002EBED0_inl1(mUp);
            mRight = CrossProduct(mUp, mAt); func_002EBED0_inl1(mRight);
        }
    } else {
        mRight = CrossProduct(mUp, mAt); func_002EBED0_inl1(mRight);
        mAt = CrossProduct(mRight, mUp); func_002EBED0_inl1(mAt);
    }
}

// [C decomp body] transpose the rotation, then pos = -(R^T pos)
inline void CGtMatrix3x4::Invert() {
    CGtV3d lRight = mRight;
    CGtV3d lUp = mUp;
    CGtV3d lAt = mAt;
    mRight.SetY(lUp.X());
    mRight.SetZ(lAt.X());
    mUp.SetX(lRight.Y());
    mUp.SetZ(lAt.Y());
    mAt.SetX(lRight.Z());
    mAt.SetY(lUp.Z());
    CGtV3d lPos = mPos;
    CGtV3d lZ = mAt * lPos.Z();
    CGtV3d lX = mRight * lPos.X();
    CGtV3d lResult = lX + mUp * lPos.Y();
    lResult += lZ;
    mPos = -lResult;
}

// [C decomp body] mPos is not written (left as the caller's storage holds it)
inline CGtMatrix3x4 Matrix3x4SetLookAt(CGtV3d at, CGtV3d up) {
    CGtMatrix3x4 lReturnMatrix;
    lReturnMatrix.mAt = at;
    lReturnMatrix.mRight = CrossProduct(up, lReturnMatrix.mAt);
    func_002EBED0_inl1(lReturnMatrix.mRight);
    lReturnMatrix.mUp = CrossProduct(lReturnMatrix.mAt, lReturnMatrix.mRight);
    return lReturnMatrix;
}
inline CGtMatrix3x4 Matrix3x4SetLookAt(CGtV3d at) { return Matrix3x4SetLookAt(at, CGtV3d(0.0f, 1.0f, 0.0f)); }

// ---------------------------------------------------------------------------------------------------------------
// CGtV3dPlus / CGtPlane / CGtSphere [C layout: Revenge stabs]
// ---------------------------------------------------------------------------------------------------------------
struct CGtV3dPlus {
    u_long128 mData;
    CGtV3dPlus() { b3host::zero_if_host(this, sizeof *this); }   // lane-noise: as CGtMatrix3x4()
    void SetGtV3d(CGtV3d v) { mData = b3vu::add<b3vu::XYZ>(mData, b3vu::vf0(), v.mData); }   // [C] vadd.xyz m, vf0, v
    // [C] vsubw.w m, vf0, vf0w (w = 0); vaddx.w m, m, s.x
    void SetPlus(float f) { u_long128 t = b3vu::sub<b3vu::W>(mData, b3vu::vf0(), b3vu::bc<3>(b3vu::vf0())); mData = b3vu::add<b3vu::W>(t, t, b3vu::splat(f)); }
    void SetPlus(YAxisType y) { u_long128 t = b3vu::sub<b3vu::W>(mData, b3vu::vf0(), b3vu::bc<3>(b3vu::vf0())); mData = b3vu::add<b3vu::W>(t, t, b3vu::bc<1>(y.lVector)); }
    // gen1 (decomp GtMathPs2.h gapN): GtV3d() / Plus() as CGtPlane's getters; SetPlus(WAxisType) one `vmove.w D, S`
    CGtV3d GtV3d() const { return CGtV3d(mData); }
    WAxisType Plus() const { return WAxisType(mData); }
    void SetPlus(WAxisType a) { mData.w[3] = a.lVector.w[3]; }
    void Zero() { mData = b3vu::sub<b3vu::XYZW>(mData, b3vu::vf0(), b3vu::vf0()); }   // gen1 (decomp gapO): as CGtV4d::Zero
};
struct CGtPlane {
    u_long128 mData;
    CGtPlane() { b3host::zero_if_host(this, sizeof *this); }   // lane-noise: as CGtMatrix3x4()
    CGtV3d Normal() const { return CGtV3d(mData); }
    WAxisType Distance() const { return WAxisType(mData); }
    void SetNormal(CGtV3d v) { mData = b3vu::add<b3vu::XYZ>(mData, b3vu::vf0(), v.mData); }
    void SetDistance(float f) { mData = b3vu::mul<b3vu::W>(mData, b3vu::vf0(), b3vu::splat(f)); }        // [C] vmulx.w m, vf0, s.x
    void SetDistance(XAxisType d) { mData = b3vu::mul<b3vu::W>(mData, b3vu::vf0(), b3vu::bc<0>(d.lVector)); }
};
struct CGtSphere {
    u_long128 mData;
    CGtSphere() { b3host::zero_if_host(this, sizeof *this); }   // lane-noise: as CGtMatrix3x4()
    CGtV3d Position() const { return CGtV3d(mData); }
    WAxisType Radius() const { return WAxisType(mData); }
    void SetPosition(CGtV3d v) { mData = b3vu::add<b3vu::XYZ>(mData, b3vu::vf0(), v.mData); }
    void SetRadius(float f) { mData = b3vu::mul<b3vu::W>(mData, b3vu::vf0(), b3vu::splat(f)); }
};

}  // namespace GtMathPs2

#endif
