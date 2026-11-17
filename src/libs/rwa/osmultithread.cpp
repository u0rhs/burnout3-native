// osmultithread.o (librwacore.a) -- RWA's threads, semaphores and locked counts over the EE kernel. Retail
// 0x2A1F80-0x2A2310 [C E map]. C port written from retail's code (audio1); thread and semaphore creation reach kernel
// calls the checker cannot run (ORACLE).
#include "rwa/rwacore.h"

// An RWA thread [C _RwaThreadCreate 0x2A2048-0x2A2084].
typedef RwInt32 *(*UnkFn_002A2110)(RwUInt32 arg);
struct UnkStruct_002A2010 {
    RwInt32 field_0;              // 0x00 kernel thread id
    UnkFn_002A2110 field_4;       // 0x04 entry (returns the semaphore to signal, or null to exit)
    RwUInt32 field_8;             // 0x08 its argument (_RwaThreadStart)
    RwUInt32 field_C;             // 0x0C 1 = the caller's stack
    void *field_10;               // 0x10 the stack
};
// The kernel's thread / semaphore parameter blocks (ee_thread_t, ee_sema_t) [C sp+0x60 / sp+0x20].
typedef void (*UnkFn_002A2010)(UnkStruct_002A2010 *t);
struct UnkStruct_002A2010_param {
    RwUInt32 field_0;
    UnkFn_002A2010 field_4;       // entry
    void *field_8;                // stack
    RwUInt32 field_C;             // stack size
    RwUInt32 field_10;            // gp
    RwUInt32 field_14;            // initial priority
    RwUInt32 field_18;
};
struct UnkStruct_002A21D0_param {
    RwUInt32 field_0;
    RwInt32 field_4;              // max count
    RwInt32 field_8;              // initial count
    RwUInt32 field_C;
};

extern RwInt32 _rwaGprogramContext;
extern RwUInt8 D_004E8670[4];             // _gp
extern "C" RwInt32 TerminateThread(RwInt32 id);
extern "C" RwInt32 DeleteThread(RwInt32 id);
extern "C" RwInt32 _StartThread(RwInt32 id, void *arg);
extern "C" RwInt32 CreateThread(UnkStruct_002A2010_param *p);
extern "C" RwInt32 SleepThread(void);
extern "C" void ExitDeleteThread(void);
extern "C" RwInt32 SignalSema(RwInt32 id);
extern "C" RwInt32 PollSema(RwInt32 id);
extern "C" RwInt32 CreateSema(UnkStruct_002A21D0_param *p);
extern "C" RwInt32 func_00114E60(void);   // DIntr
extern "C" RwInt32 EIntr(void);

// 0x002A1F80
extern "C" void RwaThreadTerminate(UnkStruct_002A2010 *t)
{
    if (!t->field_C)
        func_00293440(t->field_10);
    TerminateThread(t->field_0);
    DeleteThread(t->field_0);
}

// 0x002A1FC0
extern "C" UnkStruct_002A2010 *_RwaThreadStart(UnkStruct_002A2010 *t, RwUInt32 arg)
{
    t->field_8 = arg;
    if (_StartThread(t->field_0, t) == -1)
        return 0;
    return t;
}

extern "C" void _RwaThreadFunc(UnkStruct_002A2010 *t);

// A thread at `priority` with the caller's stack (field_10) or a new one of `stackSize`. On failure retail frees the
// stack only when it was the caller's [C 0x2A20C8: the test is inverted against RwaThreadTerminate's].
// 0x002A2010
extern "C" UnkStruct_002A2010 *_RwaThreadCreate(UnkStruct_002A2010 *t, UnkFn_002A2110 entry, RwUInt32 priority,
                                               RwUInt32 stackSize)
{
    UnkStruct_002A2010_param p;
    if (t->field_10) {
        p.field_8 = t->field_10;
        t->field_C = 1;
    } else {
        p.field_8 = func_002934C0(stackSize, 0x30800);
        t->field_10 = p.field_8;
        t->field_C = 0;
    }
    p.field_14 = priority;
    p.field_10 = (unsigned int)D_004E8670;
    p.field_4 = _RwaThreadFunc;
    p.field_C = stackSize;
    if (!p.field_8)
        return 0;
    t->field_0 = CreateThread(&p);
    t->field_4 = entry;
    if (t->field_0 > 0)
        return t;
    if (t->field_C)
        func_00293440(t->field_10);
    return 0;
}

// The kernel thread's entry: runs the RWA entry; its result (a semaphore) is signalled and the thread sleeps, or with
// a null result the thread exits.
// 0x002A2110
extern "C" void _RwaThreadFunc(UnkStruct_002A2010 *t)
{
    RwInt32 *r = t->field_4(t->field_8);
    if (!r) {
        ExitDeleteThread();
        return;
    }
    if (_rwaGprogramContext == 2)
        SignalSema(*r);
    SleepThread();
}

// 1 when the semaphore could be taken; always 0 without RWA threads (context != 2) [C 0x2A2188: the other poll is
// unreachable].
// 0x002A2170
extern "C" RwBool _rwaSemaphorePoll(RwInt32 *sema)
{
    if (_rwaGprogramContext == 2)
        return !(PollSema(*sema) < 0);
    return 0;
}

// 0x002A21D0
extern "C" RwInt32 *RwaSemaphoreCreate(RwInt32 *sema, RwInt32 init, RwInt32 max)
{
    UnkStruct_002A21D0_param p;
    p.field_8 = init;
    p.field_4 = max;
    *sema = CreateSema(&p);
    if (*sema == -1)
        return 0;
    return sema;
}

// *count - value, read with interrupts off (with RWA threads).
// 0x002A2220
extern "C" RwInt32 _rwaLockedCountCompare(RwInt32 *count, RwInt32 value)
{
    RwInt32 was = 0;
    if (_rwaGprogramContext == 2)
        was = func_00114E60();
    RwInt32 c = *count;
    if (_rwaGprogramContext == 2 && was == 1)
        EIntr();
    return c - value;
}

// 0x002A22A0
extern "C" RwInt32 *_rwaLockedCountCreate(RwInt32 *count, RwInt32 value)
{
    RwInt32 was = 0;
    if (_rwaGprogramContext == 2)
        was = func_00114E60();
    *count = value;
    if (_rwaGprogramContext == 2 && was == 1)
        EIntr();
    return count;
}
