// RenderWare 3.6 core: the string functions (RW 3.1 file name: src/rwstring.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

// [C] ourGlobals + 0xF0 .. + 0x133: 17 function slots _rwStringOpen fills ([L] RwStringFunctions, 3.1 order; the
// [L] names below are 3.1's slot names). Only the addresses are stored here, so the signatures are 3.1's, unchecked;
// the printf / scanf slots are declared without `...` (decomp2host's gfn has no variadic form).
struct UnkStruct_ourGlobals {
    unsigned char pad_0[0xF0];
    int (*field_F0)(char *, const char *);                  // [C] sprintf ([L] vecSprintf)
    int (*field_F4)(char *, const char *, void *);          // [C] vsprintf ([L] vecVsprintf)
    char *(*field_F8)(char *, const char *);                // [C] strcpy ([L] vecStrcpy)
    char *(*field_FC)(char *, const char *, unsigned int);  // [C] strncpy ([L] vecStrncpy)
    char *(*field_100)(char *, const char *);               // [C] strcat ([L] vecStrcat)
    char *(*field_104)(char *, const char *, unsigned int); // [C] strncat ([L] vecStrncat)
    char *(*field_108)(const char *, int);                  // [C] func_001DFC80 ([L] vecStrrchr)
    char *(*field_10C)(const char *, int);                  // [C] func_001DFC48 ([L] vecStrchr)
    char *(*field_110)(const char *, const char *);         // [C] func_00127BF8 ([L] vecStrstr)
    int (*field_114)(const char *, const char *);           // [C] strcmp ([L] vecStrcmp)
    int (*field_118)(const char *, const char *, unsigned int); // [C] strncmp ([L] vecStrncmp)
    int (*field_11C)(const char *, const char *);           // [C] func_001DFB48 ([L] vecStricmp)
    unsigned int (*field_120)(const char *);                // [C] strlen ([L] vecStrlen)
    char *(*field_124)(char *);                             // [C] func_001DFBB8 ([L] vecStrUpr)
    char *(*field_128)(char *);                             // [C] func_001DFC00 ([L] vecStrLwr)
    char *(*field_12C)(char *, const char *);               // [C] func_00128BE8 ([L] vecStrtok)
    int (*field_130)(const char *, const char *);           // [C] sscanf ([L] vecSscanf)
};
extern UnkStruct_ourGlobals ourGlobals;

// [C] retail's own newlib functions, spelled func_<address> so decomp2host binds retail's code rather than the
// host's C library (only their addresses are stored): sprintf 0x00126B40, vsprintf 0x0012BC70, strcpy 0x001272D0,
// strncpy 0x00127960, strcat 0x00126EC8, strncat 0x001275F8, strcmp 0x00127188, strncmp 0x001277A8, strlen
// 0x001273E8, sscanf 0x00126BD8 (names from the decomp's symbol tables).
extern "C" int func_00126B40(char *, const char *);
extern "C" int func_0012BC70(char *, const char *, void *);
extern "C" char *func_001272D0(char *, const char *);
extern "C" char *func_00127960(char *, const char *, unsigned int);
extern "C" char *func_00126EC8(char *, const char *);
extern "C" char *func_001275F8(char *, const char *, unsigned int);
extern "C" int func_00127188(const char *, const char *);
extern "C" int func_001277A8(const char *, const char *, unsigned int);
extern "C" unsigned int func_001273E8(const char *);
extern "C" int func_00126BD8(const char *, const char *);
// [C] RW's own helpers (unnamed in the tables); [L] 3.1 slot names as above.
extern "C" char *func_001DFC80(const char *, int);
extern "C" char *func_001DFC48(const char *, int);
extern "C" char *func_00127BF8(const char *, const char *);
extern "C" int func_001DFB48(const char *, const char *);
extern "C" char *func_001DFBB8(char *);
extern "C" char *func_001DFC00(char *);
extern "C" char *func_00128BE8(char *, const char *);

// [C] fills the 17 slots (stores in the asm's order) and returns 1.
// 0x001DF540
extern "C" int _rwStringOpen(void)
{
    ourGlobals.field_120 = func_001273E8;
    ourGlobals.field_124 = func_001DFBB8;
    ourGlobals.field_128 = func_001DFC00;
    ourGlobals.field_12C = func_00128BE8;
    ourGlobals.field_130 = func_00126BD8;
    ourGlobals.field_F0 = func_00126B40;
    ourGlobals.field_F4 = func_0012BC70;
    ourGlobals.field_F8 = func_001272D0;
    ourGlobals.field_FC = func_00127960;
    ourGlobals.field_100 = func_00126EC8;
    ourGlobals.field_104 = func_001275F8;
    ourGlobals.field_108 = func_001DFC80;
    ourGlobals.field_10C = func_001DFC48;
    ourGlobals.field_110 = func_00127BF8;
    ourGlobals.field_114 = func_00127188;
    ourGlobals.field_118 = func_001277A8;
    ourGlobals.field_11C = func_001DFB48;
    return 1;
}

// [C] empty (jr ra).
// 0x001DFA40
extern "C" void _rwStringClose(void)
{
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): RW's own string helpers (slots of the string function table, see _rwStringOpen).

// [C] (slot +0x11C, [L] rwstricmp) 0 when either pointer is NULL. Else compares the two strings byte by byte (signed
// chars), 'A'..'Z' mapped to lower case on both sides; the first differing pair returns (a - b), both strings ending
// together returns 0.
// 0x001DFB48
extern "C" int func_001DFB48(const char *a, const char *b)
{
    int c1;
    int c2;

    if (a == 0) {
        return 0;
    }
    if (b == 0) {
        return 0;
    }
    for (;;) {
        c1 = *a;
        if ((unsigned int)(c1 - 0x41) < 0x1A) {
            c1 = (signed char)(c1 + 0x20);
        }
        c2 = *b;
        if ((unsigned int)(c2 - 0x41) < 0x1A) {
            c2 = (signed char)(c2 + 0x20);
        }
        a = a + 1;
        if (c1 != c2) {
            return c1 - c2;
        }
        if (c1 == 0) {
            return 0;
        }
        b = b + 1;
    }
}

// [C] (slot +0x124, [L] rwstrupr) 'a'..'z' -> upper case in place until the terminator; NULL is returned as it is.
// Returns the string.
// 0x001DFBB8
extern "C" char *func_001DFBB8(char *s)
{
    char *p;
    int c;

    if (s == 0) {
        return s;
    }
    if (*s == 0) {
        return s;
    }
    p = s;
    c = *p;
    do {
        if ((unsigned int)(c - 0x61) < 0x1A) {
            *p = (char)(c - 0x20);
        }
        p = p + 1;
        if (*p == 0) {
            break;
        }
        c = *p;
    } while (1);
    return s;
}

// [C] (slot +0x128, [L] rwstrlwr) 'A'..'Z' -> lower case in place until the terminator. Returns the string.
// 0x001DFC00
extern "C" char *func_001DFC00(char *s)
{
    char *p;
    int c;

    if (s == 0) {
        return s;
    }
    if (*s == 0) {
        return s;
    }
    p = s;
    c = *p;
    do {
        if ((unsigned int)(c - 0x41) < 0x1A) {
            *p = (char)(c + 0x20);
        }
        p = p + 1;
        if (*p == 0) {
            break;
        }
        c = *p;
    } while (1);
    return s;
}

// [C] (slot +0x10C, [L] rwstrchr) the first position of the (sign-extended low byte of the) character in the string,
// the terminator included, else NULL.
// 0x001DFC48
extern "C" char *func_001DFC48(const char *s0, int ch)
{
    int c = (signed char)ch;
    char *s = (char *)s0;
    char *p = s;
    int v;

    for (;;) {
        v = *p;
        p = p + 1;
        if (v == c) {
            return s;
        }
        if (v == 0) {
            return 0;
        }
        s = s + 1;
    }
}

// [C] (slot +0x108, [L] rwstrrchr) the last position of the character (terminator included), else NULL.
// 0x001DFC80
extern "C" char *func_001DFC80(const char *s0, int ch)
{
    int c = (signed char)ch;
    char *s = (char *)s0;
    char *p = s;
    char *found = 0;
    int v;

    for (;;) {
        v = *p;
        p = p + 1;
        if (v == c) {
            found = s;
        }
        if (v == 0) {
            break;
        }
        s = s + 1;
    }
    return found;
}
