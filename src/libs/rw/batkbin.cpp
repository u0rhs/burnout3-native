// RenderWare 3.6 core: plugin registry streaming (RW 3.1 file name: src/batkbin.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

struct UnkStruct_toolkitRegEntries;

// [C] a plugin registry ([L] RwPluginRegistry); see batkreg.c.
struct UnkStruct_TKList {
    unsigned char pad_0[0x10];
    UnkStruct_toolkitRegEntries *field_10;      // [C] first entry ([L] firstRegEntry)
};

// [C] a registry entry ([L] RwPluginRegEntry); see batkreg.c.
struct UnkStruct_toolkitRegEntries {
    int field_0;                                 // [C] ([L] offset)
    unsigned char pad_4[0x4];
    unsigned int field_8;                        // [C] ([L] pluginID)
    void *(*field_C)(void *, int, void *, int, int);        // [C] ([L] readCB)
    void *(*field_10)(void *, int, const void *, int, int); // [C] ([L] writeCB)
    int (*field_14)(const void *, int, int);     // [C] ([L] getSizeCB)
    unsigned char pad_18[0x18];
    UnkStruct_toolkitRegEntries *field_30;       // [C] next entry ([L] nextRegEntry)
};

// [C] the entry with pluginID gets the three stream callbacks and its offset is returned; -1 when there is none.
// 0x001DD6E8
extern "C" int _rwPluginRegistryAddPluginStream(UnkStruct_TKList *registry, unsigned int pluginID, void *readCB,
                                                void *writeCB, void *getSizeCB)
{
    UnkStruct_toolkitRegEntries *entry;

    for (entry = registry->field_10; entry != 0; entry = entry->field_30) {
        if (entry->field_8 == pluginID) {
            entry->field_C = (void *(*)(void *, int, void *, int, int))readCB;
            entry->field_10 = (void *(*)(void *, int, const void *, int, int))writeCB;
            entry->field_14 = (int (*)(const void *, int, int))getSizeCB;
            return entry->field_0;
        }
    }
    return -1;
}
