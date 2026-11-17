// Car shading model: the math of the game's car microprograms as C++ (docs/car-shading.md), i.e. what a GPU shader
// must compute to draw a car like the PS2 does. Three stages, each mirroring one piece of game code:
//   car_part_constants  VU0 0x47F6D0 at vcallms 0x7D8 (CB3VehicleRenderer::RenderObject 0x21FBB0 /
//                       RenderDeformedObject 0x21F790, once per car part): SH irradiance matrix, view direction,
//                       sphere-map basis, all in the part's space -> VU1 962..973
//   car_mesh_constants  VU0 0x47F6D0 at vcallms 0xB50 (per mesh entry): base/full colours and alphas, env-pass colour,
//                       GS ALPHA_1 / ALPHA_2 -> VU1 1008..1013
//   car_batch           VU1 0x46BB80 (per batch): damage deformation, lighting, Fresnel alpha, UVs, projection,
//                       the second (environment map) pass
// tools/render/b3vucheck_car.cpp runs the three programs in the b3asset VU interpreter on real car batches and
// compares their output with these functions.
// Tags: [C] seen in code (VU address = instruction index in build/vu/game_*.txt from tools/render/vudis.py) or data;
// [L] likely; [G] guess.
#ifndef B3ASSET_CARSHADE_H
#define B3ASSET_CARSHADE_H

#include <cstdint>
#include <vector>

#include "b3asset/vif.h"

namespace b3asset {

// What the EE side knows when it draws a car.
struct CarScene {
    // Camera: the RW view matrix rows the programs read (VU1 1014..1017, x/z and y/z in [0,1] across the view
    // window; CB3CameraData +0x120 [C]), GS window mapping (1018/1019) and guard band (1020..1023), as for the track.
    float view[4][3] = {};
    float scale[4] = {640.0f, 448.0f, 4194304.0f, 0.0f};
    float shift[4] = {2048.0f, 2048.0f, 0.0f, 0.0f};
    float guard[4][4] = {{325.0f, 1.0f / 325.0f, -1.0f, 1000.0f}, {229.0f, 1.0f / 229.0f, 1.001f, 0.5f},
                         {1727.9374f, 1.0f / 1727.9374f, -1.0f, 1000.0f}, {1823.9374f, 1.0f / 1823.9374f, 1.001f, 0.5f}};
    float cam_pos[3] = {}, cam_up[3] = {0, 1, 0};  // camera frame position / up (CB3CameraData +0xD0 / +0xB0) [C]
    // Lighting at the car: the 9 SH irradiance coefficients CB3StreamedTrack::GetIrradiance 0x301340 blends from the
    // track's per-collision-vertex compressed maps (CGtIrradianceMapCompressed, LOD block +0x94) [C names, L body],
    // in the order L00, L1-1(y), L10(z), L11(x), L2-2(xy), L2-1(yz), L20, L21(xz), L22(x2-y2) [C VU0 run, §2].
    float sh[9] = {};
    float shininess = 1.0f;  // f0 of BeginRenderingSingleVehicle 0x220040: reflectivity alphas [C]; damage lowers it
    float lighting = 1.0f;   // f1: diffuse colour scale [C]; explosions lower it
    float fade = 1.0f;       // RenderObject's float argument (always 1.0 from RenderVehicle) [C]
};

// One part draw (RenderObject / RenderDeformedObject call).
struct CarPartInput {
    float mat[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};  // part -> world rows right, up, at, pos
    float car_pos[3] = {};       // CB3VehicleEssentialParams +0x30 (the car, not the part) [C 0x220040]
    bool rotate_lighting = true;  // the bool argument: rebuild the SH matrix in this part's space (VU0 path at 0) [C]
    float prev_m[4][4] = {};     // the SH matrix left by the previous part, used when !rotate_lighting [C]
    float bias = 65537.0f;       // CB3VehicleObject +0x08 (= the list's float 0x47800080) [C data]
    std::uint16_t lit_bits = 0;  // lx16LitBits: which light meshes are on (bits 2..7) [L]
    int le_type = 0;             // 2 = clip at the guard band (RenderVehicle passes 2 when the frustum test says so) [C]
    int zones = 0;               // damage zone count (8 body, 2 panels; 0 = undeformed) [C RenderVehicle]
    float zone[8][4][3] = {};    // zone matrices (CB3VehicleDeform output), rows x, y, z, translation
};

struct CarPartConstants {  // VU1 962..973
    float part[4][4];   // 962..965: part matrix rows (965.w = 0: "not prepared") [C]
    float m[4][4];      // 966..969: SH irradiance matrix in part space, E(n) = (n,1) M (n,1)^T [C]
    float view[4];      // 970: xyz direction camera -> car in part space (normalised), w the normal bias [C]
    float env[3][4];    // 971..973: xyz sphere-map basis rows, w the camera position in part space [C]
};
CarPartConstants car_part_constants(const CarScene& sc, const CarPartInput& in);
// The R&H irradiance matrix of the 9 coefficients in world axes (966..969 before the part rotation): E(n) =
// (n,1) M (n,1)^T for a world normal n. The GPU shader (b3gfx car.vert) uses it with world normals.
void car_irradiance_matrix(const float sh[9], float m[4][4]);
// The VU0 builds 966..969 from the coefficients rotated into part space, not as R M R^T: on unit normals the two agree,
// but they differ by k diag(1,1,1,-1) (the only form that vanishes on the sphere), and the program's normals are not
// unit length ((b - 128) / 128). With L20' the part-space L20 (the traceless 3x3 block rotates exactly; c5 = c3 / 3):
// k = c5 (L20' - L20) = 0.5 (a^T M3 a - M3[2][2]), a = the part's at axis in world axes. So the program computes
// E = (n_w,1) M (n_w,1)^T + k (|n|^2 - 1) [derivation; checked by b3vucheck_car --shader on yawed parts].
float car_irradiance_k(const float m[4][4], const float at[3]);

// One mesh entry of a car list (16 bytes: mesh, continuation, material word, colour word) [C FixUpVehicleData].
struct CarMeshInput {
    std::uint16_t mask = 0;      // material word bits 0..15: light bits 2..7, glass 0x100/0x200, bit 1 [C VU0 0x16A..]
    std::uint8_t alpha_byte = 0; // material word byte 2: GS ALPHA_1 A/B/C/D selector [C 0x189]
    std::uint8_t tex_slot = 0;   // material word byte 3: renderer texture register slot (0 = paint atlas) [C 0x21FBB0]
    std::uint8_t fix = 0x80;     // colour word byte 1: ALPHA_1 FIX (byte 0 is its default; glass state changes it) [C]
    std::uint16_t lit_bits = 0;
};
CarMeshInput car_mesh_input(std::uint32_t material_word, std::uint32_t colour_word, std::uint16_t lit_bits);

struct CarMeshConstants {
    float base[4], full[4];   // 1010 / 1011: colour = base + E(n) (full - base); alpha from the Fresnel term [C]
    std::int32_t env[4];      // 1012: env-pass vertex colour (integers) [C]
    std::uint32_t alpha1[2];  // 1008: GS ALPHA_1 (A/B/C/D byte, FIX) [C]
    std::uint32_t alpha2;     // 1009: GS ALPHA_2 (env pass blend) [C]
    std::uint32_t no_env;     // 1013.y: non-zero = no env pass [C]
};
CarMeshConstants car_mesh_constants(const CarScene& sc, const CarMeshInput& in);

// Per-vertex GS output of VU1 0x46BB80: pass 1 (context 1, the car atlas) and, unless no_env, pass 2 (context 2: the
// sphere map TEX0_2, ALPHA_2, TEST_2 FB_ONLY) with the same positions.
struct CarVertexOut {
    std::int32_t rgba[4];   // RGBAQ as the program writes it (integers; the GS keeps the low 8 bits)
    float s, t, q;          // pass 1 STQ (perspective: s = u q)
    float x, y, z;          // screen x, y (GS 12.4 units / 16) and z (24-bit units)
    bool adc;               // the strip's ADC flag (data; plus the guard-band test when clipping)
    float es, et;           // pass 2 sphere-map coordinates (before the * q)
    float pos[3], normal[3];  // deformed position / normal in part space (for the shader comparison)
    float light;            // E(n)
};
// Runs the model on a batch as unpacked by the VIF (position @3i, UV/influences @3i+1, normal @3i+2).
void car_batch(const VuMemory& in, std::uint32_t n, const CarScene& sc, const CarPartConstants& part,
               const CarMeshConstants& mesh, const CarPartInput& pin, std::vector<CarVertexOut>& out);

}  // namespace b3asset

#endif
