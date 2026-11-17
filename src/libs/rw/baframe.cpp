// RenderWare 3.6 core: frames (RW 3.1 file name: src/baframe.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

extern unsigned char frameTKList[];   // [C] the frame plugin registry (0x18 bytes, symbol table)

extern "C" int _rwPluginRegistrySetStaticPluginsSize(void *registry, int size);

// [C] the size rounded up to 16 ((size + 0xF) & 0xFFFFFFF0) for frameTKList.
// 0x001E4330
extern "C" int RwFrameSetStaticPluginsSize(int size)
{
    return _rwPluginRegistrySetStaticPluginsSize(frameTKList, (size + 0xF) & 0xFFFFFFF0);
}

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)
extern int frameModule;                // [C] gp-relative 0x004E27A0 (8 bytes): +0 the frame globals' offset in ourGlobals
extern int D_004E27A4;                 // [C] gp-relative 0x004E27A4 = frameModule + 4: the module's open count
extern int _rwFrameFreeListBlockSize;            // [C] gp-relative
extern int _rwFrameFreeListPreallocBlocks;       // [C] gp-relative
extern unsigned char D_00674BD0[];     // [C] in-place space for the frame free list

extern "C" void *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock, int alignment,
                                                     int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint);
extern "C" int RwFreeListDestroy(void *freeList);

// [C] a frame list head at ourGlobals + 0xBC: next and prev both point to itself ([L] the dirty frame list).
struct UnkStruct_frameDirtyList {
    UnkStruct_frameDirtyList *field_0;
    UnkStruct_frameDirtyList *field_4;
};

// [C] the frame globals: only the free list.
struct UnkStruct_frameModule {
    void *field_0;                     // [C] the frame free list
};

// [C] frameModule = offset; +0 = RwFreeListCreateAndPreallocateSpace(first word of frameTKList (the frame size),
// _rwFrameFreeListBlockSize, 0x10, _rwFrameFreeListPreallocBlocks, D_00674BD0, 0x4000E); 0 -> returns 0. Else the open
// count += 1 and the list head at ourGlobals + 0xBC is made empty (next = prev = &head); returns the instance.
// 0x001E4518
extern "C" void *_rwFrameOpen(void *instance, int offset, int size)
{
    void *freeList;
    UnkStruct_frameDirtyList *head;
    int count;

    frameModule = offset;
    freeList = RwFreeListCreateAndPreallocateSpace(*(int *)frameTKList, _rwFrameFreeListBlockSize, 0x10,
                                                   _rwFrameFreeListPreallocBlocks, D_00674BD0, 0x4000E);
    ((UnkStruct_frameModule *)(ourGlobals + frameModule))->field_0 = freeList;
    if (freeList == 0) {
        return 0;
    }
    count = D_004E27A4;
    head = (UnkStruct_frameDirtyList *)(ourGlobals + 0xBC);
    head->field_0 = head;
    count = count + 1;
    head->field_4 = head;
    D_004E27A4 = count;
    return instance;
}

// [C] if the free list is set: RwFreeListDestroy it and clear it; open count -= 1; returns the instance.
// 0x001E44B0
extern "C" void *_rwFrameClose(void *instance, int offset, int size)
{
    void *freeList = ((UnkStruct_frameModule *)(ourGlobals + frameModule))->field_0;

    if (freeList != 0) {
        RwFreeListDestroy(freeList);
        ((UnkStruct_frameModule *)(ourGlobals + frameModule))->field_0 = 0;
    }
    D_004E27A4 = D_004E27A4 - 1;
    return instance;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): frame creation / init / dirty marking.

struct UnkStruct_ourGlobalsFrame {
    unsigned char pad_0[0x144];
    void *(*field_144)(void *, unsigned int);   // [C] (free list, hint) ([L] memoryAlloc)
};

// [C] a matrix as _rwFrameInit fills it (0x40 bytes) ([L] RwMatrix: right, flags, up, pad, at, pad, pos, pad).
struct UnkStruct_frameMatrix {
    float field_0;                     // [C] right.x
    float field_4;                     // [C] right.y
    float field_8;                     // [C] right.z
    unsigned int field_C;              // [C] the flags word, 0x20003 at init ([L] flags)
    float field_10;                    // [C] up.x
    float field_14;                    // [C] up.y
    float field_18;                    // [C] up.z
    unsigned char pad_1C[0x4];
    float field_20;                    // [C] at.x
    float field_24;                    // [C] at.y
    float field_28;                    // [C] at.z
    unsigned char pad_2C[0x4];
    float field_30;                    // [C] pos.x
    float field_34;                    // [C] pos.y
    float field_38;                    // [C] pos.z
    unsigned char pad_3C[0x4];
};

// [C] a frame ([L] RwFrame; retail 3.6 offsets).
struct UnkStruct_RwFrameView {
    unsigned char field_0;             // [C] 0 ([L] type)
    unsigned char field_1;             // [C] 0 ([L] subType)
    unsigned char field_2;             // [C] 0 ([L] flags)
    unsigned char field_3;             // [C] private flags: bit 0-1 dirty-listed, 2-3 ... ([L] privateFlags)
    void *field_4;                     // [C] parent (0) ([L] object.parent)
    unsigned char pad_8[0x8];
    UnkStruct_frameMatrix field_10;    // [C] the modelling matrix ([L] modelling)
    UnkStruct_frameMatrix field_50;    // [C] the LTM ([L] ltm)
    UnkStruct_frameDirtyList field_90; // [C] the object list head, empty = itself ([L] objectList)
    void *field_98;                    // [C] 0 ([L] child)
    void *field_9C;                    // [C] 0 ([L] next)
    UnkStruct_RwFrameView *field_A0;   // [C] the root, the frame itself ([L] root)
};

extern "C" void *_rwPluginRegistryInitObject(void *registry, void *object);

// [C] stores byte 0..3 = 0, parent = 0, the object list head (+0x90) points at itself, the modelling matrix and the
// LTM are set to identity (floats 1.0 / 0.0; flags word at +0x1C / +0x5C = 0x20003), +0x98 / +0x9C = 0, root (+0xA0)
// = the frame, then the frame plugins' constructors (_rwPluginRegistryInitObject(frameTKList, frame)).
// 0x001E3690
extern "C" void _rwFrameInit(UnkStruct_RwFrameView *frame)
{
    float one = 1.0f;
    float zero = 0.0f;

    frame->field_0 = 0;
    frame->field_1 = 0;
    frame->field_2 = 0;
    frame->field_3 = 0;
    frame->field_4 = 0;
    frame->field_90.field_4 = &frame->field_90;
    frame->field_90.field_0 = &frame->field_90;
    frame->field_10.field_28 = one;
    frame->field_10.field_14 = one;
    frame->field_10.field_0 = one;
    frame->field_10.field_10 = zero;
    frame->field_10.field_8 = zero;
    frame->field_10.field_4 = zero;
    frame->field_10.field_24 = zero;
    frame->field_10.field_20 = zero;
    frame->field_10.field_18 = zero;
    frame->field_10.field_34 = zero;
    frame->field_10.field_38 = zero;
    frame->field_10.field_30 = zero;
    frame->field_10.field_C = 0x20003;
    frame->field_50.field_28 = one;
    frame->field_50.field_14 = one;
    frame->field_50.field_0 = one;
    frame->field_50.field_10 = zero;
    frame->field_50.field_8 = zero;
    frame->field_50.field_4 = zero;
    frame->field_50.field_24 = zero;
    frame->field_50.field_20 = zero;
    frame->field_50.field_18 = zero;
    frame->field_50.field_34 = zero;
    frame->field_50.field_38 = zero;
    frame->field_50.field_30 = zero;
    frame->field_50.field_C = 0x20003;
    frame->field_98 = 0;
    frame->field_9C = 0;
    frame->field_A0 = frame;
    _rwPluginRegistryInitObject(frameTKList, frame);
}

// [C] a frame from the frame free list (ourGlobals + 0x144, hint 0x3000E); NULL when that fails, else _rwFrameInit and
// the frame.
// 0x001E4040
extern "C" UnkStruct_RwFrameView *RwFrameCreate(void)
{
    UnkStruct_RwFrameView *frame;

    frame = (UnkStruct_RwFrameView *)(((UnkStruct_ourGlobalsFrame *)ourGlobals)->field_144)(
        *(void **)(ourGlobals + frameModule), 0x3000E);
    if (frame == 0) {
        return 0;
    }
    _rwFrameInit(frame);
    return frame;
}

// [C] puts the frame's root on the dirty list (ourGlobals + 0xBC; root +8 = next, +0xC = the list head) unless its
// root's private flags bits 0-1 are already set, then sets those bits (|3) on the root and bits 2-3 (|0xC) on the
// frame. Returns the frame.
// 0x001E3FD0
extern "C" UnkStruct_RwFrameView *RwFrameUpdateObjects(UnkStruct_RwFrameView *frame)
{
    UnkStruct_RwFrameView *root;
    UnkStruct_frameDirtyList *head;
    UnkStruct_frameDirtyList *link;
    UnkStruct_frameDirtyList *first;
    unsigned int flags;

    root = frame->field_A0;
    flags = root->field_3;
    if ((flags & 3) == 0) {
        head = (UnkStruct_frameDirtyList *)(ourGlobals + 0xBC);
        link = (UnkStruct_frameDirtyList *)((unsigned char *)root + 8);
        first = head->field_0;
        *(UnkStruct_frameDirtyList **)((unsigned char *)root + 0xC) = head;
        *(UnkStruct_frameDirtyList **)((unsigned char *)root + 8) = first;
        head->field_0->field_4 = link;
        head->field_0 = link;
    }
    root = frame->field_A0;
    root->field_3 = (unsigned char)(flags | 3);
    frame->field_3 = (unsigned char)(frame->field_3 | 0xC);
    return frame;
}

// ---------------------------------------------------------------------------------------------------------------
// boot2 additions (track rw): the LTM accessor.

extern "C" unsigned int _rwFrameSyncHierarchyLTM(UnkStruct_RwFrameView *root);

// [C] when the frame's root has private flag bit 0 set (+3 & 1: the hierarchy is dirty) _rwFrameSyncHierarchyLTM(root)
// is run; returns the address of the frame's LTM (frame + 0x50).
// 0x001E3F90
extern "C" UnkStruct_frameMatrix *RwFrameGetLTM(UnkStruct_RwFrameView *frame)
{
    UnkStruct_RwFrameView *root = frame->field_A0;

    if ((root->field_3 & 1) != 0) {
        _rwFrameSyncHierarchyLTM(root);
    }
    return &frame->field_50;
}

// ---------------------------------------------------------------------------------------------------------------
// fe1 (2026-11-10): frame destruction (the game's shutdown before the EXTRAS page's demo: CB3GraphicsManagerBase::
// Destruct -> CGtViewport::Destruct -> RwFrameDestroy).

extern "C" void *_rwPluginRegistryDeInitObject(void *registry, void *object);

// [C] the RW allocator's free at ourGlobals + 0x148 ((free list, entry) [L] memoryFree).
struct UnkStruct_ourGlobalsFrameFree {
    unsigned char pad_0[0x148];
    void (*field_148)(void *, void *);
};

// RwFrameRemoveChild (0x1E3CF8) and rwSetHierarchyRoot (0x1E45A0), which RwFrameDestroy reaches only for a frame with
// a parent, stay retail: no path run so far calls them (B3NATIVE_DIFF over the shutdown: 0 calls), so they have no
// per-call proof yet.
extern "C" UnkStruct_RwFrameView *RwFrameRemoveChild(UnkStruct_RwFrameView *child);

// [C] the frame plugins' destructors (_rwPluginRegistryDeInitObject(frameTKList, frame)); RwFrameRemoveChild when the
// frame has a parent (+4); when its private flags bits 0-1 are set it is taken off the dirty list (*(+0xC) = +8, then
// (+8)->+4 = +0xC); every child's parent (+4) = 0; the frame back to the frame free list (ourGlobals + 0x148 with the
// list at ourGlobals + frameModule); returns 1.
// 0x001E4170
extern "C" int RwFrameDestroy(UnkStruct_RwFrameView *frame)
{
    UnkStruct_RwFrameView *c;
    UnkStruct_frameDirtyList *link;

    _rwPluginRegistryDeInitObject(frameTKList, frame);
    if (frame->field_4 != 0) {
        RwFrameRemoveChild(frame);
    }
    if ((frame->field_3 & 3) != 0) {
        link = (UnkStruct_frameDirtyList *)((unsigned char *)frame + 8);
        link->field_4->field_0 = link->field_0;
        link->field_0->field_4 = link->field_4;
    }
    for (c = (UnkStruct_RwFrameView *)frame->field_98; c != 0; c = (UnkStruct_RwFrameView *)c->field_9C) {
        c->field_4 = 0;
    }
    ((UnkStruct_ourGlobalsFrameFree *)ourGlobals)->field_148(*(void **)(ourGlobals + frameModule), frame);
    return 1;
}
