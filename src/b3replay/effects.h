// b3replay effects stage (fx1, docs/native-renderer.md §14): the game's effects drawn from a capture's version-3 state,
// after the track and the cars as CB3World::Render 0x276300 orders them (shadows, then the pre-fog effects). Shared by
// the offline replay and the live renderer through View.
//
// Skid marks (docs/effects-survey.md §3.2): each listed CB3TrailEmitter's ribbon as VU1 0x4771B0 builds it
// (b3asset/trailshade.h), at the displayed time, with the trail type's texture and ALPHA_1, Z not written.
//
// Scrapes (fx2, §19): each CB3ScrapeEmitter's strip, after the skid marks, as RenderPreFogEffects 0x2620C0 orders them.
//
// Car shadows (docs/effects-survey.md §3.1): the game's own geometry rebuilt from the captured inputs, per vehicle the
// 8-vertex B3Im3d strip of CB3ShadowRenderer::RenderAmbientVehicleShadow 0x303A40 in the frame RenderAmbientShadow
// 0x303170 builds, with the GS state of BeginShadowPlaneRendering 0x303C10: GLOBAL.TXD "blobbyshadow", ALPHA_1 0x44
// ((Cs - Cd) As + Cd), TEST_1 0x50003 (Z GEQUAL), ZBUF ZMSK, FRAME FBMSK 0xFF000000, CLAMP_1 5.
#ifndef B3REPLAY_EFFECTS_H
#define B3REPLAY_EFFECTS_H

#include <cstdint>
#include <future>
#include <map>
#include <string>
#include <vector>

#include "b3asset/particleshade.h"
#include "b3gfx/b3gfx.h"
#include "capture.h"
#include "sparks.h"   // ParticleDraw / ParticleBuildStats (the worker-built particle layer)

namespace b3replay {

// RenderAmbientShadow 0x303170 + RenderAmbientVehicleShadow 0x303A40 [L drafts, every op from the asm]: the strip's 8
// world-space vertices (RGB 0x80, alpha = int(lerp(0, 90, darkness) x opacity)) in the game's order; false when the
// game draws nothing for it (opacity <= 0: more than 1.25 m above the wheel radius). `k` = krShadowDarkness.
bool shadow_strip(const Vehicle::Shadow& s, float k, b3gfx::Vertex out[8]);
// fe2 (§24): CB3ShadowMgr::RenderFrontEndShadow 0x303750's strip for the front end's car (`s`: its at row, box and wheel
// base; the ground, distance and darkness are not used).
void frontend_shadow_strip(const Vehicle::Shadow& s, b3gfx::Vertex out[8]);

// Scrapes (fx2, docs/native-renderer.md §19.1): CB3ScrapeRenderer::Render 0x375F90's tristrip for one emitter, 2 x count
// world-space vertices in the game's order (empty when count < 2) [C asm, every op]. Per segment k: colour =
// lerp(params +0x0, params +0x10, 0) x 128 -> FTOI0, min (255, 255, 255, 128); half width = (params +0x34 + 0 x (+0x38 -
// +0x34)) x 0.5; offset = side x half x karScrapeUVs[emitter +0x0 & 3], offset.y += D_004E0FA0 (0.0254); vertices
// position -+ offset with ST (k & 1, 0) and (k & 1, uv). B3Im3d sends them as they are (RenderTristrip 0x365040).
void scrape_strip(const ContactFx& c, const ScrapeEmitter& e, std::vector<b3gfx::Vertex>& out);

struct EffectStats {
    int shadows = 0;              // strips drawn this frame
    int trail_emitters = 0, trail_points = 0;   // listed emitters with >= 2 points, their points
    int scrape_emitters = 0, scrape_segments = 0;   // fx2: drawn scrape strips, their segments
    int sparks = 0;               // fx2: spark sprites drawn
    int popcorn = 0;              // fx2: popcorn sprites drawn
    int particles = 0;            // effects3: particle quads and triangles drawn
    int explosion_flash = 0;      // the crash explosion's flash fill: its alpha byte (0: none this frame)
};

// Per-frame inputs of the effects (from the frame's tick).
struct FrameEffects {
    bool valid = false;           // the capture holds them (version 3 / the live feed)
    bool shadows_on = false, effects_on = false;
    float shadow_k = 3.3333333f;
    float now = 0;                // gWorld+0x20 at the displayed time (blended between ticks)
    std::vector<std::shared_ptr<const TrailEmitter>> trails;
    std::uint32_t trail_vu[5][12] = {};
    std::uint32_t trail_alpha[5] = {};
    char trail_name[5][16] = {};
    CoronaInputs coronas;         // corona1 (§16): the tick's corona inputs (the vehicles' parts are in Vehicle::corona)
    ContactFx contact;            // fx2 (§19): the tick's scrapes, sparks, popcorn
    std::uint32_t spark_cam914[4] = {};   // camera record 0 (+0x170, +0x174, +0x178, +0x168): the spark program's VU 914
    std::uint32_t trail_cam[10][4] = {};  // camera record 0 +0x120 (popcorn: vu1DataXYZScale / Shift rows)
};

class Effects {
public:
    // GLOBAL.TXD textures ("blobbyshadow", the trails' "fxskid"); call after the device's resources were cleared.
    bool load(b3gfx::Device& dev, const std::string& game, std::string* why);
    void clear();   // forget GPU ids (Device::clear_resources released them)
    // Appends the effects stage's draws (DrawItem::effect) for frame `f`.
    // `fades` (traffic-popin, optional): per vehicle, the screen-door fade its car is drawn with; its shadow's alpha follows
    void draw(b3gfx::Device& dev, const std::vector<Vehicle>& vehicles, const FrameEffects& fx, std::vector<b3gfx::DrawItem>& out,
              EffectStats* stats = nullptr, const std::vector<float>* fades = nullptr);
    b3gfx::TextureId texture(b3gfx::Device& dev, const std::string& name);   // GLOBAL.TXD by name, once
    // fe2 (§24): RenderFrontEndShadow's strip as an effects-stage draw (the state of RenderShadows' strips).
    void draw_frontend_shadow(b3gfx::Device& dev, const Vehicle::Shadow& s, std::vector<b3gfx::DrawItem>& out);
    bool particles = true;        // fx2: scrapes / sparks / popcorn / particles (View::show_particles: --no-particles, K)
    // perf1 (§33): the live window builds a new tick's spark quads on a worker and draws the last finished build until it
    // is ready (a crash's 50,000 quads take up to 10 ms); offline, the bench and shots build synchronously (identical)
    bool async_sparks = false;
    Effects() = default;
    Effects(Effects&&) = default;              // (split screen swaps two views' effects; a pending build moves along)
    Effects& operator=(Effects&&) = default;
    ~Effects();
    bool shadows = true, trails = true, scrapes = true;   // settings1: one row each
    int sparks = 3, popcorn = 3;   // effects2: the levels of SPARKS and CRASH DEBRIS (b3settings kLevel*: 0 off .. 3 high, the whole set)
    // Sparks (fx2, §19.2): the post-fog spark quads of frame `fx` (after the coronas, RenderPostFogEffects 0x261E00).
    void draw_sparks(b3gfx::Device& dev, const FrameEffects& fx, std::vector<b3gfx::DrawItem>& out, EffectStats* stats = nullptr);
    // Popcorn (fx2, §19.3): first of the post-fog effects (before the particles and coronas), only while the player's
    // crash flag is set; `game` = the tick's game camera (the program's set-up), `view` = the displayed camera.
    void draw_popcorn(b3gfx::Device& dev, const FrameEffects& fx, const Camera& game, const Camera& view, std::vector<b3gfx::DrawItem>& out,
                      EffectStats* stats = nullptr);

    // Particles (effects3, §19.10): CB3ParticleRenderer after the popcorn and before the coronas, the game's own program's
    // quads (see build_particles); rebuilt once per tick. Levels: arrays 0..5 and the crash banks follow `popcorn`
    // (CRASH DEBRIS), the smoke / fire / explosion arrays `sparks` (SPARKS).
    void draw_particles(b3gfx::Device& dev, const FrameEffects& fx, std::vector<b3gfx::DrawItem>& out, EffectStats* stats = nullptr);
    // The crash explosion's full-screen flash (§19.4, a11y.h explosion_flash): RenderPostFogEffects 0x261E00 draws it after the
    // sparks while the player's crash flag is set, the flag 0x2000 is clear and the crash time (+0x3534) is positive:
    // one additive full-view fill from 0.025 s to 0.145 s after it. `crash_time` / `crash_flag` / `flags` are the view's
    // (PostIn); `cam` = the displayed camera, `aspect` the render's width / height; `reduce` = SCREEN FLASHES Reduced.
    // Returns the fill's alpha byte (0: none drawn).
    int draw_explosion_flash(b3gfx::Device& dev, const FrameEffects& fx, const PostIn& post, bool crashed, const Camera& cam, float aspect,
                             bool reduce, std::vector<b3gfx::DrawItem>& out);

private:
    void push_shadows(b3gfx::Device& dev, std::vector<b3gfx::DrawItem>& out);   // verts_ / idx_ as one draw
    struct Trail { b3gfx::BufferId vb = b3gfx::kNone, ib = b3gfx::kNone; std::vector<b3gfx::Vertex> verts; std::vector<std::uint32_t> idx; };
    Trail trail_[5];
    std::vector<std::uint8_t> txd_;
    std::map<std::string, b3gfx::TextureId> textures_;
    b3gfx::TextureId shadow_tex_ = b3gfx::kNone;
    b3gfx::BufferId shadow_vb_ = b3gfx::kNone, shadow_ib_ = b3gfx::kNone;
    std::vector<b3gfx::Vertex> verts_;
    std::vector<std::uint32_t> idx_;
    Trail scrape_;                // fx2: every scrape strip of the frame (one draw: one texture and state)
    Trail spark_;                 // fx2: every spark quad of the frame
    // perf1 (§33): the spark quads depend only on the tick (its contact effects and camera record), not on the displayed
    // time, so they are built and uploaded once per tick. The key keeps the tick's objects alive (no address reuse).
    struct SparkKey {
        std::shared_ptr<const SparkHistory> history;
        std::shared_ptr<const SparkArrayState> arrays[6];
        std::uint8_t statics[0x10] = {}, d1e31a20[0x10] = {};
        std::uint32_t cam914[4] = {};
        bool valid = false, history_valid = false;
        int level = 3;   // effects2: the level the quads were built at
        bool same(const ContactFx& c, const std::uint32_t cam[4], int level) const;
    } spark_key_;
    bool spark_cached_ = false;
    int spark_quads_ = 0;
    struct SparkBuild { std::vector<b3gfx::Vertex> verts; std::vector<std::uint32_t> idx; int quads = 0; bool ok = false; };
    std::future<SparkBuild> spark_job_;   // perf1: the worker's build (async_sparks)
    SparkKey spark_job_key_;
    void set_spark_key(SparkKey& k, const ContactFx& c, const std::uint32_t cam[4], int level);
    Trail popcorn_;               // fx2: every popcorn sprite of the frame (one draw per array)
    // effects3: the particle quads depend on the tick only (its captured arrays and camera record), so they are built and
    // uploaded once per tick; the key keeps the tick's objects alive
    Trail particle_;                      // staging (verts / idx) of the tick's build; vb is the one persistent vertex buffer
    b3gfx::BufferId particle_ident_ = b3gfx::kNone;   // 0, 1, 2 ...: the index buffer the flat (expanded) vertices are drawn with, grown only
    std::size_t particle_ident_n_ = 0;
    std::vector<b3gfx::Vertex> particle_flat_;
    b3asset::ParticleVu particle_vu_;
    std::shared_ptr<const std::vector<std::uint8_t>> particle_prog_;
    struct ParticleKey {
        std::shared_ptr<const ParticleArrayState> arrays[24];
        std::shared_ptr<const std::array<std::uint8_t, 0x230>> cam;
        bool valid = false, crash = false, skip = false;
        int smoke = 3, debris = 3;
    } particle_key_;
    ParticleKey particle_job_key_;        // the key the running worker build is for (async_sparks)
    struct ParticleBuild {
        std::vector<b3gfx::Vertex> verts;
        std::vector<std::uint32_t> idx;
        std::vector<ParticleDraw> draws;
        ParticleBuildStats stats;
        long long us = 0;
        bool ok = false;
    };
    std::future<ParticleBuild> particle_job_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> particle_ranges_;
    struct PDraw { std::string texture; std::uint64_t alpha = 0, test = 0; std::uint32_t first = 0, count = 0; };
    std::vector<PDraw> particle_draws_;
    int particle_prims_ = 0;
    Trail flash_;                 // the explosion flash's quad
};

}  // namespace b3replay

#endif
