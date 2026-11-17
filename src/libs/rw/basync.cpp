// RenderWare 3.6 core: frame hierarchy synchronisation (RW 3.1 file name: src/basync.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot2 (track rw).

extern unsigned char ourGlobals[];     // [C] 0x00670BD0, the RW globals block (0x4000 bytes, symbol table)

extern "C" void *RwMatrixMultiply(void *dst, const void *a, const void *b);

// [C] a doubly linked list link / head (next, prev).
struct UnkStruct_syncLink {
    UnkStruct_syncLink *field_0;       // [C] next
    UnkStruct_syncLink *field_4;       // [C] prev
};

// [C] an object attached to a frame as the sync walks it: its link (+8) is what the frame's object list holds; +0x10 is
// the object's sync function, called with the object ([L] RwObjectHasFrame.sync; the camera's is CameraSync).
struct UnkStruct_syncObject {
    unsigned char pad_0[0x10];
    void (*field_10)(void *);          // [C] called with the object (a0 = link - 8)
};

// [C] a frame ([L] RwFrame; retail 3.6 offsets).
struct UnkStruct_syncFrame {
    unsigned char field_0;
    unsigned char field_1;
    unsigned char field_2;
    unsigned char field_3;             // [C] private flags: bit 0 hierarchy dirty, bit 2 LTM dirty, bit 3 objects dirty, high nibble kept ([L] privateFlags)
    UnkStruct_syncFrame *field_4;      // [C] parent
    UnkStruct_syncLink field_8;        // [C] the dirty list link
    unsigned int field_10[16];         // [C] the modelling matrix ([L] modelling)
    unsigned int field_50[16];         // [C] the LTM ([L] ltm)
    UnkStruct_syncLink field_90;       // [C] the object list head ([L] objectList)
    UnkStruct_syncFrame *field_98;     // [C] first child
    UnkStruct_syncFrame *field_9C;     // [C] next sibling
};

extern "C" void FrameSyncHierarchyRecurse(UnkStruct_syncFrame *frame, unsigned int flags);
extern "C" void FrameSyncHierarchyRecurseNoLTM(UnkStruct_syncFrame *frame);
extern "C" void FrameSyncHierarchyLTMRecurse(UnkStruct_syncFrame *frame, unsigned int flags);

// [C] runs the sync function of every object on the frame's object list (head = frame + 0x90): object = link - 8,
// object + 0x10 called with the object; the next link is read after the call.
static inline void func_001E8600_inl1(UnkStruct_syncLink *head)
{
    UnkStruct_syncLink *link = head->field_0;

    while (link != head) {
        UnkStruct_syncObject *obj = (UnkStruct_syncObject *)((unsigned char *)link - 8);

        obj->field_10(obj);
        link = link->field_0;
    }
}

// [C] the LTM = the modelling matrix (four quadwords, +0x10..0x4F -> +0x50..0x8F).
static inline void func_001E8600_inl2(UnkStruct_syncFrame *frame)
{
    unsigned int w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15;

    w0 = frame->field_10[0];
    w1 = frame->field_10[1];
    w2 = frame->field_10[2];
    w3 = frame->field_10[3];
    w4 = frame->field_10[4];
    w5 = frame->field_10[5];
    w6 = frame->field_10[6];
    w7 = frame->field_10[7];
    w8 = frame->field_10[8];
    w9 = frame->field_10[9];
    w10 = frame->field_10[10];
    w11 = frame->field_10[11];
    w12 = frame->field_10[12];
    w13 = frame->field_10[13];
    w14 = frame->field_10[14];
    w15 = frame->field_10[15];
    frame->field_50[0] = w0;
    frame->field_50[1] = w1;
    frame->field_50[2] = w2;
    frame->field_50[3] = w3;
    frame->field_50[4] = w4;
    frame->field_50[5] = w5;
    frame->field_50[6] = w6;
    frame->field_50[7] = w7;
    frame->field_50[8] = w8;
    frame->field_50[9] = w9;
    frame->field_50[10] = w10;
    frame->field_50[11] = w11;
    frame->field_50[12] = w12;
    frame->field_50[13] = w13;
    frame->field_50[14] = w14;
    frame->field_50[15] = w15;
}

// [C] syncs one frame hierarchy (root). Flags s6 = root.flags (+3). If bit 0 (hierarchy dirty): when bit 2 (LTM dirty)
// the modelling matrix is copied to the root's LTM; the root's objects are synced; then for each child c (+0x98, next
// +0x9C): s4 = (root.flags & 4) | c.flags; when s4 & 4: c.LTM = RwMatrixMultiply(c.modelling, parent.LTM); c's objects
// are synced; c.flags &= 0xF3; FrameSyncHierarchyRecurse(c.firstChild, s4). Without bit 0: the root's objects, then for
// each child its objects, c.flags &= 0xF7 and FrameSyncHierarchyRecurseNoLTM(c.firstChild). Finally root.flags =
// s6 & 0xF0 (low nibble cleared). Returns that byte.
// 0x001E8600
extern "C" unsigned int FrameSyncHierarchy(UnkStruct_syncFrame *frame)
{
    unsigned int s6 = frame->field_3;
    unsigned int s5, s4, v0;
    UnkStruct_syncFrame *s1;

    if ((s6 & 1) != 0) {
        s5 = s6 & 4;
        if (s5 != 0) {
            func_001E8600_inl2(frame);
        }
        func_001E8600_inl1(&frame->field_90);
        s1 = frame->field_98;
        if (s1 != 0) {
            v0 = s1->field_3;
            for (;;) {
                s4 = s5 | v0;
                if ((s4 & 4) != 0) {
                    RwMatrixMultiply(s1->field_50, s1->field_10, s1->field_4->field_50);
                }
                func_001E8600_inl1(&s1->field_90);
                v0 = s1->field_3;
                v0 = v0 & 0xF3;
                s1->field_3 = (unsigned char)v0;
                FrameSyncHierarchyRecurse(s1->field_98, s4);
                s1 = s1->field_9C;
                if (s1 == 0) {
                    break;
                }
                v0 = s1->field_3;
            }
        }
        v0 = s6 & 0xF0;
    } else {
        func_001E8600_inl1(&frame->field_90);
        s1 = frame->field_98;
        if (s1 != 0) {
            for (;;) {
                func_001E8600_inl1(&s1->field_90);
                v0 = s1->field_3;
                v0 = v0 & 0xF7;
                s1->field_3 = (unsigned char)v0;
                FrameSyncHierarchyRecurseNoLTM(s1->field_98);
                s1 = s1->field_9C;
                if (s1 == 0) {
                    break;
                }
            }
        }
        v0 = s6 & 0xF0;
    }
    frame->field_3 = (unsigned char)v0;
    return v0;
}

// [C] the dirty frame list head is ourGlobals + 0xBC (next, prev); every listed link (frame = link - 8) is passed to
// FrameSyncHierarchy (the next link is read after the call); then the list is made empty (next = prev = &head). Returns 1.
// 0x001E87E0
extern "C" unsigned int _rwFrameSyncDirty(void)
{
    UnkStruct_syncLink *head = (UnkStruct_syncLink *)(ourGlobals + 0xBC);
    UnkStruct_syncLink *link = head->field_0;

    while (link != head) {
        FrameSyncHierarchy((UnkStruct_syncFrame *)((unsigned char *)link - 8));
        link = link->field_0;
    }
    head->field_0 = head;
    head->field_4 = head;
    return 1;
}

// [C] the LTM-only sync of one hierarchy root (flags s3 = root.flags): when bit 2 the modelling matrix is copied to the
// LTM; for each child c: s2 = s3 | c.flags; when s2 & 4: c.LTM = RwMatrixMultiply(c.modelling, parent.LTM) and c.flags
// &= 0xFB; FrameSyncHierarchyLTMRecurse(c.firstChild, s2). Finally root.flags = s3 & 0xFA (bits 0 and 2 cleared), which
// is also the return value.
// 0x001E8848
extern "C" unsigned int _rwFrameSyncHierarchyLTM(UnkStruct_syncFrame *frame)
{
    unsigned int s3 = frame->field_3;
    unsigned int s2, v0;
    UnkStruct_syncFrame *s0;

    if ((s3 & 4) != 0) {
        func_001E8600_inl2(frame);
    }
    s0 = frame->field_98;
    v0 = s3 & 0xFA;
    if (s0 != 0) {
        v0 = s0->field_3;
        for (;;) {
            s2 = s3 | v0;
            if ((s2 & 4) != 0) {
                RwMatrixMultiply(s0->field_50, s0->field_10, s0->field_4->field_50);
                s0->field_3 = (unsigned char)(s0->field_3 & 0xFB);
            }
            FrameSyncHierarchyLTMRecurse(s0->field_98, s2);
            s0 = s0->field_9C;
            if (s0 == 0) {
                break;
            }
            v0 = s0->field_3;
        }
        v0 = s3 & 0xFA;
    }
    frame->field_3 = (unsigned char)v0;
    return v0;
}

// [C] the recursion of FrameSyncHierarchy's flagged branch for frame (and its siblings): flags s4 = the passed flags;
// per frame f: s3 = s4 | f.flags; when s3 & 4: f.LTM = RwMatrixMultiply(f.modelling, parent.LTM); f's objects are synced;
// f.flags &= 0xF3; recurse into f's children with s3; then f = f.next.
// 0x001E8910
extern "C" void FrameSyncHierarchyRecurse(UnkStruct_syncFrame *frame, unsigned int flags)
{
    unsigned int s3, v0;

    if (frame != 0) {
        v0 = frame->field_3;
        for (;;) {
            s3 = flags | v0;
            if ((s3 & 4) != 0) {
                RwMatrixMultiply(frame->field_50, frame->field_10, frame->field_4->field_50);
            }
            func_001E8600_inl1(&frame->field_90);
            v0 = frame->field_3;
            v0 = v0 & 0xF3;
            frame->field_3 = (unsigned char)v0;
            FrameSyncHierarchyRecurse(frame->field_98, s3);
            frame = frame->field_9C;
            if (frame == 0) {
                break;
            }
            v0 = frame->field_3;
        }
    }
}

// [C] the recursion of FrameSyncHierarchy's unflagged branch: per frame f (and its siblings): f's objects are synced,
// f.flags &= 0xF7, recurse into f's children.
// 0x001E89D8
extern "C" void FrameSyncHierarchyRecurseNoLTM(UnkStruct_syncFrame *frame)
{
    unsigned int v0;

    if (frame != 0) {
        for (;;) {
            func_001E8600_inl1(&frame->field_90);
            v0 = frame->field_3;
            v0 = v0 & 0xF7;
            frame->field_3 = (unsigned char)v0;
            FrameSyncHierarchyRecurseNoLTM(frame->field_98);
            frame = frame->field_9C;
            if (frame == 0) {
                break;
            }
        }
    }
}

// [C] the recursion of _rwFrameSyncHierarchyLTM: per frame f (and its siblings): s1 = flags | f.flags; when s1 & 4:
// f.LTM = RwMatrixMultiply(f.modelling, parent.LTM), f.flags &= 0xFB; recurse into f's children with s1.
// 0x001E8A60
extern "C" void FrameSyncHierarchyLTMRecurse(UnkStruct_syncFrame *frame, unsigned int flags)
{
    unsigned int s1, v0;

    if (frame != 0) {
        v0 = frame->field_3;
        for (;;) {
            s1 = flags | v0;
            if ((s1 & 4) != 0) {
                RwMatrixMultiply(frame->field_50, frame->field_10, frame->field_4->field_50);
                frame->field_3 = (unsigned char)(frame->field_3 & 0xFB);
            }
            FrameSyncHierarchyLTMRecurse(frame->field_98, s1);
            frame = frame->field_9C;
            if (frame == 0) {
                break;
            }
            v0 = frame->field_3;
        }
    }
}
