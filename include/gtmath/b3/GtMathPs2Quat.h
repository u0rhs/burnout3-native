// GtMathPs2Quat for the PC port: the decomp's include/b3/GtMathPs2Quat.h API on the host (see GtMathPs2.h here).
// Float expressions are spelled as one b3fp call per COP1 instruction in C++ evaluation order; lane-type expressions
// use the same overloads as the decomp, so C++ picks the same VU operation MWCC did.
// libm (sinf/cosf/acosf/asinf) goes through b3libm: in PS2-faithful mode the bit-exact port of retail's libm
// (include/b3libm, verified against the retail code), in fast mode the host's.
#ifndef GTMATHPS2QUAT_H
#define GTMATHPS2QUAT_H

#include "b3/GtMathPs2.h"
#include "b3libm/ps2libm.h"

#include <cmath>

namespace GtMath {
// [C decomp Quat header] retail compiles sqrtf to one sqrt.s (UncompressRotation 0x446BBC)
inline float Sqrt(float x) { return b3fp::sqrt(x); }
}  // namespace GtMath

namespace GtMathPs2 {

struct CGtQuat {
    CGtV4d mRot;
    CGtQuat() { b3host::zero_if_host(this, sizeof *this); }   // lane-noise: as CGtMatrix3x4() (GtMathPs2.h)
    CGtQuat(const CGtQuat& o) = default;   // native2: defaulted (a byte copy, as before): trivially copyable, passes by value
    CGtQuat(CGtV4d v) : mRot(v) {}
    CGtQuat& operator=(const CGtQuat& o) = default;
    CGtQuat operator*(float s) { CGtQuat r(mRot * s); return r; }
    CGtMatrix3x4 ConvertToMatrix();
    void SetFromMatrix(const CGtMatrix3x4& m);
};
struct CGtQuatPos {
    CGtQuat mQuat;
    CGtV3d mPos;
    CGtQuatPos() { b3host::zero_if_host(this, sizeof *this); }   // lane-noise: as CGtMatrix3x4() (GtMathPs2.h)
    CGtMatrix3x4 ConvertToMatrix();
    void SetFromMatrix(const CGtMatrix3x4& m);
};

// [C decomp body] q scaled by sqrt(2) (so 2*q_i*q_j needs one product), lane products on the VU, the 1 - a - b and
// a +- b sums on the FPU. mPos and the rows' w lanes are not written.
inline CGtMatrix3x4 CGtQuat::ConvertToMatrix() {
    using namespace b3fp;
    CGtQuat q;
    q = *this * 1.4142135f;
    const float xx = q.mRot.X() * q.mRot.X();
    const float xy = q.mRot.X() * q.mRot.Y();
    const float xz = q.mRot.X() * q.mRot.Z();
    const float yy = q.mRot.Y() * q.mRot.Y();
    const float yz = q.mRot.Y() * q.mRot.Z();
    const float zz = q.mRot.Z() * q.mRot.Z();
    const float wx = q.mRot.W() * q.mRot.X();
    const float wy = q.mRot.W() * q.mRot.Y();
    const float wz = q.mRot.W() * q.mRot.Z();
    CGtMatrix3x4 m;
    m.mRight.mData = m.mUp.mData = m.mAt.mData = m.mPos.mData = b3vu::vf0();   // retail: uninitialised [G: defined here]
    m.mRight.SetX(sub(sub(1.0f, yy), zz));
    m.mRight.SetY(add(xy, wz));
    m.mRight.SetZ(sub(xz, wy));
    m.mUp.SetX(sub(xy, wz));
    m.mUp.SetY(sub(sub(1.0f, xx), zz));
    m.mUp.SetZ(add(yz, wx));
    m.mAt.SetX(add(xz, wy));
    m.mAt.SetY(sub(yz, wx));
    m.mAt.SetZ(sub(sub(1.0f, xx), yy));
    return m;
}
inline CGtMatrix3x4 CGtQuatPos::ConvertToMatrix() {
    CGtMatrix3x4 m;
    m = mQuat.ConvertToMatrix();
    m.mPos = mPos;
    return m;
}

// [C decomp body] largest-diagonal quaternion extraction
inline void CGtQuat::SetFromMatrix(const CGtMatrix3x4& m) {
    using b3fp::lt; using b3fp::mul; using b3fp::div; using b3fp::add;
    const float trace = m.mRight.X() + m.mUp.Y() + m.mAt.Z();   // vaddy.x, vaddz.x
    if (lt(0.0f, trace)) {
        const float s = GtMath::Sqrt(add(trace, 1.0f));
        mRot.SetX(m.mUp.Z() - m.mAt.Y());
        mRot.SetY(m.mAt.X() - m.mRight.Z());
        mRot.SetZ(m.mRight.Y() - m.mUp.X());
        mRot *= div(0.5f, s);
        mRot.SetW(mul(0.5f, s));
    } else {
        int i;
        if (lt(m.mRight.X(), m.mUp.Y())) {
            if (lt(m.mUp.Y(), m.mAt.Z())) i = 2; else i = 1;
        } else {
            if (lt(m.mRight.X(), m.mAt.Z())) i = 2; else i = 0;
        }
        switch (i) {
        case 0: {
            float s = GtMath::Sqrt(m.mRight.X() - (m.mUp.Y() + m.mAt.Z()) + 1.0f);
            mRot.SetX(mul(0.5f, s));
            s = div(0.5f, s);
            mRot.SetW((m.mUp.Z() - m.mAt.Y()) * s);
            mRot.SetY((m.mRight.Y() + m.mUp.X()) * s);
            mRot.SetZ((m.mRight.Z() + m.mAt.X()) * s);
            break;
        }
        case 1: {
            float s = GtMath::Sqrt(m.mUp.Y() - (m.mAt.Z() + m.mRight.X()) + 1.0f);
            mRot.SetY(mul(0.5f, s));
            s = div(0.5f, s);
            mRot.SetW((m.mAt.X() - m.mRight.Z()) * s);
            mRot.SetZ((m.mUp.Z() + m.mAt.Y()) * s);
            mRot.SetX((m.mUp.X() + m.mRight.Y()) * s);
            break;
        }
        case 2: {
            float s = GtMath::Sqrt(m.mAt.Z() - (m.mRight.X() + m.mUp.Y()) + 1.0f);
            mRot.SetZ(mul(0.5f, s));
            s = div(0.5f, s);
            mRot.SetW((m.mRight.Y() - m.mUp.X()) * s);
            mRot.SetX((m.mAt.X() + m.mRight.Z()) * s);
            mRot.SetY((m.mAt.Y() + m.mUp.Z()) * s);
            break;
        }
        }
    }
}
inline void CGtQuatPos::SetFromMatrix(const CGtMatrix3x4& m) {
    mQuat.SetFromMatrix(m);
    mPos = m.mPos;
}

// [C decomp body] 32-bit compressed rotation: bits 14.. index a triangle grid of (theta, phi), bits 0-9 w, 10-13 signs
inline CGtQuat UncompressRotation(unsigned int c) {
    using namespace b3fp;
    CGtQuat q;
    q.mRot.mData = b3vu::vf0();   // retail leaves q's lanes uninitialised until set [G: zero here]
    const int n = static_cast<int>(c >> 14);
    const int a = cvt_w_s(GtMath::Sqrt(cvt_s_w(n)));
    const int b = n - a * a;
    const int d = a * 2;
    const float t = mul(1.5707964f, div(cvt_s_w(a), 511.0f));
    float p;
    if (d > 0) p = mul(1.5707964f, div(cvt_s_w(b), cvt_s_w(d))); else p = 0.0f;
    q.mRot.SetY(b3libm::cosf(t));
    q.mRot.SetX(b3libm::sinf(p));
    q.mRot.SetW(div(cvt_s_w(static_cast<int>(c & 0x3FF)), 1023.0f));
    const float r = GtMath::Sqrt(1.0f - q.mRot.W() * q.mRot.W());   // vmulw.w, then vsubw.x (float - WAxisType)
    if (lt(0.001f, r)) {
        q.mRot.SetX(q.mRot.X() * r);
        q.mRot.SetY(q.mRot.Y() * r);
        const float z = 1.0f - q.mRot.X() * q.mRot.X() - q.mRot.Y() * q.mRot.Y() - q.mRot.W() * q.mRot.W();
        if (lt(0.0f, z)) q.mRot.SetZ(GtMath::Sqrt(z)); else q.mRot.SetZ(0.0f);
        if (c & 0x400) q.mRot.SetX(-q.mRot.X());
        if (c & 0x800) q.mRot.SetY(-q.mRot.Y());
        if (c & 0x1000) q.mRot.SetZ(-q.mRot.Z());
        if (c & 0x2000) q.mRot.SetW(-q.mRot.W());
    } else {
        q.mRot.SetX(0.0f);
        q.mRot.SetY(0.0f);
        q.mRot.SetZ(0.0f);
    }
    return q;
}

// [C] vmul.xyzw m; vaddy.x t, m, m.y; vaddz.x t, t, m.z; vaddw.x t, t, m.w (three chopped adds, no ACC)
inline XAxisType DotProduct(CGtV4d a, CGtV4d b) {
    const u_long128 m = b3vu::mul<b3vu::XYZW>(a.mData, a.mData, b.mData);
    u_long128 t = b3vu::add<b3vu::X>(m, m, b3vu::bc<1>(m));
    t = b3vu::add<b3vu::X>(t, t, b3vu::bc<2>(m));
    t = b3vu::add<b3vu::X>(t, t, b3vu::bc<3>(m));
    return XAxisType(t);
}
inline XAxisType DotProduct(CGtQuat a, CGtQuat b) { return DotProduct(a.mRot, b.mRot); }

namespace detail {
// x + x^3 * P(x^2), P = the decomp's Taylor-like sin coefficients; evaluated as written, left to right
inline float slerp_sin(float x, float x2) {
    using namespace b3fp;
    float p = add(-2.505075969e-08f, mul(1.589691018e-10f, x2));
    p = add(2.75573143e-06f, mul(x2, p));
    p = add(-0.0001984127011f, mul(x2, p));
    p = add(0.008333333768f, mul(x2, p));
    p = add(-0.1666666716f, mul(x2, p));
    return add(x, mul(mul(x2, x), p));
}
}  // namespace detail

// [C decomp body]
inline CGtQuat Slerp(CGtQuat a, CGtQuat b, float t) {
    using namespace b3fp;
    float s0 = sub(1.0f, t);
    float c = DotProduct(a, b);
    bool flip;
    if (lt(c, 0.0f)) { c = neg(c); flip = true; } else flip = false;
    if (lt(c, 0.999f)) {
        const float om = b3libm::acosf(c);
        const float o2 = mul(om, om);
        const float k = div(1.0f, detail::slerp_sin(om, o2));
        const float x0 = mul(s0, om);
        const float x1 = mul(t, om);
        s0 = mul(detail::slerp_sin(x0, mul(x0, x0)), k);
        t = mul(detail::slerp_sin(x1, mul(x1, x1)), k);
    }
    if (flip) t = neg(t);
    CGtQuat r;
    r.mRot.mData = b3vu::vf0();   // [G] retail: uninitialised, all four lanes are then set
    r.mRot.SetX(b.mRot.X() * t + a.mRot.X() * s0);
    r.mRot.SetY(b.mRot.Y() * t + a.mRot.Y() * s0);
    r.mRot.SetZ(b.mRot.Z() * t + a.mRot.Z() * s0);
    r.mRot.SetW(b.mRot.W() * t + a.mRot.W() * s0);
    return r;
}

// [C decomp bodies] the first / second row of ConvertToMatrix
inline CGtV3d GetRight(CGtQuat a) {
    using namespace b3fp;
    CGtQuat q;
    q = a * 1.4142135f;
    const float xy = q.mRot.X() * q.mRot.Y();
    const float xz = q.mRot.X() * q.mRot.Z();
    const float yy = q.mRot.Y() * q.mRot.Y();
    const float zz = q.mRot.Z() * q.mRot.Z();
    const float wy = q.mRot.W() * q.mRot.Y();
    const float wz = q.mRot.W() * q.mRot.Z();
    CGtV3d r(b3vu::vf0());
    r.SetX(sub(sub(1.0f, yy), zz));
    r.SetY(add(xy, wz));
    r.SetZ(sub(xz, wy));
    return r;
}
inline CGtV3d GetUp(CGtQuat a) {
    using namespace b3fp;
    CGtQuat q;
    q = a * 1.4142135f;
    const float xx = q.mRot.X() * q.mRot.X();
    const float xy = q.mRot.X() * q.mRot.Y();
    const float yz = q.mRot.Y() * q.mRot.Z();
    const float zz = q.mRot.Z() * q.mRot.Z();
    const float wx = q.mRot.W() * q.mRot.X();
    const float wz = q.mRot.W() * q.mRot.Z();
    CGtV3d r(b3vu::vf0());
    r.SetX(sub(xy, wz));
    r.SetY(sub(sub(1.0f, xx), zz));
    r.SetZ(add(yz, wx));
    return r;
}

}  // namespace GtMathPs2

namespace GtMath {
inline float ACosRad(float x) { return b3libm::acosf(x); }   // retail: jal acosf (func_0011E328)
inline float ACosDeg(float x) { return b3fp::mul(57.29578f, ACosRad(x)); }
inline float ASinRad(float x) { return b3libm::asinf(x); }   // retail: asinf (func_0011E340)
inline float ASinDeg(float x) { return b3fp::mul(57.29578f, ASinRad(x)); }
}  // namespace GtMath

namespace GtMathPs2 {
// [C decomp body] degrees between two vectors (copies normalised with NormaliseFast)
inline float AngleBetweenVectors(CGtV3d a, CGtV3d b) {
    CGtV3d lA = a;
    CGtV3d lB = b;
    lA.NormaliseFast();
    lB.NormaliseFast();
    const XAxisType lCos = CosAngleBetweenNormalisedVectors(lA, lB);
    return GtMath::ACosDeg(lCos);
}
}  // namespace GtMathPs2

#endif
