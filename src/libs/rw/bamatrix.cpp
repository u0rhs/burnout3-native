// RenderWare 3.6 core: matrices (RW 3.1 file name: src/bamatrix.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)
extern int matrixModule;               // [C] gp-relative 0x004E2760 (8 bytes): +0 the matrix globals' offset in ourGlobals

typedef void (*UnkFn_001DAB10)(void *, const void *, const void *);   // [L] (dst, a, b)

// [C] the matrix globals at ourGlobals + matrixModule ([L] rwMatrixGlobals).
struct UnkStruct_matrixModule {
    unsigned char pad_0[0x8];
    UnkFn_001DAB10 field_8;            // [C] the multiply function ([L] multMatrix (dst, a, b))
};

// [C] the default: reads a1 / a2 as 3x4 float matrices (rows at 0x10) and writes a0 ([L] matrix multiply).
extern "C" void func_001D8BF0(void *dst, const void *a, const void *b);

// [C] multFn, or func_001D8BF0 when NULL, stored at +8 of the matrix globals. Returns 1.
// 0x001DAB10
extern "C" int _rwMatrixSetMultFn(UnkFn_001DAB10 multFn)
{
    if (multFn == 0) {
        multFn = func_001D8BF0;
    }
    ((UnkStruct_matrixModule *)(ourGlobals + matrixModule))->field_8 = multFn;
    return 1;
}

extern int D_004E2764;                 // [C] gp-relative 0x004E2764 = matrixModule + 4: the matrix module's open count
extern unsigned int D_004B8240[];      // [C] 0x004B8240: 12 bytes, the default matrix tolerances ([L] three floats)
extern unsigned char D_00670AE0[];     // [C] in-place space for the matrix free list ([L] _rwMatrixFreeList's preallocation)
extern int _rwMatrixFreeListBlockSize;           // [C] gp-relative 0x004E171C
extern int _rwMatrixFreeListPreallocBlocks;      // [C] gp-relative 0x004E1720

extern "C" void *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock, int alignment,
                                                     int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint);
extern "C" int RwFreeListDestroy(void *freeList);

// [C] a tolerance block of 12 bytes ([L] RwMatrixTolerance: three floats).
struct UnkStruct_matrixTolerances {
    unsigned int field_0;
    unsigned int field_4;
    unsigned int field_8;
};

// [C] the matrix globals as _rwMatrixOpen builds them.
struct UnkStruct_matrixModuleOpen {
    void *field_0;                     // [C] the matrix free list (entries of 0x40, alignment 0x10)
    unsigned int field_4;              // [C] 0x20000
    UnkFn_001DAB10 field_8;            // [C] func_001D8BF0 (the multiply function)
    UnkStruct_matrixTolerances field_C;   // [C] the tolerances
};

// [C] copies the 12 bytes at tol to the matrix globals + 0xC (an 8-byte then a 4-byte unaligned copy). Returns 1.
// 0x001DA3E0
extern "C" int RwEngineSetMatrixTolerances(const UnkStruct_matrixTolerances *tol)
{
    UnkStruct_matrixModuleOpen *globals = (UnkStruct_matrixModuleOpen *)(ourGlobals + matrixModule);
    unsigned int a = tol->field_0;
    unsigned int b = tol->field_4;
    unsigned int c = tol->field_8;

    globals->field_C.field_0 = a;
    globals->field_C.field_4 = b;
    globals->field_C.field_8 = c;
    return 1;
}

// [C] matrixModule = offset; the free list (entry 0x40, block size _rwMatrixFreeListBlockSize, alignment 0x10,
// _rwMatrixFreeListPreallocBlocks blocks preallocated in D_00670AE0, hint 0x4000D) is stored at +0; on failure returns
// 0. Else +4 = 0x20000, +8 = func_001D8BF0, the default tolerances (12 bytes at D_004B8240, copied through a local)
// are set with RwEngineSetMatrixTolerances, the open count (matrixModule+4) += 1; returns the instance.
// 0x001DAB40
extern "C" void *_rwMatrixOpen(void *instance, int offset, int size)
{
    UnkStruct_matrixModuleOpen *globals;
    UnkStruct_matrixTolerances tol;
    void *freeList;

    matrixModule = offset;
    freeList = RwFreeListCreateAndPreallocateSpace(0x40, _rwMatrixFreeListBlockSize, 0x10,
                                                   _rwMatrixFreeListPreallocBlocks, D_00670AE0, 0x4000D);
    globals = (UnkStruct_matrixModuleOpen *)(ourGlobals + matrixModule);
    globals->field_0 = freeList;
    if (freeList == 0) {
        return 0;
    }
    globals->field_4 = 0x20000;
    globals->field_8 = func_001D8BF0;
    tol.field_0 = D_004B8240[0];
    tol.field_4 = D_004B8240[1];
    tol.field_8 = D_004B8240[2];
    RwEngineSetMatrixTolerances(&tol);
    D_004E2764 = D_004E2764 + 1;
    return instance;
}

// [C] if the free list (+0 of the matrix globals) is set: RwFreeListDestroy it and clear +0 (matrixModule re-read);
// then the open count (matrixModule+4) -= 1; returns the instance.
// 0x001DABF8
extern "C" void *_rwMatrixClose(void *instance, int offset, int size)
{
    void *freeList = ((UnkStruct_matrixModuleOpen *)(ourGlobals + matrixModule))->field_0;

    if (freeList != 0) {
        RwFreeListDestroy(freeList);
        ((UnkStruct_matrixModuleOpen *)(ourGlobals + matrixModule))->field_0 = 0;
    }
    D_004E2764 = D_004E2764 - 1;
    return instance;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): the default matrix multiply (scalar float code, no VU0).

// [C] (default multMatrix) dst = a * b for the 3x4 matrices at +0x00 / +0x10 / +0x20 (rows, first three floats each)
// and +0x30 (translation). For rows r = 0..2: dst[r][c] = (a[r][0]*b[0][c] + a[r][1]*b[1][c]) + a[r][2]*b[2][c],
// c = 0..2, stored row by row; then the translation row: dst[3][c] = ((a[3][0]*b[0][c] + a[3][1]*b[1][c]) +
// a[3][2]*b[2][c]) + b[3][c]. Every operand is read right before its use (dst may alias a or b).
// 0x001D8BF0
extern "C" void func_001D8BF0(void *dstv, const void *av, const void *bv)
{
    float *dst = (float *)dstv;
    const float *a = (const float *)av;
    const float *b = (const float *)bv;
    int r;
    int c;
    float f0, f1, f2;

    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            f1 = a[r * 4 + 0] * b[0 + c];
            f2 = a[r * 4 + 1] * b[4 + c];
            f0 = a[r * 4 + 2] * b[8 + c];
            f1 = f1 + f2;
            f1 = f1 + f0;
            dst[r * 4 + c] = f1;
        }
    }
    for (c = 0; c < 3; c++) {
        f0 = a[12] * b[0 + c];
        f2 = a[13] * b[4 + c];
        f1 = a[14] * b[8 + c];
        f0 = f0 + f2;
        f0 = f0 + f1;
        f0 = f0 + b[12 + c];
        dst[12 + c] = f0;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): RwMatrixOptimize (scalar float code) and RwMatrixMultiply (VU0).

// [C] (VU0) dst = a * b for RW matrices (rows of 4 floats: right, up, at, pos; word 3 of the first three rows = the
// flags word). Rows of a go to $vf1..$vf4, rows of b to $vf5..$vf8; result row i (xyz) = a[i].x * b[0] + a[i].y * b[1]
// + a[i].z * b[2], plus b[3] for the position row (vmaddw with $vf0.w = 1). The w lanes of $vf9..$vf12 are left
// from earlier use ([G] the asm does not write them; the stores put those lanes in word 3 of rows 1..3), except that
// word 3 of row 0 is overwritten with (a.flags & b.flags) (both lwu'd from word 3). Returns dst.
// 0x001DA4E8
extern "C" void *RwMatrixMultiply(void *dst, const void *a, const void *b)
{
    asm volatile(
        "lqc2 $vf1, 0x0(%1)" "\n\t"
        "lqc2 $vf2, 0x10(%1)" "\n\t"
        "lqc2 $vf3, 0x20(%1)" "\n\t"
        "lqc2 $vf4, 0x30(%1)" "\n\t"
        "lwu $a0, 0xC(%1)" "\n\t"
        "lqc2 $vf5, 0x0(%2)" "\n\t"
        "lqc2 $vf6, 0x10(%2)" "\n\t"
        "lqc2 $vf7, 0x20(%2)" "\n\t"
        "lqc2 $vf8, 0x30(%2)" "\n\t"
        "lwu $v1, 0xC(%2)" "\n\t"
        "vmulax.xyz $ACC, $vf5, $vf1x" "\n\t"
        "vmadday.xyz $ACC, $vf6, $vf1y" "\n\t"
        "vmaddz.xyz $vf9, $vf7, $vf1z" "\n\t"
        "vmulax.xyz $ACC, $vf5, $vf2x" "\n\t"
        "vmadday.xyz $ACC, $vf6, $vf2y" "\n\t"
        "vmaddz.xyz $vf10, $vf7, $vf2z" "\n\t"
        "vmulax.xyz $ACC, $vf5, $vf3x" "\n\t"
        "vmadday.xyz $ACC, $vf6, $vf3y" "\n\t"
        "vmaddz.xyz $vf11, $vf7, $vf3z" "\n\t"
        "vmulax.xyz $ACC, $vf5, $vf4x" "\n\t"
        "vmadday.xyz $ACC, $vf6, $vf4y" "\n\t"
        "vmaddaz.xyz $ACC, $vf7, $vf4z" "\n\t"
        "vmaddw.xyz $vf12, $vf8, $vf0w" "\n\t"
        "and $v1, $v1, $a0" "\n\t"
        "sqc2 $vf9, 0x0(%0)" "\n\t"
        "sqc2 $vf10, 0x10(%0)" "\n\t"
        "sqc2 $vf11, 0x20(%0)" "\n\t"
        "sqc2 $vf12, 0x30(%0)" "\n\t"
        "sw $v1, 0xC(%0)"
        : : "r"(dst), "r"(a), "r"(b) : "memory");
    return dst;
}

// [C] the matrix's flags word (word 3) is updated from three tests against tol (3 floats; NULL = the matrix globals'
// tolerance block at ourGlobals + matrixModule + 0xC). With R = right (words 0..2), U = up (4..6), A = at (8..10),
// P = position (12..14) and n2(v) = (x*x + y*y) + z*z:
//   bit 0 (|1, else cleared): ((n2(R) - 1)^2 + (n2(U) - 1)^2) + (n2(A) - 1)^2 <= tol[0]   ([L] normal);
//   bit 1 (|2): ((U.A)^2 + (A.R)^2) + (R.U)^2 <= tol[1], each dot summed (x + y) + z      ([L] orthogonal);
//   bit 0x20000 (|, else cleared; tested only when both of the above held): the squared distance of the matrix from
//   the identity, ((rx-1)^2 + ry^2) + rz^2 + ((ux^2 + (uy-1)^2) + uz^2) + (ax^2 + ay^2 + (az-1)^2) + (px^2 + py^2) + pz^2
//   (summed in the asm's order) <= tol[2]                                                  ([L] identity).
// Comparisons are <= (false for NaN). Returns the matrix. The three stack temporaries (sp+0 .. 0x18) stay in registers.
// 0x001D9990
extern "C" void *RwMatrixOptimize(void *matrix, const void *tolerance)
{
    float *m = (float *)matrix;
    const float *tol = (const float *)tolerance;
    float f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19;
    int a3, a2, t0;
    unsigned int flags;

    if (tol == 0) {
        tol = (const float *)(ourGlobals + matrixModule + 0xC);
    }
    f9 = m[1];
    f12 = m[5];
    f8 = m[4];
    f18 = f9 * f9;
    f13 = m[0];
    f0 = f12 * f12;
    f17 = f8 * f8;
    f10 = m[6];
    f2 = f13 * f13;
    f7 = m[9];
    f6 = m[2];
    f19 = f10 * f10;
    f5 = m[8];
    f1 = f7 * f7;
    f2 = f2 + f18;
    f11 = m[10];
    f4 = f5 * f5;
    f14 = 1.0f;
    f16 = f6 * f6;
    f0 = f17 + f0;
    f15 = f4 + f1;
    f3 = f11 * f11;
    f2 = f2 + f16;
    f0 = f0 + f19;
    f3 = f15 + f3;
    f2 = f2 - f14;
    f0 = f0 - f14;
    f3 = f3 - f14;
    f1 = f2 * f2;
    f4 = f0 * f0;
    f2 = f3 * f3;
    f1 = f1 + f4;
    f0 = tol[0];
    f1 = f1 + f2;
    a3 = (f1 <= f0) ? 1 : 0;
    f2 = f5 * f13;
    f3 = f7 * f9;
    f0 = f8 * f5;
    f4 = f12 * f7;
    f2 = f2 + f3;
    f3 = f9 * f12;
    f0 = f0 + f4;
    f4 = f10 * f11;
    f5 = f11 * f6;
    f1 = f13 * f8;
    f0 = f0 + f4;
    f2 = f2 + f5;
    f1 = f1 + f3;
    f6 = f6 * f10;
    f3 = f0 * f0;
    f4 = f2 * f2;
    f1 = f1 + f6;
    f3 = f3 + f4;
    f2 = f1 * f1;
    f0 = tol[1];
    f3 = f3 + f2;
    a2 = (f3 <= f0) ? 1 : 0;
    t0 = 0;
    if (a3 != 0 && a2 != 0) {
        f2 = f13 - f14;
        f0 = f12 - f14;
        f1 = m[12];
        f5 = f11 - f14;
        f3 = m[13];
        f2 = f2 * f2;
        f4 = m[14];
        f0 = f0 * f0;
        f6 = tol[2];
        f5 = f5 * f5;
        f2 = f2 + f18;
        f0 = f17 + f0;
        f1 = f1 * f1;
        f2 = f2 + f16;
        f0 = f0 + f19;
        f3 = f3 * f3;
        f5 = f15 + f5;
        f0 = f2 + f0;
        f1 = f1 + f3;
        f4 = f4 * f4;
        f0 = f0 + f5;
        f1 = f1 + f4;
        f0 = f0 + f1;
        if (f0 <= f6) {
            t0 = 1;
        }
    }
    flags = ((unsigned int *)m)[3];
    if (a3 != 0) {
        flags = flags | 1;
    } else {
        flags = flags & 0xFFFFFFFE;
    }
    if (a2 != 0) {
        flags = flags | 2;
    } else {
        flags = flags & 0xFFFFFFFD;
    }
    if (t0 != 0) {
        flags = flags | 0x20000;
    } else {
        flags = flags & 0xFFFDFFFF;
    }
    ((unsigned int *)m)[3] = flags;
    return matrix;
}

// fb1: [C] the matrix's flags word (word 3) loses bits 0, 1 and 0x20000 -- the three RwMatrixOptimize sets ([L] normal,
// orthogonal, identity: the matrix was changed, so none is known any more). Returns the matrix.
// 0x001DA860
extern "C" void *RwMatrixUpdate(void *matrix)
{
    ((unsigned int *)matrix)[3] &= 0xFFFDFFFCu;
    return matrix;
}
