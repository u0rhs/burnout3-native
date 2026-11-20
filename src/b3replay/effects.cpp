#include <chrono>
#include "effects.h"

#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

#include "b3asset/enviro.h"
#include "b3asset/export.h"
#include "b3asset/raster.h"
#include "a11y.h"
#include "b3asset/trailshade.h"
#include "sparks.h"
#include "../b3view/scene.h"   // hdkey: upload_texture
#include "b3report/thread_name.h"   // split2p: the workers named (last: it includes windows.h)

namespace b3replay {

namespace {

void cross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

b3gfx::TextureId load_global(b3gfx::Device& dev, const std::vector<std::uint8_t>& txd, const char* name) {
    b3asset::GsMemory gs;
    b3asset::DecodedRaster r;
    if (!b3asset::find_txd_raster(b3asset::Bytes{txd.data(), txd.size()}, name, gs, r)) return b3gfx::kNone;
    return b3view::upload_texture(dev, r.levels, r.original);   // hdkey: and its original
}

}  // namespace

namespace {

// RenderAmbientVehicleShadow 0x303A40's strip in the frame up / right = normalise(up x forward) / at = right x up
// (CrossProduct = a x b, [C] GtMathPs2.h vopmula/vopmsub) at `pos`, for the box and wheel base scaled by `sc` and the
// vertex alpha byte `alpha` (its float args f12, f13: alpha = int((f13 x 90 + 0) x f12) [L draft 0x303A40]).
void shadow_strip_frame(const float up[3], const float fwd[3], const float pos[3], const Vehicle::Shadow& s, float sc, int alpha,
                        b3gfx::Vertex out[8]) {
    float right[3], at[3];
    cross(up, fwd, right);
    const float l = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (l > 0) for (float& c : right) c /= l;
    cross(right, up, at);
    const float sx = s.sup[0] * sc, ix = s.inf[0] * sc, sz = s.sup[2] * sc, iz = s.inf[2] * sc;
    const float fz = s.front_z * sc, rz = s.rear_z * sc;         // lWheelBase mSup.z, mInf.z
    // RenderAmbientVehicleShadow 0x303A40: (x, 0.06, z) in that frame, V bands 0 / 0.1875 / 0.8125 / 1 from
    // BeginShadowPlaneRendering 0x303C10
    const float h = 0.06f;
    const float corner[8][2] = {{ix, sz}, {sx, sz}, {ix, fz}, {sx, fz}, {ix, rz}, {sx, rz}, {ix, iz}, {sx, iz}};
    const float uv[8][2] = {{0, 0}, {1, 0}, {0, 0.1875f}, {1, 0.1875f}, {0, 0.8125f}, {1, 0.8125f}, {0, 1}, {1, 1}};
    for (int v = 0; v < 8; ++v) {
        b3gfx::Vertex& o = out[v];
        std::memset(&o, 0, sizeof o);
        for (int c = 0; c < 3; ++c) o.pos[c] = corner[v][0] * right[c] + h * up[c] + corner[v][1] * at[c] + pos[c];
        std::memcpy(o.normal, up, sizeof o.normal);
        o.uv[0] = uv[v][0];
        o.uv[1] = uv[v][1];
        o.rgba = 0x808080u | (std::uint32_t(alpha & 0xFF) << 24);   // the byte the program sends (u8 store)
    }
}

}  // namespace

bool shadow_strip(const Vehicle::Shadow& s, float k, b3gfx::Vertex out[8]) {
    // RenderAmbientShadow 0x303170
    const float darkness = std::min(1.0f, k * s.darkness);
    const float offset = s.dist - s.radius;                      // lrHeightAboveOffset
    const float opacity = 1.0f - std::min(offset / 1.25f, 1.0f);
    if (opacity <= 0.0f) return false;
    // the frame: up = the ground normal, forward = the car's at row, position = the car's lowered by the distance;
    // the box (+0xE80 sup, +0xE90 inf) and the wheel base (z of wheels 1 / n-2), y = 0, scaled by 1 + 0.4 x offset
    const float pos[3] = {s.ess.r[3][0], s.ess.r[3][1] - s.dist, s.ess.r[3][2]};
    shadow_strip_frame(s.normal, s.ess.r[2], pos, s, 1.0f + 0.4f * offset, int((0.0f + (90.0f - 0.0f) * darkness) * opacity), out);
    return true;
}

void frontend_shadow_strip(const Vehicle::Shadow& s, b3gfx::Vertex out[8]) {
    // CB3ShadowMgr::RenderFrontEndShadow 0x303750 [L nonmatching C, every constant C asm]: up (0, 1, 0), forward the
    // car's at row (+0x20), position (0, 0.005, 0) (not the car's: the front end's car stands at the origin), the box
    // and wheel base unscaled, RenderAmbientVehicleShadow(m, box, base, 1, 1, 0): alpha int((1 x 90 + 0) x 1) = 90
    const float up[3] = {0, 1, 0}, pos[3] = {0, 0.005f, 0};
    shadow_strip_frame(up, s.ess.r[2], pos, s, 1.0f, 90, out);
}

void scrape_strip(const ContactFx& c, const ScrapeEmitter& e, std::vector<b3gfx::Vertex>& out) {
    if (e.count < 2) return;
    float col0[4], col1[4], w34, w38;
    std::memcpy(col0, c.scrape_params + 0x00, 16);
    std::memcpy(col1, c.scrape_params + 0x10, 16);
    std::memcpy(&w34, c.scrape_params + 0x34, 4);
    std::memcpy(&w38, c.scrape_params + 0x38, 4);
    const float t = 0.0f;                                      // vf10 = 0: the colour never fades [C 0x376004]
    const float uv = c.scrape_uvs[e.uv & 3];
    const float lift = 0.0254f;                                // D_004E0FA0 [C ELF]
    std::uint32_t rgba = 0;
    for (int ch = 0; ch < 4; ++ch) {
        const float v = (col0[ch] - col0[ch] * t + col1[ch] * t) * 128.0f;   // vaddax / vmsubax / vmaddx, vmulx 128
        int q = v >= 2147483648.0f ? 0x7FFFFFFF : v <= -2147483648.0f ? int(0x80000000u) : int(v);   // FTOI0
        q = std::min(q, ch == 3 ? 128 : 255);                  // pminw with (255, 255, 255, 128)
        rgba |= std::uint32_t(q & 0xFF) << (8 * ch);           // ppach / ppacb
    }
    const float half = ((0.0f + w34) + 0.0f * (w38 - w34)) * 0.5f;   // adda.s / madd.s, mul.s 0.5
    for (int k = 0; k < e.count; ++k) {
        const float* p = e.seg[k][0];
        const float* sd = e.seg[k][1];
        float off[3] = {sd[0] * half * uv, sd[1] * half * uv, sd[2] * half * uv};
        off[1] = off[1] + lift;
        for (int side = 0; side < 2; ++side) {
            b3gfx::Vertex o;
            std::memset(&o, 0, sizeof o);
            for (int a = 0; a < 3; ++a) o.pos[a] = side == 0 ? p[a] - off[a] : p[a] + off[a];
            o.normal[1] = 1;
            o.uv[0] = float(k & 1);
            o.uv[1] = side == 0 ? 0.0f : uv;
            o.rgba = rgba;
            out.push_back(o);
        }
    }
}

bool Effects::load(b3gfx::Device& dev, const std::string& game, std::string* why) {
    clear();
    epoch_ = dev.resource_epoch();
    std::vector<std::uint8_t> txd;
    if (!b3asset::read_file(game + "/DATA/GLOBAL.TXD", txd)) { if (why) *why = "cannot read " + game + "/DATA/GLOBAL.TXD"; return false; }
    shadow_tex_ = load_global(dev, txd, "blobbyshadow");   // D_004C4550 [C BeginShadowPlaneRendering]
    if (shadow_tex_ == b3gfx::kNone) { if (why) *why = "GLOBAL.TXD has no blobbyshadow"; return false; }
    txd_ = std::move(txd);
    return true;
}

bool Effects::load_shared(b3gfx::Device& dev, const Effects& from) {
    clear();
    epoch_ = dev.resource_epoch();
    if (from.txd_.empty() || from.shadow_tex_ == b3gfx::kNone) return false;
    txd_ = from.txd_;
    shadow_tex_ = from.shadow_tex_;   // the same texture (both go with the device's resources)
    return true;
}

void Effects::sync(b3gfx::Device& dev) {
    if (dev.resource_epoch() == epoch_) return;
    clear();
    epoch_ = dev.resource_epoch();
}

void Effects::clear() {
    shadow_tex_ = b3gfx::kNone;
    shadow_vb_ = shadow_ib_ = b3gfx::kNone;
    for (auto& t : trail_) t = Trail{};
    scrape_ = Trail{};
    spark_ = Trail{};
    if (spark_job_.valid()) spark_job_.wait();   // perf1: a worker's build is for the old buffers: dropped
    spark_job_ = {};
    spark_key_ = SparkKey{};   // perf1: the cached quads' buffers are gone
    spark_cached_ = false;
    popcorn_ = Trail{};
    particle_ = Trail{};   // effects3: the buffers are gone: rebuilt at the next draw
    particle_ident_ = b3gfx::kNone;
    particle_ident_n_ = 0;
    particle_key_ = ParticleKey{};
    particle_draws_.clear();
    flash_ = Trail{};
    headlight_ = Trail{};
    textures_.clear();
    txd_.clear();
}

b3gfx::TextureId Effects::texture(b3gfx::Device& dev, const std::string& name) {
    sync(dev);   // ids from before a Device::clear_resources are dropped
    const auto it = textures_.find(name);
    if (it != textures_.end()) return it->second;
    const b3gfx::TextureId t = textures_[name] = txd_.empty() || name.empty() ? b3gfx::kNone : load_global(dev, txd_, name.c_str());
    if (t != b3gfx::kNone) add_warm_names({name});
    return t;
}

namespace {
std::mutex g_warm_m;
std::set<std::string> g_warm_names;   // perf4
}  // namespace
std::set<std::string> Effects::warm_names() {
    std::lock_guard<std::mutex> lk(g_warm_m);
    return g_warm_names;
}
void Effects::add_warm_names(const std::set<std::string>& names) {
    std::lock_guard<std::mutex> lk(g_warm_m);
    g_warm_names.insert(names.begin(), names.end());
}
void Effects::drop_warm_name(const std::string& name) {
    std::lock_guard<std::mutex> lk(g_warm_m);
    g_warm_names.erase(name);
}
void Effects::preload(b3gfx::Device& dev) {
    if (const char* e = std::getenv("B3_PIPELINE_WARM"); e && e[0] == '0') return;   // (the warm-up's off switch, as in b3gfx)
    for (const std::string& n : warm_names())
        if (texture(dev, n) == b3gfx::kNone) drop_warm_name(n);   // not in this GLOBAL.TXD (any more): not saved again
}

void Effects::push_shadows(b3gfx::Device& dev, std::vector<b3gfx::DrawItem>& out) {
    if (!verts_.empty() && shadow_tex_ != b3gfx::kNone) {
        dev.update_vertex_buffer(shadow_vb_, verts_.data(), verts_.size());
        dev.update_index_buffer(shadow_ib_, idx_.data(), idx_.size());
        b3gfx::DrawItem d;
        d.vertices = shadow_vb_;
        d.indices = shadow_ib_;
        d.index_count = std::uint32_t(idx_.size());
        d.texture = shadow_tex_;
        d.blend = b3gfx::Blend::kAlpha;
        d.shading = b3gfx::Shading::kTrack;   // MODULATE, TEST_1, fog: the track model's GS stage on untransformed colours
        d.track_type = 0;
        d.gs_test = 0x50003;                  // ATE, ATST ALWAYS, ZTE, ZTST GEQUAL
        d.lighting = 0;
        d.effect = true;
        d.gs_alpha = 0x44;
        d.clamp_uv = true;                    // CLAMP_1 = 5
        out.push_back(d);
    }
}

void Effects::draw_frontend_shadow(b3gfx::Device& dev, const Vehicle::Shadow& s, std::vector<b3gfx::DrawItem>& out) {
    sync(dev);   // ids from before a Device::clear_resources are dropped
    verts_.clear();
    idx_.clear();
    b3gfx::Vertex v[8];
    frontend_shadow_strip(s, v);
    verts_.insert(verts_.end(), v, v + 8);
    for (std::uint32_t t = 0; t < 6; ++t) { idx_.push_back(t); idx_.push_back(t + 1); idx_.push_back(t + 2); }
    push_shadows(dev, out);
}

void Effects::draw(b3gfx::Device& dev, const std::vector<Vehicle>& vehicles, const FrameEffects& fx, std::vector<b3gfx::DrawItem>& out,
                   EffectStats* stats, const std::vector<float>* fades) {
    sync(dev);   // ids from before a Device::clear_resources are dropped
    if (!fx.valid) return;
    // CB3ShadowMgr::RenderShadows 0x3033F0: race cars, then traffic, then crashed vehicles (the order of the frame's
    // vehicle list; strips do not overlap in a way the order changes: same texture, one blend)
    verts_.clear();
    idx_.clear();
    if (fx.shadows_on && shadows) {   // settings1: CAR SHADOWS
        for (std::size_t n = 0; n < vehicles.size(); ++n) {
            const Vehicle& v = vehicles[n];
            if (!v.shadow.cast) continue;
            b3gfx::Vertex s[8];
            if (!shadow_strip(v.shadow, fx.shadow_k, s)) continue;
            if (fades && n < fades->size() && (*fades)[n] < 1.0f) {   // traffic-popin: the shadow fades with its car
                const float a = (*fades)[n];
                if (!(a > 0.0f)) continue;
                for (auto& q : s) q.rgba = (q.rgba & 0xFFFFFFu) | (std::uint32_t(float(q.rgba >> 24) * a) << 24);
            }
            const std::uint32_t b = std::uint32_t(verts_.size());
            verts_.insert(verts_.end(), s, s + 8);
            for (std::uint32_t t = 0; t < 6; ++t) { idx_.push_back(b + t); idx_.push_back(b + t + 1); idx_.push_back(b + t + 2); }
        }
    }
    if (stats) stats->shadows = int(verts_.size() / 8);
    push_shadows(dev, out);
    // Skid marks: CB3EffectsManager::RenderPreFogEffects 0x2620C0 -> func_002BD560 -> CB3TrailRenderer::Render 0x2F3600
    // per type 0..4 (list order), each emitter's ribbon as VU1 0x4771B0 builds it (b3asset::trail_ribbon, bit-exact
    // against the program: b3vucheck_trail), evaluated at the displayed time; the GPU projects and clips. One draw per
    // type: texture mgaDefaultParams +0x0 ("fxskid"), ALPHA_1 from +0x20, ZBUF ZMSK [C BeginRender 0x2F38B0].
    if (!fx.effects_on) return;
    for (int ty = 0; ty < 5 && trails; ++ty) {   // settings1: SKID MARKS
        Trail& tr = trail_[ty];
        tr.verts.clear();
        tr.idx.clear();
        for (const auto& e : fx.trails) {
            if (e->type != ty || e->count < 2) continue;
            b3asset::TrailEmitterInput in;
            in.count = e->count;
            std::memcpy(in.point, e->pt, sizeof in.point);
            std::memcpy(in.side, e->dir, sizeof in.side);
            const b3asset::TrailRibbon rb = b3asset::trail_ribbon(fx.trail_vu[ty], fx.now, in);
            if (rb.points < 2) continue;
            if (stats) { ++stats->trail_emitters; stats->trail_points += rb.points; }
            const std::uint32_t b = std::uint32_t(tr.verts.size());
            for (const auto& v : rb.verts) {
                b3gfx::Vertex o;
                std::memset(&o, 0, sizeof o);
                std::memcpy(o.pos, v.pos, sizeof o.pos);
                o.normal[1] = 1;
                o.uv[0] = v.s;
                o.uv[1] = v.t;
                o.rgba = std::uint32_t(v.rgba[0]) | std::uint32_t(v.rgba[1]) << 8 | std::uint32_t(v.rgba[2]) << 16 | std::uint32_t(v.rgba[3]) << 24;
                tr.verts.push_back(o);
            }
            for (std::uint32_t k = 0; k + 2 < std::uint32_t(rb.verts.size()); ++k) { tr.idx.push_back(b + k); tr.idx.push_back(b + k + 1); tr.idx.push_back(b + k + 2); }
        }
        if (tr.idx.empty()) continue;
        const b3gfx::TextureId tex = texture(dev, std::string(fx.trail_name[ty], strnlen(fx.trail_name[ty], 16)));
        dev.update_vertex_buffer(tr.vb, tr.verts.data(), tr.verts.size());
        dev.update_index_buffer(tr.ib, tr.idx.data(), tr.idx.size());
        b3gfx::DrawItem d;
        d.vertices = tr.vb;
        d.indices = tr.ib;
        d.index_count = std::uint32_t(tr.idx.size());
        d.texture = tex;
        d.textured = tex != b3gfx::kNone;
        d.blend = b3gfx::Blend::kAlpha;
        d.shading = b3gfx::Shading::kTrack;
        d.gs_test = 0;                        // the trail code sets no TEST_1 [C 0x2F38B0 / 0x2F3600]: no alpha test [L]
        d.lighting = 0;
        d.effect = true;
        d.gs_alpha = std::uint8_t(fx.trail_alpha[ty] & 0xFF);
        if (((d.gs_alpha >> 4) & 3) == 2) d.blend_fix = float(fx.trail_alpha[ty] >> 24) / 128.0f;   // C = FIX
        d.clamp_uv = true;                    // s, t stay in [0, 1]; CLAMP_1 at that point not known [G]
        out.push_back(d);
    }
    // Scrapes (fx2, §19.1): RenderPreFogEffects 0x2620C0 -> CB3ScrapeSystem::Render 0x375AA0 when the view's race car
    // +0x18EA is set: CB3ScrapeRenderer::BeginRender 0x376260 (B3Im3d::BeginRendering 0x365640's state: ALPHA_1 0x44, TEST_1
    // 0x5000B with AREF 0 (_grATestThreshold = 0), CLAMP_1 0 = repeat; then SetZWrite(false)), the texture named at params
    // +0x20 ("fxscrape"), one RenderTristrip per listed emitter in list order [C asm].
    const ContactFx& c = fx.contact;
    if (!particles || !scrapes || !c.scrapes_valid || !c.scrape_gate) return;   // settings1: SCRAPES
    scrape_.verts.clear();
    scrape_.idx.clear();
    for (const auto& e : c.scrapes) {
        const std::uint32_t b = std::uint32_t(scrape_.verts.size());
        scrape_strip(c, *e, scrape_.verts);
        const std::uint32_t n = std::uint32_t(scrape_.verts.size()) - b;
        if (n == 0) continue;
        if (stats) { ++stats->scrape_emitters; stats->scrape_segments += e->count; }
        for (std::uint32_t k = 0; k + 2 < n; ++k) { scrape_.idx.push_back(b + k); scrape_.idx.push_back(b + k + 1); scrape_.idx.push_back(b + k + 2); }
    }
    if (scrape_.idx.empty()) return;
    const char* name = reinterpret_cast<const char*>(c.scrape_params + 0x20);
    const b3gfx::TextureId tex = texture(dev, std::string(name, strnlen(name, 12)));
    dev.update_vertex_buffer(scrape_.vb, scrape_.verts.data(), scrape_.verts.size());
    dev.update_index_buffer(scrape_.ib, scrape_.idx.data(), scrape_.idx.size());
    b3gfx::DrawItem d;
    d.vertices = scrape_.vb;
    d.indices = scrape_.ib;
    d.index_count = std::uint32_t(scrape_.idx.size());
    d.texture = tex;
    d.textured = tex != b3gfx::kNone;
    d.blend = b3gfx::Blend::kAlpha;
    d.shading = b3gfx::Shading::kTrack;
    d.gs_test = 0x5000B;                      // ATE, ATST GEQUAL, AREF 0 (passes), ZTE, ZTST GEQUAL
    d.lighting = 0;
    d.effect = true;
    d.gs_alpha = 0x44;
    d.clamp_uv = false;                       // CLAMP_1 0 (REPEAT) [C BeginRendering 0x365640]
    out.push_back(d);
}

// Sparks (fx2, §19.2): CB3SparkRenderer (BeginRender 0x2638C0, RenderSparkArray 0x2637F0, EndRender 0x2636B0) after the
// coronas, when D_00665F2C is set [C RenderPostFogEffects 0x261E00]. GS state [C SetRenderstates 0x2633A0, matched]: TEST_1
// 0x71001 (alpha test NEVER, AFAIL FB_ONLY: colour written, Z not; ZTST GREATER), ALPHA_1 0x48 (Cs As + Cd, additive),
// CLAMP_1 5; the raster of the first renderable array ("fxspark" for all six), MODULATE; flat RGBA per ring.
bool Effects::SparkKey::same(const ContactFx& c, const std::uint32_t cam[4], int lv) const {
    if (!valid || level != lv || history != c.history || history_valid != c.history_valid || std::memcmp(statics, c.spark_statics, sizeof statics) != 0 ||
        std::memcmp(d1e31a20, c.spark_d1e31a20, sizeof d1e31a20) != 0 || std::memcmp(cam914, cam, sizeof cam914) != 0)
        return false;
    for (int a = 0; a < 6; ++a) if (arrays[a] != c.sparks[a]) return false;
    return true;
}

Effects::~Effects() {
    if (spark_job_.valid()) spark_job_.wait();
    if (particle_job_.valid()) particle_job_.wait();
}

void Effects::set_spark_key(SparkKey& k, const ContactFx& c, const std::uint32_t cam[4], int level) {
    k = SparkKey{};
    k.level = level;
    if (!c.sparks_valid) return;
    k.history = c.history;
    for (int a = 0; a < 6; ++a) k.arrays[a] = c.sparks[a];
    std::memcpy(k.statics, c.spark_statics, sizeof k.statics);
    std::memcpy(k.d1e31a20, c.spark_d1e31a20, sizeof k.d1e31a20);
    std::memcpy(k.cam914, cam, sizeof k.cam914);
    k.history_valid = c.history_valid;
    k.valid = true;
}

void Effects::draw_sparks(b3gfx::Device& dev, const FrameEffects& fx, std::vector<b3gfx::DrawItem>& out, EffectStats* stats) {
    sync(dev);   // ids from before a Device::clear_resources are dropped
    if (!fx.valid || !fx.effects_on || !particles || !sparks) return;   // settings1: SPARKS
    const ContactFx& c = fx.contact;
    if (async_sparks) {   // perf1: the worker's finished build in, a new tick's build out; draw what is uploaded
        if (spark_job_.valid() && spark_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            SparkBuild b = spark_job_.get();
            spark_.verts.swap(b.verts);
            spark_.idx.swap(b.idx);
            spark_cached_ = b.ok;
            spark_quads_ = b.quads;
            if (b.ok) {
                dev.update_vertex_buffer(spark_.vb, spark_.verts.data(), spark_.verts.size());
                dev.update_index_buffer(spark_.ib, spark_.idx.data(), spark_.idx.size());
            }
            spark_key_ = spark_job_key_;
        }
        if (!c.sparks_valid) { spark_cached_ = false; spark_key_ = SparkKey{}; }
        else if (!spark_key_.same(c, fx.spark_cam914, sparks) && !spark_job_.valid()) {
            set_spark_key(spark_job_key_, c, fx.spark_cam914, sparks);
            std::uint32_t cam[4];
            std::memcpy(cam, fx.spark_cam914, sizeof cam);
            const int level = sparks;
            spark_job_ = std::async(std::launch::async, [c, cam, level]() {   // the tick's objects are shared_ptrs: copied, kept alive
                b3port::name_this_thread("fx sparks");   // split2p (the crash report)
                SparkBuild b;
                SparkStats ss;
                b.ok = build_sparks(c, cam, b.verts, b.idx, &ss, level);
                b.quads = ss.quads;
                return b;
            });
        }
    } else if (!c.sparks_valid || !spark_key_.same(c, fx.spark_cam914, sparks)) {   // perf1: a new tick's sparks (else the uploaded ones)
        spark_.verts.clear();
        spark_.idx.clear();
        SparkStats ss;
        spark_cached_ = build_sparks(c, fx.spark_cam914, spark_.verts, spark_.idx, &ss, sparks);
        spark_quads_ = ss.quads;
        if (spark_cached_) {
            dev.update_vertex_buffer(spark_.vb, spark_.verts.data(), spark_.verts.size());
            dev.update_index_buffer(spark_.ib, spark_.idx.data(), spark_.idx.size());
        }
        set_spark_key(spark_key_, c, fx.spark_cam914, sparks);
    }
    if (!spark_cached_) return;
    if (stats) stats->sparks = spark_quads_;
    const b3gfx::TextureId tex = texture(dev, spark_texture_name());
    b3gfx::DrawItem d;
    d.vertices = spark_.vb;
    d.indices = spark_.ib;
    d.index_count = std::uint32_t(spark_.idx.size());
    d.texture = tex;
    d.textured = tex != b3gfx::kNone;
    d.blend = b3gfx::Blend::kAlpha;
    d.shading = b3gfx::Shading::kTrack;   // MODULATE on GS colours, as the other effects
    d.track_type = 0;
    d.gs_test = 0x71001;                  // ATE, ATST NEVER, AFAIL FB_ONLY, ZTE, ZTST GREATER
    d.lighting = 0;
    d.effect = true;
    d.post_fog = true;                    // after CB3Fog::Render, unfogged
    d.gs_alpha = 0x48;                    // Cs As + Cd
    d.clamp_uv = true;                    // CLAMP_1 5
    d.night_glow = true;                  // night mode: sparks stay bright
    out.push_back(d);
}

// Popcorn (fx2, §19.3): RenderPostFogEffects 0x261E00 draws the four CB3PopcornArray first, only while the player's crash
// flag (gWorld + 0x12C63A = the view race car +0x18EA) is set. GS state [C CB3PopcornArray::Render 0x365920 draft + asm]:
// ALPHA_1 = array +0x0 (0x48 additive for array 0, 0x44 for 1-3), TEST_1 0x53001 (RGB only, no Z write, ZTST GEQUAL),
// CLAMP_1 5, the array's raster ("fxpopcornspark" / "fxpopcorndebris"), MODULATE; RGBAQ per sprite, Q 1.
void Effects::draw_popcorn(b3gfx::Device& dev, const FrameEffects& fx, const Camera& game, const Camera& view, std::vector<b3gfx::DrawItem>& out,
                           EffectStats* stats) {
    sync(dev);   // ids from before a Device::clear_resources are dropped
    const ContactFx& c = fx.contact;
    if (!fx.valid || !fx.effects_on || !particles || !popcorn || !c.popcorn_valid || !c.scrapes_valid || !c.scrape_gate) return;
    popcorn_.verts.clear();
    popcorn_.idx.clear();
    std::vector<PopcornDraw> draws;
    const int n = build_popcorn(c, game, fx.trail_cam, fx.now, view, popcorn_.verts, popcorn_.idx, draws, popcorn);
    if (stats) stats->popcorn = n;
    if (draws.empty()) return;
    dev.update_vertex_buffer(popcorn_.vb, popcorn_.verts.data(), popcorn_.verts.size());
    dev.update_index_buffer(popcorn_.ib, popcorn_.idx.data(), popcorn_.idx.size());
    for (const PopcornDraw& pd : draws) {
        const b3gfx::TextureId tex = texture(dev, pd.texture);
        b3gfx::DrawItem d;
        d.vertices = popcorn_.vb;
        d.indices = popcorn_.ib;
        d.first_index = pd.first;
        d.index_count = pd.count;
        d.texture = tex;
        d.textured = tex != b3gfx::kNone;
        d.blend = b3gfx::Blend::kAlpha;
        d.shading = b3gfx::Shading::kTrack;
        d.track_type = 0;
        d.gs_test = 0x53001;              // ATE, ATST NEVER, AFAIL RGB_ONLY, ZTE, ZTST GEQUAL
        d.lighting = 0;
        d.effect = true;
        d.post_fog = true;
        d.gs_alpha = std::uint8_t(pd.alpha & 0xFF);
        if (((d.gs_alpha >> 4) & 3) == 2) d.blend_fix = float((pd.alpha >> 24) & 0xFF) / 128.0f;
        d.clamp_uv = true;                // CLAMP_1 5
        d.night_glow = std::strstr(pd.texture, "spark") != nullptr;   // night mode: the crash's sparks glow, its debris does not
        out.push_back(d);
    }
}

// Particles (effects3, §19.10): RenderPostFogEffects 0x261E00 draws the 24 CB3ParticleArray after the popcorn and before
// the coronas (CB3ParticleRenderer::BeginRender / RenderParticleArray / EndRender). Every array and bank is the game's own
// program run on its data (b3asset/particleshade.h); GS state per packet as the program sends it: ALPHA_1 (the array's
// blend), TEST_1 (alpha test GREATER the array's reference, AFAIL FB_ONLY: nothing dropped, Z tested GEQUAL), ZBUF ZMSK
// (no Z write), CLAMP_1 5, the array's raster by name, MODULATE.
void Effects::draw_particles(b3gfx::Device& dev, const FrameEffects& fx, std::vector<b3gfx::DrawItem>& out, EffectStats* stats) {
    sync(dev);   // ids from before a Device::clear_resources are dropped
    // $B3_PARTICLES=off: this layer not drawn at all (the golden record's reference look; tools/golden/baseline.sh exports it);
    // `full` draws the whole set at High. The build (the game's VU program on the captured arrays, ~5.5 ms per tick with
    // smoke on a Ryzen 9 7950X3D) runs on a worker in the window (async_sparks), so it never lands on a frame.
    static const bool env_off = [] { const char* e = std::getenv("B3_PARTICLES"); return e && !std::strcmp(e, "off"); }();
    const ContactFx& c = fx.contact;
    if (env_off || !fx.valid || !fx.effects_on || !particles || !c.particles_valid || !c.particle_prog || !c.particle_cam) return;
    if (particle_prog_ != c.particle_prog) {
        if (particle_job_.valid()) particle_job_.wait();   // the worker runs particle_vu_
        particle_job_ = {};
        particle_prog_ = c.particle_prog;
        particle_vu_ = std::make_shared<b3asset::ParticleVu>();   // (a running build keeps its own)
        std::string why;
        if (!particle_vu_->load(c.particle_prog->data(), c.particle_prog->size(), &why)) std::fprintf(stderr, "particles: program: %s\n", why.c_str());
        particle_key_ = ParticleKey{};
        particle_job_key_ = ParticleKey{};
    }
    if (!particle_vu_->loaded()) return;
    const bool crash = c.scrapes_valid && c.scrape_gate;
    static const bool full = [] { const char* e = std::getenv("B3_PARTICLES"); return e && !std::strcmp(e, "full"); }();
    const int smoke_lv = particle_layer_level(sparks, full), debris_lv = particle_layer_level(popcorn, full);
    ParticleKey want;
    for (int a = 0; a < 24; ++a) want.arrays[a] = c.particles[a];
    want.cam = c.particle_cam;
    want.crash = crash;
    want.skip = c.particle_skip;
    want.smoke = smoke_lv;
    want.debris = debris_lv;
    want.valid = true;
    auto same = [&](const ParticleKey& k) {
        if (!k.valid || k.cam != want.cam || k.crash != want.crash || k.skip != want.skip || k.smoke != want.smoke || k.debris != want.debris) return false;
        for (int a = 0; a < 24; ++a) if (k.arrays[a] != want.arrays[a]) return false;
        return true;
    };
    // the finished build into the persistent buffer: one upload, the triangles expanded in index order (drawn with a grow-only
    // identity index buffer, no second copy pass); nothing is uploaded when no triangle is left
    auto apply = [&](ParticleBuild& b) {
        particle_draws_.clear();
        if (b.ok) {
            for (const ParticleDraw& d : b.draws) particle_draws_.push_back({d.texture, d.alpha, d.test, d.first, d.count});
            particle_flat_.clear();
            particle_flat_.reserve(b.idx.size());
            for (std::uint32_t i : b.idx) particle_flat_.push_back(b.verts[i]);
            if (!particle_flat_.empty()) {
                if (particle_flat_.size() > particle_ident_n_) {
                    std::size_t n = std::max<std::size_t>(std::size_t(4096), particle_ident_n_ * 2);
                    while (n < particle_flat_.size()) n *= 2;
                    std::vector<std::uint32_t> id(n);
                    for (std::size_t i = 0; i < n; ++i) id[i] = std::uint32_t(i);
                    dev.update_index_buffer(particle_ident_, id.data(), id.size());
                    particle_ident_n_ = n;
                }
                dev.update_vertex_buffer(particle_.vb, particle_flat_.data(), particle_flat_.size());
            }
        }
        particle_prims_ = b.stats.quads + b.stats.triangles;
        static const bool log = std::getenv("B3PORT_PARTICLE_LOG") != nullptr;   // dev: one line per rebuilt tick with particles
        if (log && particle_prims_ > 0)
            std::fprintf(stderr, "particles: %lld us, %d quads, %d triangles, %zu draws, %llu kicks, %llu vertices, %llu unprojected-bad\n",
                         b.us, b.stats.quads, b.stats.triangles, b.draws.size(), static_cast<unsigned long long>(b.stats.vu.kicks),
                         static_cast<unsigned long long>(b.stats.vu.vertices), static_cast<unsigned long long>(b.stats.vu.unprojected_bad));
    };
    auto build = [](ContactFx cc, bool cr, int sl, int dl, std::shared_ptr<b3asset::ParticleVu> vu, bool worker) {
        if (worker) b3port::name_this_thread("fx particles");   // split2p (the crash report)
        ParticleBuild b;
        const auto t0 = std::chrono::steady_clock::now();
        b.ok = build_particles(cc, cr, sl, dl, *vu, b.verts, b.idx, b.draws, &b.stats);
        b.us = static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
        return b;
    };
    if (async_sparks) {   // the window: the worker's finished build in, a new tick's build out; draw what is uploaded
        if (particle_job_.valid() && particle_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            ParticleBuild b = particle_job_.get();
            apply(b);
            particle_key_ = particle_job_key_;
        }
        if (!same(particle_key_) && !particle_job_.valid()) {
            particle_job_key_ = want;
            particle_job_ = std::async(std::launch::async, build, c, crash, smoke_lv, debris_lv, particle_vu_, true);   // the tick's objects and the VU are shared_ptrs: copied, kept alive
        }
    } else if (!same(particle_key_)) {   // offscreen: the tick's own build, in place (deterministic shots)
        ParticleBuild b = build(c, crash, smoke_lv, debris_lv, particle_vu_, false);
        apply(b);
        particle_key_ = want;
    }
    if (stats) stats->particles = particle_prims_;
    for (const PDraw& pd : particle_draws_) {
        const b3gfx::TextureId tex = texture(dev, pd.texture);
        b3gfx::DrawItem d;
        d.vertices = particle_.vb;
        d.indices = particle_ident_;
        d.first_index = pd.first;
        d.index_count = pd.count;
        d.texture = tex;
        d.textured = tex != b3gfx::kNone;
        d.blend = b3gfx::Blend::kAlpha;
        d.shading = b3gfx::Shading::kTrack;
        d.track_type = 0;
        d.gs_test = std::uint32_t(pd.test);
        d.lighting = 0;
        d.effect = true;
        d.post_fog = true;
        d.gs_alpha = std::uint8_t(pd.alpha & 0xFF);
        if (((d.gs_alpha >> 4) & 3) == 2) d.blend_fix = float((pd.alpha >> 32) & 0xFF) / 128.0f;
        d.clamp_uv = true;                // CLAMP_1 5
        // night mode: fire and flashes glow; smoke, dust, gravel, snow and glass are lit by the night like the rest
        d.night_glow = pd.texture.find("fire") != std::string::npos || pd.texture.find("flash") != std::string::npos ||
                       pd.texture.find("spark") != std::string::npos;
        out.push_back(d);
    }
}

// The crash explosion's full-screen flash (§19.4). [C RenderPostFogEffects 0x261E00 matched: `crash` (player block +0x18EA)
// and D_005179C0[idx] +0x0 & 0x2000 == 0 and the crash object's +0x3534 t > 0 -> RenderFullScreenFlash(world clock - t);
// 0x399BA0 -> the full-screen colour fill 0x2276C0: one GS SPRITE over the viewport, TEST_1 0x33001 (alpha test NEVER,
// AFAIL RGB_ONLY, no Z test result kept), ALPHA_1 = table 0x4B9DF0 [1] = 0x48 (Cs As + Cd).] The quad sits just in front of
// the near plane (reverse-Z: it passes every depth test) and is oversized so it covers the view at any aspect.
int Effects::draw_explosion_flash(b3gfx::Device& dev, const FrameEffects& fx, const PostIn& post, bool crashed, const Camera& cam,
                                  float aspect, bool reduce, std::vector<b3gfx::DrawItem>& out) {
    sync(dev);   // ids from before a Device::clear_resources are dropped
    if (!fx.valid || !fx.effects_on || !crashed || !post.inputs || !post.has_crash_obj || (post.flags & 0x2000u) != 0 || !(post.crash_time > 0.0f) ||
        !cam.valid)
        return 0;
    const a11y::ExplosionFlash f = a11y::explosion_flash(fx.now - post.crash_time, reduce);
    if (!f.on || f.a == 0) return 0;
    const float d = std::max(0.05f, cam.znear) * 1.5f;
    const float hw = std::max(cam.half_w, aspect > 0 ? cam.half_h * aspect : 0.0f) * d * 1.1f, hh = cam.half_h * d * 1.1f;
    const float* right = cam.m.r[0];
    const float* up = cam.m.r[1];
    const float* at = cam.m.r[2];
    const float* pos = cam.m.r[3];
    flash_.verts.clear();
    flash_.idx.clear();
    for (int c = 0; c < 4; ++c) {
        const float sx = (c == 0 || c == 3) ? -1.0f : 1.0f, sy = c < 2 ? -1.0f : 1.0f;
        b3gfx::Vertex v;
        std::memset(&v, 0, sizeof v);
        for (int k = 0; k < 3; ++k) v.pos[k] = pos[k] + at[k] * d + right[k] * hw * sx + up[k] * hh * sy;
        v.normal[1] = 1;
        v.rgba = std::uint32_t(f.r) | std::uint32_t(f.g) << 8 | std::uint32_t(f.b) << 16 | std::uint32_t(f.a) << 24;
        flash_.verts.push_back(v);
    }
    for (std::uint32_t t : {0u, 1u, 2u, 0u, 2u, 3u}) flash_.idx.push_back(t);
    dev.update_vertex_buffer(flash_.vb, flash_.verts.data(), flash_.verts.size());
    dev.update_index_buffer(flash_.ib, flash_.idx.data(), flash_.idx.size());
    b3gfx::DrawItem di;
    di.vertices = flash_.vb;
    di.indices = flash_.ib;
    di.index_count = 6;
    di.texture = b3gfx::kNone;
    di.textured = false;
    di.blend = b3gfx::Blend::kAlpha;
    di.shading = b3gfx::Shading::kTrack;
    di.track_type = 0;
    di.gs_test = 0x53001;
    di.lighting = 0;
    di.effect = true;
    di.post_fog = true;
    di.gs_alpha = 0x48;
    di.night_glow = true;
    out.push_back(di);
    return f.a;
}

}  // namespace b3replay

namespace b3replay {

void Effects::draw_headlights(b3gfx::Device& dev, const std::vector<Vehicle>& vehicles, const float shape[4], const float colour[3],
                              std::vector<b3gfx::DrawItem>& out, const std::vector<float>* fades) {
    Trail& h = headlight_;
    h.verts.clear();
    h.idx.clear();
    const float range = shape[1], spread = shape[0];
    for (std::size_t n = 0; n < vehicles.size(); ++n) {
        const Vehicle& v = vehicles[n];
        if (v.kind == VehicleKind::kCrashed) continue;
        if (v.kind == VehicleKind::kTraffic && !(v.shadow.cast || v.corona.submit)) continue;
        const Vehicle::Shadow& s = v.shadow;
        // airborne: the light leaves the road (the shadow's lrHeightAboveOffset, over a longer fade)
        float a = 1.0f - std::clamp((s.dist - s.radius) / 2.5f, 0.0f, 1.0f);
        if (fades && n < fades->size()) a *= (*fades)[n];
        if (!(a > 0.01f)) continue;
        float nrm[3] = {s.normal[0], s.normal[1], s.normal[2]};
        float l = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        if (!(l > 0.1f)) { nrm[0] = 0; nrm[1] = 1; nrm[2] = 0; l = 1; }
        for (float& c : nrm) c /= l;
        const float* at = s.ess.r[2];
        const float fd = at[0] * nrm[0] + at[1] * nrm[1] + at[2] * nrm[2];
        float fw[3] = {at[0] - fd * nrm[0], at[1] - fd * nrm[1], at[2] - fd * nrm[2]};
        l = std::sqrt(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
        if (!(l > 1e-3f)) continue;
        for (float& c : fw) c /= l;
        const float rt[3] = {nrm[1] * fw[2] - nrm[2] * fw[1], nrm[2] * fw[0] - nrm[0] * fw[2], nrm[0] * fw[1] - nrm[1] * fw[0]};
        // the ground under the car (where RenderAmbientShadow puts its strip), lifted 4 cm; the lamps at the box's front
        const float lift = 0.04f;
        const float front = s.sup[2] > 0.5f ? s.sup[2] : 2.0f;
        const float half = std::clamp(0.8f * std::max(std::fabs(s.sup[0]), std::fabs(s.inf[0])), 0.4f, 1.4f);
        const float g[3] = {s.ess.r[3][0] + lift * nrm[0], s.ess.r[3][1] - s.dist + lift * nrm[1], s.ess.r[3][2] + lift * nrm[2]};
        const float far_half = half + range * spread;

        auto byte = [&](float c) { return std::uint32_t(std::clamp(c * a, 0.0f, 1.0f) * 255.0f + 0.5f); };
        const std::uint32_t rgba = byte(colour[0]) | byte(colour[1]) << 8 | byte(colour[2]) << 16 | 0xFF000000u;
        const std::uint32_t b = std::uint32_t(h.verts.size());
        const float corner[4][2] = {{-half, -0.25f}, {half, -0.25f}, {-far_half, range}, {far_half, range}};
        for (const auto& c : corner) {
            b3gfx::Vertex o;
            std::memset(&o, 0, sizeof o);
            const float along = front + c[1];
            for (int k = 0; k < 3; ++k) o.pos[k] = g[k] + fw[k] * along + rt[k] * c[0];
            o.normal[0] = half;   // light.frag: the half width at the lamps
            o.uv[0] = c[0];
            o.uv[1] = c[1];
            o.rgba = rgba;
            h.verts.push_back(o);
        }
        for (std::uint32_t t : {0u, 1u, 2u, 1u, 3u, 2u}) h.idx.push_back(b + t);
    }
    if (h.idx.empty()) return;
    dev.update_vertex_buffer(h.vb, h.verts.data(), h.verts.size());
    dev.update_index_buffer(h.ib, h.idx.data(), h.idx.size());
    b3gfx::DrawItem d;
    d.vertices = h.vb;
    d.indices = h.ib;
    d.index_count = std::uint32_t(h.idx.size());
    d.texture = b3gfx::kNone;
    d.textured = false;
    d.blend = b3gfx::Blend::kAlpha;
    d.shading = b3gfx::Shading::kSimple;
    d.lighting = 0;
    d.effect = true;
    d.post_fog = true;
    d.night_light = true;
    for (int k = 0; k < 4; ++k) d.night_shape[k] = shape[k];
    out.push_back(d);
}

}  // namespace b3replay
