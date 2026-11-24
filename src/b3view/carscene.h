// Cars drawn the game's way (render5, docs/native-renderer.md §12.6): the car files in part space, the textures of the
// renderer's register slots, and CB3VehicleRenderer::RenderVehicle 0x21E6F0's draw order and per-part / per-mesh
// constants, turned into b3gfx kCar draw items. Shared by b3view (one car) and b3replay (captured races).
#ifndef B3VIEW_CARSCENE_H
#define B3VIEW_CARSCENE_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "b3asset/enviro.h"
#include "b3gfx/b3gfx.h"
#include "scene.h"

namespace b3view {

// A CGtMatrix3x4 as the game stores it: rows right, up, at, pos (row-vector convention: p' = x R + y U + z A + P).
struct RowMat {
    float r[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
};
RowMat row_mul(const RowMat& a, const RowMat& b);  // apply a, then b

// The R&H irradiance matrix of 9 SH coefficients (order L00, L1-1, L10, L11, L2-2, L2-1, L20, L21, L22; VU0 0x47F6D0
// [C docs/car-shading.md §2]), column-major, and E(n) = (n,1) M (n,1)^T for a world normal.
void car_sh_matrix(const float sh[9], float out[16]);
float car_irradiance(const float sh[9], const float n[3]);

// Textures of the vehicle renderer's register slots 1..4 (material byte 3): CB3GraphicsManagerBase::Prepare 0x19EBF0
// finds "UnbrokenGlass", "CrackedGlass", "SmashedGlass", "VehicleUnderside" in the global texture dictionary
// (gGraphicsManager +0x40..+0x4C) and CB3GraphicsManager::Prepare 0x228070 uploads them as the TEX0 at +0x3D0
// (underside), +0x3E0 (unbroken), +0x3F0 (cracked), +0x400 (smashed) [C both matched], which BeginVehicleRendering
// 0x220540 copies to the renderer's slots 1..4 (D_00666290..D_006662C0 -> +0x30..+0x60) [C]. Slot 0 is the car's
// atlas with its paint palette (SetTexture 0x21FF60). Plus the env map's sky (the route's backdrop) and sun.
struct CarGlobals {
    b3gfx::TextureId slot[5] = {b3gfx::kNone, b3gfx::kNone, b3gfx::kNone, b3gfx::kNone, b3gfx::kNone};
    b3gfx::TextureId env_sky = b3gfx::kNone;
    bool env_frame_units = false;   // fe2: env_sky holds frame units (a baked map, as the GS env map), not display units
    float sun_colour[4] = {0, 0, 0, 0};
    std::uint32_t env_world = b3gfx::kNone;   // reflect-tunnel: this frame's world env map (Device::render_env), kNone: the sky alone
};
// Slots 1..4 from <game>/DATA/GLOBAL.TXD. False (with a reason) if the file or a texture is missing.
bool load_car_globals(b3gfx::Device& dev, const std::string& game, CarGlobals& out, std::string* why);
// loadlag1: load_car_globals in two halves. decode_car_globals (no device: any thread) decodes slots 1..4 from the
// file's bytes in order and returns how many it got (4: all; else `why` says what is missing); upload_car_globals
// uploads those `n`.
int decode_car_globals(const std::vector<std::uint8_t>& global_txd, b3asset::DecodedRaster (&slots)[5], std::string* why);
void upload_car_globals(b3gfx::Device& dev, const b3asset::DecodedRaster (&slots)[5], int n, CarGlobals& out);
// The env map's sky and sun from a route's ENVIRO.DAT (Scene::enviro after add_enviro).
void set_car_environment(b3gfx::Device& dev, const b3asset::Enviro& env, CarGlobals& out);

// One car file at one LOD, every list in part space (b3asset::CarOptions::bake_frames = false).
struct CarModel {
    Scene scene;                   // items parallel to scene.model.draws
    struct Draw { int list = -1, entry = -1, glass_entry = -1; std::uint32_t material = 0, colour = 0; };
    std::vector<Draw> draws;
    int lod = -1;                  // the LOD block used
    int nwheel = 0, npanel = 0;
    RowMat wheel_frame[8], panel_frame[8];  // file frames +0xB80 / +0xD00 (traffic draws its parts there)
    float radius = 0;              // vehicle data +0x18 (wheel radius scale [L])
};
// RenderVehicle's LOD fallback: the requested block, else the next one up to 4, else down [L draft 0x21E6F0].
int car_lod_block(const std::vector<std::uint8_t>& file, int lod);
bool load_car_model(b3gfx::Device& dev, const std::vector<std::uint8_t>& file, int palette, int lod, CarModel& out,
                    std::string* why);

// What RenderVehicle reads for one vehicle (world space).
struct CarPose {
    RowMat main, body;             // EssentialParams +0x00 and the tilted body matrix
    bool parts = false;            // VehicleParams present (race cars, crashed vehicles); else file frames (traffic)
    int nbit = 0, nwheel = 0;
    RowMat bit[7], wheel[6];       // world matrices (state applied)
    std::int8_t bit_state[7] = {};
    float wheel_speed[6] = {};
    float sh[9] = {};
    float shininess = 1.0f, lighting = 1.0f;  // BeginRenderingSingleVehicle's f0 / f1 (car_shading_scales)
    std::uint16_t lit = 0;         // EssentialParams +0x58 & 0xFC
    float tail = -1.0f, brake = 0; // taillights (night, rain): the rear lamps' tail and brake levels (tail < 0: the game's)
    std::uint8_t glass = 0;        // VehicleParams +0x1014
    // Damage deformation (render6): RenderVehicle draws the body through RenderDeformedObject with the 8 zone matrices at
    // VehicleParams +0x500 while +0x101B, panel p with the 2 at +0x900 + 0x80 p while +0x101C[p] [C draft 0x21E6F0]
    bool deformed = false;
    RowMat zone[8];
    bool bit_deformed[7] = {};
    RowMat bit_zone[7][2];
    // payloads1: the payload objects RenderVehicle draws (vehicle data +0x16C0 set): C list pay_list[i] at pay[i] (world)
    static constexpr int kMaxPayload = 32;   // (the flatbed, PVEH/HEVY/CAR27.BTV: 8 variants of up to 19)
    int npay = 0;
    std::int8_t pay_list[kMaxPayload] = {};
    RowMat pay[kMaxPayload];
};

// CB3TrafficSystem::Draw 0x3078F0's VehicleParams for a traffic vehicle [L draft, C asm: angles +0x128 / +0x130, speed
// +0xC4, axes kXAxisVector / kYAxisVector, CreateRotationMatrixOnPrincipalAxis 0x3090A0]: panels at their file frames
// (+0xD00), wheels relative to the main matrix: wheel 0 (spin x steer) x frame, 1 (spin(-a) x steer) x frame, other even
// spin x frame, odd spin(-a) x frame, with spin = R(x, +0x128), steer = R(y, -(+0x130)); wheel speeds +0xC4 / the wheel
// radius (vehicle data +0x18) for the B1 / B2 choice.
void traffic_parts(const CarModel& car, float spin, float steer, const float spin_axis[3], const float steer_axis[3],
                   float speed, CarPose& pose);
// RenderVehicle's damage / explosion scales [C draft 0x21E6F0, constants D_004E0E94 0.6, D_004E0C50 0.4, D_004E0E24 0.1]:
// deformed: shininess = 1 - 0.6 damage; exploded, t seconds after: lighting x (1 - 1.875 t), shininess x (1 - 2.25 t)
// up to t = 0.4, then x 0.25 / x 0.1.
void car_shading_scales(bool deformed, float damage, bool exploded, float t, float& shininess, float& lighting);

// RenderVehicle's draws for `pose`, appended to `out` in the game's order: body meshes with mask & 0xFF, the panels far
// to near (mask 0x3FF, lit with the body's SH matrix), the body's glass (mask 0x300), the wheels (list B0 / B1 / B2 by
// |speed| > 25 / > 50, mask 0x3FF). `eye` / `cam_up`: the render camera; `lod_eye`: CGtCamera +0x20 (panel sort).
void add_car_draws(const CarModel& car, const CarGlobals& g, const CarPose& pose, const float eye[3],
                   const float lod_eye[3], std::vector<b3gfx::DrawItem>& out);

}  // namespace b3view

#endif
