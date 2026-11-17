// Logitech's lgcodec EE library (the USB headset's voice codecs), track lg (boot2). Written from the retail asm (decomp
// assembly/asm/lg/lgcodec.s); only what the game's start-up enters so far: the headset's IOP RPC service (0x50494C42)
// has no server on the platform (platform_sif.cpp), so the boot uses the EE-side tables only. Tags: [C] the asm, [L],
// [G].

// [C] lgcodec.s: the codec descriptors, 20 bytes each, six of them (lgCodecEnumerate's bound 5)
extern unsigned char g_codecs[];

// [C] 0x2C3080 (13 instructions): &g_codecs[index] for 0 <= index <= 5, else 0 (null). CGtLogitechHeadset::
// GetHeadsetCodecDesc enumerates through it from CB3NetworkVoIPManager::Construct.
// 0x002C3080
extern "C" void *lgCodecEnumerate(int index)
{
    if (index < 0 || index > 5)
        return 0;
    return g_codecs + index * 20;
}
