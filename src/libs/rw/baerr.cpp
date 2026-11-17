// RenderWare 3.6 core: errors (RW 3.1 file name: src/baerr.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)
extern int errorModule;                // [C] gp-relative 0x004E2758 (8 bytes): +0 the error globals' offset in ourGlobals

// [C] an error: {plugin ID, code} ([L] RwError); the error globals at ourGlobals + errorModule start with one
// ([L] errorGlobals.errError).
struct UnkStruct_RwErrorSet {
    int field_0;                       // [L] pluginID
    int field_4;                       // [L] errorCode; 0x80000000 = no error [C RwErrorSet's test]
};

// [C] only while no error is pending (globals field_0 == 0 and field_4 == 0x80000000): field_0 = code->field_0 when
// code->field_4 >= 0 (signed), else 0; then field_4 = code->field_4 (errorModule re-read). Returns code.
// 0x001D8A80
extern "C" UnkStruct_RwErrorSet *RwErrorSet(UnkStruct_RwErrorSet *code)
{
    UnkStruct_RwErrorSet *globals = (UnkStruct_RwErrorSet *)(ourGlobals + errorModule);

    if (globals->field_0 == 0 && globals->field_4 == (int)0x80000000) {
        if (code->field_4 >= 0) {
            globals->field_0 = code->field_0;
        } else {
            globals->field_0 = 0;
        }
        ((UnkStruct_RwErrorSet *)(ourGlobals + errorModule))->field_4 = code->field_4;
    }
    return code;
}

// [C] variadic in RW ([L] _rwerror(RwInt32 code, ...)): returns code. Its prologue spills a1..t3 (sd) and
// f12/f14/f16/f18 (swc1) into its own 0x80-byte frame below the caller's sp (+0x38..+0x7F) and never reads them; those
// dead stack stores are not expressed here. Declared with the code alone, as at its callers (b3host's variadic
// guest_fn does not compile at -O2, see badevice_more.cpp).
// 0x001D89B8
extern "C" int _rwerror(int code)
{
    return code;
}

extern int D_004E2754;                 // [C] gp-relative 0x004E2754: a 4-byte word just below errorModule, used as a counter
extern int D_004E275C;                 // [C] gp-relative 0x004E275C = errorModule + 4: the error module's open count

// [C] a module callback (instance, offset in ourGlobals, size): D_004E2754 += 1; returns the instance. Only the
// first argument is read. [G] the open callback of a module registered just before the error module.
// 0x001D8988
extern "C" void *func_001D8988(void *instance, int offset, int size)
{
    D_004E2754 = D_004E2754 + 1;
    return instance;
}

// [C] D_004E2754 -= 1; returns the instance ([G] the close callback paired with func_001D8988).
// 0x001D89A0
extern "C" void *func_001D89A0(void *instance, int offset, int size)
{
    D_004E2754 = D_004E2754 - 1;
    return instance;
}

// [C] the error module's open: errorModule+4 (open count) += 1; the globals at ourGlobals + offset get field_4 =
// 0x80000000 (no error) then (after errorModule = offset) field_0 = 0. Returns the instance. [L] _rwErrorOpen.
// 0x001D89F8
extern "C" void *func_001D89F8(void *instance, int offset, int size)
{
    int count = D_004E275C;
    UnkStruct_RwErrorSet *globals;

    D_004E275C = count + 1;
    globals = (UnkStruct_RwErrorSet *)(ourGlobals + offset);
    globals->field_4 = (int)0x80000000;
    errorModule = offset;
    globals->field_0 = 0;
    return instance;
}

// [C] the error module's close: errorModule+4 -= 1; returns the instance. [L] _rwErrorClose.
// 0x001D8A28
extern "C" void *func_001D8A28(void *instance, int offset, int size)
{
    D_004E275C = D_004E275C - 1;
    return instance;
}
