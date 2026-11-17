// GtMath for the PC port: the decomp's include/b3/GtMath.h API (Criterion's scalar helpers) on the host.
// Bodies follow the decomp's single-instruction asm (max.s, min.s, abs.s, pminw/pmaxw, rsqrt.s, mfc1 bit tests).
#ifndef B3_GTMATH_H
#define B3_GTMATH_H

#include "b3host/ps2float.h"

#include <cstdint>

namespace GtMath {

inline float Max(float a, float b) { return b3fp::max(a, b); }   // [C] max.s (EE total order on encodings)
inline float Min(float a, float b) { return b3fp::min(a, b); }   // [C] min.s
inline float Abs(float x) { return b3fp::abs(x); }               // [C] abs.s (sign clear)

union USplitBits {
    float mrReal;
    unsigned int mxBits;
    USplitBits(float lrReal) { mrReal = lrReal; }
};
// [C] the sign bit of x on 1.0 (bit operations: +0 -> 1, -0 -> -1)
inline float Sign(float lrReal) {
    USplitBits lSplitBits(lrReal);
    lSplitBits.mxBits = (lSplitBits.mxBits & 0x80000000u) | 0x3F800000u;
    return lSplitBits.mrReal;
}

inline int Min(int a, int b) { return a < b ? a : b; }           // [C] pminw (+ pextlw: zero-extends the result [L])
inline int Max(int a, int b) { return a > b ? a : b; }           // [C] pmaxw
inline int Clamp(int lnValue, int lnMin, int lnMax) { return Min(Max(lnValue, lnMin), lnMax); }   // [C] pmaxw, pminw

inline float InvSqrt(float x) { return b3fp::rsqrt(1.0f, x); }   // [C] rsqrt.s r, 1.0, x

// [C] exponent of x at most 2^-16's: (bits & 0x7F800000) <= 0x37800000 (raw bits: denormals are "zero")
inline bool IsEffectivelyZero(float lrValue) {
    USplitBits b(lrValue);
    return (b.mxBits & 0x7F800000u) <= 0x37800000u;
}

// [C] (float)(int)(x + copysign(0.5, x)) -- add.s, cvt.w.s (truncates), cvt.s.w
inline float RoundToNearest(float lrValue) {
    USplitBits lSplitHalf(0.5f);
    USplitBits lSplitValue(lrValue);
    lSplitHalf.mxBits |= lSplitValue.mxBits & 0x80000000u;
    return b3fp::cvt_s_w(b3fp::cvt_w_s(b3fp::add(lrValue, lSplitHalf.mrReal)));
}

}  // namespace GtMath

#endif
