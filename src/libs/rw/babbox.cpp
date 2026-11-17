// RenderWare 3.6 core: bounding boxes (RW 3.1 file name: src/babbox.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot2 (track rw).

// [C] a bounding box, 0x18 bytes ([L] RwBBox): the supremum (max) then the infimum (min) corner.
struct UnkStruct_RwBBoxView {
    float field_0;                     // [C] sup.x
    float field_4;                     // [C] sup.y
    float field_8;                     // [C] sup.z
    float field_C;                     // [C] inf.x
    float field_10;                    // [C] inf.y
    float field_14;                    // [C] inf.z
};

// [C] three floats ([L] RwV3d).
struct UnkStruct_RwV3dBBox {
    float x;
    float y;
    float z;
};

// [C] both corners start as the first vertex (the infimum is written first, the supremum second; numVerts is
// decremented in between); then, while the counter (numVerts - 1, tested before each pass and decremented after the
// test) is nonzero, the next vertex is merged: each infimum component = the vertex component when that is < it, each
// supremum component = the vertex component when it is < that ([C] c.lt.s both ways, so a NaN vertex component never
// replaces anything). With numVerts == 1 only the copy is done; with numVerts <= 0 the counter never reaches 0 within
// the vertex array ([G] undefined use). Returns the box.
// 0x001DFD08
extern "C" UnkStruct_RwBBoxView *RwBBoxCalculate(UnkStruct_RwBBoxView *bbox, const UnkStruct_RwV3dBBox *verts, int numVerts)
{
    int a2 = numVerts;
    int v0;
    float f0, f1;

    bbox->field_C = verts->x;
    bbox->field_10 = verts->y;
    bbox->field_14 = verts->z;
    a2 = a2 - 1;
    bbox->field_0 = verts->x;
    bbox->field_4 = verts->y;
    bbox->field_8 = verts->z;
    v0 = a2;
    a2 = a2 - 1;
    verts = verts + 1;
    if (v0 != 0) {
        do {
            f0 = bbox->field_C;
            f1 = verts->x;
            if (f1 < f0) {
                bbox->field_C = f1;
            }
            f0 = bbox->field_10;
            f1 = verts->y;
            if (f1 < f0) {
                bbox->field_10 = f1;
            }
            f0 = bbox->field_14;
            f1 = verts->z;
            if (f1 < f0) {
                bbox->field_14 = f1;
            }
            f0 = bbox->field_0;
            f1 = verts->x;
            if (f0 < f1) {
                bbox->field_0 = f1;
            }
            f0 = bbox->field_4;
            f1 = verts->y;
            if (f0 < f1) {
                bbox->field_4 = f1;
            }
            f0 = bbox->field_8;
            f1 = verts->z;
            if (f0 < f1) {
                bbox->field_8 = f1;
            }
            v0 = a2;
            verts = verts + 1;
            a2 = a2 - 1;
        } while (v0 != 0);
    }
    return bbox;
}
