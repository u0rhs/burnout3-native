// GtMathPs2 on the host: layouts, identities (both float modes), and bit-exact values in PS2-faithful mode.
// Built twice: -DB3_PS2_FLOAT=0 (test_gtmath_fast) and -DB3_PS2_FLOAT=1 (test_gtmath_ps2).
// Bit-exact expectations come from tools/vuref.py (independent exact-rational model) unless noted.
#include "b3/GtMath.h"
#include "b3/GtMathPs2.h"
#include "b3/GtMathPs2Quat.h"
#include "b3/GtMathPs2Trig.h"
#include "test.h"

#include <cstddef>
#include <limits>
#include <type_traits>

using namespace GtMathPs2;
using t::fbits;

// ---- layouts the original code shares (decomp header facts) ----
static_assert(sizeof(CGtV2d) == 8 && alignof(CGtV2d) == 8);
static_assert(std::is_trivially_default_constructible_v<CGtV3d> && std::is_trivially_default_constructible_v<CGtV4d>);   // gen1: a goto may cross them (decomp2host T8b)
static_assert(sizeof(CGtV3d) == 16 && alignof(CGtV3d) == 16);
static_assert(sizeof(CGtV4d) == 16 && alignof(CGtV4d) == 16);
static_assert(sizeof(XAxisType) == 16 && sizeof(WAxisType) == 16);
static_assert(sizeof(CGtMatrix2x3) == 24);
static_assert(sizeof(CGtMatrix3x3) == 48);
static_assert(sizeof(CGtMatrix3x4) == 64 && offsetof(CGtMatrix3x4, mUp) == 0x10 && offsetof(CGtMatrix3x4, mAt) == 0x20 &&
              offsetof(CGtMatrix3x4, mPos) == 0x30);
static_assert(sizeof(CGtQuat) == 16 && sizeof(CGtQuatPos) == 32 && offsetof(CGtQuatPos, mPos) == 16);
static_assert(sizeof(CGtPlane) == 16 && sizeof(CGtSphere) == 16 && sizeof(CGtV3dPlus) == 16);

static float fx(const CGtV3d& v) { return b3vu::lane(v.mData, 0); }
static float fy(const CGtV3d& v) { return b3vu::lane(v.mData, 1); }
static float fz(const CGtV3d& v) { return b3vu::lane(v.mData, 2); }
static CGtV3d V(float x, float y, float z) { return CGtV3d(x, y, z); }
static bool near3(const CGtV3d& a, float x, float y, float z, float tol) {
    return std::fabs(fx(a) - x) <= tol && std::fabs(fy(a) - y) <= tol && std::fabs(fz(a) - z) <= tol;
}
static bool near_m(const CGtMatrix3x4& a, const CGtMatrix3x4& b, float tol) {
    const CGtV3d* ra[3] = {&a.mRight, &a.mUp, &a.mAt};
    const CGtV3d* rb[3] = {&b.mRight, &b.mUp, &b.mAt};
    for (int i = 0; i < 3; ++i)
        if (!near3(*ra[i], fx(*rb[i]), fy(*rb[i]), fz(*rb[i]), tol)) return false;
    return true;
}
static CGtMatrix3x4 Identity() { CGtMatrix3x4 m; m.mRight.mData = m.mUp.mData = m.mAt.mData = m.mPos.mData = b3vu::vf0(); m.SetIdentity(); return m; }

TEST(v3_basics) {
    CGtV3d a = V(1, 2, 3), b = V(4, 5, 6);
    CHECK(near3(a + b, 5, 7, 9, 0));
    CHECK(near3(b - a, 3, 3, 3, 0));
    CHECK(near3(a * 2.0f, 2, 4, 6, 0));
    CHECK(near3(a * b, 4, 10, 18, 0));
    CHECK(near3(-a, -1, -2, -3, 0));
    CHECK(float(DotProduct(a, b)) == 32.0f);
    CHECK(near3(CrossProduct(V(1, 0, 0), V(0, 1, 0)), 0, 0, 1, 0));
    CHECK(near3(CrossProduct(a, b), -3, 6, -3, 0));
    CHECK(float(a.X()) == 1 && float(a.Y()) == 2 && float(a.Z()) == 3);
    CGtV3d c = a;
    c += b;
    c -= a;
    CHECK(near3(c, 4, 5, 6, 0));
    c.SetX(7.0f); c.SetY(b.Z()); c.SetZ(a.X());
    CHECK(near3(c, 7, 6, 1, 0));
    c.Set(a.X(), b.Y(), a.Z());
    CHECK(near3(c, 1, 5, 3, 0));
    CHECK(near3(a * b.Y(), 5, 10, 15, 0));
    c = a; c *= b.Z(); CHECK(near3(c, 6, 12, 18, 0));
    c = a; c += b.X(); CHECK(near3(c, 5, 6, 7, 0));
    CHECK(near3(Min(a, V(0, 5, 2)), 0, 2, 2, 0) && near3(Max(a, V(0, 5, 2)), 1, 5, 3, 0));
    CHECK(near3(Abs(V(-1, 2, -3)), 1, 2, 3, 0));
    CHECK(near3(Interpolate(a, b, 0.5f), 2.5f, 3.5f, 4.5f, 1e-6f));
    CHECK(near3(Interpolate(a, b, 0.0f), 1, 2, 3, 0) && near3(Interpolate(a, b, 1.0f), 4, 5, 6, 1e-6f));
}

TEST(v3_lengths) {
    CGtV3d a = V(3, 4, 12);
    CHECK(float(a.GetSquaredLength()) == 169.0f);
    CHECK(float(a.GetLength()) == 13.0f);
    CGtV3d n = a;
    n.Normalise();
    CHECK_NEAR(float(n.GetLength()), 1.0, 1e-6);
    CGtV3d m = a;
    CHECK(float(m.NormaliseGetLength()) == 13.0f);
    CHECK(near3(m, 3.0f / 13, 4.0f / 13, 12.0f / 13, 1e-6f));
    CGtV3d f = a;
    CHECK_NEAR(float(f.NormaliseFast()), 1.0 / 13, 1e-7);   // NormaliseFast returns the reciprocal length [C asm]
    CHECK(near3(f, 3.0f / 13, 4.0f / 13, 12.0f / 13, 1e-6f));
    CHECK(V(0, 0, 0).IsZero() && V(-0.0f, 0, -0.0f).IsZero() && !V(0, 1e-30f, 0).IsZero());
    CHECK(V(1e-6f, 0, 0).IsEffectivelyZero() && !V(1e-4f, 0, 0).IsEffectivelyZero());
    // the PS2's safe zero-length behaviour, kept in both modes: no NaN
    CGtV3d z = V(0, 0, 0);
    z.Normalise();
    CHECK(near3(z, 0, 0, 0, 0));
    CGtV3d z2 = V(0, 0, 0);
    CHECK(float(z2.NormaliseFast()) == 3.4028235e38f && near3(z2, 0, 0, 0, 0));
    CGtV3d z3 = V(0, 0, 0);
    CHECK(float(z3.NormaliseGetLength()) == 0.0f && near3(z3, 0, 0, 0, 0));
    CHECK_NEAR(float(CosAngleBetweenNormalisedVectors(V(1, 0, 0), V(1.0000001f, 0, 0))), 1.0, 0);
    CHECK_NEAR(float(CosAngleBetweenNormalisedVectors(V(1, 0, 0), V(-1.5f, 0, 0))), -1.0, 0);
    CHECK_NEAR(AngleBetweenVectors(V(2, 0, 0), V(0, 3, 0)), 90.0, 1e-4);
}

TEST(v4_and_lanes) {
    CGtV4d a(1, 2, 3, 4), b(5, 6, 7, 8);
    CHECK(float(DotProduct(a, b)) == 70.0f);
    CGtV4d p = a * b;
    CHECK(p.f[0] == 5 && p.f[3] == 32);
    CGtV4d d = b - a;
    CHECK(d.f[0] == 4 && d.f[3] == 4);
    CGtV4d s = a * 0.5f;
    CHECK(s.f[1] == 1.0f);
    s *= 2.0f;
    CHECK(s.f[3] == 4.0f);
    s.SetW(9.0f); s.SetA(10.0f); s.SetX(-1.0f); s.SetY(a.X()); s.SetZ(b.Y());
    CHECK(s.f[0] == -1 && s.f[1] == 1 && s.f[2] == 6 && s.f[3] == 10);
    s.SetW(b.Z()); CHECK(s.f[3] == 7);
    s.SetW(b.W()); CHECK(s.f[3] == 8);
    s.Zero(); CHECK(s.f[0] == 0 && s.f[3] == 0);
    CGtV4d m = Min(a, CGtV4d(0, 9, 3, -1));
    CHECK(m.f[0] == 0 && m.f[1] == 2 && m.f[3] == -1);
    CGtV4d i = Interpolate(a, b, 0.25f);
    CHECK(i.f[0] == 2.0f && i.f[3] == 5.0f);
    // lane arithmetic
    CHECK(float(a.X() * b.Y()) == 6 && float(a.W() * b.W()) == 32 && float(a.Z() * 2.0f) == 6);
    CHECK(float(a.X() + b.Z()) == 8 && float(a.Y() - b.X()) == -3 && float(1.0f - a.W()) == -3);
    CHECK(float(-a.X()) == -1 && float(-a.Y()) == -2 && float(-a.Z()) == -3 && float(-a.W()) == -4);
    CHECK(float(a.Y() + 0.5f) == 2.5f && float(a.Z() - 0.5f) == 2.5f);
    // gen1: float - ZAxisType is retail's vsubz.x (an XAxisType), not host float; the other scalar-left forms are
    // deleted (a compile error: write (float)v.X() for an FPU form)
    static_assert(std::is_same_v<decltype(1.0f - a.Z()), XAxisType>);
    CHECK(float(1.0f - a.Z()) == -2 && float(10.0f - b.Z()) == 3);
    // float * / + lane: the FPU op on the values (float); lane - float for X (vsubx.x) / W (sub.s), W + float (vaddx.w)
    static_assert(std::is_same_v<decltype(2.0f * a.W()), float> && std::is_same_v<decltype(2.0f + a.X()), float>);
    CHECK(2.0f * a.W() == 8 && 2.0f + a.X() == 3 && float(a.X() - 0.5f) == 0.5f && a.W() - 1.0f == 3 && float(a.W() + 1.0f) == 5);
    static_assert(std::is_same_v<decltype(a.X() - 1.0f), XAxisType> && std::is_same_v<decltype(a.W() + 1.0f), WAxisType>);
    // lane + W (vaddw.y / vaddw.z), and division with a lane operand: the ps2f division of the two values (div.s)
    static_assert(std::is_same_v<decltype(a.Y() + b.W()), YAxisType> && std::is_same_v<decltype(a.Z() + b.W()), ZAxisType>);
    CHECK(float(a.Y() + b.W()) == 10 && float(a.Z() + b.W()) == 11 && float(b.W() + a.Y()) == 10);
    static_assert(std::is_same_v<decltype(a.X() / b.Z()), float> && std::is_same_v<decltype(1.0f / a.W()), float>);
    CHECK(a.X() / b.W() == 0.125f && 6.0f / a.Y() == 3.0f && a.Z() / 2.0f == 1.5f);
}

TEST(matrices) {
    const CGtMatrix3x4 I = Identity();
    CHECK(near3(TransformPoint(I, V(1, 2, 3)), 1, 2, 3, 0));
    CGtMatrix3x4 R = CreateRotationMatrix3x4(V(0, 2, 0), 1.5707964f);   // 90 degrees about +Y (axis normalised inside)
    CHECK(near3(TransformVector(R, V(1, 0, 0)), 0, 0, -1, 2e-6f));     // rows are the basis images: mRight = (c, 0, -s)
    CHECK(near3(TransformVector(R, V(0, 0, 1)), 1, 0, 0, 2e-6f));
    CHECK(near3(R.mPos, 0, 0, 0, 0));
    R.mPos = V(10, 20, 30);
    CHECK(near3(TransformPoint(R, V(1, 0, 0)), 10, 20, 29, 2e-6f));
    CHECK(near_m(I * R, R, 0) && near3((I * R).mPos, 10, 20, 30, 0));
    CHECK(near_m(Multiply(R, I), R, 0));
    // R * R = 180 degrees
    CGtMatrix3x4 R2 = R * R;
    CHECK(near3(TransformVector(R2, V(1, 0, 0)), -1, 0, 0, 4e-6f));
    // inverse of a rigid transform
    CGtMatrix3x4 Rinv = R;
    Rinv.Invert();
    CHECK(near3(TransformPoint(Rinv, TransformPoint(R, V(1, 2, 3))), 1, 2, 3, 1e-5f));
    // orthonormalise a skewed basis
    CGtMatrix3x4 S = I;
    S.mRight = V(2.0f, 0.1f, 0.0f);
    S.mUp = V(0.05f, 3.0f, 0.1f);
    S.mAt = V(0.0f, 0.0f, 0.5f);
    S.OrthoNormalise();
    CHECK_NEAR(float(S.mRight.GetLength()), 1, 2e-6);
    CHECK_NEAR(float(S.mUp.GetLength()), 1, 2e-6);
    CHECK_NEAR(float(S.mAt.GetLength()), 1, 2e-6);
    CHECK_NEAR(float(DotProduct(S.mRight, S.mUp)), 0, 2e-6);
    CHECK_NEAR(float(DotProduct(S.mUp, S.mAt)), 0, 2e-6);
    CHECK_NEAR(float(DotProduct(S.mAt, S.mRight)), 0, 2e-6);
    // look-at
    CGtMatrix3x4 L = Matrix3x4SetLookAt(V(0, 0, 1));
    CHECK(near3(L.mRight, 1, 0, 0, 0) && near3(L.mUp, 0, 1, 0, 0) && near3(L.mAt, 0, 0, 1, 0));
}

TEST(trig) {
    for (float a = -20.0f; a <= 20.0f; a += 0.37f) {
        CGtV4d sc = SinCosFast(a);
        CHECK_NEAR(sc.f[0], std::sin(a), 3e-6);
        CHECK_NEAR(sc.f[1], std::cos(a), 3e-6);
        CGtV4d sc2 = SinCosFast2(a, 0.5f * a);
        CHECK_NEAR(sc2.f[2], std::sin(0.5f * a), 3e-6);
        CHECK_NEAR(sc2.f[3], std::cos(0.5f * a), 3e-6);
    }
    CGtV4d c = CosFast4(CGtV4d(0.0f, 1.0f, 3.14159f, 100.0f));
    CHECK_NEAR(c.f[0], 1.0, 1e-6);
    CHECK_NEAR(c.f[3], std::cos(100.0), 1e-5);   // PS2: 0x3F5CC0A8 = 0.8623147, 4e-6 off (frac(x/2pi) truncated; vuref.py)
    CGtMatrix2x3 m = CreateRotationMatrix2x3(1.5707964f);
    CGtV2d p = TransformPoint(m, CGtV2d(1.0f, 0.0f));
    CHECK_NEAR(p.X(), 0.0, 1e-6);
    CHECK_NEAR(p.Y(), -1.0, 1e-6);
    CGtV2d pts[2] = {CGtV2d(1, 0), CGtV2d(0, 1)}, out[2];
    TransformPoints(m, out, pts, 2);
    CHECK_NEAR(out[1].X(), 1.0, 1e-6);
}

static CGtQuat AxisAngle(float x, float y, float z, float a) {
    const float s = std::sin(a / 2) / std::sqrt(x * x + y * y + z * z);
    return CGtQuat(CGtV4d(x * s, y * s, z * s, std::cos(a / 2)));
}

TEST(quaternions) {
    const CGtQuat q = AxisAngle(1, 2, 3, 0.7f);
    CGtQuat qc = q;
    CGtMatrix3x4 Mq = qc.ConvertToMatrix();
    CGtMatrix3x4 Ma = CreateRotationMatrix3x4(V(1, 2, 3), 0.7f);
    CHECK(near_m(Mq, Ma, 3e-6f));
    CHECK(near3(GetRight(q), fx(Mq.mRight), fy(Mq.mRight), fz(Mq.mRight), 0));
    CHECK(near3(GetUp(q), fx(Mq.mUp), fy(Mq.mUp), fz(Mq.mUp), 0));
    // back to a quaternion (q or -q)
    Mq.mPos = V(1, 2, 3);
    CGtQuatPos qp;
    qp.SetFromMatrix(Mq);
    const float sgn = qp.mQuat.mRot.f[3] * q.mRot.f[3] < 0 ? -1.0f : 1.0f;
    for (int i = 0; i < 4; ++i) CHECK_NEAR(sgn * qp.mQuat.mRot.f[i], q.mRot.f[i], 3e-6);
    CHECK(near3(qp.mPos, 1, 2, 3, 0));
    CHECK(near_m(qp.ConvertToMatrix(), Mq, 3e-6f) && near3(qp.ConvertToMatrix().mPos, 1, 2, 3, 0));
    // the trace <= 0 branches (rotations near 180 degrees about each axis)
    for (int axis = 0; axis < 3; ++axis) {
        const CGtQuat r = AxisAngle(axis == 0 ? 1.0f : 0.1f, axis == 1 ? 1.0f : 0.1f, axis == 2 ? 1.0f : 0.1f, 3.0f);
        CGtQuat rc = r;
        CGtQuat back;
        back.SetFromMatrix(rc.ConvertToMatrix());
        const float sg = back.mRot.f[axis] * r.mRot.f[axis] < 0 ? -1.0f : 1.0f;
        for (int i = 0; i < 4; ++i) CHECK_NEAR(sg * back.mRot.f[i], r.mRot.f[i], 5e-6);
    }
    // slerp
    const CGtQuat a = AxisAngle(0, 1, 0, 0.2f), b = AxisAngle(0, 1, 0, 1.2f);
    CGtQuat s0 = Slerp(a, b, 0.0f), s1 = Slerp(a, b, 1.0f), sh = Slerp(a, b, 0.5f);
    for (int i = 0; i < 4; ++i) {
        CHECK_NEAR(s0.mRot.f[i], a.mRot.f[i], 2e-6);
        CHECK_NEAR(s1.mRot.f[i], b.mRot.f[i], 2e-6);
    }
    const CGtQuat mid = AxisAngle(0, 1, 0, 0.7f);
    for (int i = 0; i < 4; ++i) CHECK_NEAR(sh.mRot.f[i], mid.mRot.f[i], 2e-6);
    CHECK_NEAR(float(DotProduct(sh, sh)), 1.0, 3e-6);
    // compressed rotations: n = c >> 14 indexes (theta, phi), bits 0-9 are w; not every 32-bit value is a code that
    // CompressRotation would produce, so check decodes of known shapes and that the result stays bounded
    const CGtQuat u0 = UncompressRotation(0u);             // w = 0, theta = 0: (0, 1, 0, 0)
    CHECK(u0.mRot.f[0] == 0 && u0.mRot.f[1] == 1 && u0.mRot.f[2] == 0 && u0.mRot.f[3] == 0);
    const CGtQuat u1 = UncompressRotation(0x3FFu);         // w = 1: r = 0 -> (0, 0, 0, 1)
    CHECK(u1.mRot.f[0] == 0 && u1.mRot.f[1] == 0 && u1.mRot.f[2] == 0 && u1.mRot.f[3] == 1);
    const CGtQuat u2 = UncompressRotation(0x1FFu | 0x2000u);   // w = 511/1023, negated by bit 13
    CHECK_NEAR(u2.mRot.f[3], -511.0 / 1023.0, 1e-6);
    CHECK_NEAR(float(DotProduct(u2, u2)), 1.0, 2e-6);
    const CGtQuat u3 = UncompressRotation((200000u << 14) | 0x2A5u | 0x400u);   // z from 1 - x^2 - y^2 - w^2 > 0
    CHECK_NEAR(float(DotProduct(u3, u3)), 1.0, 3e-6);
    CHECK(u3.mRot.f[0] < 0.0f);
    CHECK(UncompressRotation(0x400u | 0x1FFu).mRot.f[0] <= 0.0f);   // sign bit 10 -> x negated
}

TEST(planes_spheres) {
    CGtPlane pl;
    pl.mData = b3vu::vf0();
    pl.SetNormal(V(0, 1, 0));
    pl.SetDistance(5.0f);
    CHECK(near3(pl.Normal(), 0, 1, 0, 0) && float(pl.Distance()) == 5.0f);
    pl.SetDistance(V(7, 0, 0).X());
    CHECK(float(pl.Distance()) == 7.0f && near3(pl.Normal(), 0, 1, 0, 0));
    CGtSphere sp;
    sp.mData = b3vu::vf0();
    sp.SetPosition(V(1, 2, 3));
    sp.SetRadius(4.0f);
    CHECK(near3(sp.Position(), 1, 2, 3, 0) && float(sp.Radius()) == 4.0f);
    CGtV3dPlus vp;
    vp.mData = b3vu::vf0();
    vp.SetGtV3d(V(1, 2, 3));
    vp.SetPlus(6.0f);
    CHECK(b3vu::lane(vp.mData, 3) == 6.0f && b3vu::lane(vp.mData, 0) == 1.0f);
    vp.SetPlus(V(0, 8, 0).Y());
    CHECK(b3vu::lane(vp.mData, 3) == 8.0f);
}

// gen1: the decomp's gapN additions (CGtV3dPlus::GtV3d / Plus / SetPlus(WAxisType), Abs(CGtV4d), CGtV3d - WAxisType,
// TransformVector on a CGtMatrix3x3, TransformPoints on a CGtMatrix3x4)
TEST(gapN_additions) {
    CGtV3dPlus vp;
    vp.mData = b3vu::make(1, 2, 3, 0);
    WAxisType w(b3vu::make(0, 0, 0, 9));
    vp.SetPlus(w);
    CHECK(float(vp.Plus()) == 9.0f && near3(vp.GtV3d(), 1, 2, 3, 0));
    vp.Zero();
    CHECK(near3(vp.GtV3d(), 0, 0, 0, 0) && float(vp.Plus()) == 0.0f);
    CGtV4d a(b3vu::make(-1, 2, -3, -4));
    CGtV4d b = Abs(a);
    CHECK(b3vu::lane(b.mData, 0) == 1 && b3vu::lane(b.mData, 2) == 3 && b3vu::lane(b.mData, 3) == 4);
    CGtV3d d = V(5, 6, 7) - WAxisType(b3vu::make(0, 0, 0, 2));
    CHECK(near3(d, 3, 4, 5, 0));
    CGtMatrix3x3 m3;
    m3.mRight = V(0, 1, 0); m3.mUp = V(-1, 0, 0); m3.mAt = V(0, 0, 1);
    m3.mAt.mData.w[3] = b3vu::make(0, 0, 0, 1).w[3];
    CGtV3d r = TransformVector(m3, V(1, 2, 3));
    CHECK(near3(r, -2, 1, 3, 0) && b3vu::lane(r.mData, 3) == 1.0f);   // x*R + y*U + z*A, w = A.w
    CGtMatrix3x4 m = Identity();
    m.mPos = V(10, 0, 0);
    CGtV3d src[2] = {V(1, 0, 0), V(0, 2, 0)}, dst[2];
    TransformPoints(m, dst, src, 2);
    CHECK(near3(dst[0], 11, 0, 0, 0) && near3(dst[1], 10, 2, 0, 0));
    CGtMatrix3x4 a2 = m, prod = m * m;
    a2 *= m;
    CHECK(near_m(a2, prod, 0) && near3(a2.mPos, fx(prod.mPos), fy(prod.mPos), fz(prod.mPos), 0));
}

TEST(v2) {
    CGtV2d a(1, 2), b(3, 4);
    CHECK((a + b).X() == 4 && (b - a).Y() == 2 && (a * b).Y() == 8 && (b / a).Y() == 2);
    CHECK((a * 3.0f).Y() == 6 && (b / 2.0f).X() == 1.5f && (-a).X() == -1);
    a += b; a -= CGtV2d(1, 1); a *= 2.0f;
    CHECK(a.X() == 6 && a.Y() == 10);
    CHECK((CGtV2d(1, 1) / 0.0f).X() == 3.4028235e38f);   // PS2-safe: no Inf in either mode
}

TEST(gtmath_scalar) {
    CHECK(GtMath::Max(1.0f, 2.0f) == 2.0f && GtMath::Min(1.0f, 2.0f) == 1.0f && GtMath::Abs(-3.0f) == 3.0f);
    CHECK(GtMath::Sign(-0.0f) == -1.0f && GtMath::Sign(0.0f) == 1.0f && GtMath::Sign(-5.0f) == -1.0f);
    CHECK(GtMath::Min(3, -4) == -4 && GtMath::Max(3, -4) == 3 && GtMath::Clamp(12, 0, 10) == 10 && GtMath::Clamp(-1, 0, 10) == 0);
    CHECK_NEAR(GtMath::InvSqrt(4.0f), 0.5, 0);
    CHECK(GtMath::IsEffectivelyZero(1e-5f) && !GtMath::IsEffectivelyZero(1e-4f) && GtMath::IsEffectivelyZero(-0.0f));
    CHECK(GtMath::RoundToNearest(2.5f) == 3.0f && GtMath::RoundToNearest(-2.5f) == -3.0f && GtMath::RoundToNearest(1.4f) == 1.0f);
    CHECK_NEAR(GtMath::Sqrt(16.0f), 4.0, 0);
    CHECK_NEAR(GtMath::ACosDeg(0.0f), 90.0, 1e-4);
    CHECK_NEAR(GtMath::ASinDeg(1.0f), 90.0, 1e-4);
}

// ---- where the two modes differ (each row: the PS2 result, with its source) ----
#if !B3_PS2_FLOAT && (B3_HAVE_SSE2 || B3_HAVE_NEON)
// mac1: the fast mode's vector kernels (SSE on x86, NEON on AArch64) give the same lanes: maxps / minps order (the second
// operand on equal or NaN), separately rounded madd / msub, the destination's lanes outside the mask kept.
TEST(fast_vector_kernels_portable) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float xs[] = {1.5f, -0.0f, 0.0f, nan, -3.0f, 1e30f, 1.0f + 0x1p-23f, 0x1p-130f};
    const float ys[] = {-2.0f, 0.0f, -0.0f, 1.0f, nan, 1e30f, 1.0f - 0x1p-24f, 3.0f};
    const u_long128 d = b3vu::make(10.0f, 20.0f, 30.0f, 40.0f);
    for (unsigned k = 0; k + 3 < sizeof xs / sizeof xs[0]; ++k) {
        const u_long128 a = b3vu::make(xs[k], xs[k + 1], xs[k + 2], xs[k + 3]);
        const u_long128 b = b3vu::make(ys[k], ys[k + 1], ys[k + 2], ys[k + 3]);
        const u_long128 mx = b3vu::max<b3vu::XYZW>(d, a, b), mn = b3vu::min<b3vu::XYZW>(d, a, b);
        const u_long128 ma = b3vu::madd<b3vu::XYZW>(d, d, a, b), ms = b3vu::msub<b3vu::XYZW>(d, d, a, b);
        const u_long128 ab = b3vu::abs<b3vu::XYZW>(d, a), part = b3vu::add<b3vu::XZ>(d, a, b);
        for (unsigned i = 0; i < 4; ++i) {
            const float x = xs[k + i], y = ys[k + i], c = b3vu::lane(d, i);
            volatile float p = x * y;   // the product rounded on its own
            CHECK_BITS(mx.w[i], fbits(x > y ? x : y));
            CHECK_BITS(mn.w[i], fbits(x < y ? x : y));
            CHECK_BITS(ma.w[i], fbits(c + p));
            CHECK_BITS(ms.w[i], fbits(c - p));
            CHECK_BITS(ab.w[i], fbits(x) & 0x7FFFFFFFu);
            CHECK_BITS(part.w[i], (i == 0 || i == 2) ? fbits(x + y) : d.w[i]);
        }
    }
}
#endif

TEST(mode_specific) {
#if B3_PS2_FLOAT
    // [P] tools/vuref.py
    CGtV4d sc = SinCosFast(1.0f);
    CHECK_BITS(fbits(sc.f[0]), 0x3F576AA1u);
    CHECK_BITS(fbits(sc.f[1]), 0x3F0A513Eu);
    CGtV4d sc2 = SinCosFast2(1.0f, -2.5f);
    CHECK_BITS(fbits(sc2.f[0]), 0x3F576AA1u);
    CHECK_BITS(fbits(sc2.f[1]), 0x3F0A513Eu);
    CHECK_BITS(fbits(sc2.f[2]), 0xBF193581u);
    CHECK_BITS(fbits(sc2.f[3]), 0xBF4D17BBu);
    CGtV4d c = CosFast4(CGtV4d(0.0f, 1.0f, 3.14159f, 100.0f));
    CHECK_BITS(fbits(c.f[0]), 0x3F800000u);
    CHECK_BITS(fbits(c.f[1]), 0x3F0A513Eu);
    CHECK_BITS(fbits(c.f[2]), 0xBF800000u);
    CHECK_BITS(fbits(c.f[3]), 0x3F5CC0A8u);
    CHECK_BITS(fbits(float(DotProduct(V(0.1f, 0.2f, 0.3f), V(0.4f, 0.5f, 0.6f)))), 0x3EA3D70Au);
    CGtV3d n = V(1, 2, 3);
    n.Normalise();
    CHECK_BITS(n.mData.w[0], 0x3E88D677u);
    CHECK_BITS(n.mData.w[1], 0x3F08D677u);
    CHECK_BITS(n.mData.w[2], 0x3F4D41B2u);
    // [C] RoundToNearest(0.49999997): add.s truncates 0.99999997 below 1 -> 0 (host round-to-nearest gives 1)
    CHECK(GtMath::RoundToNearest(0.49999997f) == 0.0f);
    // [M] VU 2.1.1: a denormal operand is zero, an exponent-255 operand is Fmax (D1)
    CGtV3d d = V(0, 0, 0);
    d.SetX(t::bitsf(0x00000001u));
    CHECK_BITS(d.mData.w[0], 0u);
    d.SetY(t::bitsf(0x7F800000u));
    CHECK_BITS(d.mData.w[1], 0x7F7FFFFFu);
    // [C asm] IsZero tests raw bits: a denormal lane is not zero; IsEffectivelyZero squares it to 0
    CGtV3d dn(b3host::make_u128(0x00000001u, 0, 0, 0));
    CHECK(!dn.IsZero() && dn.IsEffectivelyZero());
    // [C asm] unary minus of an x lane is 0 - x (+0 stays +0); of a z lane it is x * -1 (+0 becomes -0)
    CHECK_BITS(fbits(float(-V(0, 0, 0).X())), 0x00000000u);
    CHECK_BITS(fbits(float(-V(0, 0, 0).Z())), 0x80000000u);
#else
    CHECK(GtMath::RoundToNearest(0.49999997f) == 1.0f);   // host round-to-nearest (documented difference)
    CGtV4d sc = SinCosFast(1.0f);
    CHECK_NEAR(sc.f[0], 0.8414709848, 3e-7);
#endif
}

#if B3_PS2_FLOAT
TEST_MAIN("GtMathPs2 (PS2-faithful mode)")
#else
TEST_MAIN("GtMathPs2 (native fast mode)")
#endif
