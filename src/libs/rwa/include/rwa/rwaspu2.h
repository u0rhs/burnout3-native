// RWA SPU2 voices (librwaspu2.a rwaspu2voice.o, rwaspu2voiceint.o): the types both objects use. Type and member names
// [L Revenge stabs (build/audio1/revenge_rwa_types.h); offsets checked against retail's accesses at the cited
// addresses]; where Revenge's offsets differ (RwaSPU2OutputState) the fields stay placeholders.
#ifndef RWA_RWASPU2_H
#define RWA_RWASPU2_H

#include "rwa/rwacore.h"

// RwaSPU2VoiceStateDMA [L], 0x20 bytes: one hardware voice's registers as the EE hands them to the IOP side.
struct RwaSPU2VoiceStateDMA {
    RwInt32 PlayState;            // 0x00 1 key off (a wave change or play = 0 writes it), 2 key on requested (play = 1
                                  //      [C func_002B0A80]), 4 playing (2 -> 4 when the snapshot is sent), 3 idle /
                                  //      finished (the IOP's reply says 1), 5 idle on a fixed (attached) voice
                                  //      [C _RwaSPU2OutputFinishExecute 0x2B31BC-0x2B3238; func_002B1C10 0x2B1D84]
    RwUInt32 StartAddress;             // 0x04 start address in sound RAM [C 0x2B1084]
    RwUInt32 LoopStartAddress;             // 0x08 loop start address (relative to StartAddress until update flag 0x200 adds it) [C 0x2B1308]
    RwInt32 Frequency;              // 0x0C pitch: frequency * frequency scale, Hz [C 0x2B1330]
    RwUInt32 Volumes;            // 0x10 volumes: left << 16 | right, 0-0x7FFF each [C _RwaSPU2VoiceSetLRVol 0x2B0EAC]
    RwUInt32 CurrentPosition;            // 0x14 current position [L]
    RwUInt32 TriggerPosition;            // 0x18 trigger (start) position [L]
    RwUInt32 Flags;            // 0x1C flags: 1 = loop [C _rwaSPU2VoiceSetLoop], 2 / 4 = phase invert (Dolby rear
                                  //      steering) [C 0x2B1990-0x2B1A7C]
};

// RwaConnection [L], 0xC bytes [C 0x2B1504 stride 12].
struct RwaConnection {
    RwUInt32 inputSocketNumber;             // 0x0 input socket [L]
    RwUInt32 outputSocketNumber;             // 0x4 output socket: 1 FL, 2 FR, 3 RL, 4 RR, 5 centre, 7 reverb [C jtbl_004C1B50]
    RwUInt32 outputHardwareIndex;             // 0x8 output hardware index [L]
};
// RwaPatchbay [L].
struct RwaPatchbay {
    RwUInt32 numInputs;             // 0x00 [L stabs numInputs]
    RwUInt32 numConnections;             // 0x04 retail runs the gain loop over 0..numConnections inclusive [C 0x2B1698]
    RwUInt32 maxNumConnections;             // 0x08 [L stabs maxNumConnections]
    RwaConnection *connections;       // 0x0C connections
    RwBool overridePositional;              // 0x10 override positional: the summed gains replace the listener's pan [C 0x2B16A4]
};
// RwaGainArray [L], 8 bytes.
struct RwaGainArray {
    RwaPatchbay *patchbay;         // 0x0 the patchbay
    RwReal *gainValues;              // 0x4 one gain per connection
};

// RwaVoicePool [L], 8 bytes: the SPU2 voice interface's pool of hardware voices [C func_002B2370].
struct RwaVoicePool {
    RwInt32 numVoices;            // 0x0
    RwaObjHandle **voices;        // 0x4 handles of SPU2 voice objects
};

// RwaSPU2VoiceCreationParams [L], 8 bytes [C func_002B1C10 0x2B1C28 / 0x2B1C50].
struct RwaSPU2VoiceCreationParams {
    RwUInt32 mode;                // 0x0 -> RwaSPU2VoiceState::mode
    RwUInt32 voice;               // 0x4 the hardware voice to attach (mode 0x40)
};

// RwaSPU2VoiceState [L], 0x94 bytes [C _RwaSPU2VoiceRegisterCommon 0x2B1E90: state size 0x94, alignment 0x30 -> 16].
struct RwaSPU2VoiceState {
    RwaSPU2VoiceStateDMA *StateDMA; // 0x00 its hardware voice's registers [L stabs StateDMA]
    RwaObj *Output;              // 0x04 the output object [L stabs Output]
    RwaWave *Wave;             // 0x08 the wave [L stabs Wave]
    RwaSPU2VoiceStateDMA *DolbyPartner; // 0x0C the Dolby partner voice's registers, or null [L stabs DolbyPartner]
    RwaGainArray *gainArray;       // 0x10 gain array, or null [L stabs gainArray]
    RwUInt32 DolbyDesired;            // 0x14 Dolby mode wanted: creation flags & 0x38 [L stabs DolbyDesired]
    RwUInt32 DolbyState;            // 0x18 Dolby mode in use: 0, 0x10 (single voice) or 0x20 (voice pair)
                                  //      [L stabs DolbyState; C 0x2B1108]
    RwReal VolL;              // 0x1C left gain [L stabs VolL]
    RwReal VolR;              // 0x20 right gain [L stabs VolR]
    RwBool Reserved;              // 0x24 reserved from the free-voice pool until the next finish [L stabs Reserved]
    RwUInt32 mode;            // 0x28 creation flags: 4 = Doppler (frequency scale from the listener), 0x40 =
                                  //      attach a fixed hardware voice, 0x80 = 3D [C func_002B1C10, 0x2B16DC]
    RwUInt16 updateFlags;            // 0x2C pending updates [L stabs updateFlags]: 1 frequency, 2 / 4 volume / pan,
                                  //      0x40 wave, 0x80 keep trigger, 0x100 reverb send, 0x200 loop start relative
    RwUInt8 voiceNum;             // 0x2E hardware voice number (bit 0 = SPU2 core) [L stabs voiceNum]
    unsigned char pad_2F[0x1];
    RwUInt32 voiceBit;            // 0x30 the voice's bit in its core's masks [L stabs voiceBit]
    unsigned char pad1[0x4];
    RwV3d Position3D;               // 0x38 position [L stabs Position3D]
    RwV3d Velocity3D;               // 0x44 velocity
    RwV3d ListenerRelativePosition3D;               // 0x50 listener-relative position
    RwV3d ListenerRelativeVelocity3D;               // 0x5C listener-relative velocity
    RwReal Frequency;              // 0x68 frequency, Hz
    RwReal reverbSendGain;              // 0x6C reverb send gain (> 0 enables the voice's reverb bit)
    RwReal Volume;              // 0x70 volume
    RwReal Pan;              // 0x74 pan, 0 (left) - 1 (right) [C _rwaSPU2VoiceSetPan: pan * 0.5 + 0.5]
    RwReal FrequencyScale3D;              // 0x78 frequency scale (Doppler)
    RwReal VolumeScale3D;              // 0x7C volume scale (distance)
    RwReal Pan3D;              // 0x80 3D pan, 0-1
    RwReal MinDistance3D;              // 0x84 min distance
    RwReal MaxDistance3D;              // 0x88 max distance
    RwBool Is3D;              // 0x8C 3D [L stabs Is3D]
    RwReal OldYPan;              // 0x90 last front/back pan [L stabs OldYPan]
};

// RwaSPU2Voice [L], 0xC4 bytes: the object, then the state at 0x30 (16-aligned) [C _rwaSPU2VoiceSetPan 0x2B0F34].
struct RwaSPU2Voice {
    RwaObj obj;                   // 0x00 (obj.state -> state)
    unsigned char pad_2C[0x4];
    RwaSPU2VoiceState state;      // 0x30
};

// RwaOutputSPU2DMAStateTag / RwaSPU2DMAState [L Revenge stabs], 0x680 bytes: the snapshot of all hardware voices and
// mixer settings the EE sends to the IOP every update (opcode 0x14) [C _RwaSPU2OutputFinishExecute 0x2B3158-0x2B319C;
// every offset below is used at the cited addresses].
struct RwaSPU2DMAState {
    RwaSPU2VoiceStateDMA VoiceState[48];   // 0x000 hardware voices 0-23 core 0, 24-47 core 1 [C 0x2B2B28]
    RwUInt8 UsedVoices[48];                // 0x600 1 = allocated [C _RwaSPU2OutputAllocateHWVoice 0x2B2CD8]
    RwUInt32 outputMixDryLeft[2];          // 0x630 per core, one bit per voice (voiceBit) [C 0x2B2D20]
    RwUInt32 outputMixDryRight[2];         // 0x638
    RwUInt32 outputMixWetLeft[2];          // 0x640 reverb send (RwaSPU2VoiceState::reverbSendGain > 0) [C 0x2B118C]
    RwUInt32 outputMixWetRight[2];         // 0x648
    RwUInt32 voiceSwitchMask[2];           // 0x650 set on allocation, cleared on free [C 0x2B2DB0, 0x2B28D8]
    RwInt32 ReverbType;                    // 0x658 0-10 from the environment [C _rwaSPU2OutputSetEnvironment]
    RwInt16 ReverbGainLeft;                // 0x65C 0x7FFF * 10^(room / 1000)
    RwInt16 ReverbGainRight;               // 0x65E = left
    RwInt32 Gain;                          // 0x660 master gain, 0xFFFF = 1 [C 0x2B33EC; func_002B2490 / 65535]
    RwInt32 SpeakerConfig;                 // 0x664 1 at creation [C 0x2B33E8]
    RwInt32 Delay;                         // 0x668 reverb delay, 0-127
    RwInt32 Feedback;                      // 0x66C reverb feedback, 0-127
    RwInt32 PacketID;                      // 0x670 the update counter of the snapshot [C 0x2B3178]
    unsigned char pad[12];                 // 0x674
};

// RwaEnvironment [L Revenge stabs], 0x30 bytes: an I3DL2-style reverb description [C _rwaSPU2OutputSetEnvironment
// 0x2B2E80-0x2B30D4 reads +0x0, 0xC, 0x10, 0x14, 0x18, 0x20, 0x24; default D_0049FE30 = room -10000, roomHF -10000,
// decay 1.49, decayHF 0.83, reflections -2602, reflectionsDelay 0.007, reverb 200, reverbDelay 0.011, diffusion 100,
// density 100, hfReference 5000].
struct RwaEnvironment {
    RwInt32 room;                 // 0x00 mB
    RwInt32 roomHF;               // 0x04
    RwReal roomRolloffFactor;     // 0x08
    RwReal decayTime;             // 0x0C s
    RwReal decayHFRatio;          // 0x10
    RwInt32 reflections;          // 0x14 mB
    RwReal reflectionsDelay;      // 0x18 s
    RwInt32 reverb;               // 0x1C mB
    RwReal reverbDelay;           // 0x20 s
    RwReal diffusion;             // 0x24 %
    RwReal density;               // 0x28 %
    RwReal hfReference;           // 0x2C Hz
};

// RwaOutputMixerData [L Revenge stabs], 8 bytes: what a mixer / IRQ callback returns; bus 0 is the buffer sent to the
// IOP [C RwaSPU2OutputTickUpdate 0x2B3A88].
struct RwaOutputMixerData {
    RwUInt32 numBusses;           // 0x0
    void **busses;                // 0x4
};
typedef RwaOutputMixerData *(*RwaOutputCallback)(RwUInt32 samples, RwUInt32 data);

// RwaSPU2OutputState [L Revenge stabs], 0x13DC bytes in B3 (registered state size 0x1400): the output object's state
// (obj + 0x40). Up to MixerBufferMem the offsets are Revenge's [C 0x2B33C4-0x2B33E8, 0x2B2614-0x2B2638]; B3 has 8
// bytes less after IRQCallbackData (no mnCore1ReverbBuffer* pair: DMAStateIOPAddr 0x13A4 [C 0x2B3184], Environment
// 0x13A8 [C 0x2B2EA0], Listener 0x13D8 [C func_002B2470]).
struct RwaSPU2OutputState {
    RwaSPU2DMAState DMAState;          // 0x0000 the EE's working copy (voices write their StateDMA here)
    RwaSPU2DMAState DMAStateToIOP;     // 0x0680 the snapshot being sent
    RwaSPU2DMAState DMAStateReply;     // 0x0D00 written by the IOP (read uncached): play states, positions, PacketID
    RwBool Initialized;                // 0x1380
    RwaOutputCallback MixerCallback;   // 0x1384
    RwUInt32 MixerCallbackData;        // 0x1388
    RwUInt32 MixerBufferLength;        // 0x138C
    RwInt16 *MixerBuffer;              // 0x1390 64-aligned in MixerBufferMem
    RwInt16 *MixerBufferMem;           // 0x1394
    RwaOutputCallback IRQCallback;     // 0x1398
    RwUInt32 IRQCallbackData;          // 0x139C
    RwUInt32 NumVoices;                // 0x13A0 hardware voices in use (creation maxVoices)
    RwUInt32 DMAStateIOPAddr;          // 0x13A4 the IOP's copy of the snapshot (reply to opcode 0x15)
    RwaEnvironment Environment;        // 0x13A8
    void *Listener;                    // 0x13D8
};
// RwaSPU2Output [L]: the object, then the state at 0x40.
struct RwaSPU2Output {
    RwaObj obj;                        // 0x00
    unsigned char pad_2C[0x14];
    RwaSPU2OutputState state;          // 0x40
};
// RwaSPU2OutputCreationParams [L Revenge stabs]: B3 reads the first three [C _RwaSPU2OutputCreate 0x2B33E0-0x2B343C].
struct RwaSPU2OutputCreationParams {
    RwUInt32 maxVoices;                // 0x00
    RwBool reverbEnableCore0;          // 0x04
    RwBool reverbEnableCore1;          // 0x08
};

// rwaallocbitmap.o's allocation bitmap (no type name in the stabs), 0xC bytes [C _rwaAllocBitmapCreate 0x2B484C-0x2B4870], from the free list D_01E835F0 unless
// the caller gives one.
struct UnkStruct_002B4760 {
    RwUInt8 *field_0;      // 0x0 the bits (bit j of byte i = unit 8i+j)
    RwUInt32 field_4;      // 0x4 bytes of bits
    RwUInt8 field_8;       // 0x8 handle shift: floor(log2(units rounded up to 8)) [C 0x2B4764-0x2B4830]
    RwUInt8 field_9;       // 0x9 flags: 1 = struct from the free list, 2 = bits allocated here [C 0x2B488C / 0x2B48A4]
    RwUInt8 pad_A[2];
};
// The memory bitmap, 0x18 bytes: an allocation bitmap plus the memory it maps [C _rwaMemoryBitmapCreate 0x2B43E4],
// from the free list D_01E835C0 unless the caller gives one. Its field_9 bit 0 means "from D_01E835C0" [C 0x2B43DC].
struct UnkStruct_002B4370 {
    UnkStruct_002B4760 field_0;
    RwUInt32 field_C;      // 0xC base address
    RwUInt32 field_10;     // 0x10 size in bytes
    RwUInt32 field_14;     // 0x14 unit size in bytes
};

#endif
