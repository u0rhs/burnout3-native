#include "crashscene.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "b3asset/enviro.h"
#include "b3asset/export.h"
#include "scene.h"   // hdkey: upload_texture

namespace b3view {

namespace {

// CB3GameData::Draw's objects as track-model draws: B3Im3d sends the vertex colour as is with a texture (RenderTrilist
// 0x364B70 [C matched]: doubled only untextured), MODULATE, TEST_1 from SetAlphaTestThreshold, ALPHA_1 0x44 (normal
// blend by As, BeginRendering 0x365640 [L nonmatching]), no back-face cull (_gbBFC starts 0 in .sbss and only the
// wrong-way wall sets it, to false [C]), Z written (BeginRendering sends the camera record's ZBUF; only SetZWrite(false)
// masks it [C matched 0x365460]), and after the fog pass (CB3World::Render: CB3Fog::Render, then CB3GameData::Draw
// [C matched 0x276300]): b3gfx's post-fog stage.
b3gfx::DrawItem crash_item(b3gfx::BufferId vb, b3gfx::BufferId ib, std::uint32_t first, std::uint32_t count, std::uint32_t test) {
    b3gfx::DrawItem it;
    it.vertices = vb;
    it.indices = ib;
    it.first_index = first;
    it.index_count = count;
    it.shading = b3gfx::Shading::kTrack;
    it.track_type = 0;
    it.blend = b3gfx::Blend::kAlpha;
    it.lighting = 0;
    it.gs_test = test;
    it.textured = true;
    it.cull = false;
    it.post_fog = true;
    return it;
}

b3gfx::TextureId global_texture(b3gfx::Device& dev, const std::vector<std::uint8_t>& txd, const char* name) {
    b3asset::GsMemory gs;
    b3asset::DecodedRaster r;
    if (!b3asset::find_txd_raster(b3asset::Bytes{txd.data(), txd.size()}, name, gs, r)) {
        std::fprintf(stderr, "crash objects: GLOBAL.TXD has no %s\n", name);
        return b3gfx::kNone;
    }
    return upload_texture(dev, r.levels, r.original);   // hdkey: and its original
}

}  // namespace

bool load_crash_scene(b3gfx::Device& dev, const std::string& route, const std::string& game, CrashScene& out, std::string* why) {
    std::vector<std::uint8_t> bgd, txd;
    if (!b3asset::read_file(route + "/GAMEDATA.BGD", bgd)) { out = CrashScene{}; if (why) why->clear(); return false; }
    // CB3GameData::Prepare looks the textures up by name ("ramp", D_00488080[9]); all are in DATA/GLOBAL.TXD [C]
    b3asset::read_file(game + "/DATA/GLOBAL.TXD", txd);
    return load_crash_scene(dev, bgd, txd, out, why);
}

bool load_crash_scene(b3gfx::Device& dev, const std::vector<std::uint8_t>& bgd, const std::vector<std::uint8_t>& txd, CrashScene& out,
                      std::string* why) {
    out = CrashScene{};
    if (!b3asset::read_game_data(b3asset::Bytes{bgd.data(), bgd.size()}, out.stages, why)) { out = CrashScene{}; return false; }
    const b3gfx::TextureId ramp_tex = global_texture(dev, txd, b3asset::kCrashRampTexture);
    for (int k = 0; k < 9; ++k) out.pickup_tex[k] = global_texture(dev, txd, b3asset::kCrashPickupTextures[k]);
    // perf1: CB3StartFinishLine::Prepare / CB3SplitTimeCheckpoint::Prepare look "finish" (D_004B7108) and "check"
    // (D_004B7100) up in the global dictionary [C ELF strings, matched 0x15E7C0 / 0x15E650]
    out.finish_tex[0] = global_texture(dev, txd, "finish");
    out.finish_tex[1] = global_texture(dev, txd, "check");
    std::vector<b3gfx::Vertex> verts;
    auto put = [&](const b3asset::ImVertex& iv, std::uint32_t rgba) {
        b3gfx::Vertex o{};
        std::memcpy(o.pos, iv.pos, sizeof o.pos);
        o.uv[0] = iv.uv[0];
        o.uv[1] = iv.uv[1];
        o.rgba = rgba;
        verts.push_back(o);
    };
    b3asset::ImVertex iv[b3asset::kCrashRampVertices];
    b3asset::crash_pickup_vertices(iv);
    for (int i = 0; i < b3asset::kCrashPickupVertices; ++i) put(iv[i], b3asset::kCrashPickupColour);
    for (const auto& st : out.stages)
        for (const auto& r : st.ramps) {
            b3asset::crash_ramp_vertices(r.corner, r.type, iv);
            for (int i = 0; i < b3asset::kCrashRampVertices; ++i) put(iv[i], b3asset::kCrashRampColour);
        }
    // perf1: the finish quads (local x = side, y = up; DrawCheckpoint's v0..v3 = up + side, up - side, -up + side,
    // -up - side with UVs (0, 0), (1, 0), (0, 1), (1, 1), one tristrip)
    const std::size_t finish_at = verts.size();
    static const float kQuad[4][4] = {{1, 1, 0, 0}, {-1, 1, 1, 0}, {1, -1, 0, 1}, {-1, -1, 1, 1}};
    for (std::uint32_t colour : {b3view::kFinishColour, b3view::kSplitColour})
        for (const auto& q : kQuad) {
            b3gfx::Vertex o{};
            o.pos[0] = q[0];
            o.pos[1] = q[1];
            o.uv[0] = q[2];
            o.uv[1] = q[3];
            o.rgba = colour;
            verts.push_back(o);
        }
    std::vector<std::uint32_t> idx(finish_at);
    for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = std::uint32_t(i);
    for (std::uint32_t q = 0; q < 2; ++q)
        for (std::uint32_t k : {0u, 1u, 2u, 2u, 1u, 3u}) idx.push_back(std::uint32_t(finish_at) + 4 * q + k);
    const b3gfx::BufferId vb = dev.create_vertex_buffer(verts.data(), verts.size());
    const b3gfx::BufferId ib = dev.create_index_buffer(idx.data(), idx.size());
    if (vb == b3gfx::kNone || ib == b3gfx::kNone) { out = CrashScene{}; if (why) *why = "crash objects: GPU buffer creation failed"; return false; }
    out.pickup = crash_item(vb, ib, 0, b3asset::kCrashPickupVertices, b3asset::kCrashPickupTest);
    for (int q = 0; q < 2; ++q) out.finish_quad[q] = crash_item(vb, ib, std::uint32_t(finish_at) + 6 * std::uint32_t(q), 6, kFinishTest);
    std::uint32_t at = b3asset::kCrashPickupVertices;
    out.ramps.assign(out.stages.size(), {});
    for (std::size_t s = 0; s < out.stages.size(); ++s)
        for (std::size_t k = 0; k < out.stages[s].ramps.size(); ++k) {
            out.ramps[s].push_back(crash_item(vb, ib, at, b3asset::kCrashRampVertices, b3asset::kCrashRampTest));
            out.ramps[s].back().texture = ramp_tex;
            at += b3asset::kCrashRampVertices;
        }
    return true;
}

void add_crash_pickup(const CrashScene& cs, const float pos[3], int type, const float right[3], std::vector<b3gfx::DrawItem>& out) {
    if (type < 0 || type >= 9) return;
    b3gfx::DrawItem d = cs.pickup;
    d.texture = cs.pickup_tex[type];
    // local x = the camera right row, y = world up (kPickupUp), both x 1.4; column-major
    const float h = b3asset::kCrashPickupHalf;
    const float m[16] = {right[0] * h, right[1] * h, right[2] * h, 0, 0, h, 0, 0, 0, 0, 1, 0, pos[0], pos[1], pos[2], 1};
    std::memcpy(d.model, m, sizeof m);
    out.push_back(d);
}

void add_finish_line(const CrashScene& cs, const float pos[3], const float dir[3], float half_h, float half_w, int texture,
                     std::uint32_t rgba, std::vector<b3gfx::DrawItem>& out) {
    if (texture < 0 || texture > 1 || cs.finish_tex[texture] == b3gfx::kNone) return;
    // side = (0, 1, 0) x dir = (dir.z, 0, -dir.x), normalised (VU: rsqrt of the squared length)
    float sx = dir[2], sz = -dir[0];
    const float l2 = sx * sx + sz * sz;
    if (!(l2 > 0)) return;
    const float r = 1.0f / std::sqrt(l2);
    sx *= r;
    sz *= r;
    b3gfx::DrawItem d = cs.finish_quad[rgba == kSplitColour ? 1 : 0];
    d.texture = cs.finish_tex[texture];
    const float m[16] = {sx * half_w, 0, sz * half_w, 0, 0, half_h, 0, 0, 0, 0, 1, 0, pos[0], pos[1] + kFinishRise, pos[2], 1};
    std::memcpy(d.model, m, sizeof m);
    out.push_back(d);
}

void add_crash_stage(const CrashScene& cs, int st, const float right[3], std::vector<b3gfx::DrawItem>& out) {
    if (st < 0 || std::size_t(st) >= cs.stages.size()) return;
    for (const auto& p : cs.stages[std::size_t(st)].pickups) if (p.modes & 1) add_crash_pickup(cs, p.pos, p.type, right, out);
    out.insert(out.end(), cs.ramps[std::size_t(st)].begin(), cs.ramps[std::size_t(st)].end());
}

}  // namespace b3view
