// RenderWare Audio (RWA), EE side: the types our C port of retail's library uses (audio1).
//
// Written in the decomp's style (raw pointers, float, `extern` guest globals) so that tools/decomp2host turns it into
// host code at retail's layouts, like decomp game code. Names: RWA function names come from the E map
// (research/retail_names.csv, [C] per function). Type and member names come from the Revenge prototype's stabs
// (research/stabs/revenge_jul14_types.h: the same library a year later) and are [L] for B3: a struct takes them only
// where the offsets retail's code uses agree (cited per struct); anything else is a placeholder `field_XX` / `pad_XX`.
// Revenge's unions (RwaUniqueID::uuid, ::name; RwaParamTypeHandle::u) are flattened to their first member.
#ifndef RWA_RWACORE_H
#define RWA_RWACORE_H

// RenderWare's base types as RW 3.5's sky2 rwplcore.h declares them (MWCC: int/float are 32-bit) [C header].
typedef int RwInt32;
typedef unsigned int RwUInt32;
typedef short RwInt16;
typedef unsigned short RwUInt16;
typedef signed char RwInt8;
typedef unsigned char RwUInt8;
typedef float RwReal;
typedef RwInt32 RwBool;
typedef char RwChar;

// RenderWare's vectors [C RW 3.5 rwplcore.h].
struct RwV2d {
    RwReal x;
    RwReal y;
};
struct RwV3d {
    RwReal x;
    RwReal y;
    RwReal z;
};

// RenderWare's intrusive list: a link {next, prev} and a list head holding one link as its sentinel [C RW 3.5
// rwplcore.h; the RWA structs below embed them, Revenge stabs]. Unlinking writes prev->next = next, next->prev = prev
// [C RwaObjHandleRelease 0x294A7C-0x294A90]; a new element goes after the sentinel (first).
struct RwLLLink {
    RwLLLink *next;
    RwLLLink *prev;
};
struct RwLinkList {
    RwLLLink link;
};

// RwaLLNode [L], 0xC bytes: an RwaLList entry (rwallist.o). Lists of them walk `next` [C RwaLListGetCount 0x2997DC].
struct RwaLLNode {
    RwaLLNode *prev;
    RwaLLNode *next;
    void *data;
};

// RwaUUID [L], 16 bytes: the GUID layout, as _rwaUUIDSerialize swaps it [C 0x29369C-0x2936D0: 4, 2, 2 bytes].
struct RwaUUID {
    RwUInt32 time_low;
    RwUInt16 time_mid;
    RwUInt16 time_hi_and_version;
    RwUInt8 node[8];
};

// RwaUniqueID [L], 0xC bytes [C rwauuid.o 0x293710-0x2938B0].
struct RwaUniqueID {
    RwaUUID *uuid;           // 0x0 owned (from D_01E7CF50) when flags & 1
    const RwChar *name;      // 0x4 owned (RWA allocator) when flags & 2
    RwUInt32 flags;          // 0x8
};

// RwaParamTypeHandle [L], 8 bytes [C _rwaParamTypeHandleRefreshType(entry + 0xC), RwaObjInterfaceDefSetup 0x293D78].
struct RwaParamTypeHandle {
    void *paramType;         // 0x0 Revenge: union of RwaUUID *paramTypeUUID / RwaParamType *paramType
    RwBool isUUID;           // 0x4
};

struct RwaObj;
struct RwaObjDef;
struct RwaObjInterface;
struct RwaObjInterfaceDef;

// A parameter accessor: (object, parameter index, value or out pointer) -> success [C call sites 0x294644 /
// 0x294824: a0 = handle->obj, a1 = the map's paramIndex, a2 = a stack slot; v0 tested].
typedef RwBool (*RwaObjParamFunc)(RwaObj *obj, RwUInt32 paramIndex, void *value);

// Callbacks an object definition holds [L]; an object's own functions are stored into them whatever their declared
// parameter types (MWCC; b3host::gfn takes any function).
// An object allocator RwaObjCreate may be given: (definition, its argument, creation parameters, size) -> memory or
// null [C 0x292FA8].
typedef void *(*UnkFn_00292E40)(RwaObjDef *def, RwUInt32 arg, void *params, RwUInt32 size);
// The state init gets RwaObjCreate's arguments: object, parent, creation parameters, allocator and its argument
// [C 0x2930E0-0x2930F0].
typedef RwaObj *(*RwaObjCreateFunc)(RwaObj *obj, RwaObj *parent, void *params, UnkFn_00292E40 allocFn,
                                    RwUInt32 allocArg);
typedef RwaObj *(*RwaObjExecuteFunc)(RwaObj *obj);
typedef void (*RwaObjDestroyFunc)(RwaObj *obj);
typedef RwaObj *(*RwaObjGetParamsFunc)(RwaObj *obj, RwUInt32 *out);

// RwaInputParamDef / RwaOutputParamDef [L], 0x18 bytes: what an object definition offers, one per parameter
// [C RwaObjInterfaceAttach 0x2941D0 (index * 24), 0x2941EC (+0 to RwaUUIDCompare), 0x294344 (+0x14)].
struct RwaInputParamDef {
    RwaUniqueID uniqueID;
    RwaParamTypeHandle paramHandle;
    RwaObjParamFunc setFunc;
};
struct RwaOutputParamDef {
    RwaUniqueID uniqueID;
    RwaParamTypeHandle paramHandle;
    RwaObjParamFunc getFunc;
};

// RwaObjDefInterfaceInfo [L], 0x18 bytes: RwaObjDef::interfaceInfo [C RwaObjInterfaceAttach 0x2940EC-0x294238;
// RwaObjHandleCreateUsingObj 0x294BDC].
struct RwaObjDefInterfaceInfo {
    RwaObjInterfaceDef *defaultInterface;   // 0x00 Revenge types it as the definition; B3 code uses it as the interface
                                            // handed to RwaObjInterfaceAttach (0x294B5C) [C]
    RwLinkList interfaceList;               // 0x04 the attached interfaces (RwaObjInterface::link)
    RwUInt16 noInputs;                      // 0x0C
    RwUInt16 noOutputs;                     // 0x0E
    RwaInputParamDef *inputParams;          // 0x10
    RwaOutputParamDef *outputParams;        // 0x14
};

// RwaObjDef [L], 0x5C bytes [C offsets: uniqueID 0x294F2C, callbacks _RwaVirtualVoiceRegisterCommon 0x297EDC-0x297EEC,
// stateSizeAlign 0x297C50, noFreeListEntries 0x2B1E78, interfaceInfo 0x2940EC].
struct RwaObjDef {
    RwaUniqueID uniqueID;               // 0x00
    RwUInt32 flags;                     // 0x0C
    RwLinkList objDefList;              // 0x10
    RwLLLink objDefLink;                // 0x18
    RwLinkList waveDefList;             // 0x20
    RwaObjCreateFunc createFunc;        // 0x28 state init
    RwaObjExecuteFunc startExecuteFunc; // 0x2C
    RwaObjExecuteFunc finishExecuteFunc;// 0x30
    RwaObjDestroyFunc destroyFunc;      // 0x34
    void *unregisterFunc;               // 0x38
    RwaObjGetParamsFunc getParamsFunc;  // 0x3C creation-parameter info
    RwUInt32 stateSizeAlign;            // 0x40 low 28 bits: state size (the reference count follows the state);
                                        // top 4: log2 of the state alignment [C 0x297C50-0x297ED8]
    RwUInt16 createParamSize;           // 0x44
    RwUInt16 noFreeListEntries;         // 0x46
    void *instanceAllocator;            // 0x48
    RwaObjDefInterfaceInfo *interfaceInfo;  // 0x4C
    void *aliasInfo;                    // 0x50
    void *miscData;                     // 0x54
    RwUInt32 memoryHint;                // 0x58
};

// RwaObj [L], 0x28 bytes in Revenge; B3 keeps the handle reference count at +0x28 when the object has no state
// [C 0x294A44], so its B3 struct is at least 0x2C.
struct RwaObj {
    RwaObjDef *definition;              // 0x00
    RwLLLink link;                      // 0x04
    RwLinkList executeList;             // 0x0C
    RwLLLink executeLink;               // 0x14
    RwUInt32 flags;                     // 0x1C
    char *state;                        // 0x20 the object's state, or null [C 0x294C04]
    RwaObj *createObj;                  // 0x24
    RwUInt32 field_28;                  // 0x28 the handle reference count when there is no state (not in Revenge)
};

// The word an object's handle references are counted in: after the state, or field_28
// [C 0x294A1C-0x294A44, 0x294C00-0x294C20].
inline RwUInt32 *func_00294AF0_inl1(RwaObj *obj)
{
    if (obj->state)
        return (RwUInt32 *)(obj->state + (obj->definition->stateSizeAlign & 0x0FFFFFFF));
    return &obj->field_28;
}

// RwaInputParamMap / RwaOutputParamMap [L], 8 bytes: an interface's parameter tables [C 0x29480C `sll a1, 3`;
// Attach 0x294334].
struct RwaInputParamMap {
    RwaObjParamFunc setFunc;
    RwUInt16 paramIndex;
    unsigned char pad[2];
};
struct RwaOutputParamMap {
    RwaObjParamFunc getFunc;
    RwUInt16 paramIndex;
    unsigned char pad[2];
};

// RwaObjInterface [L], 0x1C bytes + the two parameter tables, which follow it [C RwaObjInterfaceAttach
// 0x294130-0x29418C].
struct RwaObjInterface {
    RwaObjInterfaceDef *interfaceDef;       // 0x00
    RwaInputParamMap *inputParamMaps;       // 0x04 (= this + 0x1C)
    RwaOutputParamMap *outputParamMaps;     // 0x08 (after the input maps)
    RwUInt32 refCount;                      // 0x0C extra attachments
    RwUInt32 flags;                         // 0x10 1 = memory from the caller's allocator
    RwLLLink link;                          // 0x14 in RwaObjDefInterfaceInfo::interfaceList
};

// RwaObjInterfaceDefParam [L], 0x14 bytes [C RwaObjInterfaceDefSetup 0x293D68 (index * 20), 0x293D74 (+8 cleared:
// the uniqueID's flags), 0x293D7C (+0xC)].
struct RwaObjInterfaceDefParam {
    RwaUniqueID uniqueID;
    RwaParamTypeHandle paramType;
};

// RwaObjInterfaceDef [L], 0x28 bytes from its free list D_01E7CF80 [C _rwaObjInterfaceOpenModule 0x294504; fields
// RwaObjInterfaceDefSetup 0x293DD0-0x293DE8, Destroy 0x293AF4-0x293BA0].
struct RwaObjInterfaceDef {
    RwaUniqueID uniqueID;                   // 0x00
    RwaObjInterfaceDefParam *outputParams;  // 0x0C
    RwaObjInterfaceDefParam *inputParams;   // 0x10 (one allocation holds both unless the caller gave them)
    RwUInt16 noInputParams;                 // 0x14
    RwUInt16 noOutputParams;                // 0x16
    RwUInt32 flags;                         // 0x18 1 = parameter arrays are the caller's, 2 = the struct is the caller's
    void (*unregFunc)(RwaObjInterfaceDef *def);   // 0x1C called by Destroy, if set
    RwLLLink link;                          // 0x20 in _objInterfaceDefList
};

// RwaFreeList [L], 0x24 bytes [C rwafreelist.o; _rwaFreeListModuleOpen 0x2994F4]. The same block allocator as
// RenderWare's own free lists: blocks of entriesPerBlock entries with a used-bit map (heapSize bytes) in front.
struct RwaFreeList {
    RwUInt32 entrySize;         // 0x00 rounded up to the alignment
    RwUInt32 entriesPerBlock;   // 0x04
    RwUInt32 heapSize;          // 0x08 bytes of the used-bit map ((entries + 7) / 8)
    RwUInt32 alignment;         // 0x0C a power of two
    RwLinkList blockList;       // 0x10
    RwUInt32 flags;             // 0x18 1 = caller-provided struct (never freed), 2 = free a block once it is empty
    RwLLLink link;              // 0x1C in _freeListList
};

// A free-list block (not in the stabs): link, then the used-bit map (bit 7 of byte 0 = entry 0), then the entries.
struct UnkStruct_00298F00 {
    RwLLLink link;
    unsigned char bitmap[1];    // element count unknown: RwaFreeList::heapSize bytes
};

// Memory interface: four allocator calls at D_00670D04 (rwamemory.o) [C 0x293340]. Not in the stabs.
typedef void *(*RwaMemAllocFunc)(RwUInt32 size, RwUInt32 hint);
typedef void (*RwaMemFreeFunc)(void *p);
typedef void *(*RwaMemReallocFunc)(void *p, RwUInt32 size, RwUInt32 hint);
typedef void *(*RwaMemCallocFunc)(RwUInt32 n, RwUInt32 size, RwUInt32 hint);
struct UnkStruct_00293340 {
    RwaMemAllocFunc field_0;      // default: func_00293420 = malloc
    RwaMemFreeFunc field_4;       // default: free
    RwaMemReallocFunc field_8;    // default: func_00293410 = realloc
    RwaMemCallocFunc field_C;     // default: func_002933C0 (malloc + memset)
};

// Locals whose address goes into guest memory (decomp2host T27): `B3_RETAIL_FRAME(bytes);` as a function's first
// statement (retail's frame size) and `B3_FRAME_LOCAL(T, offset)` for a local at retail's sp offset give a pointer into
// a guest stack frame at retail's own addresses (b3host::GuestStackFrame). Text-level, so no #define here.

// The memory wrappers every RWA object allocates through (rwamemory.o; unnamed in the E map).
extern "C" void *func_002934C0(RwUInt32 size, RwUInt32 hint);   // D_00670D04.field_0
extern "C" void func_00293440(void *p);                         // D_00670D04.field_4

// RwaObjHandle [L], 0x18 bytes [C rwaintf.o 0x2949A0-0x294C74].
struct RwaObjHandle {
    RwaObj *obj;                    // 0x00
    RwaObjInterface *objInterface;  // 0x04 the interface the handle uses
    RwLLLink link;                  // 0x08 in _handleList
    RwUInt32 flags;                 // 0x10 1 = the caller's struct, 2 = (set by every create) keeps the object
    RwUInt32 refCount;              // 0x14 extra references
};

// RwaWaveFormat [L], 0x1C bytes [C sampleRate _rwaVirtualVoiceSetWave 0x297968, length _rwaVoiceManagerUpdate
// 0x29581C, noChannels RwaVoiceManagerUpdateVoice 0x294EA0].
struct RwaWaveFormat {
    RwUInt32 sampleRate;      // 0x00
    RwaUUID *dataType;        // 0x04
    RwUInt32 length;          // 0x08
    RwUInt8 bitDepth;         // 0x0C
    RwUInt8 noChannels;       // 0x0D
    unsigned char pad[2];     // 0x0E
    void *miscData;           // 0x10
    RwUInt32 miscDataSize;    // 0x14
    RwUInt8 flags;            // 0x18
    RwUInt8 reserved;         // 0x19
};
// RwaWave [L], 0x5C bytes [C format / targetFormat 0x297968, data _rwaSPU2WaveRAMGetAddress 0x2B1070, flags 0x294EAC].
struct RwaWave {
    RwaUniqueID uniqueID;         // 0x00
    struct RwaWaveDef *waveDef;   // 0x0C
    RwaWaveFormat format;         // 0x10 the data's format
    RwaWaveFormat targetFormat;   // 0x2C the played format
    RwUInt32 uncompressedLength;  // 0x48
    RwUInt32 data;                // 0x4C SPU2: its sound-RAM handle (rwaallocbitmap), not an EE address; Revenge: void *
    void *state;                  // 0x50
    RwUInt32 flags;               // 0x54 2 = positional (3D) [C 0x294EAC, 0x2B10C4]
    RwaObj *obj;                  // 0x58
};


// RwaWaveDict [L], 0x30 bytes [C waveListHead +0xC (RwaWaveDictGetWaveCount 0x2ADEC0), flagsAux +0x1A (0x29E990), link
// +0x1C (_rwaWaveDictRemoveAllWave 0x2ADDF4), waveRAMHandle / waveRAMSize +0x28 / +0x2C (RwaWaveDictFreeSRAM 0x29E9C8)].
struct RwaWaveDict {
    RwaUniqueID uniqueID;     // 0x00
    RwaLLNode waveListHead;   // 0x0C the waves (an RwaLList)
    unsigned char pad[2];     // 0x18
    RwUInt8 flagsAux;         // 0x1A
    RwUInt8 flags;            // 0x1B
    RwLLLink link;            // 0x1C in _waveDictList
    void *dumpAddr;           // 0x24
    RwUInt32 waveRAMHandle;   // 0x28 a sound-RAM handle (start | count << shift, rwaallocbitmap), not an EE address;
                              //      Revenge types it void *
    RwUInt32 waveRAMSize;     // 0x2C
};

// RwaWaveDef [L name: RwaWaveDefCreate returns it]: the callbacks a wave type provides. Their offsets 0xC-0x2C are
// [C] (_rwaNULLWaveRegister 0x2A0808-0x2A082C, rwawavedict.o 0x29E914 / 0x29EA30 / 0x29EE28 / 0x29F178); the order
// matches Revenge's typedefs RwaWaveDefCreateFunc ... RwaWaveDefFixupFunc [G: names by order], so the members stay
// placeholders with those names in comments.
struct RwaWaveDef {
    unsigned char pad_0[0xC];
    RwaWave *(*field_C)(RwaWave *wave, void *params);                     // 0x0C create
    void (*field_10)(RwaWave *wave);                                      // 0x10 destroy
    RwaWave *(*field_14)(RwaWave *wave);                                  // 0x14 upload
    RwaWave *(*field_18)(RwaWave *wave);                                  // 0x18 download
    RwBool (*field_1C)(RwaWave *wave);                                    // 0x1C upload busy
    RwUInt32 (*field_20)(RwaWave *wave);                                  // 0x20 allocate sound RAM (a handle)
    void (*field_24)(RwaWave *wave);                                      // 0x24 free sound RAM
    RwUInt32 (*field_28)(void *file, RwUInt32 handle, RwUInt32 offset, RwUInt32 size);   // 0x28 read to sound RAM
    void (*field_2C)(RwaWave *wave, RwUInt32 arg, RwBool fix);           // 0x2C fix up / unfix
    void (*field_30)(struct RwaWaveDef *def);   // 0x30 called by RwaWaveDefDestroy [C 0x29FFB4]
    RwUInt32 field_34;                  // 0x34 number of supported formats (0: any) [C 0x29F864]
    struct RwaWaveDefFormat **field_38; // 0x38 the supported formats, or null
    RwUInt32 field_3C;                  // 0x3C index of the default one
    RwaObjDef *field_40;                // 0x40 the object definition its waves belong to [C 0x29152C]
    RwUInt32 field_44;                  // 0x44 per-wave state size (0: none) [C 0x291494]
    RwUInt32 field_48;                  // 0x48 0x40 after create [C 0x2A00A0]
    RwaFreeList *field_4C;              // 0x4C its waves' free list, or null (the shared D_01E7CB30) [C 0x291414]
    RwLLLink field_50;                  // 0x50 in its object definition's waveDefList [C 0x29FED8, 0x29FFE8]
    RwUInt32 field_58;                  // 0x58 flags: 1 = the format array is RWA's (freed) [C 0x29FCEC]
};

// RwaWaveDefFormat [L], 0x14 bytes: one format a wave type accepts [C _rwaWaveDefIsTargetFormatSupported 0x29F87C-
// 0x29F914: every field].
struct RwaWaveDefFormat {
    RwaUUID *dataType;        // 0x00
    RwUInt8 bitDepth;         // 0x04
    RwUInt8 noChannels;       // 0x05
    RwUInt32 minSampleRate;   // 0x08
    RwUInt32 maxSampleRate;   // 0x0C
    RwUInt8 flags;            // 0x10
};

// RwaParamType [L], 0x28 bytes [C: link at +0x20 (_rwaSymbolTableAddParamType 0x291BC0), paramType +0x18 (0x291AD0),
// uniqueID.uuid +0 (0x291B2C)]. Revenge's `RwaType type` at 0x14 is 4 bytes; B3 reads and writes it as a halfword
// (RwaParamTypeCreateAlias 0x292244 lh, 0x292270 sh), so it is split here.
struct RwaParamType {
    RwaUniqueID uniqueID;     // 0x00
    void *rangeMin;           // 0x0C
    void *rangeMax;           // 0x10
    RwInt16 type;             // 0x14 RwaType; 0x4000 = an alias
    RwUInt16 field_16;        // 0x16
    RwaParamType *paramType;  // 0x18 the type it is based on
    RwUInt32 flags;           // 0x1C
    RwLLLink link;            // 0x20
};

// RwaObjDef::stateSizeAlign's alignment exponent, as every object's RegisterCommon computes it: RenderWare's msb macro over
// (RwUInt16)(1 << RwaFindLSB(align)) (0 -> 1), each re-evaluation calling RwaFindLSB again as retail does
// [C _RwaVirtualVoiceRegisterCommon 0x297C7C-0x297EA4; the same shape in _RwaSPU2VoiceRegisterCommon 0x2B1EA4,
// RwaListenerRegisterUsingInterface 0x298AA4, RwaVirtualListenerRegisterUsingInterface 0x2AEC24].
extern "C" RwUInt32 RwaFindLSB(RwUInt32 x);
extern RwInt8 _rwMsbBit[256];   // RenderWare's most-significant-bit table (librwcore.a badevice.obj)
inline RwInt32 func_00297C30_inl1(RwUInt32 align)
{
    RwUInt16 a = (RwUInt16)(1 << (RwUInt8)RwaFindLSB(align));
    return a ? (RwUInt16)(1 << (RwUInt8)RwaFindLSB(align)) : 1;
}
inline RwInt32 func_00297C30_inl2(RwUInt32 align)
{
    RwInt32 m;
    if (func_00297C30_inl1(align) & 0xFFFF0000) {
        if ((func_00297C30_inl1(align) >> 16) & 0xFF00)
            m = _rwMsbBit[func_00297C30_inl1(align) >> 24] + 7;
        else
            m = _rwMsbBit[func_00297C30_inl1(align) >> 16] - 1;
        m += 0x10;
    } else if (func_00297C30_inl1(align) & 0xFF00) {
        m = _rwMsbBit[func_00297C30_inl1(align) >> 8] + 7;
    } else {
        m = _rwMsbBit[func_00297C30_inl1(align)] - 1;
    }
    return m;
}
// def->stateSizeAlign = size | log2(align) << 28, in retail's store order [C 0x297C50-0x297ED8].
inline void func_00297C30_inl3(RwaObjDef *def, RwUInt32 size, RwUInt32 align)
{
    def->stateSizeAlign &= 0xF0000000;
    def->stateSizeAlign |= size;
    def->stateSizeAlign &= 0x0FFFFFFF;
    RwInt32 m = func_00297C30_inl2(align);
    def->stateSizeAlign |= (RwUInt32)m << 28;
}

#endif
