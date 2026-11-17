// osstreamcontain.o (librwaspu2.a) -- the OS stream container's status report. Retail 0x2B4900-0x2B49F0 [C E map].
// C port written from retail's code (audio1); proven by the checker on live captures.
#include "rwa/rwacore.h"

// A stream position (from _rwaStreamContainerGetStreamPos), 0x10 bytes copied whole [C 0x2B4950].
struct UnkStruct_002B4900_pos {
    RwUInt32 field_0;             // 0x0 the segment index
    unsigned char pad_4[0xC];
};
// A segment entry, 0x20 bytes [C 0x2B4978 index << 5].
struct UnkStruct_002B4900_seg {
    void *field_0;                // 0x0 a 0x10-byte block copied to the status (+0xA4)
    const char *field_4;          // 0x4 the segment's name
    unsigned char pad_8[0x18];
};
struct UnkStruct_002B4900_b {
    unsigned char pad_0[0x14];
    RwUInt32 *field_14;           // 0x14 -> the word reported as status +0xC
};
// The stream (output parameter of the container's object, index container->field_14->field_70).
struct UnkStruct_002B4900_stream {
    unsigned char pad_0[0x10];
    UnkStruct_002B4900_seg *field_10;  // 0x10 segments
    unsigned char pad_14[0x4];
    UnkStruct_002B4900_b *field_18;    // 0x18
};
struct UnkStruct_002B4900_obj {
    unsigned char pad_0[0x70];
    RwUInt32 field_70;            // 0x70 the parameter index of the stream
};
struct UnkStruct_002B4900 {
    unsigned char pad_0[0x14];
    UnkStruct_002B4900_obj *field_14;  // 0x14
    unsigned char pad_18[0x1C];
    RwReal field_34;              // 0x34 reported as status +0x10
};
// The status (RwaStreamContainerCurrentStatus [L name]), at least 0xB4 bytes.
struct UnkStruct_002B4900_status {
    unsigned char pad_0[0xC];
    RwUInt32 field_C;             // 0x0C
    RwReal field_10;              // 0x10
    unsigned char field_14[0x10]; // 0x14 the stream position
    char field_24[0x80];          // 0x24 the segment name, at most 0x7F chars + 0
    unsigned char field_A4[0x10]; // 0xA4
};

extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" unsigned int strlen(const char *s);
extern "C" char *strncpy(char *dst, const char *src, unsigned int n);
extern "C" RwUInt32 _rwaStreamContainerGetStreamPos(UnkStruct_002B4900 *c, void *p1);
extern "C" RwUInt32 RwaObjIntGetParamPointer(void *obj, void *intf, RwUInt32 index);   // boot2: as rwastreamnodety.cpp defines it (a guest word in, a guest word out)

// A name longer than 0x80 is cut to 0x7F [C 0x2B4990: sltiu 0x81].
// 0x002B4900
extern "C" void _rwaOsStreamContainerStateUpdateCurrentStatus(UnkStruct_002B4900 *c, RwUInt32 p1,
                                                              UnkStruct_002B4900_status *st)
{
    st->field_10 = c->field_34;
    UnkStruct_002B4900_pos *pos = (UnkStruct_002B4900_pos *)_rwaStreamContainerGetStreamPos(c, (void *)p1);
    if (!pos)
        return;
    UnkStruct_002B4900_stream *s =
        (UnkStruct_002B4900_stream *)RwaObjIntGetParamPointer(c->field_14, (void *)c->field_14->field_70, 2);
    memcpy(st->field_14, pos, 0x10);
    st->field_C = *s->field_18->field_14;
    const char *name = s->field_10[pos->field_0].field_4;
    RwUInt32 n = strlen(name);
    if (!(n < 0x81))
        n = 0x7F;
    strncpy(st->field_24, name, n);
    st->field_24[n] = 0;
    memcpy(st->field_A4, s->field_10[pos->field_0].field_0, 0x10);
}
