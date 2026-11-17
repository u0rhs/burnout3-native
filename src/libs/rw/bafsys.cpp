// RenderWare 3.6 core: the file functions (RW 3.1 file name: src/bafsys.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

// [C] ourGlobals + 0xC4 .. + 0xEF: 11 function slots _rwFileSystemOpen fills ([L] RwFileFunctions, 3.1 order and slot
// names). Only the addresses are stored here, so the signatures are 3.1's, unchecked.
struct UnkStruct_ourGlobals {
    unsigned char pad_0[0xC4];
    int (*field_C4)(const char *);                                        // [C] rwfexist ([L] rwfexist)
    void *(*field_C8)(const char *, const char *);                        // [C] fopen ([L] rwfopen)
    int (*field_CC)(void *);                                              // [C] func_00121468 ([L] rwfclose)
    unsigned int (*field_D0)(void *, unsigned int, unsigned int, void *); // [C] fread ([L] rwfread)
    unsigned int (*field_D4)(const void *, unsigned int, unsigned int, void *); // [C] fwrite ([L] rwfwrite)
    char *(*field_D8)(char *, int, void *);                               // [C] fgets ([L] rwfgets)
    int (*field_DC)(const char *, void *);                                // [C] fputs ([L] rwfputs)
    int (*field_E0)(void *);                                              // [C] func_00121568 ([L] rwfeof)
    int (*field_E4)(void *, int, int);                                    // [C] fseek ([L] rwfseek)
    int (*field_E8)(void *);                                              // [C] fflush ([L] rwfflush)
    int (*field_EC)(void *);                                              // [C] ftell ([L] rwftell)
};
extern UnkStruct_ourGlobals ourGlobals;
extern char D_004E1718[];   // [C] "rb"
// [C] retail's own newlib functions, spelled func_<address> so decomp2host binds retail's code rather than the
// host's C library (only their addresses are stored): fopen 0x00121C00, fread 0x00121D68, fwrite 0x00122D80,
// fgets 0x00121690, fputs 0x00121D18, fseek 0x001222F0, fflush 0x00121578, ftell 0x00122800 (names from the decomp's
// symbol tables).
extern "C" void *func_00121C00(const char *, const char *);
extern "C" unsigned int func_00121D68(void *, unsigned int, unsigned int, void *);
extern "C" unsigned int func_00122D80(const void *, unsigned int, unsigned int, void *);
extern "C" char *func_00121690(char *, int, void *);
extern "C" int func_00121D18(const char *, void *);
extern "C" int func_001222F0(void *, int, int);
extern "C" int func_00121578(void *);
extern "C" int func_00122800(void *);
// [C] unnamed in the tables; [L] fclose and feof by their slots (both lie among newlib's stdio, 0x00121468 /
// 0x00121568, next to fflush 0x00121578).
extern "C" int func_00121468(void *);
extern "C" int func_00121568(void *);

// [C] opens name "rb" through ourGlobals + 0xC8 and, when that worked, closes it through + 0xCC; returns whether it
// opened.
// 0x001D8B98
extern "C" int rwfexist(const char *name)
{
    void *fp = ourGlobals.field_C8(name, D_004E1718);
    int result = fp != 0;

    if (fp != 0) {
        ourGlobals.field_CC(fp);
    }
    return result;
}

// [C] fills the 11 slots (stores in the asm's order) and returns 1.
// 0x001D8AE8
extern "C" int _rwFileSystemOpen(void)
{
    ourGlobals.field_EC = func_00122800;
    ourGlobals.field_C4 = rwfexist;
    ourGlobals.field_C8 = func_00121C00;
    ourGlobals.field_CC = func_00121468;
    ourGlobals.field_D0 = func_00121D68;
    ourGlobals.field_D4 = func_00122D80;
    ourGlobals.field_D8 = func_00121690;
    ourGlobals.field_DC = func_00121D18;
    ourGlobals.field_E0 = func_00121568;
    ourGlobals.field_E4 = func_001222F0;
    ourGlobals.field_E8 = func_00121578;
    return 1;
}

// [C] empty (jr ra).
// 0x001D8B80
extern "C" void _rwFileSystemClose(void)
{
}

// [C] renderware.s 0x1D8B88 (3 instructions): returns the address of the file-function slots at ourGlobals + 0xC4
// ([L] RW 3.1's RwOsGetFileInterface: &RWSRCGLOBAL(fileFuncs)); CGTFileSystem::InstallToRenderWare replaces slots
// through it.
// 0x001D8B88
extern "C" void *func_001D8B88(void)
{
    return &ourGlobals.field_C4;
}
