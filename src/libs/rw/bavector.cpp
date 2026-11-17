// RenderWare 3.6 core: vectors (RW 3.1 file name: src/bavector.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)
extern int vectorModule;               // [C] gp-relative 0x004E2788 (8 bytes): +0 the vector globals' offset in ourGlobals

typedef void *(*UnkFn_001DE550_3)(void *, const void *, const void *);        // [C] (out, in, matrix)
typedef void *(*UnkFn_001DE550_4)(void *, const void *, int, const void *);   // [C] (out, in, count, matrix)

// [C] the vector globals at ourGlobals + vectorModule ([L] rwVectorGlobals). Shapes [C] from the defaults' use of
// a0..a3: (out, in, matrix) and (out, in, count, matrix).
struct UnkStruct_vectorModule {
    unsigned char pad_0[0x8];
    UnkFn_001DE550_3 field_8;   // [C] default func_001DE7C0 ([L] multPoint: uses +0x30)
    UnkFn_001DE550_4 field_C;   // [C] default func_001DE5A8 ([L] multPoints)
    UnkFn_001DE550_3 field_10;  // [C] default func_001DEA28 ([L] multVector)
    UnkFn_001DE550_4 field_14;  // [C] default func_001DE858 ([L] multVectors)
};

extern "C" void *func_001DE7C0(void *out, const void *in, const void *matrix);
extern "C" void *func_001DE5A8(void *out, const void *in, int count, const void *matrix);
extern "C" void *func_001DEA28(void *out, const void *in, const void *matrix);
extern "C" void *func_001DE858(void *out, const void *in, int count, const void *matrix);

// [C] each argument, or its default when NULL, stored at +8, +0xC, +0x10, +0x14 of the vector globals in that order.
// Returns 1.
// 0x001DE550
extern "C" int _rwVectorSetMultFn(UnkFn_001DE550_3 multPoint, UnkFn_001DE550_4 multPoints,
                                  UnkFn_001DE550_3 multVector, UnkFn_001DE550_4 multVectors)
{
    UnkStruct_vectorModule *globals = (UnkStruct_vectorModule *)(ourGlobals + vectorModule);

    if (multPoint == 0) {
        multPoint = func_001DE7C0;
    }
    globals->field_8 = multPoint;
    if (multPoints == 0) {
        multPoints = func_001DE5A8;
    }
    globals->field_C = multPoints;
    if (multVector == 0) {
        multVector = func_001DEA28;
    }
    globals->field_10 = multVector;
    if (multVectors == 0) {
        multVectors = func_001DE858;
    }
    globals->field_14 = multVectors;
    return 1;
}

extern int D_004E278C;                 // [C] gp-relative 0x004E278C = vectorModule + 4: the vector module's open count

// [C] the vector module's open (instance, offset, size): vectorModule+4 += 1; the four default functions are stored at
// ourGlobals + offset + 0x14 (func_001DE858), +8 (func_001DE7C0), +0xC (func_001DE5A8), +0x10 (func_001DEA28);
// vectorModule = offset. Returns the instance. [L] _rwVectorOpen.
// 0x001DE4E0
extern "C" void *func_001DE4E0(void *instance, int offset, int size)
{
    UnkStruct_vectorModule *globals = (UnkStruct_vectorModule *)(ourGlobals + offset);
    int count = D_004E278C;

    count = count + 1;
    globals->field_14 = func_001DE858;
    globals->field_8 = func_001DE7C0;
    globals->field_C = func_001DE5A8;
    globals->field_10 = func_001DEA28;
    D_004E278C = count;
    vectorModule = offset;
    return instance;
}

// [C] vectorModule+4 -= 1; returns the instance. [L] _rwVectorClose.
// 0x001DE538
extern "C" void *func_001DE538(void *instance, int offset, int size)
{
    D_004E278C = D_004E278C - 1;
    return instance;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): vector length / normalize and the default multiply functions (all scalar float code).

extern "C" float sqrtf(float x);
extern "C" void *RwErrorSet(void *error);
extern "C" int _rwerror(int code);

// [C] RwError on the stack: {plugin ID 1, _rwerror(code)} ([L] RwError).
struct UnkStruct_RwErrorV3d {
    int field_0;
    int field_4;
};

// [C] f2 = x*x + y*y (first two) + z*z; len = sqrt of it ($f20 in the asm: sqrt.s; a callee-saved register that the
// function saves and restores). When 0 < len the factor is 1.0 / len, else len itself. out = in * factor per component
// (x, y, z; each input component read right before its store). When len <= 0 error {1, _rwerror(0x19)} is set
// (RwErrorSet). Returns len.
// 0x001DE198
extern "C" float RwV3dNormalize(float *out, const float *in)
{
    float f0, f1, f2, len;
    UnkStruct_RwErrorV3d err;

    f0 = in[0];
    f2 = in[1];
    f0 = f0 * f0;
    f1 = in[2];
    f2 = f2 * f2;
    f1 = f1 * f1;
    f0 = f0 + f2;
    f0 = f0 + f1;
    len = sqrtf(f0);
    if (0.0f < len) {
        f0 = 1.0f;
        f2 = f0 / len;
        f0 = in[0];
    } else {
        f2 = len;
        f0 = in[0];
    }
    f0 = f0 * f2;
    out[0] = f0;
    f1 = in[1];
    f1 = f1 * f2;
    out[1] = f1;
    f0 = in[2];
    f0 = f0 * f2;
    out[2] = f0;
    if (len <= 0.0f) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x19);
        RwErrorSet(&err);
    }
    return len;
}

// [C] sqrt of x*x + y*y + z*z, summed in that order.
// 0x001DE260
extern "C" float RwV3dLength(const float *v)
{
    float f0, f1, f2;

    f0 = v[0];
    f2 = v[1];
    f0 = f0 * f0;
    f1 = v[2];
    f2 = f2 * f2;
    f1 = f1 * f1;
    f0 = f0 + f2;
    f0 = f0 + f1;
    return sqrtf(f0);
}

// [C] (default multPoints) for count points (count <= 0: nothing) out[i] = in[i] * M (3x3 part of rows 0, 1, 2 at
// matrix +0x00 / +0x10 / +0x20 plus the translation row at +0x30): per component ((x*m0c + y*m1c) + z*m2c) + m3c,
// x then y then z stored; the asm unrolls by two with the same per-point arithmetic and order. Returns out.
// 0x001DE5A8
extern "C" void *func_001DE5A8(void *outv, const void *inv, int count, const void *mv)
{
    float *out = (float *)outv;
    const float *in = (const float *)inv;
    const float *m = (const float *)mv;
    float *o = out;
    const float *p = in;
    float f1, f2, f3, f6, f4, f8, f7, f5, f0, f9, f10;
    int n = count - 1;

    while (n >= 0) {
        f10 = p[0];
        f1 = m[0];
        f2 = m[1];
        f3 = m[2];
        f1 = f10 * f1;
        f2 = f10 * f2;
        f6 = m[4];
        f3 = f10 * f3;
        f8 = m[5];
        f10 = p[1];
        f4 = m[6];
        f6 = f10 * f6;
        f7 = m[8];
        f4 = f10 * f4;
        f9 = m[12];
        f8 = f10 * f8;
        f5 = m[9];
        f10 = p[2];
        f1 = f1 + f6;
        f0 = m[10];
        f3 = f3 + f4;
        f7 = f10 * f7;
        f0 = f10 * f0;
        f2 = f2 + f8;
        f1 = f1 + f7;
        f5 = f10 * f5;
        f3 = f3 + f0;
        f1 = f1 + f9;
        f2 = f2 + f5;
        o[0] = f1;
        f0 = m[13];
        f2 = f2 + f0;
        o[1] = f2;
        f0 = m[14];
        f3 = f3 + f0;
        o[2] = f3;
        o += 3;
        p += 3;
        n = n - 1;
    }
    return out;
}

// [C] (default multPoint) out = in * M as one point of func_001DE5A8 (same arithmetic and order). Returns out.
// 0x001DE7C0
extern "C" void *func_001DE7C0(void *outv, const void *inv, const void *mv)
{
    float *out = (float *)outv;
    const float *in = (const float *)inv;
    const float *m = (const float *)mv;
    float f1, f2, f3, f6, f4, f8, f7, f5, f0, f9, f10;

    f10 = in[0];
    f1 = m[0];
    f2 = m[1];
    f3 = m[2];
    f1 = f10 * f1;
    f2 = f10 * f2;
    f6 = m[4];
    f3 = f10 * f3;
    f8 = m[5];
    f10 = in[1];
    f4 = m[6];
    f6 = f10 * f6;
    f7 = m[8];
    f4 = f10 * f4;
    f9 = m[12];
    f8 = f10 * f8;
    f5 = m[9];
    f10 = in[2];
    f1 = f1 + f6;
    f0 = m[10];
    f3 = f3 + f4;
    f7 = f10 * f7;
    f0 = f10 * f0;
    f2 = f2 + f8;
    f1 = f1 + f7;
    f5 = f10 * f5;
    f3 = f3 + f0;
    f1 = f1 + f9;
    f2 = f2 + f5;
    out[0] = f1;
    f0 = m[13];
    f2 = f2 + f0;
    out[1] = f2;
    f0 = m[14];
    f3 = f3 + f0;
    out[2] = f3;
    return out;
}

// [C] (default multVectors) as func_001DE5A8 without the translation row: ((x*m0c + y*m1c) + z*m2c), count <= 0 does
// nothing. Returns out.
// 0x001DE858
extern "C" void *func_001DE858(void *outv, const void *inv, int count, const void *mv)
{
    float *out = (float *)outv;
    const float *in = (const float *)inv;
    const float *m = (const float *)mv;
    float *o = out;
    const float *p = in;
    float f0, f1, f2, f3, f4, f5, f6, f7;
    int n = count - 1;

    while (n >= 0) {
        f7 = p[0];
        f4 = m[0];
        f5 = m[1];
        f6 = m[2];
        f4 = f7 * f4;
        f5 = f7 * f5;
        f2 = m[4];
        f6 = f7 * f6;
        f3 = m[5];
        f7 = p[1];
        f0 = m[6];
        f2 = f7 * f2;
        f1 = m[8];
        f0 = f7 * f0;
        f3 = f7 * f3;
        f7 = p[2];
        f4 = f4 + f2;
        f6 = f6 + f0;
        f1 = f7 * f1;
        f5 = f5 + f3;
        f1 = f1 + f4;
        o[0] = f1;
        f0 = m[9];
        f0 = f7 * f0;
        f0 = f0 + f5;
        o[1] = f0;
        f1 = m[10];
        f1 = f7 * f1;
        f1 = f1 + f6;
        o[2] = f1;
        o += 3;
        p += 3;
        n = n - 1;
    }
    return out;
}

// [C] (default multVector) out = in * M's 3x3 part for one vector (func_001DE858's arithmetic). Returns out.
// 0x001DEA28
extern "C" void *func_001DEA28(void *outv, const void *inv, const void *mv)
{
    float *out = (float *)outv;
    const float *in = (const float *)inv;
    const float *m = (const float *)mv;
    float f0, f1, f2, f3, f4, f5, f6, f7;

    f7 = in[0];
    f4 = m[0];
    f5 = m[1];
    f6 = m[2];
    f4 = f7 * f4;
    f5 = f7 * f5;
    f2 = m[4];
    f6 = f7 * f6;
    f3 = m[5];
    f7 = in[1];
    f0 = m[6];
    f2 = f7 * f2;
    f1 = m[8];
    f0 = f7 * f0;
    f3 = f7 * f3;
    f7 = in[2];
    f4 = f4 + f2;
    f6 = f6 + f0;
    f1 = f7 * f1;
    f5 = f5 + f3;
    f1 = f1 + f4;
    out[0] = f1;
    f0 = m[9];
    f0 = f7 * f0;
    f0 = f0 + f5;
    out[1] = f0;
    f1 = m[10];
    f1 = f7 * f1;
    f1 = f1 + f6;
    out[2] = f1;
    return out;
}

// [C] (RwV3dNormalize without the error report, the function between _rwVectorClose and _rwVectorSetMultFn) out = in * k
// with s = sqrt((x*x + y*y) + z*z) of in (the `c1 0x10044` is sqrt.s $f1, $f1) and k = 1 / s when 0 < s, else s itself;
// the components are read right before their stores (in-place safe). Returns k ($f0 = $f2).
// 0x001DE460
extern "C" float _rwV3dNormalize(float *out, const float *in)
{
    float f0, f1, f2;

    f1 = in[0];
    f2 = in[1];
    f1 = f1 * f1;
    f0 = in[2];
    f2 = f2 * f2;
    f0 = f0 * f0;
    f1 = f1 + f2;
    f1 = f1 + f0;
    f1 = sqrtf(f1);
    if (0.0f < f1) {
        f0 = 1.0f;
        f2 = f0 / f1;
        f0 = in[0];
    } else {
        f2 = f1;
        f0 = in[0];
    }
    f0 = f0 * f2;
    out[0] = f0;
    f1 = in[1];
    f1 = f1 * f2;
    out[1] = f1;
    f0 = in[2];
    f0 = f0 * f2;
    out[2] = f0;
    return f2;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): the points transform entry (calls through the vector globals' multiply slot).

// [C] calls the function at +0xC of the vector globals (the multPoints slot: default func_001DE5A8) with (a0, a1, a2,
// a3) unchanged and returns a0 (saved in $s0).
// 0x001DE3A0
extern "C" void *RwV3dTransformPoints(void *out, const void *in, int count, const void *matrix)
{
    ((UnkStruct_vectorModule *)(ourGlobals + vectorModule))->field_C(out, in, count, matrix);
    return out;
}
