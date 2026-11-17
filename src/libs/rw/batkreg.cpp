// RenderWare 3.6 core: the plugin registries (RW 3.1 file name: src/batkreg.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

struct UnkStruct_toolkitRegEntries;

// [C] a plugin registry ([L] RwPluginRegistry; the *TKList globals are 0x18 bytes each [C symbol table]).
struct UnkStruct_TKList {
    int field_0;                                // [C] current size, grows by each plugin's size rounded to 4 ([L] sizeOfStruct)
    int field_4;                                // [C] restored into field_0 by _rwPluginRegistryClose ([L] origSizeOfStruct)
    int field_8;                                // [C] size limit, 0 = none ([L] maxSizeOfStruct)
    int field_C;                                // [C] set to 1 by _rwPluginRegistrySetStaticPluginsSize ([L] staticAlloc)
    UnkStruct_toolkitRegEntries *field_10;      // [C] first entry ([L] firstRegEntry)
    UnkStruct_toolkitRegEntries *field_14;      // [C] last entry ([L] lastRegEntry)
};

// [C] a registry entry, 0x3C bytes (the size of toolkitRegEntries' entries) ([L] RwPluginRegEntry).
struct UnkStruct_toolkitRegEntries {
    int field_0;                                // [C] the plugin's offset in the object ([L] offset)
    int field_4;                                // [C] the size as passed, not rounded ([L] size)
    unsigned int field_8;                       // [C] plugin ID ([L] pluginID)
    void *(*field_C)(void *, int, void *, int, int);       // [C] 0 here; set by _rwPluginRegistryAddPluginStream ([L] readCB)
    void *(*field_10)(void *, int, const void *, int, int); // [C] 0 here ([L] writeCB)
    int (*field_14)(const void *, int, int);    // [C] 0 here ([L] getSizeCB)
    void *(*field_18)(void *, int, int);        // [C] 0 here ([L] alwaysCB)
    void *(*field_1C)(void *, int, int);        // [C] 0 here ([L] rightsCB)
    void *(*field_20)(void *, int, int);        // [C] constructCB or func_001DE180 ([L] constructCB)
    void *(*field_24)(void *, int, int);        // [C] destructCB or func_001DE188 ([L] destructCB)
    void *(*field_28)(void *, const void *, int, int); // [C] the sixth argument or func_001DE190 ([L] copyCB)
    void *(*field_2C)(void *, int, int);        // [C] 0 here ([L] errStrCB)
    UnkStruct_toolkitRegEntries *field_30;      // [C] next entry ([L] nextRegEntry)
    UnkStruct_toolkitRegEntries *field_34;      // [C] previous entry ([L] prevRegEntry)
    UnkStruct_TKList *field_38;                 // [C] the registry ([L] parentRegistry)
};

struct UnkStruct_ourGlobals {
    unsigned char pad_0[0x134];
    void *(*field_134)(unsigned int, unsigned int);  // [C] (size, hint) ([L] rwmalloc)
    void (*field_138)(void *);                       // [C] ([L] rwfree)
    unsigned char pad_13C[0x8];
    void *(*field_144)(void *, unsigned int);        // [C] (free list, hint) ([L] memoryAlloc)
    void *(*field_148)(void *, void *);              // [C] (free list, entry) ([L] memoryFree)
};
extern UnkStruct_ourGlobals ourGlobals;
extern void *toolkitRegEntries;                       // [C] gp-relative: the free list of registry entries
extern unsigned int numRegToolkits;                   // [C] gp-relative: entries of toolkitNonFLRegList
extern UnkStruct_TKList **toolkitNonFLRegList;        // [C] gp-relative: registries seen while free lists are off
extern int _rwPluginRegFreeListBlockSize;             // [C] gp-relative
extern int _rwPluginRegListPreallocBlocks;            // [C] gp-relative
extern unsigned char D_00670B58[];                    // [C] in-place space for toolkitRegEntries' struct

// [C] RwError on the stack: {plugin ID 1 ([L] rwID_COREPLUGIN), _rwerror(code)} ([L] RwError).
struct UnkStruct_RwErrorSet {
    int field_0;
    int field_4;
};

extern "C" void *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock, int alignment,
                                                     int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint);
extern "C" int RwFreeListDestroy(void *freeList);
typedef void (*UnkFn_001DBBB8)(void *, void *);   // [C] (entry, data), as bamemory.cpp
extern "C" void *RwFreeListForAllUsed(void *freeList, UnkFn_001DBBB8 fpCallBack, void *data);
extern "C" void *_rwFreeListAllocReal(void *freeList, unsigned int hint);
extern "C" int _rwGetNumEngineInstances(void);
// [C] _rwerror (0x001D89B8) is variadic in RW but only returns its first argument (the rest are spilled by its
// va_start prologue and never read), so it is declared with the code alone: b3host's variadic guest_fn does not
// compile at -O2 here (call_guest_varargs is declared after guest_fn<R(A..., ...)> in b3host/call.h).
extern "C" int _rwerror(int code);
extern "C" void *RwErrorSet(UnkStruct_RwErrorSet *code);

// [C] the default constructor / destructor / copy callbacks: each returns its first argument.
// 0x001DE180
extern "C" void *func_001DE180(void *object, int offset, int size)
{
    return object;
}

// 0x001DE188
extern "C" void *func_001DE188(void *object, int offset, int size)
{
    return object;
}

// 0x001DE190
extern "C" void *func_001DE190(void *dstObject, const void *srcObject, int offset, int size)
{
    return dstObject;
}

// [C] RwFreeListForAllUsed's callback in _rwPluginRegistryClose: resets the entry's registry (field_38) when it has an
// entry (field_14 = 0, field_0 = field_4, field_10 = 0), then hands the entry to ourGlobals.field_148(data, entry).
// 0x001DE138
extern "C" void rwDestroyEntry(void *mem, void *data)
{
    UnkStruct_toolkitRegEntries *entry = (UnkStruct_toolkitRegEntries *)mem;
    UnkStruct_TKList *registry = entry->field_38;

    if (registry->field_10 != 0) {
        registry->field_14 = 0;
        registry->field_0 = registry->field_4;
        registry->field_10 = 0;
    }
    ourGlobals.field_148(data, entry);
}

// [C] for each registry seen while free lists were off (toolkitNonFLRegList, only when ourGlobals.field_144 is not
// _rwFreeListAllocReal), every entry goes back through ourGlobals.field_148(NULL, entry); the registry then gets
// field_14 = 0, field_0 = field_4, field_10 = 0 when it had an entry. First of all RwFreeListForAllUsed runs
// rwDestroyEntry over toolkitRegEntries (its data argument the free list itself). Returns 1.
// 0x001DDA68
extern "C" int _rwPluginRegistryClose(void)
{
    unsigned int i;
    UnkStruct_toolkitRegEntries *entry;
    UnkStruct_toolkitRegEntries *next;
    UnkStruct_TKList *registry;

    if (toolkitRegEntries == 0) {
        return 1;
    }
    RwFreeListForAllUsed(toolkitRegEntries, rwDestroyEntry, toolkitRegEntries);
    if (ourGlobals.field_144 != _rwFreeListAllocReal) {
        for (i = 0; i < numRegToolkits; i++) {
            entry = toolkitNonFLRegList[i]->field_10;
            registry = 0;
            if (entry != 0) {
                registry = entry->field_38;
                do {
                    next = entry->field_30;
                    ourGlobals.field_148(0, entry);
                    entry = next;
                } while (entry != 0);
            }
            if (registry != 0 && registry->field_10 != 0) {
                registry->field_14 = 0;
                registry->field_0 = registry->field_4;
                registry->field_10 = 0;
            }
        }
        if (toolkitNonFLRegList != 0) {
            ourGlobals.field_138(toolkitNonFLRegList);
            toolkitNonFLRegList = 0;
        }
    }
    RwFreeListDestroy(toolkitRegEntries);
    toolkitRegEntries = 0;
    return 1;
}

// [C] Arguments past pluginID: constructCB, destructCB, copyCB (a NULL one gets the default func_001DE180 /
// func_001DE188 / func_001DE190). Fails (-1) with no entry free list; with an engine instance open ([L] after
// RwEngineOpen) it sets error {1, _rwerror(0x80000017)} and returns -1. While free lists are off the registry is
// recorded once in toolkitNonFLRegList (the array regrown by one through the memory functions, hint 0x40000, old one
// freed). A plugin ID already present sets the same error and returns that entry's offset. Otherwise the size rounded
// up to 4 is added to field_0 unless that passes the limit field_8 (-1); the entry (from toolkitRegEntries, hint
// 0x40000; -1 when that fails) takes the old field_0 as its offset and is appended; returns the offset.
// 0x001DDB98
extern "C" int _rwPluginRegistryAddPlugin(UnkStruct_TKList *registry, int size, unsigned int pluginID,
                                          void *constructCB, void *destructCB, void *copyCB)
{
    UnkStruct_toolkitRegEntries *entry;
    UnkStruct_TKList **list;
    unsigned int n;
    unsigned int i;
    int newSize;
    UnkStruct_RwErrorSet err;

    if (toolkitRegEntries == 0) {
        return -1;
    }
    if (_rwGetNumEngineInstances() != 0) {
        err.field_0 = 1;
        err.field_4 = _rwerror(0x80000017);
        RwErrorSet(&err);
        return -1;
    }
    if (ourGlobals.field_144 != _rwFreeListAllocReal) {
        n = numRegToolkits;
        i = 0;
        if (n != 0 && registry != toolkitNonFLRegList[0]) {
            for (i = 1; i < n; i++) {
                if (registry == toolkitNonFLRegList[i]) {
                    break;
                }
            }
        }
        if (n == i) {
            list = (UnkStruct_TKList **)ourGlobals.field_134((n + 1) * 4, 0x40000);
            i = 0;
            if (toolkitNonFLRegList != 0) {
                for (; i < numRegToolkits; i++) {
                    list[i] = toolkitNonFLRegList[i];
                }
                ourGlobals.field_138(toolkitNonFLRegList);
                toolkitNonFLRegList = 0;
            }
            toolkitNonFLRegList = list;
            list[i] = registry;
            numRegToolkits = numRegToolkits + 1;
        }
    }
    for (entry = registry->field_10; entry != 0; entry = entry->field_30) {
        if (entry->field_8 == pluginID) {
            err.field_0 = 1;
            err.field_4 = _rwerror(0x80000017);
            RwErrorSet(&err);
            return entry->field_0;
        }
    }
    newSize = registry->field_0 + ((size + 3) & -4);
    if (registry->field_8 != 0 && registry->field_8 < newSize) {
        return -1;
    }
    entry = (UnkStruct_toolkitRegEntries *)ourGlobals.field_144(toolkitRegEntries, 0x40000);
    if (entry == 0) {
        return -1;
    }
    entry->field_C = 0;
    entry->field_0 = registry->field_0;
    registry->field_0 = newSize;
    entry->field_4 = size;
    entry->field_8 = pluginID;
    entry->field_10 = 0;
    entry->field_14 = 0;
    entry->field_18 = 0;
    entry->field_1C = 0;
    if (constructCB != 0) {
        entry->field_20 = (void *(*)(void *, int, int))constructCB;
    } else {
        entry->field_20 = func_001DE180;
    }
    if (destructCB != 0) {
        entry->field_24 = (void *(*)(void *, int, int))destructCB;
    } else {
        entry->field_24 = func_001DE188;
    }
    entry->field_30 = 0;
    entry->field_34 = 0;
    if (copyCB != 0) {
        entry->field_28 = (void *(*)(void *, const void *, int, int))copyCB;
    } else {
        entry->field_28 = func_001DE190;
    }
    entry->field_2C = 0;
    entry->field_38 = registry;
    if (registry->field_10 == 0) {
        registry->field_14 = entry;
        registry->field_10 = entry;
    } else {
        registry->field_14->field_30 = entry;
        entry->field_34 = registry->field_14;
        registry->field_14 = entry;
    }
    return entry->field_0;
}

// [C] the entry free list: 0x3C-byte entries, _rwPluginRegFreeListBlockSize per block, aligned 4,
// _rwPluginRegListPreallocBlocks blocks, its struct in place at D_00670B58, hint 0x40000. numRegToolkits is cleared
// only on success.
// 0x001DDEF8
extern "C" int _rwPluginRegistryOpen(void)
{
    toolkitRegEntries = RwFreeListCreateAndPreallocateSpace(0x3C, _rwPluginRegFreeListBlockSize, 4,
                                                            _rwPluginRegListPreallocBlocks, D_00670B58, 0x40000);
    if (toolkitRegEntries != 0) {
        numRegToolkits = 0;
        return 1;
    }
    return 0;
}

// [C] only while no plugin has grown the registry (field_0 == field_4; else 0): the limit field_8 becomes field_0 +
// size unless it is already positive and not above size (signed compares), and field_C = 1. Returns 1.
// 0x001DDF58
extern "C" int _rwPluginRegistrySetStaticPluginsSize(UnkStruct_TKList *registry, int size)
{
    if (registry->field_0 != registry->field_4) {
        return 0;
    }
    if (registry->field_8 <= 0 || size < registry->field_8) {
        registry->field_8 = registry->field_0 + size;
    }
    registry->field_C = 1;
    return 1;
}

// [C] each entry from the first (field_10, along field_30) constructs: field_20(object, field_0, field_4). A NULL
// answer runs the destructors of the entries before it (from field_34 of the failing one, along field_34:
// field_24(object, field_0, field_4)) and returns NULL. Returns the registry.
// 0x001DDFD0
extern "C" UnkStruct_TKList *_rwPluginRegistryInitObject(UnkStruct_TKList *registry, void *object)
{
    UnkStruct_toolkitRegEntries *entry;

    for (entry = registry->field_10; entry != 0; entry = entry->field_30) {
        if (entry->field_20(object, entry->field_0, entry->field_4) == 0) {
            for (entry = entry->field_34; entry != 0; entry = entry->field_34) {
                entry->field_24(object, entry->field_0, entry->field_4);
            }
            return 0;
        }
    }
    return registry;
}

// [C] each entry from the last (field_14, along field_34) destructs: field_24(object, field_0, field_4). Returns the
// registry.
// 0x001DE068
extern "C" UnkStruct_TKList *_rwPluginRegistryDeInitObject(UnkStruct_TKList *registry, void *object)
{
    UnkStruct_toolkitRegEntries *entry;

    for (entry = registry->field_14; entry != 0; entry = entry->field_34) {
        entry->field_24(object, entry->field_0, entry->field_4);
    }
    return registry;
}
