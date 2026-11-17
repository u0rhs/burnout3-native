// RenderWare 3.6 core (RW 3.1 file name: src/batextur.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

extern unsigned char textureTKList[];   // [C] the plugin registry (0x18 bytes, symbol table) ([L] RwPluginRegistry)

extern "C" int _rwPluginRegistryAddPlugin(void *registry, int size, unsigned int pluginID, void *constructCB,
                                          void *destructCB, void *copyCB);

// [C] a call of _rwPluginRegistryAddPlugin(&textureTKList, size, pluginID, constructCB, destructCB, copyCB), all five
// arguments passed on ([L] copyCB: the sixth argument lands in the entry's +0x28).
// 0x001EAF88
extern "C" int RwTextureRegisterPlugin(int size, unsigned int pluginID, void *constructCB, void *destructCB, void *copyCB)
{
    return _rwPluginRegistryAddPlugin(textureTKList, size, pluginID, constructCB, destructCB, copyCB);
}

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)
extern int textureModule;              // [C] gp-relative 0x004E27B8 (8 bytes): +0 the texture globals' offset in ourGlobals
extern int useFarClip;                 // [C] gp-relative 0x004E27BC = textureModule + 4: the module's open count (the symbol
                                       //     table's name for that word is useFarClip)
extern void *dummyTexDict;             // [C] gp-relative 0x004E17A0: the dummy texture dictionary
extern unsigned char texDictTKList[];  // [C] 0x0049A2D8: the texture dictionary plugin registry (0x18 bytes)
extern int _rwTextureFreeListBlockSize;          // [C] gp-relative
extern int _rwTextureFreeListPreallocBlocks;     // [C] gp-relative
extern int _rwTexDictionaryFreeListBlockSize;    // [C] gp-relative
extern int _rwTexDictionaryFreeListPreallocBlocks; // [C] gp-relative
extern unsigned char D_00674C70[];     // [C] in-place space for the texture free list
extern unsigned char D_00674C98[];     // [C] in-place space for the texture dictionary free list

extern "C" void *RwFreeListCreateAndPreallocateSpace(int entrySize, int entriesPerBlock, int alignment,
                                                     int numBlocksToPreallocate, void *inPlaceSpace, unsigned int hint);
extern "C" int RwFreeListDestroy(void *freeList);
extern "C" void *_rwPluginRegistryInitObject(void *registry, void *object);
extern "C" void *_rwPluginRegistryDeInitObject(void *registry, void *object);

// [C] default callbacks stored in the texture globals (not ported here).
extern "C" void TextureDefaultFind(void);
extern "C" void TextureDefaultRead(void);
extern "C" void TextureRasterDefaultBuildMipmaps(void);
extern "C" void TextureDefaultMipmapName(void);
extern "C" void RwTextureDestroy(void);

// [C] a link, 8 bytes ([L] RwLLLink: next, prev).
struct UnkStruct_texLink {
    UnkStruct_texLink *field_0;        // [C] next
    UnkStruct_texLink *field_4;        // [C] previous
};

// [C] a texture dictionary, as the module builds and tears it down ([L] RwTexDictionary); size 0x30 [L] (its
// plugin data follows). The dictionary list is at the texture globals +0 (head: first, last).
struct UnkStruct_RwTexDictionary {
    unsigned char field_0;             // [C] 6 ([L] object type rwTEXDICTIONARY)
    unsigned char field_1;             // [C] 0
    unsigned char field_2;             // [C] 0
    unsigned char field_3;             // [C] 0
    void *field_4;                     // [C] 0
    UnkStruct_texLink field_8;         // [C] the dictionary's texture list head, empty = itself
    UnkStruct_texLink field_10;        // [C] the link into the dictionary list (next, prev)
};

// [C] the texture globals at ourGlobals + textureModule ([L] textureModule).
struct UnkStruct_textureModule {
    UnkStruct_texLink field_0;         // [C] the texture dictionary list head
    void *field_8;                     // [C] the texture free list (entries of the first word of textureTKList, alignment 4)
    void *field_C;                     // [C] the texture dictionary free list (first word of texDictTKList)
    UnkStruct_RwTexDictionary *field_10;  // [C] the current texture dictionary = the dummy dictionary ([L] currentTexDict)
    void (*field_14)(void);            // [C] TextureDefaultRead ([L] readTexture)
    void (*field_18)(void);            // [C] TextureDefaultFind ([L] findCallBack)
    int field_1C;                      // [C] 0
    int field_20;                      // [C] 0
    void *field_24;                    // [C] 0 ([L] the mipmap name buffer; close frees it)
    short field_28;                    // [C] 0
    unsigned char pad_2A[0x2];
    void (*field_2C)(void);            // [C] TextureRasterDefaultBuildMipmaps
    void (*field_30)(void);            // [C] TextureDefaultMipmapName
};

// [C] the RW allocator fields of ourGlobals.
struct UnkStruct_ourGlobalsTexture {
    unsigned char pad_0[0x134];
    void *(*field_134)(unsigned int, unsigned int);  // [C] (size, hint) ([L] rwmalloc)
    void (*field_138)(void *);                       // [C] ([L] rwfree)
    unsigned char pad_13C[0x8];
    void *(*field_144)(void *, unsigned int);        // [C] (free list, hint) ([L] memoryAlloc)
    void *(*field_148)(void *, void *);              // [C] (free list, entry) ([L] memoryFree)
};

typedef int (*UnkFn_001EAEA0)(void *texture, void *data);   // [C] called (texture = link - 8, data); a zero result stops

// [C] walks the textures of the dictionary: head = dict + 8; for the link from head->next until it is the head: next =
// link->next (read first), calls callback(link - 8, data); stops when it returns 0. Returns the dictionary.
// 0x001EAEA0
extern "C" void *RwTexDictionaryForAllTextures(UnkStruct_RwTexDictionary *dict, UnkFn_001EAEA0 callback, void *data)
{
    UnkStruct_texLink *head = &dict->field_8;
    UnkStruct_texLink *link = head->field_0;
    UnkStruct_texLink *next;

    while (link != head) {
        next = link->field_0;
        if (callback((unsigned char *)link - 8, data) == 0) {
            break;
        }
        link = next;
    }
    return dict;
}

// [C] textureModule = offset; +8 = Create(first word of textureTKList, _rwTextureFreeListBlockSize, 4,
// _rwTextureFreeListPreallocBlocks, D_00674C70, 0x40006), 0 -> return 0; +0xC = Create(first word of texDictTKList,
// _rwTexDictionaryFreeListBlockSize, 4, _rwTexDictionaryFreeListPreallocBlocks, D_00674C98, 0x40408), 0 -> destroy +8,
// +8 = 0, return 0. Then open count (useFarClip) += 1, the list head +0 made empty (next = prev = itself), the dummy
// dictionary allocated from the dictionary free list (ourGlobals+0x144 (list, 0x30016)): when non-NULL its type byte =
// 6, bytes 1..3 and +4 = 0, its texture list (+8) empty, linked at the head of the dictionary list, and
// _rwPluginRegistryInitObject(texDictTKList, dict). dummyTexDict and +0x10 = the dictionary (or 0); on 0 the two free
// lists are destroyed (+0xC cleared, +8 cleared) and 0 returned (the count stays incremented). Else the default
// callbacks are stored (+0x18 TextureDefaultFind, +0x14 TextureDefaultRead, +0x2C TextureRasterDefaultBuildMipmaps,
// +0x30 TextureDefaultMipmapName), +0x28 (short), +0x1C, +0x20, +0x24 = 0; returns the instance.
// 0x001EA4B0
extern "C" void *_rwTextureOpen(void *instance, int offset, int size)
{
    UnkStruct_ourGlobalsTexture *rw = (UnkStruct_ourGlobalsTexture *)ourGlobals;
    UnkStruct_textureModule *g;
    UnkStruct_RwTexDictionary *dict;
    void *freeList;

    textureModule = offset;
    freeList = RwFreeListCreateAndPreallocateSpace(*(int *)textureTKList, _rwTextureFreeListBlockSize, 4,
                                                   _rwTextureFreeListPreallocBlocks, D_00674C70, 0x40006);
    g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
    g->field_8 = freeList;
    if (freeList == 0) {
        return 0;
    }
    freeList = RwFreeListCreateAndPreallocateSpace(*(int *)texDictTKList, _rwTexDictionaryFreeListBlockSize, 4,
                                                   _rwTexDictionaryFreeListPreallocBlocks, D_00674C98, 0x40408);
    g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
    g->field_C = freeList;
    if (freeList == 0) {
        RwFreeListDestroy(g->field_8);
        g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
        g->field_8 = 0;
        return 0;
    }
    useFarClip = useFarClip + 1;
    g->field_0.field_4 = &g->field_0;
    g->field_0.field_0 = &g->field_0;
    dict = (UnkStruct_RwTexDictionary *)rw->field_144(freeList, 0x30016);
    if (dict != 0) {
        UnkStruct_texLink *oldFirst;

        dict->field_1 = 0;
        dict->field_0 = 6;
        dict->field_2 = 0;
        dict->field_3 = 0;
        dict->field_4 = 0;
        g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
        oldFirst = g->field_0.field_0;
        dict->field_10.field_4 = (UnkStruct_texLink *)g;
        dict->field_10.field_0 = oldFirst;
        g->field_0.field_0->field_4 = &dict->field_10;
        g->field_0.field_0 = &dict->field_10;
        dict->field_8.field_4 = &dict->field_8;
        dict->field_8.field_0 = &dict->field_8;
        _rwPluginRegistryInitObject(texDictTKList, dict);
    }
    g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
    dummyTexDict = dict;
    g->field_10 = dict;
    if (dict == 0) {
        RwFreeListDestroy(g->field_C);
        g->field_C = 0;
        RwFreeListDestroy(g->field_8);
        g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
        g->field_8 = 0;
        return 0;
    }
    g->field_18 = TextureDefaultFind;
    g->field_14 = TextureDefaultRead;
    g->field_2C = TextureRasterDefaultBuildMipmaps;
    g->field_30 = TextureDefaultMipmapName;
    g->field_28 = 0;
    g->field_1C = 0;
    g->field_20 = 0;
    g->field_24 = 0;
    return instance;
}

// [C] frees the +0x24 block when set (+0x28 and +0x24 cleared). If the texture free list (+8) is set: when the
// dictionary free list (+0xC) is set too, walks the dictionary list (head +0) for the dummy dictionary (dummyTexDict):
// found, it clears +0x10 if that is the dummy, destroys its textures (RwTexDictionaryForAllTextures with
// RwTextureDestroy, data 0), _rwPluginRegistryDeInitObject(texDictTKList, dict), unlinks it from the list, returns it to
// the dictionary free list (ourGlobals+0x148) and clears dummyTexDict; the walk then ends. Then each free list is
// destroyed and cleared when set (+8 then +0xC); open count (useFarClip) -= 1; returns the instance.
// 0x001EA318
extern "C" void *_rwTextureClose(void *instance, int offset, int size)
{
    UnkStruct_ourGlobalsTexture *rw = (UnkStruct_ourGlobalsTexture *)ourGlobals;
    UnkStruct_textureModule *g = (UnkStruct_textureModule *)(ourGlobals + textureModule);

    if (g->field_24 != 0) {
        rw->field_138(g->field_24);
        g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
        g->field_28 = 0;
        g->field_24 = 0;
    }
    g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
    if (g->field_8 != 0) {
        if (g->field_C != 0) {
            UnkStruct_texLink *head = &g->field_0;
            UnkStruct_texLink *link = head->field_0;

            if (link != head) {
                while (1) {
                    UnkStruct_RwTexDictionary *dict = (UnkStruct_RwTexDictionary *)((unsigned char *)link - 0x10);
                    UnkStruct_texLink *next = link->field_0;

                    if ((void *)dict == dummyTexDict) {
                        UnkStruct_texLink *prev;
                        UnkStruct_texLink *following;
                        UnkStruct_textureModule *g2 = (UnkStruct_textureModule *)(ourGlobals + textureModule);

                        if (g2->field_10 == dict) {
                            g2->field_10 = 0;
                        }
                        RwTexDictionaryForAllTextures(dict, (UnkFn_001EAEA0)RwTextureDestroy, 0);
                        _rwPluginRegistryDeInitObject(texDictTKList, dict);
                        prev = dict->field_10.field_4;
                        following = dict->field_10.field_0;
                        g2 = (UnkStruct_textureModule *)(ourGlobals + textureModule);
                        prev->field_0 = following;
                        dict->field_10.field_0->field_4 = prev;
                        rw->field_148(g2->field_C, dict);
                        dummyTexDict = 0;
                        break;
                    }
                    if (next == head) {
                        break;
                    }
                    link = next;
                }
            }
        }
        g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
        if (g->field_8 != 0) {
            RwFreeListDestroy(g->field_8);
            g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
            g->field_8 = 0;
        }
        g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
    }
    if (g->field_C != 0) {
        RwFreeListDestroy(g->field_C);
        g = (UnkStruct_textureModule *)(ourGlobals + textureModule);
        g->field_C = 0;
    }
    useFarClip = useFarClip - 1;
    return instance;
}
