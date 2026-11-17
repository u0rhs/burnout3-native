// Sony EE library libscf (SDK 2.8.0 as retail links it): reads the PS2 OSD / ROM configuration (libscf.s, data asm/data/sce/libscf.{data,rodata}.s,
// code 0x0024ABB0-0x0024B258). Written from the retail asm. Tags: [C] confirmed from the asm, [L] likely, [G] guess.
// [C] Two sources of settings: the console's OSD configuration (kernel GetOsdConfigParam / GetOsdConfigParam2) or, on a T10K (a
// "TOOL" console: ROM version string 'T' at +4, see IsT10K), a fixed record in the library's data (__ps2_libinfo__ + 8..0xF, filled by
// func_0024ACC0). The ROM version string is read once from "rom0:ROMVER" (14 bytes) into gConfig and cached (gConfig[0] != 0).
// [C] The date / time arguments are 8-byte records of BCD bytes: +1 second [L], +2 minute [L], +3 hour [L], +5 day [L], +6 month [L],
// +7 year [L] (the field meanings are [L] from how AdjustTime / adddate use them; the offsets are [C]). The month table is the 12
// bytes at D_004BB5F8 (31 28 31 30 ...), copied to the stack with the February entry set to 29 when (year & 3) == 0 (as written,
// no century rule).
// boot2 (2026-11-06).

// ---- data
extern unsigned char __ps2_libinfo__[];   // [C] 0x0049BA18: +8 timezone (s16, 0x21C), +0xA aspect, +0xB date notation, +0xC language, +0xD spdif, +0xE summer time, +0xF time notation
extern char gConfig[];                    // [C] 0x0049BA28: ROM version string cache (8 bytes in the table, 14 read; [0] == 0 until read, [4] == 'T' on a T10K)
extern char D_004BB5E8[];                 // [C] 0x004BB5E8: "rom0:ROMVER"
extern unsigned char D_004BB5F8[];        // [C] 0x004BB5F8: days per month, 12 bytes

// ---- kernel / file calls
extern "C" int sceOpen(const char *name, int flags, int mode);   // libkernl_fio.cpp: a2 = the mode word sent
extern "C" int sceRead(int fd, void *buf, int size);
extern "C" int sceClose(int fd);
extern "C" void GetOsdConfigParam(unsigned int *param);
extern "C" void GetOsdConfigParam2(unsigned char *param, int size, int offset);

extern "C" char *GetRomName(void);
extern "C" int IsT10K(void);
extern "C" int sceScfGetTimeZone(void);
extern "C" int sceScfGetSummerTime(void);
extern "C" unsigned int tobcd(unsigned int a);
extern "C" unsigned int frombcd(unsigned int a);
extern "C" void convertfrombcd(unsigned char *p);
extern "C" void converttobcd(unsigned char *p);
extern "C" void adddate(unsigned char *p);
extern "C" void subdate(unsigned char *p);
extern "C" void addhour(unsigned char *p);
extern "C" void subhour(unsigned char *p);
extern "C" void AdjustTime(unsigned char *p, int minutes);

// [C] reads "rom0:ROMVER" (14 bytes) into gConfig once; returns gConfig.
// 0x0024ABB0
extern "C" char *GetRomName(void)
{
    if (gConfig[0] == 0) {
        // retail passes no mode: a2 is what the callers left, 24 on this path (request +0x10 == 24), and it
        // lands in the request buffer
        int fd = sceOpen(D_004BB5E8, 1, 24);
        if (fd >= 0) {
            sceRead(fd, gConfig, 0xE);
            sceClose(fd);
        }
    }
    return gConfig;
}

// [C] gConfig[4] == 0x54 ('T').
// 0x0024AC20
extern "C" int IsT10K(void)
{
    if (gConfig[0] == 0) {
        GetRomName();
    }
    return gConfig[4] == 0x54;
}

// 0x0024AC60
extern "C" int sceScfGetLanguage(void)
{
    unsigned int param;
    GetOsdConfigParam(&param);                      // [C] called before IsT10K, result unused when T10K
    if (IsT10K()) {
        return __ps2_libinfo__[0xC];
    }
    GetOsdConfigParam(&param);
    if (((param >> 13) & 7) != 0) {
        return (param >> 16) & 0x1F;
    }
    return (param >> 4) & 1;
}

// [C] copies 8 bytes (unaligned) from src to __ps2_libinfo__ + 8 (the T10K record).
// 0x0024ACC0
extern "C" void func_0024ACC0(unsigned char *src)
{
    unsigned char tmp[8];
    int i;
    for (i = 0; i < 8; i++) {
        tmp[i] = src[i];
    }
    for (i = 0; i < 8; i++) {
        __ps2_libinfo__[8 + i] = tmp[i];
    }
}

// 0x0024ACE0
extern "C" int sceScfGetAspect(void)
{
    unsigned int param;
    if (IsT10K()) {
        return __ps2_libinfo__[0xA];
    }
    GetOsdConfigParam(&param);
    return (param >> 1) & 3;
}

// 0x0024AD20
extern "C" int sceScfGetSpdif(void)
{
    unsigned int param;
    if (IsT10K()) {
        return __ps2_libinfo__[0xD];
    }
    GetOsdConfigParam(&param);
    return param & 1;
}

// 0x0024AD60
extern "C" int sceScfGetTimeZone(void)
{
    unsigned int param;
    if (IsT10K()) {
        return *(short *)(__ps2_libinfo__ + 8);
    }
    GetOsdConfigParam(&param);
    int tz = (int)param >> 21;
    if (((param >> 13) & 7) == 0) {
        tz = 0x21C;
    }
    return tz;
}

// 0x0024ADB0
extern "C" int sceScfGetDateNotation(void)
{
    unsigned int param;
    unsigned char buf[8];
    if (IsT10K()) {
        return __ps2_libinfo__[0xB];
    }
    GetOsdConfigParam(&param);
    if (((param >> 13) & 7) == 0) {
        return 0;
    }
    GetOsdConfigParam2(buf, 1, 1);
    return buf[0] >> 6;
}

// 0x0024AE18
extern "C" int sceScfGetSummerTime(void)
{
    unsigned int param;
    unsigned char buf[8];
    if (IsT10K()) {
        return __ps2_libinfo__[0xE];
    }
    GetOsdConfigParam(&param);
    if (((param >> 13) & 7) == 0) {
        return 0;
    }
    GetOsdConfigParam2(buf, 1, 1);
    return (buf[0] >> 4) & 1;
}

// 0x0024AE80
extern "C" int sceScfGetTimeNotation(void)
{
    unsigned int param;
    unsigned char buf[8];
    if (IsT10K()) {
        return __ps2_libinfo__[0xF];
    }
    GetOsdConfigParam(&param);
    if (((param >> 13) & 7) == 0) {
        return 0;
    }
    GetOsdConfigParam2(buf, 1, 1);
    return (buf[0] >> 5) & 1;
}

// [C] a + 6 * (a / 10), as bytes.
// 0x0024AEE8
extern "C" unsigned int tobcd(unsigned int a)
{
    a &= 0xFF;
    return (a + 6 * (a / 10)) & 0xFF;
}

// [C] v - 6 * (v >> 4), as bytes.
// 0x0024AF18
extern "C" unsigned int frombcd(unsigned int a)
{
    unsigned int v = a & 0xFF;
    return (v - 6 * (v >> 4)) & 0xFF;
}

// 0x0024AF38
extern "C" void convertfrombcd(unsigned char *p)
{
    p[7] = frombcd(p[7]);
    p[6] = frombcd(p[6]);
    p[5] = frombcd(p[5]);
    p[3] = frombcd(p[3]);
    p[2] = frombcd(p[2]);
    p[1] = frombcd(p[1]);
}

// 0x0024AFA0
extern "C" void converttobcd(unsigned char *p)
{
    p[7] = tobcd(p[7]);
    p[6] = tobcd(p[6]);
    p[5] = tobcd(p[5]);
    p[3] = tobcd(p[3]);
    p[2] = tobcd(p[2]);
    p[1] = tobcd(p[1]);
}

// [C] day + 1; if it exceeds the month length (table byte, signed compare), day = 1 and month + 1 (month 13: month = 1, year
// 99 -> 0 else + 1). The month index is p[6] - 1 (p[6] == 0 reads the byte before the table, as written).
// 0x0024B008
extern "C" void adddate(unsigned char *p)
{
    unsigned char tbl[12];
    unsigned int day = p[5] + 1;
    unsigned int year = p[7];
    int i;
    for (i = 0; i < 12; i++) {
        tbl[i] = D_004BB5F8[i];
    }
    p[5] = day;
    if ((year & 3) == 0) {
        tbl[1] = 0x1D;
    }
    if ((signed char)tbl[(int)p[6] - 1] < (int)p[5]) {
        unsigned int month;
        p[5] = 1;
        month = p[6] + 1;
        p[6] = month;
        if ((month & 0xFF) == 0xD) {
            if (p[7] == 0x63) {
                p[7] = 0;
            } else {
                p[7] = p[7] + 1;
            }
            p[6] = 1;
        }
    }
}

// [C] day - 1; at 0: month - 1 (month 0: month = 12, year 0 -> 99 else - 1), day = that month's length.
// 0x0024B0C0
extern "C" void subdate(unsigned char *p)
{
    unsigned char tbl[12];
    unsigned int day = p[5] - 1;
    unsigned int year = p[7];
    int i;
    for (i = 0; i < 12; i++) {
        tbl[i] = D_004BB5F8[i];
    }
    p[5] = day;
    if ((year & 3) == 0) {
        tbl[1] = 0x1D;
    }
    if (p[5] == 0) {
        unsigned int month = p[6] - 1;
        p[6] = month;
        if ((month & 0xFF) == 0) {
            unsigned int y = p[7];
            if (y != 0) {
                y = y - 1;
            } else {
                y = 0x63;
            }
            p[7] = y;
            p[6] = 0xC;
        }
        p[5] = tbl[(int)p[6] - 1];
    }
}

// 0x0024B168
extern "C" void addhour(unsigned char *p)
{
    unsigned int h = p[3] + 1;
    p[3] = h;
    if ((h & 0xFF) == 0x18) {
        p[3] = 0;
        adddate(p);                                 // [C] tail jump
    }
}

// 0x0024B198
extern "C" void subhour(unsigned char *p)
{
    unsigned int h = p[3];
    if (h == 0) {
        p[3] = 0x17;
        subdate(p);                                 // [C] tail jump
        return;
    }
    p[3] = h - 1;
}

// [C] converts the record from BCD, adds `minutes` to the minute byte (+2) carrying into hours / dates in steps of 60, converts back.
// 0x0024B1C0
extern "C" void AdjustTime(unsigned char *p, int minutes)
{
    int m;
    convertfrombcd(p);
    m = (int)p[2] + minutes;
    if (m < 0) {
        do {
            m += 0x3C;
            subhour(p);
        } while (m < 0);
        p[2] = m;
    } else if (m < 0x3C) {
        p[2] = m;
    } else {
        do {
            m -= 0x3C;
            addhour(p);
        } while (m >= 0x3C);
        p[2] = m;
    }
    converttobcd(p);                                // [C] tail jump
}

// 0x0024B250
extern "C" void sceScfGetGMTfromRTC(unsigned char *p)
{
    AdjustTime(p, -0x21C);                          // [C] tail jump
}

// [C] a1 = timezone + summertime * 60 - 0x21C.
// 0x0024B258
extern "C" void sceScfGetLocalTimefromRTC(unsigned char *p)
{
    int tz = sceScfGetTimeZone();
    int summer = sceScfGetSummerTime();
    AdjustTime(p, tz + (summer * 0x3C - 0x21C));    // [C] tail jump
}
