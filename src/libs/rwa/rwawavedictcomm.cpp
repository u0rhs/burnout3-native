// rwawavedictcomm (librwacore.a, truncated in the E map) -- wave dictionaries: the waves they hold (an RwaLList of
// RwaWave pointers), the current dictionary, the list of all dictionaries. Retail 0x2ADDD0-0x2AE3B8 [C E map].
#include "rwa/rwacore.h"

extern RwLinkList _waveDictList;
extern RwaWaveDict *_currentDict;
extern "C" RwaLLNode *RwaLListRemoveData(RwaLLNode *list, void *data);
extern "C" RwaLLNode *RwaLListAddData(RwaLLNode *list, void *data);
extern "C" RwInt32 RwaLListFindDataIndex(RwaLLNode *list, void *data);
extern "C" RwInt32 RwaLListGetCount(RwaLLNode *list);
extern "C" void *RwaLListFindDataByIndex(RwaLLNode *list, RwUInt32 index);
extern "C" void RwaLListEmpty(RwaLLNode *list);
extern RwaWaveDict *_workingDict;
extern RwaFreeList D_01E810E0;      // the dictionaries' free list (static struct)
extern RwaWaveDict D_01E810B0;      // the working dictionary (static struct)
extern RwUInt32 D_004E2BE0;         // _moduleOpen of rwawavedictcomm [L name, research/retail_data_names.csv]
extern RwUInt32 D_004E2BE4;         // _moduleClosing [L name, ibid.]
extern void *_rwaGwaveDictLoadBuf;
extern RwUInt32 _rwaGwaveDictLoadBufSize;
extern RwBool _rwaGwaveDictLoadBufCreatedByRWA;
extern RwaUUID rwaWAVEDICT_WORK;
extern char D_004C19B0[];           // the working dictionary's name
extern "C" void RwaWaveDictFreeSRAM(RwaWaveDict *dict);
extern "C" void RwaWaveDestroy(RwaWave *wave);
extern "C" void *_rwaUniqueIDInitialize(void *id);
extern "C" void *_rwaUniqueIDFreeData(void *id);
extern "C" void *_rwaUniqueIDAssignUUID(void *id, RwaUUID *uuid);
extern "C" void *_rwaUniqueIDAssignName(void *id, const RwChar *name);
extern "C" void *RwaFreeListAlloc(RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);
extern "C" void func_00293450(void *p);

static RwaWaveDict *func_002ADDD0_inl1(RwLLLink *l)
{
    return (RwaWaveDict *)((char *)l - 0x1C);
}

// Removes `wave` from every dictionary.
// 0x002ADDD0
extern "C" void _rwaWaveDictRemoveAllWave(RwaWave *wave)
{
    for (RwLLLink *l = _waveDictList.link.next; l != &_waveDictList.link; l = l->next)
        RwaLListRemoveData(&func_002ADDD0_inl1(l)->waveListHead, wave);
}

// 0x002ADE30
extern "C" RwaWaveDict *RwaWaveDictGetCurrent(void)
{
    return _currentDict;
}

// Adds `wave` unless the dictionary has it already; the dictionary, or null when the list entry cannot be allocated.
// 0x002ADE40
extern "C" RwaWaveDict *RwaWaveDictAddWave(RwaWaveDict *dict, RwaWave *wave)
{
    if (RwaLListFindDataIndex(&dict->waveListHead, wave) != -1)
        return dict;
    if (!RwaLListAddData(&dict->waveListHead, wave))
        return 0;
    return dict;
}

// 0x002ADEC0
extern "C" RwInt32 RwaWaveDictGetWaveCount(RwaWaveDict *dict)
{
    return RwaLListGetCount(&dict->waveListHead);
}

// 0x002ADED0
extern "C" RwaWave *RwaWaveDictFindWaveByIndex(RwaWaveDict *dict, RwUInt32 index)
{
    return (RwaWave *)RwaLListFindDataByIndex(&dict->waveListHead, index);
}

// Whether any dictionary holds a wave of definition `waveDef`: `waveDef`, or null.
// 0x002ADEE0
extern "C" void *RwaWaveDictUsingAllWaveDef(void *waveDef)
{
    for (RwLLLink *l = _waveDictList.link.next; l != &_waveDictList.link; l = l->next) {
        RwaWaveDict *dict = func_002ADDD0_inl1(l);
        for (RwaLLNode *n = dict->waveListHead.next; n != &dict->waveListHead; n = n->next)
            if (((RwaWave *)n->data)->waveDef == waveDef)
                return waveDef;
    }
    return 0;
}

typedef RwaWave *(*UnkFn_002ADF30)(RwaWave *wave, RwUInt32 arg);

// Calls `cb(wave, arg)` for each wave until one returns null (the next entry is read before the call). No result.
// `arg` is a plain word: callers pass sound-RAM handles as well as pointers.
// 0x002ADF30
extern "C" void RwaWaveDictForAllWaves(RwaWaveDict *dict, UnkFn_002ADF30 cb, RwUInt32 arg)
{
    RwaLLNode *n = dict->waveListHead.next;
    while (n != &dict->waveListHead) {
        RwaWave *w = (RwaWave *)n->data;
        n = n->next;
        if (!cb(w, arg))
            return;
    }
}

// Destroys a dictionary: off _waveDictList (the working one becomes current if it was), its sound RAM freed when it
// has its own (flag 0x40), then by flags: 0x20 nothing more, 0x10 only the dump buffer freed; otherwise each wave that
// no other dictionary holds is destroyed, the list emptied, the ID freed and the struct released (flag 1: free list,
// 4: the caller's, else the RWA allocator). With no dictionary left on the list retail destroys every wave twice (once
// before the search, once for "not found" [C 0x2AE088 and 0x2AE134]); kept.
// 0x002ADFA0
extern "C" void RwaWaveDictDestroy(RwaWaveDict *dict)
{
    dict->link.prev->next = dict->link.next;
    dict->link.next->prev = dict->link.prev;
    if (dict == _currentDict)
        _currentDict = _workingDict;
    if ((dict->flags & 0x40) && (dict->waveRAMHandle || (dict->flagsAux & 1)))
        RwaWaveDictFreeSRAM(dict);
    if (dict->flags & 0x20)
        return;
    if (dict->flags & 0x10) {
        func_00293450(dict->dumpAddr);
        return;
    }
    for (RwaLLNode *n = dict->waveListHead.next; n != &dict->waveListHead; n = n->next) {
        RwaWave *w = (RwaWave *)n->data;
        RwBool held = 0;
        if (_waveDictList.link.next == &_waveDictList.link)
            RwaWaveDestroy(w);
        for (RwLLLink *l = _waveDictList.link.next; l != &_waveDictList.link; l = l->next) {
            if (RwaLListFindDataIndex(&func_002ADDD0_inl1(l)->waveListHead, w) != -1) {
                held = 1;
                break;
            }
        }
        if (!held)
            RwaWaveDestroy(w);
    }
    RwaLListEmpty(&dict->waveListHead);
    _rwaUniqueIDFreeData(dict);
    if (dict->flags & 1)
        RwaFreeListFree(&D_01E810E0, dict);
    else if (!(dict->flags & 4))
        func_00293440(dict);
}

// A new, empty dictionary in `dict` (the caller's: flags 4) or from the free list (flags 1), linked first on
// _waveDictList.
// 0x002AE190
extern "C" RwaWaveDict *_rwaWaveDictCreate(RwaWaveDict *dict)
{
    if (dict) {
        dict->flags = 4;
    } else {
        dict = (RwaWaveDict *)RwaFreeListAlloc(&D_01E810E0, 0x30808);
        if (!dict)
            return 0;
        dict->flags = 1;
    }
    _rwaUniqueIDInitialize(dict);
    dict->link.prev = 0;
    dict->link.next = 0;
    dict->link.next = _waveDictList.link.next;
    dict->link.prev = &_waveDictList.link;
    _waveDictList.link.next->prev = &dict->link;
    _waveDictList.link.next = &dict->link;
    dict->waveListHead.prev = &dict->waveListHead;
    dict->waveListHead.next = &dict->waveListHead;
    dict->waveListHead.data = 0;
    dict->dumpAddr = 0;
    dict->waveRAMHandle = 0;
    dict->waveRAMSize = 0;
    dict->flagsAux = 0;
    return dict;
}

// 0x002AE250
extern "C" void _rwaWaveDictCloseModule(void)
{
    D_004E2BE4 = 1;
    while (_waveDictList.link.next != &_waveDictList.link)
        RwaWaveDictDestroy(func_002ADDD0_inl1(_waveDictList.link.next));
    RwaFreeListDestroy(&D_01E810E0);
    void *buf = _rwaGwaveDictLoadBuf;
    _waveDictList.link.next = &_waveDictList.link;
    _waveDictList.link.prev = &_waveDictList.link;
    if (buf && _rwaGwaveDictLoadBufCreatedByRWA == 1) {
        func_00293450(buf);
        _rwaGwaveDictLoadBuf = 0;
        _rwaGwaveDictLoadBufSize = 0;
        _rwaGwaveDictLoadBufCreatedByRWA = 0;
    }
    _workingDict = 0;
    _currentDict = 0;
    D_004E2BE0 = 0;
    D_004E2BE4 = 0;
}

// The dictionary list and the working dictionary (the current one at start).
// 0x002AE2F0
extern "C" RwBool _rwaWaveDictOpenModule(void)
{
    D_004E2BE0 = 1;
    _waveDictList.link.next = &_waveDictList.link;
    _waveDictList.link.prev = &_waveDictList.link;
    if (RwaFreeListCreate(sizeof(RwaWaveDict), 8, 0x40, 0, &D_01E810E0, 0x40808)) {
        RwaWaveDict *d = _rwaWaveDictCreate(&D_01E810B0);
        _currentDict = d;
        _workingDict = d;
        if (d) {
            _rwaUniqueIDAssignUUID(d, &rwaWAVEDICT_WORK);
            _rwaUniqueIDAssignName(d, D_004C19B0);
            return 1;
        }
        RwaFreeListDestroy(&D_01E810E0);
    }
    _workingDict = 0;
    _currentDict = 0;
    D_004E2BE0 = 0;
    return 0;
}
