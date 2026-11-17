// rwalistener.o (librwacore.a) -- the listener interface definition and the listener's voice attributes. Retail
// 0x298070-0x2988D8 [C E map]. C port
// written from retail's code (audio1); every function is proven by the checker on live captures. The parameter
// accessors 0x2980E0-0x2983F0 are not ported (never captured).
#include "rwa/rwacore.h"
#include "rwa/rwalistener.h"
#include "rwa/rwavirtualvoice.h"

extern RwaObjInterfaceDef D_01E7D210;      // the listener interface definition (static struct)
extern RwaUUID rwaLISTENERINTERFACEID;
extern RwaObjInterfaceDefParam _rwaListenerIntInputs[8];
extern RwaObjInterfaceDefParam _rwaListenerIntOutputs[9];
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, RwaUUID *id, const char *name);
extern "C" void *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *inputs, RwUInt32 nInputs,
                                         RwaObjInterfaceDefParam *outputs, RwUInt32 nOutputs);

// 0x00298070
extern "C" RwaObjInterfaceDef *RwaListenerInterfaceRegister(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E7D210))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E7D210, &rwaLISTENERINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E7D210, _rwaListenerIntInputs, 8, _rwaListenerIntOutputs, 9);
    return &D_01E7D210;
}

extern RwReal D_004E0740;   // 2 pi [C value 0x40C90FDB]
extern "C" RwReal RwaV3dLength(RwReal *out, RwV3d *v);
extern "C" RwReal RwV3dNormalize(RwV3d *out, const RwV3d *in);
extern "C" float func_0011E328(float x);   // acosf [C research/E_dnat_symbols.csv: LIBM.A wf_acos.o; the decomp names it
                                            // func_0011E328, so decomp2host makes it a guest call (libm override)]

// x limited to [0, 1] as retail tests it (NaN gives 1).
static RwReal func_00298420_inl1(RwReal x)
{
    if (x < 0.0f)
        return 0.0f;
    RwReal r = 1.0f;
    if (x <= 1.0f)
        r = x;
    return r;
}

// A voice's attributes as this listener hears it, from the voice's position, velocity and min / max distance (`in`).
// The results are out->field_0 (volume) and out->field_4[0..3] (frequency, angle, two pans), each written only when
// its flag is set:
// - flags 2, frequency (Doppler): 1 at distance 0; otherwise c / (c + v), with c = 340 * DistanceFactor and v the
//   relative speed along the line, times DopplerScale, limited to +-150.
// - flags 1, volume: 0 beyond the max distance, 1 inside the min one, else (range - d) / (range + d * RolloffFactor),
//   with d measured from the min distance.
// - flags 4: [2] = 0.5 - 0.5 (dir . Right) and [3] = 0.5 - 0.5 (dir . At), both limited to [0, 1]. [1] = the angle
//   acosf(dir . At), turned to 2 pi - angle when (dir x At) . Up > 0. Then pan mode 3 halves [2]'s distance from 0.5,
//   and mode 2 sets it to 0.5.
//   Retail quirk: the direction's y is replaced by At.y before it is normalised [C 0x298558, a store in a branch
//   delay slot]; a zero direction becomes At itself.
// No live capture covered this code, so it is unchecked against one.
// Each multiply-add follows retail's accumulator order.
// 0x00298420
extern "C" void _rwaListenerCalculateVoiceAttributes(RwaListener *listener, UnkStruct_002AE520_in *in,
                                                        UnkStruct_002AE520_out *out, RwUInt32 panMode, RwUInt32 flags)
{
    RwaListenerState *st = &listener->state;
    RwV3d d;
    RwReal len;
    d.x = in->field_0.x - st->Position.x;
    d.y = in->field_0.y - st->Position.y;
    d.z = in->field_0.z - st->Position.z;
    RwaV3dLength(&len, &d);
    if (flags & 2) {
        if (len == 0.0f) {
            out->field_4[0] = 1.0f;
        } else {
            RwV3d n;
            RwV3dNormalize(&n, &d);
            RwReal vy = in->field_C.y - st->Velocity.y;
            RwReal vx = in->field_C.x - st->Velocity.x;
            RwReal vz = in->field_C.z - st->Velocity.z;
            RwReal v = st->DopplerScale * (vy * n.y + vx * n.x + vz * n.z);
            if (!(v <= 150.0f))
                v = 150.0f;
            else if (v < -150.0f)
                v = -150.0f;
            RwReal c = 340.0f * st->DistanceFactor;
            out->field_4[0] = c / (c + v);
        }
    }
    if (flags & 1) {
        if (!(len <= in->field_1C)) {
            out->field_0 = 0.0f;
        } else if (len < in->field_18) {
            out->field_0 = 1.0f;
        } else {
            len = len - in->field_18;
            RwReal range = in->field_1C - in->field_18;
            RwReal acc = 0.0f + range;   // retail: adda.s with zero
            out->field_0 = (range - len) / (acc + len * st->RolloffFactor);
        }
    }
    if (!(flags & 4))
        return;
    d.y = st->At.y;
    if (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f) {
        d.x = st->At.x;
        d.y = st->At.y;
        d.z = st->At.z;
    } else {
        RwV3dNormalize(&d, &d);
    }
    out->field_4[2] = func_00298420_inl1(0.5f - 0.5f * (d.y * st->Right.y + d.x * st->Right.x + d.z * st->Right.z));
    out->field_4[3] = func_00298420_inl1(0.5f - 0.5f * (d.y * st->At.y + d.x * st->At.x + d.z * st->At.z));
    RwReal c = d.y * st->At.y + d.x * st->At.x + d.z * st->At.z;
    if (!(c <= 1.0f))
        c = 1.0f;
    if (c < -1.0f)
        c = -1.0f;
    out->field_4[1] = func_0011E328(c);
    RwReal cz = d.x * st->At.y - d.y * st->At.x;
    RwReal cx = d.y * st->At.z - d.z * st->At.y;
    RwReal cy = d.z * st->At.x - d.x * st->At.z;
    if (!(cy * st->Up.y + cx * st->Up.x + cz * st->Up.z <= 0.0f))
        out->field_4[1] = D_004E0740 - out->field_4[1];
    if (panMode == 3)
        out->field_4[2] = (out->field_4[2] - 0.5f) * 0.5f + 0.5f;
    else if (panMode == 2)
        out->field_4[2] = (out->field_4[2] - 0.5f) * 0.0f + 0.5f;
}
