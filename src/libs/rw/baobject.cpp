// RenderWare 3.6 core: objects with frames (RW 3.1 file name: src/baobject.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot2 (track rw).

// [C] a two-pointer list link ([L] RwLLLink: next at +0, prev at +4).
struct UnkStruct_objLink {
    UnkStruct_objLink *field_0;        // [C] next
    UnkStruct_objLink *field_4;        // [C] previous
};

// [C] a frame, as far as the object list is concerned: the list head sits at +0x90 ([L] RwFrame.objectList).
struct UnkStruct_objFrame {
    unsigned char pad_0[0x90];
    UnkStruct_objLink field_90;        // [C] the list of the frame's objects
};

// [C] the part of an object that has a frame ([L] RwObjectHasFrame): +4 the frame it is attached to, +8 its link in
// that frame's object list (next at +8, previous at +0xC).
struct UnkStruct_RwObjectHasFrame {
    unsigned char pad_0[0x4];
    UnkStruct_objFrame *field_4;       // [C] the frame (0 = none) ([L] object.parent)
    UnkStruct_objLink field_8;         // [C] the link in the frame's object list ([L] lFrame)
};

extern "C" void *RwFrameUpdateObjects(void *frame);

// [C] when the object already has a frame (+4) its link is removed from that frame's list (prev->next = next, then
// next->prev = prev, both re-read from the object). The frame (a1) is stored at +4; when not NULL the link is inserted
// at the head of the frame's list (frame + 0x90): link.prev = &frame->list, link.next = list.next, list.next->prev =
// link, list.next = link; then RwFrameUpdateObjects(frame). No defined result.
// 0x001EB1E8
extern "C" void _rwObjectHasFrameSetFrame(UnkStruct_RwObjectHasFrame *object, UnkStruct_objFrame *frame)
{
    UnkStruct_objLink *first;

    if (object->field_4 != 0) {
        object->field_8.field_4->field_0 = object->field_8.field_0;
        object->field_8.field_0->field_4 = object->field_8.field_4;
    }
    object->field_4 = frame;
    if (frame != 0) {
        first = frame->field_90.field_0;
        object->field_8.field_4 = &frame->field_90;
        object->field_8.field_0 = first;
        frame->field_90.field_0->field_4 = &object->field_8;
        frame->field_90.field_0 = &object->field_8;
        RwFrameUpdateObjects(frame);
    }
}

// fe1 (2026-11-10: the game's shutdown, RwCameraDestroy). [C] when the object has a frame (+4) its link is removed from
// that frame's object list (prev->next = next, then next->prev = prev, both re-read from the object); the frame
// pointer itself is left. No defined result.
// 0x001EB258
extern "C" void _rwObjectHasFrameReleaseFrame(UnkStruct_RwObjectHasFrame *object)
{
    if (object->field_4 != 0) {
        object->field_8.field_4->field_0 = object->field_8.field_0;
        object->field_8.field_0->field_4 = object->field_8.field_4;
    }
}
