#include "carscene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "b3asset/car.h"
#include "b3asset/carshade.h"
#include "b3asset/export.h"
#include "b3asset/raster.h"

namespace b3view {

RowMat row_mul(const RowMat& a, const RowMat& b) {
    RowMat o;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j)
            o.r[i][j] = a.r[i][0] * b.r[0][j] + a.r[i][1] * b.r[1][j] + a.r[i][2] * b.r[2][j] + (i == 3 ? b.r[3][j] : 0.0f);
    return o;
}

namespace {

void to_colmajor(const RowMat& m, float out[16]) {
    for (int c = 0; c < 4; ++c) {
        for (int k = 0; k < 3; ++k) out[c * 4 + k] = m.r[c][k];
        out[c * 4 + 3] = c == 3 ? 1.0f : 0.0f;
    }
}

RowMat file_mat(const std::vector<std::uint8_t>& f, std::size_t at) {
    RowMat m;
    if (at + 64 > f.size()) return m;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j) std::memcpy(&m.r[i][j], f.data() + at + 16 * std::size_t(i) + 4 * std::size_t(j), 4);
    return m;
}

std::uint32_t file_u32(const std::vector<std::uint8_t>& f, std::size_t at) {
    std::uint32_t v = 0;
    if (at + 4 <= f.size()) std::memcpy(&v, f.data() + at, 4);
    return v;
}

int list_index(const std::string& part) {
    if (part.size() < 2) return -1;
    const int k = part[1] - '0';
    if (k < 0 || k > 9) return -1;
    if (part[0] == 'L') return 0;
    if (part[0] == 'A') return 1 + k;
    if (part[0] == 'B') return 7 + k;
    if (part[0] == 'C' && k <= 7) return 10 + k;   // payloads1: the payload objects
    return -1;
}

b3gfx::TextureId upload(b3gfx::Device& dev, const b3asset::DecodedRaster& r) {
    return upload_texture(dev, r.levels, r.original);   // hdkey: and its original
}

}  // namespace

void car_sh_matrix(const float L[9], float o[16]) {
    float w[4][4];
    b3asset::car_irradiance_matrix(L, w);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) o[c * 4 + r] = w[r][c];
}

float car_irradiance(const float sh[9], const float n[3]) {
    float m[16];
    car_sh_matrix(sh, m);
    const float v[4] = {n[0], n[1], n[2], 1.0f};
    float e = 0;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) e += v[r] * m[c * 4 + r] * v[c];
    return e;
}

bool load_car_globals(b3gfx::Device& dev, const std::string& game, CarGlobals& out, std::string* why) {
    std::vector<std::uint8_t> txd;
    if (!b3asset::read_file(game + "/DATA/GLOBAL.TXD", txd)) { if (why) *why = "cannot read " + game + "/DATA/GLOBAL.TXD"; return false; }
    b3asset::DecodedRaster slots[5];
    const int n = decode_car_globals(txd, slots, why);
    upload_car_globals(dev, slots, n, out);
    return n == 4;
}

int decode_car_globals(const std::vector<std::uint8_t>& txd, b3asset::DecodedRaster (&slots)[5], std::string* why) {
    const char* names[5] = {nullptr, "VehicleUnderside", "UnbrokenGlass", "CrackedGlass", "SmashedGlass"};
    for (int s = 1; s < 5; ++s) {
        b3asset::GsMemory gs;
        if (!b3asset::find_txd_raster(b3asset::Bytes{txd.data(), txd.size()}, names[s], gs, slots[s])) {
            if (why) *why = std::string("GLOBAL.TXD has no ") + names[s];
            return s - 1;
        }
    }
    return 4;
}

void upload_car_globals(b3gfx::Device& dev, const b3asset::DecodedRaster (&slots)[5], int n, CarGlobals& out) {
    for (int s = 1; s <= n && s < 5; ++s) out.slot[s] = upload(dev, slots[s]);
}

void set_car_environment(b3gfx::Device& dev, const b3asset::Enviro& env, CarGlobals& out) {
    out.env_sky = upload_backdrop(dev, env, env.to_sun);   // hdkey: and the original gradient's
    // the env map's sun blob [G colour: RenderBlob's sun colour x 128 MODULATE = the colour in frame units]
    for (int k = 0; k < 3; ++k) out.sun_colour[k] = env.sun_colour[k];
    out.sun_colour[3] = env.sun_corona.levels.empty() ? 0.0f : 1.0f;   // Prepare skips the sun without +0xA4 [C]
}

int car_lod_block(const std::vector<std::uint8_t>& f, int lod) {
    lod = std::clamp(lod, 0, 4);
    int l = lod;
    while (l < 4 && !file_u32(f, 0x4C + 4 * std::size_t(l))) ++l;
    while (l > 0 && !file_u32(f, 0x4C + 4 * std::size_t(l))) --l;
    return file_u32(f, 0x4C + 4 * std::size_t(l)) ? l : -1;
}

bool load_car_model(b3gfx::Device& dev, const std::vector<std::uint8_t>& f, int palette, int lod, CarModel& out,
                    std::string* why) {
    out = CarModel{};
    out.lod = car_lod_block(f, lod);
    if (out.lod < 0) { if (why) *why = "no LOD block"; return false; }
    b3asset::Model m;
    b3asset::GsMemory gs;
    b3asset::CarOptions co;
    co.lod = out.lod;
    co.palette = palette;
    co.bake_frames = false;
    if (!b3asset::read_car(b3asset::Bytes{f.data(), f.size()}, co, gs, m, why)) return false;
    if (!build_scene(dev, std::move(m), false, out.scene, why)) return false;
    for (std::size_t i = 0; i < out.scene.model.draws.size(); ++i) {
        const auto& d = out.scene.model.draws[i];
        out.draws.push_back({list_index(d.part), d.entry, d.glass_entry, d.material, d.colour});
        b3gfx::DrawItem& it = out.scene.items[i];
        it.shading = b3gfx::Shading::kCar;
        it.blend = b3gfx::Blend::kOpaque;
    }
    out.npanel = std::min<int>(8, f.size() > 0x0C ? f[0x0C] : 0);
    out.nwheel = std::min<int>(8, f.size() > 0x0D ? f[0x0D] : 0);
    for (int k = 0; k < out.npanel; ++k) out.panel_frame[k] = file_mat(f, 0xD00 + 64 * std::size_t(k));
    for (int k = 0; k < out.nwheel; ++k) out.wheel_frame[k] = file_mat(f, 0xB80 + 64 * std::size_t(k));
    if (f.size() >= 0x1C) std::memcpy(&out.radius, f.data() + 0x18, 4);
    return true;
}

namespace {
// CreateRotationMatrixOnPrincipalAxis(axis, angle) 0x3090A0 [L DNAT name; body C from the asm, the same rows as
// RenderVehicle's CreateRotationMatrixOnPrincipleAxisWithoutSinCos with sin / cos of the angle]
RowMat axis_rotation(const float a[3], float angle) {
    const float s = std::sin(angle), c = std::cos(angle);
    float t[3], as[3];
    for (int k = 0; k < 3; ++k) { t[k] = (1.0f - a[k]) * (1.0f - c); as[k] = a[k] * s; }
    RowMat m;
    m.r[0][0] = 1.0f - t[0]; m.r[0][1] = as[2];       m.r[0][2] = -as[1];
    m.r[1][0] = -as[2];      m.r[1][1] = 1.0f - t[1]; m.r[1][2] = as[0];
    m.r[2][0] = as[1];       m.r[2][1] = -as[0];      m.r[2][2] = 1.0f - t[2];
    return m;
}
}  // namespace

void traffic_parts(const CarModel& car, float spin, float steer, const float spin_axis[3], const float steer_axis[3],
                   float speed, CarPose& pose) {
    pose.parts = true;
    pose.nbit = std::min(car.npanel, 7);
    for (int k = 0; k < pose.nbit; ++k) { pose.bit[k] = row_mul(car.panel_frame[k], pose.body); pose.bit_state[k] = 0; }
    pose.nwheel = std::min(car.nwheel, 6);
    const RowMat fwd = axis_rotation(spin_axis, spin), rev = axis_rotation(spin_axis, -spin);
    const RowMat st = axis_rotation(steer_axis, -steer);
    const float inv_r = car.radius != 0 ? 1.0f / car.radius : 0.0f;
    for (int w = 0; w < pose.nwheel; ++w) {
        const RowMat& sp = (w % 2 == 0) ? fwd : rev;
        const RowMat rel = row_mul(w < 2 ? row_mul(sp, st) : sp, car.wheel_frame[w]);
        pose.wheel[w] = row_mul(rel, pose.main);
        pose.wheel_speed[w] = speed * inv_r;
    }
}

void car_shading_scales(bool deformed, float damage, bool exploded, float t, float& shininess, float& lighting) {
    shininess = 1.0f;
    lighting = 1.0f;
    if (deformed) shininess = 1.0f - 0.6f * damage;
    if (exploded) {
        if (t <= 0.4f) {
            lighting *= 1.0f - 1.875f * t;
            shininess *= 1.0f - 2.25f * t;
        } else {
            lighting *= 0.25f;
            shininess *= 0.1f;
        }
    }
}

void add_car_draws(const CarModel& car, const CarGlobals& g, const CarPose& pose, const float eye[3],
                   const float lod_eye[3], std::vector<b3gfx::DrawItem>& out) {
    b3asset::CarScene sc;
    sc.shininess = pose.shininess;
    sc.lighting = pose.lighting;
    float sh[16], shw[4][4];
    car_sh_matrix(pose.sh, sh);
    b3asset::car_irradiance_matrix(pose.sh, shw);
    // the sky the car sees: the upward (L1-1, y) share of its irradiance against the ambient term, about 1 in the open, 0.6 at
    // dusk and 0.2 in a tunnel [C measured on the captures]; the enhanced reflections' gate (b3gfx::CarParams::sky_open)
    const float sky_open = pose.sh[0] > 1e-3f ? std::clamp((pose.sh[1] / pose.sh[0] - 0.15f) / 0.35f, 0.0f, 1.0f) : 1.0f;
    float view[3] = {pose.main.r[3][0] - eye[0], pose.main.r[3][1] - eye[1], pose.main.r[3][2] - eye[2]};
    const float vl = std::sqrt(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
    if (vl > 0) for (float& c : view) c /= vl;
    // one part: every mesh entry of `list` whose mask & `mask` (RenderObject 0x21FBB0 [C draft]), at `m`, lit through
    // `light` (its own matrix, or the body's for panels)
    auto part = [&](int list, std::uint16_t mask, const RowMat& m, const RowMat& light, int zones = 0,
                    const RowMat* zone = nullptr) {
        for (std::size_t i = 0; i < car.draws.size(); ++i) {
            const CarModel::Draw& d = car.draws[i];
            if (d.list != list || !(d.material & mask & 0xFFFF)) continue;
            b3gfx::DrawItem it = car.scene.items[i];
            std::uint32_t material = d.material, colour = d.colour;
            if (d.entry == d.glass_entry && d.glass_entry >= 0) {
                // CB3VehicleObject::SetGlassState [C name; L body, RenderVehicle draft]: slot 3 / FIX 0x48 cracked,
                // slot 4 / FIX 0x80 broken, else slot 2 with the default FIX (colour byte 0)
                const std::uint32_t slot = pose.glass == 1 ? 3 : pose.glass == 2 ? 4 : 2;
                const std::uint32_t fix = pose.glass == 1 ? 0x48 : pose.glass == 2 ? 0x80 : (colour & 0xFF);
                material = (material & 0x00FFFFFFu) | slot << 24;
                colour = (colour & 0xFFFF00FFu) | fix << 8;
            }
            const b3asset::CarMeshInput mi = b3asset::car_mesh_input(material, colour, pose.lit);
            b3asset::CarMeshConstants mc = b3asset::car_mesh_constants(sc, mi);
            if (pose.tail >= 0.0f) {   // taillights: the rear lamps glow at the tail level, up to full while braking
                const float level = b3asset::rear_lamp_level(mi.mask, pose.tail, pose.brake);
                if (level >= 0.0f) mc = b3asset::car_mesh_constants_level(sc, mi, level);
            }
            b3gfx::CarParams& c = it.car_mut();
            std::memcpy(c.sh, sh, sizeof sh);
            to_colmajor(light, c.light);
            for (int k = 0; k < 3; ++k) c.light[12 + k] = 0.0f;
            c.sh_k = b3asset::car_irradiance_k(shw, light.r[2]);
            for (int k = 0; k < 3; ++k) c.view_dir[k] = view[k];
            for (int k = 0; k < 4; ++k) {
                c.base[k] = mc.base[k];
                c.full[k] = mc.full[k];
                c.env_colour[k] = float(mc.env[k]);
            }
            c.alpha_as = ((mc.alpha1[0] >> 4) & 3) == 0;
            c.fix = float(mc.alpha1[1]);
            c.env_mode = mc.no_env ? 0 : mc.alpha2 == 0x54 ? 1 : mc.alpha2 == 0x58 ? 2 : 0;
            c.env_texture = g.env_sky;
            c.env_frame_units = g.env_frame_units;
            c.env_world = g.env_world;
            if (mi.tex_slot >= 1 && mi.tex_slot <= 4) it.texture = g.slot[mi.tex_slot];
            c.sky_open = sky_open;
            c.zones = zone ? zones : 0;
            for (int z = 0; z < c.zones; ++z) to_colmajor(zone[z], c.zone[z]);
            to_colmajor(m, it.model);
            out.push_back(it);
        }
    };
    const RowMat& body = pose.body;
    const int body_zones = pose.deformed ? 8 : 0;
    part(0, 0xFF, body, body, body_zones, pose.zone);
    // panels: RenderVehicle sorts them by squared distance from the camera, farthest first [C draft]
    const int nbit = pose.parts ? pose.nbit : car.npanel;
    RowMat bits[8];
    int order[8];
    float dist[8];
    for (int k = 0; k < nbit && k < 7; ++k) {
        bits[k] = pose.parts ? pose.bit[k] : row_mul(car.panel_frame[k], body);
        order[k] = k;
        dist[k] = 0;
        for (int j = 0; j < 3; ++j) dist[k] += (bits[k].r[3][j] - lod_eye[j]) * (bits[k].r[3][j] - lod_eye[j]);
    }
    for (int a = 0; a < nbit && a < 7; ++a)
        for (int b = a + 1; b < nbit && b < 7; ++b)
            if (dist[a] < dist[b]) { std::swap(dist[a], dist[b]); std::swap(order[a], order[b]); }
    for (int k = 0; k < nbit && k < 7; ++k) {
        const int p = order[k];
        if (pose.parts && pose.bit_state[p] == 4) continue;
        if (pose.parts && pose.bit_deformed[p]) part(1 + p, 0x3FF, bits[p], body, 2, pose.bit_zone[p]);
        else part(1 + p, 0x3FF, bits[p], body);
    }
    part(0, 0x300, body, body, body_zones, pose.zone);
    const int nwheel = pose.parts ? pose.nwheel : car.nwheel;
    for (int w = 0; w < nwheel && w < 6; ++w) {
        const RowMat m = pose.parts ? pose.wheel[w] : row_mul(car.wheel_frame[w], pose.main);
        int list = 7;
        const float s = std::fabs(pose.wheel_speed[w]);
        auto has = [&](int l) { for (const auto& d : car.draws) if (d.list == l) return true; return false; };
        if (s > 50.0f && has(9)) list = 9;
        else if (s > 25.0f && has(8)) list = 8;
        const std::size_t w0 = out.size();
        part(list, 0x3FF, m, m);
        for (std::size_t k = w0; k < out.size(); ++k) out[k].car_mut().wheel = true;   // paint2: the wheels' tint
    }
    // payloads1: the payload objects after the wheels, each at its own matrix, lit through it (func_0021FBB0 with mask 0x3FF,
    // as the wheels) [C draft 0x21E6F0]
    for (int i = 0; i < pose.npay && i < CarPose::kMaxPayload; ++i)
        if (pose.pay_list[i] >= 0 && pose.pay_list[i] < 8) part(10 + pose.pay_list[i], 0x3FF, pose.pay[i], pose.pay[i]);
}

}  // namespace b3view
