// fe2 (docs/native-renderer.md §24): the World Tour globe, CB3Globe2dObject::_Render 0x3B2EE0, rebuilt from a GLOBE event
// (menu1's capture, hud_capture.cpp observe_globe). The game draws it through B3Im3d between Gt2d Suspend / Resume:
// invisible depth polygons, an anti-aliased wireframe, the textured surface, a highlighted region, the halo. Here the
// same B3Im3d batches in the game's own order and space (the vertices B3Im3d is handed: viewport 0's camera space), for
// hud_list to draw at the GLOBE event's place in the 2D draws. Decomp drafts (all [L], written from the retail asm):
// src/nonmatching/_Render__16CB3Globe2dObject..., DrawInvisiblePolys..., DrawGlobeHalo..., Prepare__16CB3Globe2dObjectFv.
#ifndef B3REPLAY_GLOBE_H
#define B3REPLAY_GLOBE_H

#include <cstdint>
#include <vector>

#include "hud.h"

namespace b3replay {

struct GlobeVertex {
    float pos[3];                // B3Im3d's input position (world = viewport 0's camera space for the globe)
    float uv[2];
    std::uint8_t rgba[4];        // the bytes the GS gets (B3Im3d doubles untextured colours, capped 255 / 128)
};

struct GlobeBatch {
    enum Kind : std::uint8_t { kLines, kStrip } kind = kStrip;
    int texture = -1;            // HudGlobe::texture index (0 surface, 1-3 highlight regions 0 / 1 / 2 [order of the
                                 // event: D_01EA460C, ..4618, ..4614], 4 halo), -1 untextured
    bool zwrite = true;          // B3Im3d::SetZWrite (the camera record's ZBUF ZMSK)
    bool cull = false;           // B3Im3d::SetBackfaceCull (_gbBFC: CLIP bit in the packet's GIF tag word) [G effect]
    bool antialias = false;      // B3Im3d::SetLineAntialiasing: PRIM AA1
    std::vector<GlobeVertex> v;  // lines: pairs; strip: strip order
};

// CB3Globe2dObject::Prepare 0x3B55A0's static tables (gmpStaticGlobeData, built once): 33 latitude rows x 65 longitude
// columns; `sphere` = field_0 (unit sphere), `plane` = field_10C20 (the flat map). field_8610 (the cylinder) only enters
// _Render through Interpolate(cylinder + offUp, sphere, 1.0) = the sphere point, so it is not built.
struct GlobeGrids {
    float sphere[33 * 65][3];
    float plane[33 * 65][3];
};
const GlobeGrids& globe_grids();

// The B3Im3d batches _Render sends for `g`, in order.
std::vector<GlobeBatch> globe_batches(const HudGlobe& g);

}  // namespace b3replay

#endif
