// rwanullwave (librwacore.a) -- the NULL wave type (no audio data): registration. Retail 0x2A0690-0x2A0844 [C E map].
// The four callbacks _RwaNULLWave{Download,Upload,Destroy,Create} (0x2A0690-0x2A07B4): the wave's data is a plain EE
// buffer (RwaWave::data = a pointer here, not a sound-RAM handle).
#include "rwa/rwacore.h"

extern char D_01E7D4D0[];     // the definition (static storage)
extern RwaUUID rwaNULLWAVEID;
extern "C" RwaWaveDef *RwaWaveDefCreate(void *p0, void *storage);
extern "C" RwaWaveDef *RwaWaveDefAssignID(RwaWaveDef *def, RwaUUID *uuid, const RwChar *name);
extern "C" RwaWave *_RwaNULLWaveCreate(RwaWave *wave, void *params);
extern "C" void _RwaNULLWaveDestroy(RwaWave *wave);
extern "C" RwaWave *_RwaNULLWaveUpload(RwaWave *wave, RwUInt32 offset, const void *src, RwUInt32 size);
extern "C" RwaWave *_RwaNULLWaveDownload(RwaWave *wave, RwUInt32 *info, RwUInt32 offset, void *dst);
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);

// 0x002A07C0
extern "C" RwaWaveDef *_rwaNULLWaveRegister(void)
{
    RwaWaveDef *def = RwaWaveDefCreate(0, D_01E7D4D0);
    if (!def)
        return 0;
    RwaWaveDefAssignID(def, &rwaNULLWAVEID, 0);
    def->field_C = _RwaNULLWaveCreate;
    def->field_10 = _RwaNULLWaveDestroy;
    def->field_14 = (RwaWave * (*)(RwaWave *)) _RwaNULLWaveUpload;
    def->field_18 = (RwaWave * (*)(RwaWave *)) _RwaNULLWaveDownload;
    return def;
}

// Download: copies info[2] bytes (info = a request {?, ?, size}) from the data at `offset` to `dst`. [L names]
// 0x002A0690
extern "C" RwaWave *_RwaNULLWaveDownload(RwaWave *wave, RwUInt32 *info, RwUInt32 offset, void *dst)
{
    RwUInt32 size = info[2];
    memcpy(dst, (void *)(wave->data + offset), size);
    return wave;
}

// Upload: copies `size` bytes from `src` to the data at `offset`.
// 0x002A06D0
extern "C" RwaWave *_RwaNULLWaveUpload(RwaWave *wave, RwUInt32 offset, const void *src, RwUInt32 size)
{
    memcpy((void *)(wave->data + offset), src, size);
    return wave;
}

// Frees the data buffer, if any.
// 0x002A0710
extern "C" void _RwaNULLWaveDestroy(RwaWave *wave)
{
    if (wave->data)
        func_00293440((void *)wave->data);
}

// Create: state cleared, a data buffer of the played format's length (read before the format is completed), and the
// played format taken from the data's when it has no type.
// 0x002A0740
extern "C" RwaWave *_RwaNULLWaveCreate(RwaWave *wave, void *params)
{
    wave->state = 0;
    wave->data = (RwUInt32)func_002934C0(wave->targetFormat.length, 0x30807);
    if (!wave->data)
        return 0;
    if (!wave->targetFormat.dataType)
        memcpy(&wave->targetFormat, &wave->format, 0x1C);
    return wave;
}
