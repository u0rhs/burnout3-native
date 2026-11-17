// oswave.o (librwacore.a) -- the platform's wave upload for a renderer. Retail 0x2A0F10-0x2A0F4C [C E map].
#include "rwa/rwacore.h"

extern "C" RwaWave *RwaWaveUpload(RwaWave *wave, RwUInt32 offset, RwUInt32 src, RwUInt32 size);
extern "C" RwaWave *_RwaSPU2WaveUploadFromIOP(RwaWave *wave, RwUInt32 offset, RwUInt32 src, RwUInt32 size);

// Flag 0x40: the wave's data is in EE memory (a normal upload); otherwise it comes from the IOP [L meaning].
// The other arguments (sound-RAM offset, source, bytes [C _RwaWaveRendererStateUploadNextBlock 0x2AC6F4-0x2AC70C])
// pass through (a tail call: a1-a3 unchanged).
// 0x002A0F10
extern "C" RwaWave *_RwaOsWaveUploadForRenderer(RwaWave *wave, RwUInt32 offset, RwUInt32 src, RwUInt32 size)
{
    if (wave->flags & 0x40)
        return RwaWaveUpload(wave, offset, src, size);
    return _RwaSPU2WaveUploadFromIOP(wave, offset, src, size);
}
