// rwamath.o (librwacore.a) -- RWA's integer and vector helpers. Retail 0x299550-0x2997CC [C E map]. C port written
// from retail's code (audio1).
#include "rwa/rwacore.h"

extern RwInt16 D_0049E280[256];   // the square-root mantissa table (as _RwaSPU2VoiceSetLRVol uses it)

// The index of the lowest set bit, 32 for 0.
// 0x00299550
extern "C" RwUInt32 RwaFindLSB(RwUInt32 x)
{
    RwUInt8 i = 0;
    while (i < 0x20) {
        if (x & (1u << i))
            break;
        i++;
    }
    return i;
}

// 32 x 32 -> 64 bit unsigned product by shift and add, with the running sum kept as high:31 | low 31 bits, as retail
// computes it; the high word wraps at 2^32 * 2, so a product of 2^63 or more loses its top bit [C 0x299590-0x299648].
// 0x00299590
extern "C" void RwaMul32By32(RwUInt32 a, RwUInt32 b, RwUInt32 *hi, RwUInt32 *lo)
{
    RwUInt32 t2 = 0, t3 = 0, t4 = 0;
    while (a & ~1u) {
        t2 <<= 1;
        if (b & 0x80000000u) {
            b &= 0x7FFFFFFF;
            t2 |= 1;
        }
        if (a & 1) {
            t4 += b;
            t3 += t2;
            if (t4 & 0x80000000u) {
                t4 &= 0x7FFFFFFF;
                t3 += 1;
            }
        }
        a >>= 1;
        b <<= 1;
    }
    t2 <<= 1;
    if (b & 0x80000000u) {
        b &= 0x7FFFFFFF;
        t2 |= 1;
    }
    if (a & 1) {
        t4 += b;
        t3 += t2;
        if (t4 & 0x80000000u) {
            t4 &= 0x7FFFFFFF;
            t3 += 1;
        }
    }
    *hi = t3 >> 1;
    *lo = t4 | (t3 << 31);
}

// *out = the vector's length by RenderWare's table square root (the squared length when it is 0); returns the
// squared length y*y + x*x + z*z (retail's accumulation order) [C 0x299670-0x299704].
// 0x00299670
extern "C" RwReal RwaV3dLength(RwReal *out, RwV3d *v)
{
    RwReal s = v->y * v->y + v->x * v->x + v->z * v->z;
    *out = s;
    if (s == 0.0f)
        return s;
    RwUInt32 *w = (RwUInt32 *)out;
    RwUInt32 bits = *w;
    *w = bits & 0x7FFFFF;
    RwInt16 e = (RwInt16)((bits >> 23) - 0x7F);
    if (e & 1)
        *w = *w | 0x800000;
    RwUInt32 m = *w;
    *w = ((RwUInt32)((e >> 1) + 0x7F) << 23) | ((RwUInt32)(RwInt32)D_0049E280[m >> 16] << 16);
    return s;
}

// Copies a 4-, 2- or 1-byte value from `src` to the address in *cursor, byte-reversed, and advances the cursor by the
// size; any other size does nothing.
// 0x00299710
extern "C" void RwaEndianCopy(RwUInt32 *cursor, const void *src, RwUInt32 size)
{
    if (size == 4) {
        RwUInt32 w = *(const RwUInt32 *)src;
        RwUInt32 *d = (RwUInt32 *)*cursor;
        *d = (w << 24) | ((w & 0xFF00) << 8) | ((w & 0xFF0000) >> 8) | (w >> 24);
        *cursor = *cursor + 4;
    } else if (size == 2) {
        RwUInt16 h = *(const RwUInt16 *)src;
        RwUInt16 *d = (RwUInt16 *)*cursor;
        *d = (RwUInt16)(((h & 0xFF) << 8) | ((h & 0xFF00) >> 8));
        *cursor = *cursor + 2;
    } else if (size == 1) {
        RwUInt8 b = *(const RwUInt8 *)src;
        RwUInt8 *d = (RwUInt8 *)*cursor;
        *d = b;
        *cursor = *cursor + 1;
    }
}
