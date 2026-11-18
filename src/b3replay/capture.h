// b3replay capture reader: decodes a scene capture written by the scene hook (src/gameview/scene_capture.cpp,
// B3PORT_SCENE_OUT) into one Tick per retail poll (= one 60 Hz simulation tick). The file is a delta stream of a sparse
// guest memory image; the reader applies it and reads the image with the same pointers and layouts the hook followed
// (docs/native-renderer.md §12 lists each value and its source).
#ifndef B3REPLAY_CAPTURE_H
#define B3REPLAY_CAPTURE_H

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "b3asset/coronashade.h"
#include "hud.h"
#include "b3mod/overlay_record.h"   // sdk2b: the code mods' overlay record (header only)

namespace b3replay {

// A CGtMatrix3x4 / RwMatrix: rows right, up, at, pos (row-vector convention: p' = x R + y U + z A + P).
struct Mat34 {
    float r[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
};
Mat34 mul(const Mat34& a, const Mat34& b);  // row-vector product: apply a, then b
Mat34 inverse_affine(const Mat34& m);
void to_colmajor(const Mat34& m, float out[16]);

enum class VehicleKind : std::uint8_t { kRaceCar, kTraffic, kCrashed };

// A vehicle data's corona tables (corona1, docs/native-renderer.md §16), shared while its bytes do not change: light
// group k at +0x1664[k] (count u8 +0x16AC[k]), the boost glow's exhaust items at +0x1684 (count u8 +0x16B4), 0x30 bytes
// each [C asm RenderLightCoronas 0x256690, func_00257080, func_00257230 0x257334].
struct CoronaTables {
    struct Light {                                       // +0x00 position, +0x10 facing (vehicle space)
        float pos[3] = {}, dir[3] = {};
        bool operator==(const Light&) const = default;
    };
    struct Exhaust {
        float pos[3] = {}, dir[3] = {};                  // +0x00 / +0x10 (vehicle space)
        float u = 0, v = 0;                              // +0x24 / +0x28: weights of body zones a and b (identity: 1 - u - v)
        std::uint8_t a = 0, b = 0;                       // +0x2C / +0x2D: the zones
        bool operator==(const Exhaust&) const = default;
    };
    std::vector<Light> group[7];
    std::vector<Exhaust> exhaust;
    bool operator==(const CoronaTables&) const = default;
};

struct Vehicle {
    std::uint32_t id = 0;        // guest address of the object: identity across ticks
    std::uint32_t data = 0;      // guest address of its CB3VehicleData (model identity, see Capture::models)
    VehicleKind kind = VehicleKind::kRaceCar;
    std::uint8_t paint = 0;      // EssentialParams +0x59 (palette index)
    std::uint8_t lit = 0;        // +0x58 & 0xFC (light bits)
    Mat34 main;                  // EssentialParams +0x00 (wheels and payload use it)
    Mat34 body;                  // the body / panels' matrix: tilt rotations (+0x44..+0x50) x main
    bool parts = false;          // VehicleParams captured (race cars, crashed vehicles)
    int nwheel = 0, nbit = 0;    // from the vehicle data (+0x0D / +0x0C)
    Mat34 wheel[6], bit[7];      // VehicleParams +0x000 / +0x180, already in world space (state applied)
    std::int8_t wheel_state[6] = {}, bit_state[7] = {};
    bool deformed = false;       // VehicleParams +0x101B: the body draws through RenderDeformedObject with 8 zones
    // Damage deformation (render6): the zone matrices RenderDeformedObject 0x21F790 appends to the VU1 header (974 + 4 k)
    // [C RenderVehicle 0x21E6F0 draft: body +0x500 x 8 while +0x101B, panel p +0x900 + 0x80 p x 2 while +0x101C[p]]
    bool zones_valid = false;    // the capture holds [0x500, 0xC80) at this tick
    Mat34 zone[8];               // body zones, part space (rows x, y, z, translation)
    bool bit_deformed[7] = {};   // VehicleParams +0x101C + p
    Mat34 bit_zone[7][2];
    std::int8_t frustum = -1;    // EssentialParams +0x5B: the game's frustum result for the whole vehicle (0 outside, 1 crossing, 2 inside [L: --info counts; DoUnitFrustumTesting's encoding])
    float radius = 0;            // vehicle data +0x14: its bounding radius [L Draw__16CB3TrafficSystem]
    int model = -1;              // index into Capture::heads: the vehicle data header at this tick (model identity; an
                                 // address can hold another car in a later race)
    // Car shading inputs (docs/car-shading.md; render5):
    float sh[9] = {};            // EssentialParams +0x5C: SH irradiance (BeginRenderingSingleVehicle 0x220040 copies 9 floats) [C]
    int sh_count = 0;            // how many of them the capture holds (race cars 9; traffic / crashed 1: [0, 0x60) only)
    std::uint8_t glass = 0;      // VehicleParams +0x1014: SetGlassState argument (0 intact, 1 cracked, 2 broken) [C RenderVehicle]
    float damage = 0;            // VehicleParams +0x370: shininess 1 - 0.6 x this while deformed (+0x101B) [C RenderVehicle, D_004E0E94]
    bool exploded = false;       // VehicleParams +0x1015 bit 0x10 [C RenderVehicle]
    float explode_time = 0;      // gWorld timer +0x20 - VehicleParams +0x100C (the W lane of +0x1000) [C RenderVehicle]
    float wheel_speed[6] = {};   // VehicleParams +0x4E8: |v| > 25 / > 50 picks the wheel list B1 / B2 [C RenderVehicle]
    // Traffic (CB3TrafficVehicle, render6): CB3TrafficSystem::Draw 0x3078F0 builds its VehicleParams per frame [L draft,
    // field offsets C from the asm]: wheels (spin x steer) x file frame, wheel speeds = speed / the file's wheel radius
    float speed = 0;             // +0xC4 (EssentialParams +0x54): Draw's +0x4E8[w] = this / vehicle data +0x18
    float spin = 0, steer = 0;   // +0x128 / +0x130: rotations about kXAxisVector / kYAxisVector (0x4C4950 / 0x4C4960)
    float spin_axis[3] = {1, 0, 0}, steer_axis[3] = {0, 1, 0};
    std::uint8_t flags = 0;      // +0x174: bit 0 active, bit 2 crashing (drawn beyond 275 m, without the unit test)
    std::uint8_t flags2 = 0;     // +0x17A: bit 1 = the camera is inside the vehicle (Draw then skips it)
    int unit = -1;               // traffic +0xCA / crashed EssentialParams +0x5A: TestPosition's safe-area slot (-1 none)
    float cull_pos[3] = {};      // where Draw measures the camera distance from (traffic +0xA0; crashed *(+0x204) +0x30)
    bool lifted = false;         // crashed: +0x2402 (drawn whatever the frustum result says) [L draft]
    bool listed = true;          // traffic: in the player race car's list that Draw walks (traffic system +0x35C24)
    std::uint8_t listings = 1;   // how often it is in that list: an index can be there twice (races poll 5476), and Draw then
                                 // renders it and its immediate effects (lights) twice [C the game's corona slots, corona1]
    // Draw's decision for traffic and crashed vehicles (game_draws; race cars always 0): 0 drawn, else why not
    enum Cull : std::uint8_t { kDrawn, kFar, kNotListed, kUnitHidden, kOutside, kCameraInside, kOverCap } cull = kDrawn;
    // split1 (§29.5): the same for player 2's view in a split screen, CB3TrafficSystem::Draw(1): player 2's list (race
    // car index gWorld+0x12AD50+0x27D0+0x19AC), track 1's units, player 2's camera (kDrawn outside split screen)
    bool listed2 = true;
    Cull cull2 = kDrawn;
    std::uint8_t listings2 = 1;  // split2: how often it is in player 2's list (Vehicle::listings for view 2)
    // split2 (§29.6): view 1's own per-vehicle results in a split screen. Draw(lePlayer) and RenderRaceCars(view) write
    // them per view (traffic +0xCA unit, +0xCB frustum, +0x17A camera inside; crashed +0x13BA / +0x13BB; race cars
    // +0x5A / +0x5B / +0x10A2), so at the poll they hold view 2's (Render(1) runs last); the native capture copies view
    // 1's at CB3World::Render(1)'s entry (scene_capture.cpp, host record 'HSV1'). v1_known: that copy holds this vehicle;
    // then unit / frustum / flags2 above stay view 2's and these are view 1's. v1_extra: race cars' +0x10A2.
    bool v1_known = false;
    std::int8_t v1_unit = -1, v1_frustum = -1;
    std::uint8_t v1_flags2 = 0, v1_extra = 0;
    // split2: RenderRaceCars(1)'s own-car test for view 2 (+0x27C0 == 1, player 2's IsPlayerVisible; cam1's rule)
    bool own_hidden2 = false;
    // traffic-popin (view.h traffic_since): renderer only, set by frame_at -- the world clock (Tick::now) of the tick from
    // which this traffic car has been within the 275 m draw radius without a break; -1 = since before the timeline (or not traffic)
    float since = -1.0f;
    float gone = -1.0f;          // traffic-popin: a removed traffic car's last tick's clock (frame_at adds it fading out), else -1
    // split2 (§29.6): since / gone for player 2's view of a split screen (its own 275 m radius, Vehicle::cull2); a
    // fading-out copy removed within one view's radius only has the other view's gone at -1 (hidden there)
    float since2 = -1.0f, gone2 = -1.0f;
    // cam1 (docs/native-renderer.md §26): race cars, RenderRaceCars 0x199A20's first test: the view's own car (+0x1914 == 0,
    // +0x27C0 == the view) is left out while its camera manager's IsPlayerVisible is false (flags +0xB80 bit 4 / 5: the
    // look-back camera holds bit 4) [C draft calls and offsets from the asm]. Game state, so applied with or without V.
    bool own_hidden = false;
    // Shadow inputs (capture version 3, fx1; docs/native-renderer.md §14.2): what CB3ShadowMgr::RenderShadows 0x3033F0
    // passes to RenderAmbientShadow 0x303170 [C drafts]
    struct Shadow {
        bool cast = false;       // RenderShadows draws it: race car +0x1914 != 0 or the player visible; traffic in the
                                 // view's list, +0x174 bit 2 / +0x17A bit 1 clear, frustum +0x5B != 0; crashed +0x13BB
        Mat34 ess;               // EssentialParams +0x00 (RenderAmbientShadow reads its at row +0x20 and position +0x30)
        float normal[3] = {0, 1, 0};  // ground normal: race car +0x510 / crashed +0x1870 xyz; traffic EssentialParams +0x10
        float dist = 0;          // distance to the ground: +0x510 / +0x1870 w; traffic (gWorld+0x4DE0+0x343F0)[+0x176] +0x18
        float darkness = 0;      // EssentialParams +0x60 (x krShadowDarkness, at most 1)
        float radius = 0;        // vehicle data +0x18 (wheel radius)
        float sup[3] = {}, inf[3] = {};  // vehicle data +0xE80 / +0xE90 (the shadow box)
        float front_z = 0, rear_z = 0;   // vehicle data +0xBB0 + 0x40 k, z of wheel 1 and wheel n-2 (the wheel base)
        bool cast2 = false;      // split2: RenderShadows(1)'s decision (player 2's IsPlayerVisible and list, view 2's results)
    } shadow;
    // Corona inputs (corona1, §16): what CB3EffectsManager::RenderLightCoronas 0x256690 and func_00257230 (boost glow)
    // read [C asm]. `submit` is the game's decision that the vehicle's immediate effects run this frame: race cars in
    // RenderRaceCars 0x199A20's drawn list with +0x18EA clear; traffic and crashed vehicles drawn by
    // CB3TrafficSystem::Draw (cull == kDrawn).
    struct Corona {
        bool valid = false;      // the capture holds the corona inputs (corona1 captures)
        bool submit = false;
        std::uint8_t lights = 0; // EssentialParams +0x58, every bit (RenderLightCoronas runs when nonzero)
        bool rear_red = false;   // race car +0x18F0: lbRearIndicatorsAreRed (Prepare: car id +0x1960 one of the 2 at 0x4B6170)
        bool red_boost = false;  // race car +0x18F1: car id one of the 5 at 0x4B6180 -> "coronaboostred" (array 2); retail_data_names
                                 // labels 0x4B6180 kaRedIndicatorCarIDs, but its E-map size (2 ids) fits 0x4B6170 [L, §16.1]
        float boost = 0;         // CB3BoostEffectParams (+0x118C) +0x14: func_0030AAE0's intensity t (race cars)
        Mat34 zone[8];           // race car +0x580 (VehicleParams +0x500): the body zones the exhaust items blend through
        float sort = 0;          // RenderRaceCars' sort key (descending: far cars draw first)
        bool submit2 = false;    // split2: the same decisions for view 2 (Draw(1), RenderRaceCars(1, 1))
        float sort2 = 0;
    } corona;
    std::shared_ptr<const CoronaTables> corona_tables;   // its vehicle data's tables (nullptr: not captured)
};

// One corona slot as the game writes it (32 bytes): position xyz, w = the fade distance (400 lights, 800 / 400 / 200
// boost); colour x 64 (GS units) xyz, w = the size (world metres across) [C asm 0x256FD0, 0x257080, 0x257230; the VU1
// program 0x478260 reads them as such, §16].
struct CoronaSlot { float pos[4] = {}, col[4] = {}; };

// Constants the corona code reads (static data: captured once, kept in Capture) [C addresses].
struct CoronaConsts {
    bool valid = false;
    float light[6][3] = {};      // D_004BBFE0, F0, D_004BC000, 10, 20, 30 (RenderLightCoronas' colours)
    float tl[5][3] = {};         // D_004C4980, 90, A0 (traffic-light colours), B0, C0 (offsets) (static init 0x4D6D00)
    float boost[2][3] = {};      // D_004C4A10 (array 1), D_004C4A20 (array 2, red cars) (static init 0x4D6DF0)
    float boost_size[2] = {};    // D_004E0E94 (0.6), D_004E09E4 (0.72)
    float boost_min = 0;         // D_004E0C6C (0.01): func_00257230 returns unless the size is above it
    float boost_k[5] = {};       // D_004E0F44 (0.56), F48 (0.28), F4C (0.14), F50 (0.08), F54 (0.18)
    int capacity[3] = {126, 72, 72};    // _gaCoronaParams 0x4BC900
    std::string texture[3];      // its names ("coronaglow", "coronaboost", "coronaboostred")
};

// Tick-level corona inputs (corona1).
struct CoronaInputs {
    bool valid = false;
    std::shared_ptr<const CoronaConsts> k;   // Capture::corona_k
    bool cars_on = true;         // D_00665F26 (graphics manager +0x66): CB3RaceCar::Draw returns when 0
    bool traffic_on = false;     // CB3TrafficSystem::Draw runs: its track data (+0x0) set and D_00665F25
    float cam_point[3] = {};     // effects manager +0x1F810: the camera position - 2 x its at axis (func_002622A0)
    std::uint32_t seed = 0;      // effects manager +0x1F820 (EndUpdate 0x2623F0): func_00257230's vrinit seed
    std::uint32_t scales[4] = {};   // D_01D9D190 (gEffectsRandomiserScales) at the poll: after the frame's rotations
    // traffic lights (CB3TrafficSystem::Draw 0x3078F0 [L draft]): per light its state (+0x14) and corona indices
    struct TrafficLight { std::uint8_t state = 0, count = 0, idx[4] = {}; };
    std::vector<TrafficLight> tlights;
    std::shared_ptr<const std::vector<CoronaTables::Light>> tl_coronas;   // track traffic data +0xAC (+0xB0 of them)
    std::vector<std::uint32_t> assigned;   // traffic system +0x36320: bit set = a light owns that corona (31 per word)
    bool tl_flat = false;        // traffic system +0x363BA: nonzero = no side offsets
    // camera record 0 as UploadCodeAndStaticData 0x306E80 uploads it (VU1 1015..1023; the VU check)
    bool vu_cam_valid = false;
    b3asset::CoronaCamera vu_cam;
    // B3PORT_SCENE_CORONA_CHECK captures: what the game wrote (player 0, the half EndCoronaSubmit closed)
    bool check = false;
    int half[3] = {-1, -1, -1};  // array +0x2E[0]: flips at every BeginUpdate (once per rendered frame)
    std::vector<CoronaSlot> game[3];
};


// A skid-mark emitter (CB3TrailEmitter, capture version 3): its point data as CB3TrailRenderer::Render 0x2F3600 sends
// it to VU1 0x4771B0 (emitter +0x8: 16 x V4-32 point xyz / time w, then 16 x 3 s8 at +0x100). Shared between ticks
// while its bytes do not change (the capture keeps one copy per change).
struct TrailEmitter {
    std::uint32_t id = 0;        // guest address of the emitter
    int type = 0;                // the list it is in (EB3TrailType 0..4)
    int count = 0;               // +0x10 s8 point count (drawn when >= 2)
    std::uint8_t flags = 0;      // +0x12
    float last = 0;              // +0x0 last time
    float pt[16][4] = {};
    std::int8_t dir[16][3] = {};
};

// Contact effects (fx2, docs/native-renderer.md §19): scrapes, sparks and popcorn, decoded from the grab_contact_effects
// block of scene_capture.cpp (no version change; ContactFx::*_valid tell whether the capture holds each part).
//
// A scrape emitter (CB3ScrapeEmitter, 0x448 bytes inside CB3ScrapeSystem) as CB3ScrapeRenderer::Render 0x375F90 reads
// it: +0x0 & 3 indexes karScrapeUVs, and the segment buffer the last render drew (+0x444 at the poll: Render draws +0x440
// and then swaps the two, so +0x444 holds what it drew [L code order 0x375AA0 / 0x3755E0 / 0x375BF0]). Segment k:
// +0x10 + 0x20 k position (y raised by D_004E0FA0 = 0.0254 when added), +0x20 + 0x20 k side vector (direction x normal).
struct ScrapeEmitter {
    std::uint32_t id = 0;        // guest address
    int uv = 0;                  // +0x0 (a random value & 3, CB3ScrapeSystem::AddScrapeSegment 0x375840)
    std::uint16_t flags = 0;     // +0x10
    int count = 0;               // the drawn buffer's +0x0 (<= 16; drawn when >= 2)
    int pending = 0;             // the other buffer's (+0x440) count: segments added since that render
    float seg[16][2][4] = {};    // position, side
    bool operator==(const ScrapeEmitter& o) const {
        return id == o.id && uv == o.uv && flags == o.flags && count == o.count && pending == o.pending &&
               std::memcmp(seg, o.seg, sizeof seg) == 0;
    }
};

// One CB3SparkArray (gEffectsManager + 0x21F0 + 0xA8 a) with its params and every ring's record blocks, raw (the spark
// VU model, b3asset/sparkshade.h, reads them). Shared between ticks while unchanged.
struct SparkArrayState {
    std::uint8_t head[0xA8] = {};      // banks +0x0 / +0x4C: 4 slots {base, capacity, write index, lifetime}, +0x40, +0x44, +0x48
    std::uint8_t params[0x7C] = {};    // CB3SparkArrayParams (+0x98 ->, CB3SparkArrayBase::mgaArrayParams)
    std::vector<std::uint8_t> dma[2];  // each bank's static DMA head [+0x48, slot 0 base)
    std::vector<std::uint8_t> rec[2][4];   // bank, ring: capacity / 4 blocks of 0x90 (4 times, 4 x {position, velocity})
    bool operator==(const SparkArrayState& o) const {
        if (std::memcmp(head, o.head, sizeof head) != 0 || std::memcmp(params, o.params, sizeof params) != 0) return false;
        for (int b = 0; b < 2; ++b) {
            if (dma[b] != o.dma[b]) return false;
            for (int k = 0; k < 4; ++k) if (rec[b][k] != o.rec[b][k]) return false;
        }
        return true;
    }
};

// One CB3PopcornArray (gEffectsManager + 0x20F0 + 0x40 a) and its record blocks, raw.
struct PopcornArrayState {
    std::uint8_t head[0x40] = {};
    std::vector<std::uint8_t> rec;     // (capacity + 3) / 4 blocks of 0x90
    bool operator==(const PopcornArrayState& o) const { return std::memcmp(head, o.head, sizeof head) == 0 && rec == o.rec; }
};

// One CB3ParticleArray (gEffectsManager + 0x170 + 0x150 a, effects3) with its two banks' record allocations (capacity x
// 28 + (capacity + 59) / 60 x 4 bytes each), raw: the game's own VU1 program 0x46D750 reads them (b3asset/particleshade.h).
struct ParticleArrayState {
    std::uint8_t head[0x150] = {};
    std::uint8_t params[0x80] = {};    // the array's CB3ParticleArrayParams (head +0xE4 ->, maStandardParams 0x49C570)
    char tex[16] = {};                 // params +0x0: the texture name
    std::vector<std::uint8_t> rec[2];  // normal, crash bank
    bool operator==(const ParticleArrayState& o) const {
        return std::memcmp(head, o.head, sizeof head) == 0 && std::memcmp(params, o.params, sizeof params) == 0 &&
               std::memcmp(tex, o.tex, sizeof tex) == 0 && rec[0] == o.rec[0] && rec[1] == o.rec[1];
    }
};

using SparkHistory = std::array<std::uint8_t, 18 * 0x60>;

struct ContactFx {
    // scrapes [C asm 0x2620C0, 0x375AA0, 0x375F90]: drawn when the view's race car +0x18EA is set
    bool scrapes_valid = false;
    bool scrape_gate = false;          // player 0's race car (gWorld + 0x12AD50) +0x18EA
    std::uint8_t scrape_params[0x50] = {};   // CB3ScrapeSystem::mgaDefaultParams D_01EA4910
    float scrape_uvs[5] = {};          // karScrapeUVs 0x4C8FF0
    std::vector<std::shared_ptr<const ScrapeEmitter>> scrapes;   // the type-0 list (+0x8A8C) in list order
    // sparks
    bool sparks_valid = false;
    std::uint8_t spark_statics[0x10] = {};   // 0x4E2A00: mnNumRenderableArrays, mnRenderPlayer, mrMaxMotionBlurTime, mbIsCrash
    std::uint8_t spark_d1e31a20[0x10] = {};  // D_01E31A20 (uploaded with the camera history, UploadStaticDataAndCallCode)
    std::uint32_t cam_index = 0;             // D_00667050: the camera record index
    bool history_valid = false;              // the whole camera history was captured this tick
    std::shared_ptr<const SparkHistory> history;   // D_01E31A30[0]: 18 x CSparkFrameData (the newest only unless history_valid)
    std::shared_ptr<const SparkArrayState> sparks[6];
    // popcorn
    bool popcorn_valid = false;
    std::uint8_t popcorn_params[4 * 0x50] = {};   // CB3PopcornArrayBase::maStandardParams 0x4A4DB0
    std::shared_ptr<const PopcornArrayState> popcorn[4];
    // particles (effects3): CB3ParticleArray x 24, the program image, the camera record they are projected with
    bool particles_valid = false;
    bool particle_skip = false;                  // D_004E29E4 (RenderParticleArray: arrays without params +0x5D are skipped when set)
    std::shared_ptr<const std::vector<std::uint8_t>> particle_prog;   // vu_game_0046D750 (0x1980 bytes)
    std::shared_ptr<const std::array<std::uint8_t, 0x230>> particle_cam;   // the camera record D_00665EC0 + 0x470 + 0x230 x D_00667050
    std::shared_ptr<const ParticleArrayState> particles[24];
};

struct Camera {
    bool valid = false;
    Mat34 m;                     // RwFrame modelling matrix of the viewport's camera: right (screen left), up, at, pos
    float half_w = 0, half_h = 0;  // CGtViewport +0x48 / +0x4C: view window at distance 1
    float znear = 0, zfar = 0;   // CGtViewport +0x84 / +0x88
    float fov = 0;               // CGtCamera +0x4 (degrees, horizontal: +0x48 = scale * tan(fov / 2))
    float screen[4][4] = {};     // CGtViewport +0x00: the game's own world -> screen matrix (x / z, y / z in 0..1)
    // LOD choice (CB3RaceCar::Draw 0x151A40 [C], CB3TrafficSystem::Draw 0x3078F0 [L]): the CGtCamera position (+0x68 ->
    // +0x20), the viewport's distance scale +0x90 and gGraphicsManager +0x70 = karDefaultLodSwitchDistances {15, 30, 60,
    // 20, 80} (SetDefaultLODSwitchDistances 0x19E9A0 [C]).
    float lod_pos[3] = {};
    float lod_scale = 1.0f;
    float lod_dist[5] = {15, 30, 60, 20, 80};
};

// A prop off its placement: an entry of the prop manager's dynamic list (0x64A854 +0x1C, CB3Prop {u16 placement, u8
// type, u8 state}) drawn by CB3PropManager::Render at the manager's live copy of its placement (+0x4, 0x40 each), which
// the prop body (CB3PropPhysics) moves in place [C Render 0x2F99E0, CB3PhysicsManager::Prepare 0x1BA020..0x1BA0A0].
struct Prop {
    int placement = 0;           // STATIC.DAT +0x48 placement index
    int type = 0;                // prop type (STATIC +0x3C)
    Mat34 m;                     // its CB3InstanceData rows X, Y, Z, T now
    int slot = -1;               // the CB3PropPhysics moving it (physics manager +0xC3900 + 0x780 k), -1 = none (at rest)
};

// Crash-mode game data (crash2, §12.9): CB3GameData gWorld+0x125B40 as CB3GameData::Draw 0x14E8C0 reads it [C asm].
struct CrashRampState {
    int type = 0;                // its CB3CrashModeRampStruct +0x40
    float corner[6][3] = {};     // CB3CrashRamp +0x00..+0x50: Prepare's six points, the ones Draw sends
    float m[4][3] = {};          // the struct's matrix (rows X, Y, Z, position)
};
struct CrashPickupState {
    float pos[3] = {};           // its CB3CrashModePickupStruct +0x00 (Draw's lPos)
    int type = 0;                // struct +0x10: the texture index (CB3CrashPickup::Prepare 0x3A17A0 [C matched])
    bool taken = false;          // CB3CrashPickup +0x8: Draw returns at once [C asm 0x3A0B2C]
};
// perf1 (docs/native-renderer.md §33): one start / finish line or split checkpoint as CB3SplitTimeCheckpoint::DrawCheckpoint
// 0x15E340 draws it: a vertical B3Im3d quad across the road, centre = the trigger's position + (0, 11, 0), half height
// up x D_004E1568 (5.0), half width side x D_004E156C (10.0), side = normalise(up x the trigger's direction) [C asm].
struct FinishLineState {
    float pos[3] = {};           // CB3PlanarTriggerStruct +0x00
    float dir[3] = {};           // +0x10
    float half_h = 5.0f, half_w = 10.0f;
    int texture = 0;             // 0 "finish" (the line's +0x8), 1 "check" (+0x4) (GLOBAL.TXD)
    std::uint32_t rgba = 0x80404040u;   // the colour x 128 (Draw: (0.5, 0.5, 0.5, 1); splits (0.375, 0.125, 0.125, 1))
};
struct CrashState {
    bool valid = false;          // the capture holds CB3GameData (crash2 captures)
    std::uint64_t stage = 0;     // CB3GameData +0x558: the loaded stage's directory id in GAMEDATA.BGD
    bool logic = false;          // D_0051BA88 +0x1B4 nonzero: Draw draws pickups and ramps only then [C 0x14E8C0]
    bool ramps_off = false;      // D_00665EA1 nonzero: CB3CrashRamp::Draw returns at once [C asm 0x3A18D8]
    std::vector<CrashRampState> ramps;       // track data +0x3FC of them
    std::vector<CrashPickupState> pickups;   // +0x3F8 slots; the ones Prepare filled (data pointer set)
    float cam_quat[4] = {};      // player 1's camera manager CGtQuatPos (gGame +0x29010) the pickups face
    float cam_pos[3] = {};
    // func_0021C010 0x21C010, right after CB3GameData::Draw: the STATIC +0x28 backdrop group (barriers and arrow signs)
    // entry `barrier_group` (the backdrop block's +0x6D) when the streamed track is on, +0x6D >= 0, D_00665F1E, the
    // view's gWorld+0x12AD50 block +0x18EA == 0, D_00665E53 == 0, and not (IsCrashMode and the logic's slot 0xAC
    // nonzero) [C asm]. -1: not known (an older capture, or a logic virtual of a form not decoded)
    int barriers = -1;
    int barrier_group = -1;
    int crash_mode = -1;         // func_00131E40 (IsCrashMode [L E-map]): logic slot 0x98 == 6 or slot 0x9C in 3..5
    // perf1: CB3GameData::Draw(0)'s start / finish lines and split checkpoints (finish_known false: an older capture, or a
    // value not decoded; nothing is drawn then)
    bool finish_known = false;
    std::vector<FinishLineState> finish;
};

// An animated track material as the game holds it (anim1, §15): CB3TrackMaterial +0x10..+0x1F of an entry of the static
// track data's update list, which CB3StaticTrack::Update 0x3329A0 advances every tick [C asm].
struct AnimMaterial {
    int material = -1;           // its index in the STATIC.DAT material table (the list's s16)
    std::int8_t cur = 0;         // +0x10 current frame
    std::int8_t p12 = 0, p13 = 0;  // +0x12 frame number, +0x13 direction
    float period = 0;            // +0x18
    float t = 0;                 // +0x1C
};

// CB3SunRenderer::SetupCamera 0x24D110's inputs from the main view's camera record (graphics manager +0x470, record 0)
// and the graphics manager's +0 (CB3EnvironmentManager::Prepare 0x2BCA20 multiplies ENVIRO +0x90 by it into the corona
// radius) [L nonmatching drafts; read from the asm]. No capture change: the region was captured since render4.
struct SunCam {
    bool valid = false;
    int gm0 = 0;                 // graphics manager +0 (int)
    float pos[3] = {};           // record +0xD0: the camera position (SetupCamera's pos for view 0)
    float m[4][4] = {};          // record +0xE0: CGtMatrix3x4 (rows X, Y, Z, T), TransformPoint -> x / z, y / z in 0..1
    float f200 = 0, f208 = 0, f20C = 0;   // aspect = f200 x f20C x sizeY / sizeX; FOV scale f208 (blob size)
    float size[2] = {};          // +0x218 / +0x21C: screen size in pixels
    float offset[2] = {};        // +0x220 / +0x224: screen offset
};

// The boost radial blur's parameters (water1, §21): func_0030A9D0 (called by CB3OnePlayerStage::Render 0x134A20 on
// &D_00666090 = graphics manager +0x1D0) fills the block through CB3BoostEffectParams::GetRadialBlurParams 0x30ABA0, and
// CB3World::Render passes D_00666274's pointer to CB3RadialBlur::Render 0x2FA9A0 [C matched callers, L drafts]. The
// region is in the capture since render4 (graphics manager [0, 0x3C0)).
struct RadialBlurIn {
    bool valid = false;
    float centre[2] = {};        // +0x00 / +0x04: fractions of the view (0.5 = the middle)
    float radius[2] = {};        // +0x08 / +0x0C: the mask's extent (fraction of the half-size buffer's width / height)
    float uv_scale[2] = {};      // +0x10 / +0x14: BlurOut's zoom towards the centre (squared after each of 3 passes)
    float colour = 1;            // +0x18: CopyToScreen RGB x 128
    float amount = 0;            // +0x1C: CopyToScreen alpha x 128 (lerp(0, 1, boost))
    bool flip = false;           // +0x30: SetAlpha's upper half mirrored too (the car's 0x200 / 0x2000 state)
    std::uint32_t texture = 0;   // +0x40: "radialblurmask" (Prepare 0x2FAD90)
    bool on = true;              // D_00665F2A (graphics manager +0x6A): Render returns when 0
    bool crashed = false;        // the view's race car +0x18EA: CB3World::Render skips the radial blur
};

// The post passes (post1, docs/native-renderer.md §25). CB3World::Render 0x276300 ends with CB3Bloom::Render on
// D_00666274 + 0x50 and `UpdateCamera(D_00666274 + 0x60) ? CB3DirectionalBlur::Render : (car +0x18EA == 0 ?
// CB3RadialBlur::Render)`; D_00666274 points at view 1's params (graphics manager +0x1D0) [C values: Prepare's defaults
// at +0x240..+0x24C]. The params and UpdateCamera's state are in every capture (graphics manager [0, 0x3C0)); the rest of
// UpdateCamera's inputs only in post1 captures (`inputs`).
struct PostIn {
    bool valid = false;
    // CB3Bloom (+0x220) [C Render 0x2659C0 matched; L FirstDownsample / CopyToScreen / Blend drafts]
    bool bloom_on = false;       // D_00665F28 (graphics manager +0x68): Render returns when 0
    float bloom_threshold = 0;   // +0x0: c = int(255 x it + 0.5), at most 254 (FirstDownsample)
    float bloom_intensity = 0;   // +0x4: CopyToScreen's RGBAQ = int(128 x it) (clamped 255): frame += P x it
    float bloom_feedback = 0;    // +0x8: Blend keeps P x int(118 x it) / 128
    bool bloom_reset = false;    // D_005179E6[idx x 0xBB0] (Render's 2nd bool): Blend writes Q alone (post1 captures)
    bool bloom_freeze = false;   // D_0051BA94 != -1 (Render's 1st bool): Blend leaves P as it is
    // CB3DirectionalBlurBase (+0x230), as the last UpdateCamera left it [C Prepare 0x426030 matched; L UpdateCamera draft]
    bool dir_on = false;         // D_00665F2A (graphics manager +0x6A): CB3DirectionalBlur::Render returns when 0
    float dir_point[3] = {};     // +0x00: the camera point 10 m ahead (position + 10 x at)
    float dir_centre[2] = {};    // +0x10 / +0x14: the blur's centre, fractions of the view (often far outside it)
    float dir_zoom[2] = {};      // +0x18 / +0x1C: BlurOut's zoom (BlurIn takes its inverse), squared after each pass
    float dir_amount = 0;        // +0x20: CopyToScreen's FIX = int(128 x it + 0.5); Render needs it >= 1/255
    float dir_time = 0;          // +0x24: D_004F50A0 at that call
    // UpdateCamera's other inputs (post1 captures only)
    bool inputs = false;
    std::uint32_t flags = 0;     // D_005179C0[idx] +0x0: 0x2000 no crash pulse, 0x400 motion blur without boost, 4 / 0x20
    float boost_1c = 0, boost_20 = 0;   // the player block's CB3BoostEffectParams +0x1C / +0x20 (func_0030A990's terms)
    std::uint8_t boost_on = 0;   // +0x52
    bool has_crash_obj = false;  // player block +0x2430 (a pointer; 0: UpdateCamera returns 0)
    float crash_time = 0;        // its +0x3534
    float now = 0;               // D_004F50A0
    float world_clock = 0;       // D_00522680 (gWorld + 0x20)
    bool split_crash = false;    // CB3World::Render skips the directional blur: Is2PlayerSplitScreen && IsCrashMode && !logic 0xAC
    // what CB3World::Render draws this tick (decided from the above; -1 = not decidable: a capture before post1)
    int choice = -1;             // 0 neither, 1 directional, 2 radial
    int branch = -1;             // UpdateCamera's path: 0 returned 0, 1 crash pulse, 2 motion
};

// gsoff1 split screen (§29): player 2's view of a 2-player split-screen frame: viewport +0x130's camera (player 1's,
// Tick::cam, is +0x90's). Valid only in split-screen frames (Tick::split).
struct View2In {
    bool valid = false;
    Camera cam;
    // split1 (§29.5): streamed track 1 (gWorld+0x12A514), the units CB3World::Render(1) draws player 2's view from, as
    // Tick's track fields hold track 0's; track_valid only when the capture holds it (split1 captures)
    bool track_valid = false;
    bool track_on = false;
    int current_unit = -1;
    int slot_unit[17], slot_kind[17];
    std::uint16_t vis[17] = {};
    std::int8_t backdrops[8];
    int barriers = -1, barrier_group = -1;   // func_0021C010(track 1)'s decision (CrashState's, for view 2)
    int env_unit[17];                        // reflect-tunnel: Tick::env_unit / env_car for view 2 (track 1, player 2's car)
    std::uint32_t env_car = 0;
    // CB3TrafficSystem::Draw(1)'s inputs as Tick's for Draw(0): track 1's safe-area slots and their visibility, and
    // whether player 2's traffic list was captured (Vehicle::listed2 / cull2 hold the per-vehicle results)
    std::uint32_t unit_visible = 0, safe_units = 0;
    bool traffic_lists = false;
    // split2 (§29.6): view 2's post passes, as Tick::post / blur hold view 1's: CB3World::Render(1) opens viewport 2,
    // whose params are graphics manager +0x270 (radial blur +0x00, bloom +0x50, directional blur state +0x60) [C
    // OpenViewport 0x227360: case 2], with D_005179C0 + 0xBB0, player 2's block gWorld+0x12AD50+0x27D0
    PostIn post;
    RadialBlurIn blur;
    // player 2's camera manager's IsPlayerVisible (0x4EE040 + 0x28E00 + 0xBB0; true when not captured)
    bool player_visible = true;
    View2In() { for (int i = 0; i < 17; ++i) slot_unit[i] = slot_kind[i] = env_unit[i] = -1; for (auto& b : backdrops) b = -1; }
};

// ghost1 (Time Attack ghosts, src/b3replay/ghost.h): view 0's lap as its CB3Score (race car block gWorld+0x12AD50 +
// 0x10C0) holds it [C CB3Score::Update 0x15B000, IncrementLapCount 0x15A1F0: +0x14 the lap (-1 until the start line,
// 0.. after; back to 0 after 8), +0x2C4 the lap count, +0x2C8 the lap's time = race time - the time at the lap's start,
// updated each racing tick; at the tick the line is crossed it still holds the finished lap's time]. No capture change:
// perf1's grabs of +0x10D4 and +0x1384 take their 16-byte blocks [+0x10D0, +0x10E0) and [+0x1380, +0x1390).
struct LapState {
    bool valid = false;          // the capture holds those blocks
    bool time_attack = false;    // the stage logic is CB3OfflineBurningLapLogic (Time Attack / Burning Lap; vtable 0x4DEBC0)
    int index = -1;              // +0x14
    int laps = 0;                // +0x2C4
    float time = 0;              // +0x2C8 (seconds)
};
// speedrun1 (speedrun.h): view 0's event timing as its CB3Score (race car block gWorld+0x12AD50 + 0x10C0) holds it
// [C CB3Score::Update 0x15B000 (nonmatching, logic complete), IncrementLapCount 0x15A1F0, SetFinished 0x151780 ->
// func_00155A40]: +0x8 the finish state SetFinished gave (0 while running; 1 finished, 4 the other finish
// IncrementLapCount gives; 2 / 3 out -- the race time then FLT_MAX), +0xC the race time = the game timer (gWorld+0x20)
// - the start time (+0x280), 0 until the start signal, updated each racing tick and frozen from the finish (state 3
// returns at once), +0x14 the lap (-1 until the start line), +0x18 the laps completed (stops at 8 when the lap index
// wraps: +0x2C1), +0x1C float[8] each lap's time (the lap timer +0x2C8 at its line), +0x3C the best lap, +0x27C the race
// state (0 countdown, 1 start, 2 racing, 3 finished), +0x2C4 the lap count (0: no end, Time Attack). Capture: the
// blocks [+0x10C0, +0x1100) and [+0x1330, +0x1350) (scene_capture.cpp, reads only), and perf1's [+0x1380, +0x1390).
struct ScoreState {
    bool valid = false;
    std::uint32_t logic = 0;     // the stage logic's vtable (0: none captured)
    int state = -1;              // +0x27C
    int finish = 0;              // +0x8
    float time = 0;              // +0xC (seconds; FLT_MAX after an out)
    int lap = -1;                // +0x14
    int laps_done = 0;           // +0x18
    float lap_times[8] = {};     // +0x1C
    float best_lap = 0;          // +0x3C
    bool wrapped = false;        // +0x2C1
    int laps = 0;                // +0x2C4
    float lap_time = 0;          // +0x2C8
};
// The race car block of view 0 (the player's race car: Vehicle::id) and the Time Attack logic's vtable [L name
// research/retail_data_names.csv, C: the vtable seen in the SINGLE EVENT > TIME ATTACK runs, ghost1]
constexpr std::uint32_t kPlayerRaceCar = 0x522660u + 0x12AD50u;
constexpr std::uint32_t kBurningLapLogicVtable = 0x4DEBC0u;

struct Tick {
    std::uint32_t poll = 0, vblank = 0;
    int ticks_per_frame = 1;     // CB3Game +0x2DA84
    Camera cam;
    bool split = false;          // gsoff1: Is2PlayerSplitScreen (*0x51BA88 == 0x51AEC0): two views, view2 player 2's
    View2In view2;
    bool view1_snap = false;     // split2: the capture's copy of view 1's per-vehicle results is this poll's (Vehicle::v1_*)
    SunCam sun;                  // water1 (§21): the sun corona's camera inputs
    RadialBlurIn blur;           // water1 (§21): the boost radial blur's parameters
    PostIn post;                 // post1 (§25): the bloom and the directional blur, the blur choice
    // route fingerprint (STATIC.DAT header in guest memory): file size +0x04, unit count +0x54
    std::uint32_t route_size = 0;
    int route_units = 0;
    // gsoff1 (§28.6): the STATIC.DAT header itself [0, 0x60) and where the game loaded it (*0x64C994); route_by_header
    // tells the route from it as soon as the game has the file (its loading screen), with no camera samples needed
    std::uint32_t route_base = 0;
    bool route_head_ok = false;
    std::array<std::uint8_t, 0x60> route_head{};
    bool track_on = false;       // CB3StreamedTrack +0x44
    int current_unit = -1;       // the camera unit (high block at +0x1C4)
    int slot_unit[17], slot_kind[17];   // +0x7C blocks: unit index, 0 = high / 1 = LOD (-1 = empty)
    std::uint16_t vis[17] = {};  // +0x58: 2 bits per camera (camera 0 = main)
    std::int8_t backdrops[8];    // current unit's high block +0x64
    // reflect-tunnel: what the game draws into the car's env-map cameras 1..3 (RenderBeforeVehicles 0x21C780 [L draft],
    // gated by D_00665F20 = graphics manager +0x60, 1 from Prepare 0x19EBF0 [C matched]): per slot the +0xC4 (LOD)
    // block's unit (-1 none) where vis bits 2..7 are set; the env map's centre is race car [gWorld+0x12AD50+0x19AC]
    // (BuildCustomCameraData(cam +0x19AC) 0x223130: D_0064D388 = gWorld+0x12AD28 [L draft])
    int env_unit[17];
    std::uint32_t env_car = 0;   // that race car's guest address (Vehicle::id), 0 = none / not captured
    int env_track = -1;          // D_00665F20 (-1 not captured)
    std::vector<Vehicle> vehicles;
    // CB3TrafficSystem::Draw's inputs beyond the vehicles (render6): the streamed track's IsUnitVisibleToMainCamera per
    // safe-area slot (bit k = slot k visible; ForEveryUnitInSafeArea's 9 slots), and the player's traffic list
    std::uint32_t unit_visible = 0, safe_units = 0;
    bool traffic_lists = false;   // the capture holds the lists (render6 captures)
    // props (capture version 2): placements not drawn at rest (their per-unit CB3Prop has state 2: dynamised, then
    // gone when its body is released) and the dynamic props drawn at their live matrices
    bool props_valid = false;
    // the stage logic's slot 0xB8 (D_0051BA88 +0x1B4): nonzero = no props in this mode (CB3PropManager::Render returns
    // at once; CB3OfflineRoadRageLogic); -1 = not captured or not a constant-return function
    int props_off = -1;
    // RenderWater's switch (live2 §20): D_00665F1D, -1 if the capture does not hold 0x665F10 (older ones: drawn)
    int water_on = -1;
    std::uint32_t logic_vtable = 0;   // the stage logic's vtable (names: decomp research/retail_data_names.csv)
    std::vector<std::uint16_t> hidden;   // sorted
    std::vector<Prop> props;
    int prop_placements = 0, prop_types = 0, prop_listed = 0;   // manager +0x10, +0x12; CB3Props in the per-unit lists
    CrashState crash;            // crash2
    // animated track materials (anim1): the update list (static track 0x64C990 +4 -> data +0x10, s16[+0x0E]) and each
    // entry's runtime fields, in list order; anim_valid only when the capture holds all of them (anim1 captures)
    bool anim_valid = false;
    std::vector<AnimMaterial> anim;
    // effects (capture version 3, fx1)
    bool effects_valid = false;
    bool shadows_on = false;     // D_00665F27: RenderShadows returns when 0
    bool effects_on = false;     // D_00665F2C: RenderPreFogEffects (skid marks) returns when 0
    bool player_visible = true;  // CB3PlayerCameraManager::IsPlayerVisible 0x2E7340 of view 0, from its captured inputs
    float shadow_k = 3.3333333f; // krShadowDarkness D_004E0FC0
    float now = 0;               // gWorld+0x20 (the VU program's time, CB3TrailRenderer::BeginRender); every version
    std::uint32_t trail_vu[5][12] = {};  // D_01E865E0: the per-type VU1 parameter rows (VU 0x3F0..0x3F2)
    std::uint32_t trail_alpha[5] = {};   // mgaDefaultParams +0x20: ALPHA_1 selectors (low byte), FIX (top byte)
    char trail_name[5][16] = {};         // mgaDefaultParams +0x0: texture name ("fxskid")
    std::uint32_t trail_cam[10][4] = {}; // camera record 0 (graphics manager +0x470) +0x120: the VU block BeginRender
                                         // uploads to 1014..1023 (only the VU reference check uses it)
    std::vector<std::shared_ptr<const TrailEmitter>> trails;   // every listed emitter, by type then list order
    CoronaInputs coronas;        // corona1 (§16); the vehicles' parts are in Vehicle::corona
    ContactFx contact;           // fx2 (§19): scrapes, sparks, popcorn
    // HUD / 2D (hud1, §17): the Gt2d call stream of the frame drawn before this poll (shared while unchanged); null
    // when the capture has no HUD region (captures before hud1, or B3PORT_SCENE_HUD=0)
    std::shared_ptr<const HudFrame> hud;
    std::shared_ptr<const HostSettings> host_settings;   // settings1: our PC settings record (null: none captured)
    std::shared_ptr<const HostFonts> host_fonts;         // mac5: the game's 2D fonts (null: none captured)
    bool host_hold = false;                              // autosave1: the display holds the previous tick's picture (hud.h kHostHoldBase)
    std::uint8_t skip_hint = 0;                          // mac5: the video skip hint's opacity (hud.h kHostSkipBase; 0 hidden)
    std::shared_ptr<const b3live::MenuRecord> menu;      // mac5: the game's menus on screen, for the mouse (null: none captured)
    std::shared_ptr<const b3mod::OverlayList> mod_overlay;   // sdk2b: the code mods' 2D (codemod.h 'HMO1'; null: none captured)
    // menu1 (§22): the front end's car (CB3World::RenderFrontEnd's, FECAR events): its guest address, 0 none; the
    // vehicle itself is in `vehicles` (a race car; drawn with the camera above, viewport 1)
    std::uint32_t fe_car = 0;
    LapState lap;                // ghost1: view 0's lap (Time Attack ghosts)
    ScoreState score;            // speedrun1: view 0's event timing (the speedrun timer)
    Tick() { for (int i = 0; i < 17; ++i) slot_unit[i] = slot_kind[i] = env_unit[i] = -1; for (auto& b : backdrops) b = -1; }
};

struct Segment { std::size_t first = 0, last = 0; std::uint32_t route_size = 0; int route_units = 0; };

// The sparse guest memory image a reader rebuilds from the capture's runs (16-byte blocks; 4 KB pages).
class Image {
public:
    Image();
    ~Image();
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    void clear();
    void write(std::uint32_t a, const std::uint8_t* p, std::uint32_t n);   // n a multiple of 16, a 16-aligned
    bool read(std::uint32_t a, void* out, std::uint32_t n) const;          // true if every byte of [a, a+n) was captured
    bool has(std::uint32_t a, std::uint32_t n) const;
    std::uint32_t u32(std::uint32_t a) const;
    std::uint16_t u16(std::uint32_t a) const;
    std::uint8_t u8(std::uint32_t a) const;
    float f32(std::uint32_t a) const;
    bool mat(std::uint32_t a, Mat34& m) const;   // a CGtMatrix3x4 / RwMatrix: four 16-byte rows, w lanes ignored
private:
    struct Pages;
    Pages* p_;
};

using ModelHead = std::array<std::uint8_t, 0x70>;

// A tick counts for a race segment: track on, a camera, a route, the camera's unit and at least one race car.
// gsoff1 (§28.6): the route under <game>/TRACKS whose STATIC.DAT header [0, 0x60) is the one in guest memory: every u32
// equal to the file's, or to the file's plus the load address (an offset the game relocated). V1 / V2 of a route share
// size and unit count but not their headers. Empty unless exactly one route matches (or the tick has no header).
std::string route_by_header(const std::string& game, const Tick& t);
bool race_tick(const Tick& t);

// menu1 (§22): the front-end segments of `ticks`: maximal runs outside `race` whose ticks hold a HUD frame, at least
// kMinFrontEndTicks long. Capture::load fills Capture::fe_segments with it; the live viewer cuts its own.
constexpr std::size_t kMinFrontEndTicks = 30;
std::vector<Segment> frontend_segments(const std::vector<Tick>& ticks, const std::vector<Segment>& race);

struct Capture {
    std::vector<Tick> ticks;
    std::map<std::uint32_t, ModelHead> models;           // vehicle data header by guest address (the last one seen)
    std::map<std::uint32_t, std::string> model_raster;   // its atlas raster's name (raster +0xA8)
    std::vector<ModelHead> heads;                        // every distinct header seen (Vehicle::model indexes it)
    std::map<ModelHead, int> head_index;
    std::vector<Segment> segments;   // runs of ticks with a race on one route (track on, a camera, vehicles)
    // menu1 (docs/native-renderer.md §22): the front end's stretches, the runs of ticks between (and around) the race
    // segments that hold a HUD / 2D frame (menus, event select, loading screens, results after the race view ends);
    // route_size / route_units 0. Drawn 2D-only (no route).
    std::vector<Segment> fe_segments;
    std::uint64_t runs = 0, bytes = 0;
    std::uint32_t version = 0;
    // skid marks: CB3TrailSystem::mgaDefaultParams 0x49FFF0 (texture name +0x0, ALPHA / FIX word +0x20)
    std::string trail_texture[5];
    std::uint32_t trail_alpha[5] = {};
    std::map<std::uint32_t, std::shared_ptr<const TrailEmitter>> trail_last;   // decode: the last copy per emitter
    // coronas (corona1): the static constants, and the last copy of each vehicle data's tables / the traffic-light list
    std::shared_ptr<const CoronaConsts> corona_k;
    std::map<std::uint32_t, std::shared_ptr<const CoronaTables>> corona_last;
    std::shared_ptr<const std::vector<CoronaTables::Light>> tl_last;
    // contact effects (fx2): the last copy of each scrape emitter, spark / popcorn array and the camera history
    std::map<std::uint32_t, std::shared_ptr<const ScrapeEmitter>> scrape_last;
    std::shared_ptr<const SparkArrayState> spark_last[6];
    std::shared_ptr<const PopcornArrayState> popcorn_last[4];
    std::shared_ptr<const ParticleArrayState> particle_last[24];
    std::shared_ptr<const std::vector<std::uint8_t>> particle_prog_last;
    std::shared_ptr<const std::array<std::uint8_t, 0x230>> particle_cam_last;
    std::shared_ptr<const SparkHistory> history_last;
    // props check: dynamic-list props whose CB3PropPhysics +0x204 points at the manager's copy of their placement
    std::uint64_t prop_body_checked = 0, prop_body_mismatch = 0;
    // HUD (hud1): the decoder and its textures (guest rasters decoded from the image on first use)
    HudDecoder hud;
    std::shared_ptr<const HudFrame> hud_last;
    std::shared_ptr<const HostSettings> host_settings_last;   // settings1
    std::shared_ptr<const HostFonts> host_fonts_last;         // mac5
    std::shared_ptr<const b3live::MenuRecord> menu_last;      // mac5
    std::shared_ptr<const b3mod::OverlayList> mod_overlay_last;   // sdk2b
    // fill_gaps = false keeps every tick as decoded (no segment gap filling; for comparing with the live feed).
    // poll_from / poll_to (live2): only the ticks of polls in [poll_from, poll_to] are decoded and kept (the runs before
    // them still build the image), so one stretch of a long session loads without every tick's decoded state (race1:
    // 53,760 ticks took 7.9 GB).
    bool load(const std::string& path, std::string* why, bool fill_gaps = true, std::uint32_t poll_from = 0,
              std::uint32_t poll_to = 0xFFFFFFFFu);
    // One record as the file holds it ('TICK', poll, vblank, run count, runs) applied to `im`, then the tick decoded
    // from `im` (the live feed, b3replay --live). False if the record is malformed (the image is then incomplete).
    bool apply_record(Image& im, const std::uint8_t* rec, std::size_t n, Tick& out);
    void decode(const Image& im, Tick& t);
};

// True if two decoded ticks hold the same values (vehicle models compared by their header bytes); else `what` names
// the first difference.
bool same_tick(const Tick& a, const Capture& ca, const Tick& b, const Capture& cb, std::string* what);

// The game's LOD for a vehicle seen from `cam` (EVehicleLOD 0 lowest .. 3 highest): race cars LOD 3 / 2 / 1 / 0 when
// |car - camera| <= 15 / 30 / 60 x viewport +0x90 (strict > tests: CB3RaceCar::Draw 0x151A40 [C matched]); traffic and
// crashed vehicles 2 / 1 / 0 at <= 20 / 80 x +0x90 (CB3TrafficSystem::Draw 0x3078F0 [L draft from the asm]).
// RenderVehicle then takes the nearest LOD block the file has (first upwards to 4, then down) [L draft 0x21E6F0].
int game_lod(const Camera& cam, const Vehicle& v);

// CB3TrafficSystem::Draw 0x3078F0's choice of the traffic and crashed vehicles it renders [L draft from the asm; tests
// and constants C asm]: traffic in the player race car's list, active, within 275 m of the CGtCamera (75625 = 0x12769
// converted, 0x307A94) unless crashing (+0x174 bit 2), in a safe-area unit the main camera sees (crashing: not tested), frustum
// result (EssentialParams +0x5B, written by the same Draw) nonzero, camera not inside (+0x17A bit 1); then the 8 nearest
// crashing and the 32 - those nearest others. Crashed vehicles: within 275 m, unit seen, frustum nonzero or +0x2402,
// the 32 nearest. Sets Vehicle::cull (kFar is the 275 m test alone; the later reasons are only set when it passes).
// Capture::load / decode call it per tick.
void game_draws(Tick& t);

}  // namespace b3replay

#endif
