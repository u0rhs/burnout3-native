// rwalistenermana.o (librwacore.a) -- the listener manager: which virtual listener a voice is heard by (mode 1 the
// output's own listener, 2 a fixed one, 3 the nearest), then the listener's attribute calculation. Retail
// 0x2AE3C0-0x2AEA44 [C E map]. C port written from retail's code (audio1); every function is proven by the checker on
// live captures. _RwaListenerManagerTransformVoice (0x2AE3C0) is not ported.
#include "rwa/rwacore.h"
#include "rwa/rwalistener.h"
#include "rwa/rwavirtualvoice.h"

typedef RwReal (*RwaListenerManagerNearestFunc)(void *voice, RwaVirtualListener *vl);
extern RwUInt32 _listenerManagerMode;
extern RwaVirtualListener *_fixedModeListener;
extern RwaListenerManagerNearestFunc _nearestModeCallback;   // added to each squared distance, if set
extern RwLLLink _virtualListeners;
extern RwUInt32 D_004E2BF8;                // _moduleOpen of rwalistenermana [L name, retail_data_names.csv]
extern RwReal D_004E073C;                  // FLT_MAX [C value 0x7F7FFFFF]
extern "C" void _RwaListenerManagerTransformVoice(UnkStruct_002AE520_in *in, RwaVirtualListener *vl, void *listener);
extern "C" void _rwaListenerCalculateVoiceAttributes(void *listener, UnkStruct_002AE520_in *in,
                                                        UnkStruct_002AE520_out *out, RwUInt32 p3, RwUInt32 p4);
extern "C" RwaObjDef *RwaVirtualListenerRegisterUsingInterface(void);
extern "C" RwaObjDef *RwaVirtualListenerUnregister(void);

// Mode 2 / 3 first move the voice into the chosen virtual listener's frame; then the listener's calculation. No result:
// retail's v0 is whatever _rwaListenerCalculateVoiceAttributes (0x298420, which sets no v0 on any path) and its
// callees left, i.e. this function's own caller's v0, the mode constant or a callee's pointer; neither caller reads it
// [C 0x2B14E0 overwrites v0; _RwaVoiceManagerReInsertVoice 0x295214 returns without reading it] (shadow probe of
// 0x2AE520 alone: 8,119 v0 "mismatches" of that stale word).
// 0x002AE520
extern "C" void _rwaListenerManagerCalculateVoiceAttributes(void *listener, void *voice, UnkStruct_002AE520_in *in,
                                                               UnkStruct_002AE520_out *out, RwUInt32 p4, RwUInt32 p5)
{
    if (_listenerManagerMode == 3) {
        RwaVirtualListener *best = 0;
        RwReal bestD = D_004E073C;
        RwLLLink *node = _virtualListeners.next;
        while (node != &_virtualListeners) {
            RwaVirtualListener *vl = (RwaVirtualListener *)((char *)node - 0x40);
            RwaVirtualListenerState *s = &vl->state;
            RwReal dy = s->Position.y - in->field_0.y;
            RwReal dx = s->Position.x - in->field_0.x;
            RwReal dz = s->Position.z - in->field_0.z;
            RwReal d = dy * dy + dx * dx + dz * dz;
            if (_nearestModeCallback)
                d = d + _nearestModeCallback(voice, vl);
            if (d < bestD) {
                best = vl;
                bestD = d;
            }
            node = node->next;
        }
        _RwaListenerManagerTransformVoice(in, best, listener);
    } else if (_listenerManagerMode == 2) {
        _RwaListenerManagerTransformVoice(in, _fixedModeListener, listener);
    }
    _rwaListenerCalculateVoiceAttributes(listener, in, out, p4, p5);
}

// Only a registered virtual listener becomes the fixed one.
// 0x002AE690
extern "C" void RwaListenerManagerSetFixedModeListener(RwaVirtualListener *vl)
{
    RwLLLink *node = _virtualListeners.next;
    while (node != &_virtualListeners) {
        if (node == &vl->state.link) {
            _fixedModeListener = vl;
            return;
        }
        node = node->next;
    }
}

// 0x002AE6E0
extern "C" void RwaListenerManagerSetMode(RwUInt32 mode)
{
    _listenerManagerMode = mode;
}

// 0x002AE6F0
extern "C" void RwaListenerManagerRegisterVirtualListener(RwaVirtualListener *vl)
{
    vl->state.link.prev = 0;
    vl->state.link.next = 0;
    vl->state.link.next = _virtualListeners.next;
    vl->state.link.prev = &_virtualListeners;
    _virtualListeners.next->prev = &vl->state.link;
    _virtualListeners.next = &vl->state.link;
}

// 0x002AE720
extern "C" void _rwaListenerManagerCloseModule(void)
{
    RwaVirtualListenerUnregister();
    D_004E2BF8 = 0;
}

// 0x002AE740
extern "C" RwBool _rwaListenerManagerOpenModule(void)
{
    D_004E2BF8 = 1;
    _virtualListeners.next = &_virtualListeners;
    _virtualListeners.prev = &_virtualListeners;
    RwaVirtualListenerRegisterUsingInterface();
    return 1;
}

extern "C" RwReal RwV3dNormalize(RwV3d *out, const RwV3d *in);

// The voice's position and velocity into the virtual listener's frame: relative to its position / velocity, then
// onto its normalised Right, Up and At axes (each dot product summed y, x, z as retail accumulates) [C 0x2AE3C0-
// 0x2AE500]. `listener` is not used.
// 0x002AE3C0
extern "C" void _RwaListenerManagerTransformVoice(UnkStruct_002AE520_in *in, RwaVirtualListener *vl, void *listener)
{
    RwV3d right, up, at;
    RwV3dNormalize(&right, &vl->state.Right);
    RwV3dNormalize(&up, &vl->state.Up);
    RwV3dNormalize(&at, &vl->state.At);
    RwReal dz = in->field_0.z - vl->state.Position.z;
    RwReal dy = in->field_0.y - vl->state.Position.y;
    RwReal dx = in->field_0.x - vl->state.Position.x;
    in->field_0.x = right.y * dy + right.x * dx + right.z * dz;
    in->field_0.y = up.y * dy + up.x * dx + up.z * dz;
    in->field_0.z = at.y * dy + at.x * dx + at.z * dz;
    dy = in->field_C.y - vl->state.Velocity.y;
    dx = in->field_C.x - vl->state.Velocity.x;
    dz = in->field_C.z - vl->state.Velocity.z;
    in->field_C.x = right.y * dy + right.x * dx + right.z * dz;
    in->field_C.y = up.y * dy + up.x * dx + up.z * dz;
    in->field_C.z = at.y * dy + at.x * dx + at.z * dz;
}

// ---- the virtual listener's parameter accessors (obj, parameter index, value): each returns obj [C 0x2AE770-0x2AEA20].
// The state (obj + 0x40) holds the manager link, Position (+0x48), Velocity (+0x54), Up (+0x60), At (+0x6C), Right
// (+0x78) and the flags (+0x84: 1 Position, 2 Velocity, 4 At, 8 Up).

// Getter: Right.
// 0x002AE770
extern "C" RwaVirtualListener *func_002AE770(RwaVirtualListener *vl, RwUInt32 param, RwV3d *out)
{
    RwReal x = vl->state.Right.x, y = vl->state.Right.y, z = vl->state.Right.z;
    out->x = x;
    out->y = y;
    out->z = z;
    return vl;
}

// Getter: At.
// 0x002AE790
extern "C" RwaVirtualListener *func_002AE790(RwaVirtualListener *vl, RwUInt32 param, RwV3d *out)
{
    RwReal x = vl->state.At.x, y = vl->state.At.y, z = vl->state.At.z;
    out->x = x;
    out->y = y;
    out->z = z;
    return vl;
}

// Setter: At; Right is recomputed as Up x At (each component a product minus a product, retail's accumulator order);
// flag 4.
// 0x002AE7B0
extern "C" RwaVirtualListener *func_002AE7B0(RwaVirtualListener *vl, RwUInt32 param, RwV3d *in)
{
    RwaVirtualListenerState *st = &vl->state;
    RwReal x = in->x, y = in->y, z = in->z;
    st->At.x = x;
    st->At.y = y;
    st->At.z = z;
    st->Right.x = st->Up.y * st->At.z - st->Up.z * st->At.y;
    st->Right.y = st->Up.z * st->At.x - st->Up.x * st->At.z;
    st->Right.z = st->Up.x * st->At.y - st->Up.y * st->At.x;
    st->flags = st->flags | 4;
    return vl;
}

// Getter: Up.
// 0x002AE830
extern "C" RwaVirtualListener *func_002AE830(RwaVirtualListener *vl, RwUInt32 param, RwV3d *out)
{
    RwReal x = vl->state.Up.x, y = vl->state.Up.y, z = vl->state.Up.z;
    out->x = x;
    out->y = y;
    out->z = z;
    return vl;
}

// Setter: Up; Right recomputed as above; flag 8.
// 0x002AE850
extern "C" RwaVirtualListener *func_002AE850(RwaVirtualListener *vl, RwUInt32 param, RwV3d *in)
{
    RwaVirtualListenerState *st = &vl->state;
    RwReal x = in->x, y = in->y, z = in->z;
    st->Up.x = x;
    st->Up.y = y;
    st->Up.z = z;
    st->Right.x = st->Up.y * st->At.z - st->Up.z * st->At.y;
    st->Right.y = st->Up.z * st->At.x - st->Up.x * st->At.z;
    st->Right.z = st->Up.x * st->At.y - st->Up.y * st->At.x;
    st->flags = st->flags | 8;
    return vl;
}

// Getter: the orientation, out = {Right at +0, Up at +0x10, At at +0x20} (three 16-byte vectors).
// 0x002AE8D0
extern "C" RwaVirtualListener *func_002AE8D0(RwaVirtualListener *vl, RwUInt32 param, RwReal *out)
{
    RwaVirtualListenerState *st = &vl->state;
    RwReal x = st->Up.x, y = st->Up.y, z = st->Up.z;
    out[0x10 / 4] = x;
    out[0x14 / 4] = y;
    out[0x18 / 4] = z;
    x = st->At.x;
    y = st->At.y;
    z = st->At.z;
    out[0x20 / 4] = x;
    out[0x24 / 4] = y;
    out[0x28 / 4] = z;
    x = st->Right.x;
    y = st->Right.y;
    z = st->Right.z;
    out[0] = x;
    out[1] = y;
    out[2] = z;
    return vl;
}

// Setter: the orientation as given (Up, At and Right all taken from the three vectors, nothing recomputed); flags 4 | 8.
// 0x002AE920
extern "C" RwaVirtualListener *func_002AE920(RwaVirtualListener *vl, RwUInt32 param, RwReal *in)
{
    RwaVirtualListenerState *st = &vl->state;
    RwReal x = in[0x10 / 4], y = in[0x14 / 4], z = in[0x18 / 4];
    st->Up.x = x;
    st->Up.y = y;
    st->Up.z = z;
    x = in[0x20 / 4];
    y = in[0x24 / 4];
    z = in[0x28 / 4];
    st->At.x = x;
    st->At.y = y;
    st->At.z = z;
    x = in[0];
    y = in[1];
    z = in[2];
    st->Right.x = x;
    st->Right.y = y;
    st->Right.z = z;
    st->flags = st->flags | 0xC;
    return vl;
}

// Getter: Velocity.
// 0x002AE980
extern "C" RwaVirtualListener *func_002AE980(RwaVirtualListener *vl, RwUInt32 param, RwV3d *out)
{
    RwReal x = vl->state.Velocity.x, y = vl->state.Velocity.y, z = vl->state.Velocity.z;
    out->x = x;
    out->y = y;
    out->z = z;
    return vl;
}

// Setter: Velocity; flag 2.
// 0x002AE9A0
extern "C" RwaVirtualListener *func_002AE9A0(RwaVirtualListener *vl, RwUInt32 param, RwV3d *in)
{
    RwReal x = in->x, y = in->y, z = in->z;
    vl->state.Velocity.x = x;
    vl->state.Velocity.y = y;
    vl->state.Velocity.z = z;
    vl->state.flags = vl->state.flags | 2;
    return vl;
}

// Getter: Position.
// 0x002AE9D0
extern "C" RwaVirtualListener *func_002AE9D0(RwaVirtualListener *vl, RwUInt32 param, RwV3d *out)
{
    RwReal x = vl->state.Position.x, y = vl->state.Position.y, z = vl->state.Position.z;
    out->x = x;
    out->y = y;
    out->z = z;
    return vl;
}

// Setter: Position; flag 1.
// 0x002AE9F0
extern "C" RwaVirtualListener *func_002AE9F0(RwaVirtualListener *vl, RwUInt32 param, RwV3d *in)
{
    RwReal x = in->x, y = in->y, z = in->z;
    vl->state.Position.x = x;
    vl->state.Position.y = y;
    vl->state.Position.z = z;
    vl->state.flags = vl->state.flags | 1;
    return vl;
}

// Takes the virtual listener off _virtualListeners (its link at +0x40): only when the link is linked (next non-null).
// 0x002AEA20
extern "C" void func_002AEA20(RwaVirtualListener *vl)
{
    RwLLLink *l = &vl->state.link;
    if (l->next) {
        l->prev->next = l->next;
        l->next->prev = l->prev;
    }
}
