// rwastreamfilequ.o (librwacore.a; truncated in the E map) -- the stream file request queue: a ring of 0x14-byte
// requests under a semaphore (taken when RWA runs its own threads), and the stream interface definition. Retail
// 0x2A1BD0-0x2A1F7C [C E map]. C port written from retail's code (audio1); proven by the checker on live captures.
#include "rwa/rwacore.h"

// The queue [C _rwaStreamFileRequestQueueCreate 0x2A1EBC-0x2A1EDC].
struct UnkStruct_002A1EB0 {
    RwUInt32 *field_0;            // 0x0 the requests (5 words each)
    RwUInt16 field_4;             // 0x4 capacity
    RwUInt16 field_6;             // 0x6 write index
    RwUInt16 field_8;             // 0x8 read index
    RwUInt16 field_A;             // 0xA full (write index caught up with the read index)
    RwInt32 field_C;              // 0xC the semaphore
};

extern RwInt32 _rwaGprogramContext;
extern RwaObjInterfaceDef D_01E7DDC0;      // the stream interface definition (static struct)
extern RwaUUID rwaSTREAMINTERFACEID;
extern RwaObjInterfaceDefParam D_0049EBD0[6];
extern RwaObjInterfaceDefParam D_0049EC50[10];
extern "C" RwInt32 WaitSema(RwInt32 id);
extern "C" RwInt32 SignalSema(RwInt32 id);
extern "C" RwInt32 DeleteSema(RwInt32 id);
extern "C" RwInt32 *RwaSemaphoreCreate(RwInt32 *sema, RwInt32 init, RwInt32 max);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def);
extern "C" RwBool RwaObjInterfaceDefDestroy(RwaObjInterfaceDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, RwaUUID *id, const char *name);
extern "C" void *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *inputs, RwUInt32 nInputs,
                                         RwaObjInterfaceDefParam *outputs, RwUInt32 nOutputs);

static void func_002A1BD0_inl1(UnkStruct_002A1EB0 *q)
{
    if (_rwaGprogramContext == 2)
        WaitSema(q->field_C);
}

static void func_002A1BD0_inl2(UnkStruct_002A1EB0 *q)
{
    if (_rwaGprogramContext == 2)
        SignalSema(q->field_C);
}

// 0x002A1BD0
extern "C" void _rwaStreamFileRequestQueueEmpty(UnkStruct_002A1EB0 *q)
{
    func_002A1BD0_inl1(q);
    q->field_8 = q->field_6;
    q->field_A = 0;
    func_002A1BD0_inl2(q);
}

// The oldest request into out (5 words); 0 when empty.
// 0x002A1C40
extern "C" RwBool _rwaStreamFileRequestQueueGet(UnkStruct_002A1EB0 *q, RwUInt32 *out)
{
    func_002A1BD0_inl1(q);
    RwUInt16 r = q->field_8;
    if (q->field_6 == r && !q->field_A) {
        func_002A1BD0_inl2(q);
        return 0;
    }
    RwUInt32 *e = q->field_0 + r * 5;
    out[1] = e[1];
    out[0] = e[0];
    out[4] = e[4];
    out[3] = e[3];
    out[2] = e[2];
    RwUInt32 n = q->field_8 + 1;
    q->field_A = 0;
    if (n == q->field_4)
        n = 0;
    q->field_8 = (RwUInt16)n;
    func_002A1BD0_inl2(q);
    return 1;
}

// Queues {w0, w1, w2, w3, w4}; 0 when full.
// 0x002A1D60
extern "C" RwBool _rwaStreamFileRequestQueueAdd(UnkStruct_002A1EB0 *q, RwUInt32 w1, RwUInt32 w2, RwUInt32 w3, RwUInt32 w0,
                                                RwUInt32 w4)
{
    func_002A1BD0_inl1(q);
    RwUInt16 w = q->field_6;
    if (w == q->field_8 && q->field_A) {
        func_002A1BD0_inl2(q);
        return 0;
    }
    RwUInt32 *e = q->field_0 + w * 5;
    e[1] = w1;
    e[2] = w2;
    e[4] = w4;
    e[3] = w3;
    e[0] = w0;
    RwUInt32 n = q->field_6 + 1;
    if (n == q->field_4)
        n = 0;
    if (n == q->field_8)
        q->field_A = 1;
    q->field_6 = (RwUInt16)n;
    func_002A1BD0_inl2(q);
    return 1;
}

// 0x002A1EA0
extern "C" RwInt32 _rwaStreamFileRequestQueueDestroy(UnkStruct_002A1EB0 *q)
{
    return DeleteSema(q->field_C);
}

// 0x002A1EB0
extern "C" UnkStruct_002A1EB0 *_rwaStreamFileRequestQueueCreate(UnkStruct_002A1EB0 *q, RwUInt32 *requests, RwUInt32 n)
{
    q->field_0 = requests;
    q->field_4 = (RwUInt16)n;
    q->field_6 = 0;
    q->field_8 = 0;
    q->field_A = 0;
    return RwaSemaphoreCreate(&q->field_C, 1, 1) ? q : 0;
}

// 0x002A1F00
extern "C" RwaObjInterfaceDef *func_002A1F00(void)
{
    return (RwaObjInterfaceDef *)RwaObjInterfaceDefDestroy(&D_01E7DDC0);
}

// The stream interface definition.
// 0x002A1F10
extern "C" RwaObjInterfaceDef *func_002A1F10(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E7DDC0))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E7DDC0, &rwaSTREAMINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E7DDC0, D_0049EBD0, 6, D_0049EC50, 0xA);
    return &D_01E7DDC0;
}
