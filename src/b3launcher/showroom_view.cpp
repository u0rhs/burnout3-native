// The launcher's showroom picture: src/b3launcher/showroom_view.h.
#include "showroom_view.h"

#include "b3asset/car.h"
#include "b3asset/discfs.h"
#include "b3asset/gs_memory.h"
#include "b3asset/hdpack.h"
#include "b3gfx/b3gfx.h"
#include "../b3view/carscene.h"
#include "../b3view/scene.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace b3launcher::showroom {

namespace fs = std::filesystem;
namespace disc = b3asset::disc;

namespace {

constexpr float kPi = 3.14159265358979f;
// The car's light: b3view's (src/b3view/main.cpp kViewerCarSh), the player car's SH coefficients in a captured race
// (US/C3_V1, poll 3000)
constexpr float kCarSh[9] = {0.3893f, 0.3764f, 0.1550f, -0.0201f, -0.0322f, 0.2018f, 0.1293f, -0.0159f, -0.1250f};
constexpr float kFovY = 34.0f * kPi / 180.0f;
// the background (display colour) and the floor's middle; the floor's edge is the background
constexpr float kClear[3] = {0.020f, 0.045f, 0.095f};
constexpr float kFloor[3] = {0.150f, 0.205f, 0.300f};

std::uint32_t rgba8(float r, float g, float b, float a) {
    auto c = [](float v) { return std::uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
}

// A disc in the xz plane at height y, `n` segments, rings from the centre outwards with their colours (frame units)
void disc_mesh(float cx, float cz, float y, float rx, float rz, int n, const std::vector<std::pair<float, std::uint32_t>>& rings,
               std::vector<b3gfx::Vertex>& vs, std::vector<std::uint32_t>& is) {
    const std::uint32_t base = std::uint32_t(vs.size());
    for (const auto& [f, col] : rings) {
        for (int k = 0; k < n; ++k) {
            const float a = 2.0f * kPi * float(k) / float(n);
            b3gfx::Vertex v{};
            v.pos[0] = cx + std::cos(a) * rx * f;
            v.pos[1] = y;
            v.pos[2] = cz + std::sin(a) * rz * f;
            v.normal[1] = 1;
            v.rgba = col;
            vs.push_back(v);
        }
    }
    for (std::size_t r = 0; r + 1 < rings.size(); ++r)
        for (int k = 0; k < n; ++k) {
            const std::uint32_t a = base + std::uint32_t(r * std::size_t(n) + std::size_t(k));
            const std::uint32_t b = base + std::uint32_t(r * std::size_t(n) + std::size_t((k + 1) % n));
            const std::uint32_t c = a + std::uint32_t(n), d = b + std::uint32_t(n);
            is.insert(is.end(), {a, c, b, b, c, d});
        }
}

}  // namespace

struct View::Impl {
    std::unique_ptr<b3gfx::Device> dev;
    bool tried = false;
    std::string fail;
    // the loaded car
    bool loaded = false;
    b3view::CarModel car;
    b3view::CarGlobals globals;
    b3view::Scene env;   // the route's ENVIRO.DAT (the env map's sky, the sun)
    float centre[3] = {}, radius = 1, floor_y = 0, half_x = 1, half_y = 0.7f, half_z = 2;
    int paints = 1, paint = 0;
    b3gfx::BufferId floor_vb = b3gfx::kNone, floor_ib = b3gfx::kNone, shadow_vb = b3gfx::kNone, shadow_ib = b3gfx::kNone;
    std::uint32_t floor_n = 0, shadow_n = 0;
    // showhd1: the HD packs (set_hd) and the textures the last load replaced
    b3asset::HdTextures hd;
    std::vector<std::string> hd_dirs;
    unsigned hd_gen = 0;
    bool hd_set = false;
    std::size_t hd_replaced = 0;
};

namespace {
// The decodes inside it run with `t` as the replacement hook (nullptr: none), the hook before it back afterwards
struct HdHook {
    b3asset::HdTextures* saved;
    explicit HdHook(b3asset::HdTextures* t) : saved(b3asset::hd_textures()) { b3asset::set_hd_textures(t); }
    ~HdHook() { b3asset::set_hd_textures(saved); }
    HdHook(const HdHook&) = delete;
    HdHook& operator=(const HdHook&) = delete;
};
}  // namespace

View::View() : p_(std::make_unique<Impl>()) {}
View::~View() = default;

bool View::ready(std::string* why) {
    Impl& p = *p_;
    if (!p.tried) {
        p.tried = true;
        b3gfx::Options opt;
        opt.msaa = 8;
        p.dev = b3gfx::Device::create(nullptr, opt, &p.fail);
        if (!p.dev && p.fail.empty()) p.fail = "no GPU device";
        if (p.dev) std::printf("[b3launcher] showroom: %s device, MSAA %d\n", p.dev->driver_name(), p.dev->msaa());
        else std::printf("[b3launcher] showroom: no GPU device: %s\n", p.fail.c_str());
        std::fflush(stdout);
    }
    if (!p.dev && why) *why = p.fail;
    return p.dev != nullptr;
}

const char* View::driver() const { return p_->dev ? p_->dev->driver_name() : "none"; }
bool View::loaded() const { return p_->loaded; }
int View::paints() const { return p_->paints; }
int View::paint() const { return p_->paint; }

std::size_t View::hd_indexed() const { return p_->hd.pack.files.size(); }
std::size_t View::hd_replaced() const { return p_->hd_replaced; }

bool View::set_hd(const std::vector<std::string>& folders, unsigned generation) {
    Impl& p = *p_;
    if (p.hd_set && folders == p.hd_dirs && generation == p.hd_gen) return false;
    const bool had = !p.hd.pack.files.empty();
    p.hd_set = true;
    p.hd_dirs = folders;
    p.hd_gen = generation;
    p.hd = b3asset::HdTextures{};
    if (!folders.empty()) {
        std::string names, why;
        for (const std::string& f : folders) names += (names.empty() ? "" : " + ") + f;
        const std::uint64_t t0 = SDL_GetTicksNS();
        p.hd.open(folders, &why);
        std::printf("[b3launcher] showroom: HD textures %s: %zu replacements indexed in %.0f ms%s%s\n", names.c_str(), p.hd.pack.files.size(),
                    double(SDL_GetTicksNS() - t0) / 1e6, why.empty() ? "" : "; ", why.c_str());
        std::fflush(stdout);
    }
    return had || !p.hd.pack.files.empty();
}

bool View::load(const fs::path& game_dir, const CarEntry& c, int paint, std::string* why) {
    Impl& p = *p_;
    p.loaded = false;
    if (!ready(why)) return false;
    std::vector<std::uint8_t> bytes;
    if (!disc::read_file(c.file, bytes)) {
        if (why) *why = "cannot read " + c.file.string();
        return false;
    }
    // the paint count and the bounds from the car as the game assembles it (the parts at their frames)
    b3asset::Model baked;
    {
        b3asset::GsMemory gs;
        b3asset::CarOptions co;
        co.lod = 3;
        if (!b3asset::read_car(b3asset::Bytes{bytes.data(), bytes.size()}, co, gs, baked, why)) return false;
    }
    const auto np = baked.stats.find("palettes");
    p.paints = std::max(1, np != baked.stats.end() ? int(np->second) : 1);
    p.paint = ((paint % p.paints) + p.paints) % p.paints;
    p.dev->clear_resources();   // the last car's buffers and textures
    p.floor_vb = p.floor_ib = p.shadow_vb = p.shadow_ib = b3gfx::kNone;
    p.car = b3view::CarModel{};
    p.globals = b3view::CarGlobals{};
    p.env = b3view::Scene{};
    p.hd_replaced = 0;
    {   // showhd1: the textures through the HD packs, as the race renderer's decodes (its View::car_model: load_car_model)
        const HdHook hook(p.hd.pack.files.empty() ? nullptr : &p.hd);
        const std::size_t hits0 = p.hd.hits;
        if (!b3view::load_car_model(*p.dev, bytes, p.paint, 3, p.car, why)) return false;
        std::string w;
        if (!b3view::load_car_globals(*p.dev, game_dir.string(), p.globals, &w))
            std::printf("[b3launcher] showroom: %s (glass and underside untextured)\n", w.c_str());
        const fs::path route = disc::resolve_ci(game_dir, "TRACKS/US/C3_V1");   // the reflections: b3view's default route
        p.env.prelit = true;
        if (!route.empty() && b3view::add_enviro(*p.dev, route.string(), p.env)) b3view::set_car_environment(*p.dev, p.env.enviro, p.globals);
        p.hd_replaced = p.hd.hits - hits0;
        p.hd.trim();   // the decoded files are on the GPU now (another car or paint reads its own again)
    }
    for (int k = 0; k < 3; ++k) p.centre[k] = 0.5f * (baked.bmin[k] + baked.bmax[k]);
    const float ex = baked.bmax[0] - baked.bmin[0], ey = baked.bmax[1] - baked.bmin[1], ez = baked.bmax[2] - baked.bmin[2];
    p.radius = std::max(0.5f, 0.5f * std::sqrt(ex * ex + ey * ey + ez * ez));
    p.floor_y = baked.bmin[1];
    p.half_x = std::max(0.3f, 0.5f * ex);
    p.half_z = std::max(0.3f, 0.5f * ez);
    p.half_y = std::max(0.3f, 0.5f * ey);
    // the floor (opaque, unlit: frame units, half the display colour) fading into the background, and the car's shadow
    std::vector<b3gfx::Vertex> vs;
    std::vector<std::uint32_t> is;
    const float R = 3.2f * p.radius;
    auto frame = [](const float c[3], float s) { return rgba8(0.5f * c[0] * s, 0.5f * c[1] * s, 0.5f * c[2] * s, 1); };
    const float mid[3] = {kFloor[0], kFloor[1], kFloor[2]};
    auto mixc = [&](float t) {
        const float c[3] = {mid[0] + (kClear[0] - mid[0]) * t, mid[1] + (kClear[1] - mid[1]) * t, mid[2] + (kClear[2] - mid[2]) * t};
        return frame(c, 1.0f);
    };
    disc_mesh(p.centre[0], p.centre[2], p.floor_y - 0.005f, R, R, 128,
              {{0.0f, mixc(0)}, {0.25f, mixc(0.08f)}, {0.5f, mixc(0.35f)}, {0.75f, mixc(0.75f)}, {1.0f, mixc(1)}}, vs, is);
    p.floor_vb = p.dev->create_vertex_buffer(vs.data(), vs.size());
    p.floor_ib = p.dev->create_index_buffer(is.data(), is.size());
    p.floor_n = std::uint32_t(is.size());
    vs.clear();
    is.clear();
    disc_mesh(0, 0, p.floor_y, p.half_x * 1.25f, p.half_z * 1.18f, 96,
              {{0.0f, rgba8(0, 0, 0, 0.72f)}, {0.6f, rgba8(0, 0, 0, 0.6f)}, {0.85f, rgba8(0, 0, 0, 0.25f)}, {1.0f, rgba8(0, 0, 0, 0)}}, vs, is);
    p.shadow_vb = p.dev->create_vertex_buffer(vs.data(), vs.size());
    p.shadow_ib = p.dev->create_index_buffer(is.data(), is.size());
    p.shadow_n = std::uint32_t(is.size());
    p.loaded = true;
    std::printf("[b3launcher] showroom: %s (%s), paint %d of %d, LOD block %d, %zu draws\n", c.name.c_str(), c.file.string().c_str(), p.paint + 1,
                p.paints, p.car.lod, p.car.draws.size());
    if (!p.hd.pack.files.empty()) std::printf("[b3launcher] showroom: %zu textures from the HD packs\n", p.hd_replaced);
    std::fflush(stdout);
    return true;
}

bool View::draw(const Turntable& t, int w, int h, std::vector<std::uint8_t>& rgba, bool car) {
    Impl& p = *p_;
    if (!p.dev || !p.loaded || w <= 0 || h <= 0) return false;
    // the car turned about its middle (row vectors: p' = p R + P, P = c - c R on the ground plane)
    const float ca = std::cos(t.yaw), sa = std::sin(t.yaw);
    b3view::RowMat rot;
    rot.r[0][0] = ca; rot.r[0][2] = -sa;
    rot.r[2][0] = sa; rot.r[2][2] = ca;
    const float cx = p.centre[0], cz = p.centre[2];
    rot.r[3][0] = cx - (cx * ca + cz * sa);
    rot.r[3][2] = cz - (cx * -sa + cz * ca);
    // the camera: in front of the turntable, at the framing distance x zoom, raised by `pitch`, looking at the middle
    const float aspect = float(w) / float(h);
    // (the car's footprint radius across the view, its height and the footprint's depth seen from `pitch` up the view)
    const float tv = std::tan(0.5f * kFovY), th = tv * aspect;
    const float foot = std::sqrt(p.half_x * p.half_x + p.half_z * p.half_z);
    const float fit = std::max(foot * 0.95f / th, (p.half_y + foot * std::sin(t.pitch)) * 1.2f / tv);
    const float dist = fit * 1.08f * t.zoom;
    const float target[3] = {p.centre[0], p.centre[1] - 0.3f * p.half_y, p.centre[2]};
    const float eye[3] = {target[0], target[1] + std::sin(t.pitch) * dist, target[2] + std::cos(t.pitch) * dist};
    float fwd[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    for (float& v : fwd) v /= fl;
    b3gfx::FrameParams fp;
    float v[16], pr[16];
    const float up[3] = {0, 1, 0};
    b3gfx::mat4_look(eye, fwd, up, v);
    b3gfx::mat4_perspective_reverse_z(kFovY, aspect, 0.05f, pr);
    b3gfx::mat4_mul(pr, v, fp.view_proj);
    b3view::frame_params(p.env, eye, fwd, false, false, fp);
    fp.present_scale = 255.0f / 128.0f;
    for (int k = 0; k < 3; ++k) fp.clear[k] = kClear[k];
    std::memcpy(fp.sun_colour, p.globals.sun_colour, sizeof fp.sun_colour);
    std::vector<b3gfx::DrawItem> items;
    b3gfx::DrawItem floor;
    floor.vertices = p.floor_vb;
    floor.indices = p.floor_ib;
    floor.index_count = p.floor_n;
    floor.lighting = 0;
    items.push_back(floor);
    b3gfx::DrawItem shadow = floor;
    shadow.vertices = p.shadow_vb;
    shadow.indices = p.shadow_ib;
    shadow.index_count = p.shadow_n;
    shadow.blend = b3gfx::Blend::kAlpha;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) shadow.model[i * 4 + j] = rot.r[i][j];
    for (int j = 0; j < 3; ++j) shadow.model[12 + j] = rot.r[3][j];
    items.push_back(shadow);
    b3view::CarPose pose;
    std::memcpy(pose.sh, kCarSh, sizeof pose.sh);
    pose.main = pose.body = rot;
    if (car) b3view::add_car_draws(p.car, p.globals, pose, eye, eye, items);
    return p.dev->draw_offscreen(w, h, fp, items, rgba);
}

}  // namespace b3launcher::showroom
