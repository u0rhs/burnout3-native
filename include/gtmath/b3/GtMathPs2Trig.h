// GtMathPs2Trig for the PC port: the decomp's include/b3/GtMathPs2Trig.h API on the host (see GtMathPs2.h here).
//
// CosFast4 [C decomp asm, constants from its ctc2 $vi21 (I register) writes]:
//   a = |v|; ACC = a * (-1/2pi); ACC -= 1.5*2^23; ACC += 1.5*2^23   (with truncation: -floor(a/2pi))
//   ACC -= a * (-1/2pi)  -> frac(a/2pi);  f = ACC - 0.5;  t = |f| - 0.25        (t in [-0.25, 0.25])
//   r = 2pi*t + ka*t^3 + kb*t^5 + k1*t^7 + kc*t^9  = sin(2pi t) = cos(v)
// The 1.5*2^23 step only floors under the VU's round-toward-zero; under host round-to-nearest it rounds, which puts
// t outside the polynomial's range. Fast mode therefore computes frac() with floor() [C reasoning above; tested].
#ifndef B3_GTMATHPS2TRIG_H
#define B3_GTMATHPS2TRIG_H

#include "b3/GtMathPs2.h"
#include "b3/GtMathPs2Quat.h"

#include <cmath>

namespace GtMathPs2 {

namespace detail {
inline u_long128 I(std::uint32_t bits) { return b3host::make_u128(bits, bits, bits, bits); }

inline u_long128 cos_fast4(const u_long128& v) {
    using namespace b3vu;
    const u_long128 a = abs<XYZW>(v, v);
    const u_long128 one = max<XYZW>(v, vf0(), bc<3>(vf0()));    // vmaxw.xyzw one, vf0, vf0w -> (1,1,1,1)
#if B3_PS2_FLOAT
    u_long128 acc = mul<XYZW>(a, a, I(0xBE22F983u));             // vmulai   ACC = a * (-1/2pi)
    acc = msub<XYZW>(acc, acc, one, I(0x4B400000u));             // vmsubai  ACC -= 1 * 1.5*2^23
    acc = madd<XYZW>(acc, acc, one, I(0x4B400000u));             // vmaddai  ACC += 1 * 1.5*2^23
    acc = msub<XYZW>(acc, acc, a, I(0xBE22F983u));               // vmsubai  ACC -= a * (-1/2pi)
#else
    u_long128 acc;
    for (int i = 0; i < 4; ++i) {
        const float u = lane(a, i) * 0.15915494f;                // -I
        acc.w[i] = b3fp::bits(u - std::floor(u));
    }
#endif
    const u_long128 f = msub<XYZW>(acc, acc, one, I(0x3F000000u));  // vmsubi  f = ACC - 0.5
    const u_long128 b = abs<XYZW>(f, f);
    const u_long128 t = sub<XYZW>(b, b, I(0x3E800000u));            // vsubi   t = b - 0.25
    const u_long128 t2 = mul<XYZW>(t, t, t);
    const u_long128 t4 = mul<XYZW>(t2, t2, t2);
    const u_long128 t8 = mul<XYZW>(t4, t4, t4);
    const u_long128 c1 = mul<XYZW>(t, t, I(0xC2992661u));
    const u_long128 c3 = mul<XYZW>(c1, c1, t2);
    const u_long128 ca = mul<XYZW>(t, t, I(0xC2255DE0u));
    const u_long128 cb = mul<XYZW>(t, t, I(0x42A33457u));
    const u_long128 cc = mul<XYZW>(t, t, I(0x421ED7B7u));
    acc = mul<XYZW>(ca, ca, t2);                                     // vmula
    acc = madd<XYZW>(acc, acc, c3, t4);                              // vmadda
    acc = madd<XYZW>(acc, acc, cb, t4);                              // vmadda
    acc = madd<XYZW>(acc, acc, t, I(0x40C90FDAu));                   // vmaddai (2pi)
    return madd<XYZW>(cc, acc, cc, t8);                              // vmadd
}
}  // namespace detail

inline CGtV4d CosFast4(CGtV4d v) { return CGtV4d(detail::cos_fast4(v.mData)); }

// [C decomp asm] v.x = a, v.z = b, v.y = v.x, v.w = 1 * v.z; xz -= pi/2 (vsubi.xz, I = 0x3FC90FDB); then CosFast4's
// body: (sin a, cos a, sin b, cos b). [L] the vsubi output shares v's register, so y/w keep v's lanes.
inline CGtV4d SinCosFast2(float a, float b) {
    using namespace b3vu;
    u_long128 v = vf0();   // retail: uninitialised CGtV4d; every lane used is written below
    v = add<X>(v, vf0(), splat(a));
    v = add<Z>(v, vf0(), splat(b));
    v = add<Y>(v, vf0(), bc<0>(v));
    v = mul<W>(v, vf0(), bc<2>(v));
    const u_long128 w = sub<XZ>(v, v, detail::I(0x3FC90FDBu));
    return CGtV4d(detail::cos_fast4(w));
}
// [C decomp asm] as SinCosFast2 without the b lanes: (sin a, cos a, ?, ?) -- z/w come from an uninitialised value in
// retail; here from zero: z = cos(-pi/2), w = cos(0) [G].
inline CGtV4d SinCosFast(float a) {
    using namespace b3vu;
    u_long128 v = b3host::make_u128(0, 0, 0, 0);
    v = add<X>(v, vf0(), splat(a));
    v = add<Y>(v, vf0(), bc<0>(v));
    const u_long128 w = sub<XZ>(v, v, detail::I(0x3FC90FDBu));
    return CGtV4d(detail::cos_fast4(w));
}

// [C decomp body, op for op] axis-angle rotation (axis normalised here)
inline CGtMatrix3x4 CreateRotationMatrix3x4(CGtV3d axis, float angle) {
    CGtMatrix3x4 lRotateMatrix;
    lRotateMatrix.mRight.mData = lRotateMatrix.mUp.mData = lRotateMatrix.mAt.mData = lRotateMatrix.mPos.mData = b3vu::vf0();  // [G] w lanes
    CGtV3d lOne(b3vu::vf0());
    lOne.SetX(1.0f);
    axis.Normalise();
    CGtV3d lSinCos(SinCosFast(angle).GetRawData());
    lSinCos.SetZ(lOne.X() - lSinCos.Y());
    CGtV3d lOneMinusSq = -(axis * axis);
    lOneMinusSq += lOne.X();
    lOneMinusSq *= lSinCos.Z();
    CGtV3d lCross(b3vu::vf0());
    lCross.SetX(axis.Y() * axis.Z());
    lCross.SetY(axis.Z() * axis.X());
    lCross.SetZ(axis.X() * axis.Y());
    CGtV3d lAxisSin = axis * lSinCos.X();
    lCross *= lSinCos.Z();
    lRotateMatrix.mRight.SetX(lOne.X() - lOneMinusSq.X());
    lRotateMatrix.mRight.SetY(lCross.Z() + lAxisSin.Z());
    lRotateMatrix.mRight.SetZ(lCross.Y() - lAxisSin.Y());
    lRotateMatrix.mUp.SetX(lCross.Z() - lAxisSin.Z());
    lRotateMatrix.mUp.SetY(lOne.X() - lOneMinusSq.Y());
    lRotateMatrix.mUp.SetZ(lCross.X() + lAxisSin.X());
    lRotateMatrix.mAt.SetX(lCross.Y() + lAxisSin.Y());
    lRotateMatrix.mAt.SetY(lCross.X() - lAxisSin.X());
    lRotateMatrix.mPos.Zero();
    lRotateMatrix.mAt.SetZ(lOne.X() - lOneMinusSq.Z());
    return lRotateMatrix;
}

}  // namespace GtMathPs2

#endif
