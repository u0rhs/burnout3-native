// b3view: offline asset viewer for the native renderer work (docs/native-renderer.md §10). Loads a game file, decodes it
// with b3asset and draws it with b3gfx in its own window: free camera, uncapped frame rate, any window size.
//
//   b3view <file> [--lod N] [--palette K] [--track-lod] [--msaa N] [--vsync] [--size WxH] [--fullscreen]
//          [--driver vulkan|metal] [--yaw DEG --pitch DEG | --eye x,y,z --target x,y,z] [--no-fog] [--no-specular]
//   b3view <file> --offscreen out.png [same options] [--waypoint N] [--time S]   (no window: render once, write a PNG;
//   tracks: animated materials at S seconds of the animation clock, default 0)
// Tracks: --crash-stage N draws GAMEDATA.BGD stage N's crash ramps and pickups (docs/native-renderer.md §12.9; a race
// loads one stage, so the static view shows none unless asked; K steps through the stages that have any).
// Cars draw with the game's car model (docs/native-renderer.md §12.6): lit by one captured SH (b3view::kViewerCarSh),
// reflecting the sky of a route (--env <route dir>, default <game>/TRACKS/US/C3_V1), with its sky behind.
// <file>: PVEH/<class>/CARn.BGV or CARn.BTV, or a track route folder TRACKS/<area>/<route> (or its STATIC.DAT).
//
// Controls: hold the right or left mouse button and move to look (M: mouse look without a button; arrow keys look); W/A/S/D move, Q/E or Ctrl/Space down/up, Shift x4 speed,
// mouse wheel changes speed; R resets the camera; O overview of the whole model; tracks: N / B jump to the next /
// previous stream unit (the camera starts on unit 0), K next crash stage, T toggles the units' LOD meshes, X (regions) one version of each place, F fog, H full-screen specular,
// G sun corona and lens flare, J its sky test (water1)
// (tracks draw with the game's track shading, sky and fog from ENVIRO.DAT: docs/native-renderer.md §11); cars: P next paint, 0-3 LOD;
// L lighting on/off; F11 or Alt+Enter fullscreen; Esc quits. The title shows the frame rate, frame time, size, MSAA and present mode.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <string>

#include "b3asset/discfs.h"
#include "b3asset/export.h"
#include "b3asset/hdpack.h"
#include "b3asset/png.h"
#include "carscene.h"
#include "crashscene.h"
#include "scene.h"
#include "sun.h"

namespace {

struct Args {
    std::string file, offscreen, driver, first, env, hd_pack;
    int lod = 3, palette = 0, msaa = 4, w = 1600, h = 900, waypoint = -1, crash_stage = -1;
    bool vsync = false, fullscreen = false, track_lod = false, exclusive = false, has_eye = false, no_fog = false, no_specular = false;
    bool no_hd = false;
    bool no_sun = false, no_sun_test = false;   // --no-sun / --no-sun-test (water1)
    float yaw = 35.0f, pitch = 20.0f;
    double time = 0;   // --time: offscreen tracks drawn at this animation time (seconds after load)
    float eye[3] = {0, 0, 0}, target[3] = {0, 0, 0};
};

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (s == "--lod") a.lod = std::atoi(next());
        else if (s == "--palette") a.palette = std::atoi(next());
        else if (s == "--msaa") a.msaa = std::atoi(next());
        else if (s == "--vsync") a.vsync = true;
        else if (s == "--fullscreen") a.fullscreen = true;
        else if (s == "--driver") a.driver = next();
        else if (s == "--offscreen") a.offscreen = next();
        else if (s == "--yaw") a.yaw = float(std::atof(next()));
        else if (s == "--pitch") a.pitch = float(std::atof(next()));
        else if (s == "--track-lod") a.track_lod = true;
        else if (s == "--exclusive") a.exclusive = true;
        else if (s == "--first") a.first = next();
        else if (s == "--env") a.env = next();
        else if (s == "--no-fog") a.no_fog = true;
        else if (s == "--no-specular") a.no_specular = true;
        else if (s == "--no-sun") a.no_sun = true;
        else if (s == "--no-sun-test") a.no_sun_test = true;
        else if (s == "--hd-pack") a.hd_pack = next();   // a PCSX2 replacement folder (docs/native-renderer.md §18); else $B3_HD_PACK
        else if (s == "--no-hd") a.no_hd = true;
        else if (s == "--waypoint") a.waypoint = std::atoi(next());
        else if (s == "--crash-stage") a.crash_stage = std::atoi(next());
        else if (s == "--time") a.time = std::atof(next());
        else if (s == "--eye") { if (std::sscanf(next(), "%f,%f,%f", &a.eye[0], &a.eye[1], &a.eye[2]) != 3) return false; a.has_eye = true; }
        else if (s == "--target") { if (std::sscanf(next(), "%f,%f,%f", &a.target[0], &a.target[1], &a.target[2]) != 3) return false; }
        else if (s == "--size") { if (std::sscanf(next(), "%dx%d", &a.w, &a.h) != 2) return false; }
        else if (!s.empty() && s[0] != '-' && a.file.empty()) a.file = s;
        else return false;
    }
    return !a.file.empty() && a.w > 0 && a.h > 0;
}

struct Camera {
    float pos[3] = {0, 0, 0};
    float yaw = 0, pitch = 0;  // radians; yaw 0 looks along +z
    float speed = 5.0f;        // units per second
    void forward(float f[3]) const {
        f[0] = std::sin(yaw) * std::cos(pitch);
        f[1] = std::sin(pitch);
        f[2] = std::cos(yaw) * std::cos(pitch);
    }
};

// Frames the model's bounds from a direction (degrees).
void frame_bounds(const b3asset::Model& m, float yaw_deg, float pitch_deg, float fovy, Camera& cam) {
    float c[3], r = 0;
    for (int i = 0; i < 3; ++i) {
        c[i] = (m.bmin[i] + m.bmax[i]) / 2;
        r += (m.bmax[i] - m.bmin[i]) * (m.bmax[i] - m.bmin[i]);
    }
    r = std::sqrt(r) / 2;
    const float dist = r / std::sin(fovy / 2) * 1.05f;
    // The camera sits at yaw/pitch around the centre and looks back at it.
    const float y = yaw_deg * 3.14159265f / 180, p = pitch_deg * 3.14159265f / 180;
    const float dir[3] = {std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)};
    for (int i = 0; i < 3; ++i) cam.pos[i] = c[i] + dir[i] * dist;
    cam.yaw = std::atan2(-dir[0], -dir[2]);
    cam.pitch = -p;
    cam.speed = std::max(1.0f, r * 0.5f);
}

void look_at(const float eye[3], const float target[3], Camera& cam) {
    const float d[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    for (int i = 0; i < 3; ++i) cam.pos[i] = eye[i];
    cam.yaw = std::atan2(d[0], d[2]);
    cam.pitch = std::atan2(d[1], std::sqrt(d[0] * d[0] + d[2] * d[2]));
}

// Tracks: the route's crash-mode objects (crash2). The game's camera frame has its right row pointing to screen left;
// b3view's camera has no frame, so that row is minus the screen-right ground vector.
bool load_crash(b3gfx::Device& dev, const std::string& file, b3view::CrashScene& cs) {
    const std::string route = b3view::route_dir(file);
    std::string game = route, why;
    for (int k = 0; k < 3; ++k) { const auto p = game.find_last_of("/\\"); game = p == std::string::npos ? "." : game.substr(0, p); }
    if (b3view::load_crash_scene(dev, route, game, cs, &why)) return true;
    if (!why.empty()) std::fprintf(stderr, "%s\n", why.c_str());
    return false;
}
void crash_items(const b3view::CrashScene& cs, int stage, float yaw, std::vector<b3gfx::DrawItem>& items) {
    const float right[3] = {std::cos(yaw), 0, -std::sin(yaw)};
    b3view::add_crash_stage(cs, stage, right, items);
}

// Water (water1, docs/native-renderer.md §21): RenderWater draws only the camera unit's water objects, the +0x30 one
// mirrored about the unit's water level (b3view::WaterTable). b3view has no streaming state, so a route takes the unit
// whose centre is nearest the camera [G]; a region (merged routes, no unit table) draws the surfaces and no reflections.
struct ViewWater {
    b3view::WaterTable table;
    std::vector<char> hide;   // per scene item: drawn only through the table (or never: a region's reflections)
    bool route = false;
};
void load_water(const std::string& file, const b3view::Scene& scene, ViewWater& w) {
    w = ViewWater{};
    w.hide.assign(scene.items.size(), 0);
    const auto& draws = scene.model.draws;
    std::string dir = file;
    if (!b3asset::disc::is_dir(dir)) dir = std::filesystem::path(file).parent_path().string();   // iso1: in an image too
    std::vector<std::uint8_t> st, sm;
    w.route = b3asset::read_file(dir + "/STATIC.DAT", st);
    if (w.route) {
        b3asset::read_file(dir + "/STREAMED.DAT", sm);
        w.table.build(scene, st, sm);
    }
    for (std::size_t k = 0; k < draws.size() && k + scene.item_base < w.hide.size(); ++k) {
        const bool refl = draws[k].part.rfind("water+30 ", 0) == 0, surf = !refl && draws[k].part.rfind("water ", 0) == 0;
        if (refl || (surf && w.route)) w.hide[k + scene.item_base] = 1;
    }
}
std::vector<b3gfx::DrawItem> with_water(const b3view::Scene& scene, const ViewWater& w, const float eye[3]) {
    std::vector<b3gfx::DrawItem> out;
    out.reserve(scene.items.size());
    for (std::size_t i = 0; i < scene.items.size(); ++i) {
        if (i < w.hide.size() && w.hide[i]) continue;
        out.push_back(scene.items[i]);
    }
    if (w.route) w.table.append(scene, b3view::WaterTable::nearest_unit(scene.model, eye), out);
    return out;
}

// Sun corona and lens flare (water1, §21): the route's ENVIRO.DAT and the game's ELF (<game> = the route's ../../..);
// b3view has no capture: graphics manager +0 = 640 and SetupCamera's aspect 448 / 480 (both as captured in race2 /
// roadrage1 [C --info]).
void load_sun(b3gfx::Device& dev, const std::string& file, const b3view::Scene& scene, b3view::Sun& sun) {
    sun = b3view::Sun{};
    if (!scene.has_enviro) return;
    const std::string route = b3view::route_dir(file);
    const std::string game = std::filesystem::path(route).parent_path().parent_path().parent_path().string();
    std::string w;
    sun.load(dev, scene.enviro, route, game, &w);
    if (!w.empty()) std::fprintf(stderr, "sun: %s\n", w.c_str());
}

// Tracks: stand 10 m above unit `i`'s centre, looking at the next unit.
bool to_waypoint(const b3asset::Model& m, int i, Camera& cam) {
    const int n = int(m.waypoints.size());
    if (n < 2) return false;
    i = ((i % n) + n) % n;
    const auto& a = m.waypoints[i];
    const auto& b = m.waypoints[(i + 1) % n];
    const float eye[3] = {a[0], a[1] + 10.0f, a[2]}, target[3] = {b[0], b[1] + 6.0f, b[2]};
    look_at(eye, target, cam);
    cam.speed = 30.0f;
    return true;
}

// Cars: the game's car model on its own. No capture here, so the lighting is one real SH: the player car's
// coefficients in race2 at poll 3000 (US/C3_V1, b3replay --offscreen ... --poll 3000 prints them) [C capture value].
constexpr float kViewerCarSh[9] = {0.3893f, 0.3764f, 0.1550f, -0.0201f, -0.0322f, 0.2018f, 0.1293f, -0.0159f, -0.1250f};
struct CarView {
    b3view::CarModel car;
    b3view::CarGlobals globals;
    std::vector<b3gfx::DrawItem> sky;
    bool active = false;
};
// Loads the car for the game's model: model (part space) + slot textures + the env route's sky; the scene keeps the
// baked model for bounds and the sky / frame parameters (prelit = game frame model).
bool load_car_view(b3gfx::Device& dev, const Args& a, int lod, int palette, b3view::Scene& scene, CarView& cv, std::string* why) {
    std::vector<std::uint8_t> bytes;
    if (!b3asset::read_file(a.file, bytes)) { if (why) *why = "cannot read " + a.file; return false; }
    if (!b3view::load_car_model(dev, bytes, palette, lod, cv.car, why)) return false;
    // <game>/PVEH/<class>/CARn.BGV
    std::string game = a.file;
    for (int k = 0; k < 3; ++k) { const auto p = game.find_last_of("/\\"); game = p == std::string::npos ? "." : game.substr(0, p); }
    std::string w;
    if (!b3view::load_car_globals(dev, game, cv.globals, &w)) std::fprintf(stderr, "cars: %s (slots 1-4 untextured)\n", w.c_str());
    scene.prelit = true;
    scene.items.clear();
    const std::string env = a.env.empty() ? game + "/TRACKS/US/C3_V1" : a.env;
    if (b3view::add_enviro(dev, env, scene)) b3view::set_car_environment(dev, scene.enviro, cv.globals);
    else std::fprintf(stderr, "%s: no ENVIRO.DAT (no sky, black reflections)\n", env.c_str());
    cv.sky = scene.items;
    cv.active = true;
    char buf[160];
    std::snprintf(buf, sizeof buf, "game car model, LOD block %d, %zu draws, env %s", cv.car.lod, cv.car.draws.size(), env.c_str());
    scene.description = buf;
    return true;
}
// The car's draws for this frame (view direction and panel order follow the eye).
void car_frame(const CarView& cv, const float eye[3], std::vector<b3gfx::DrawItem>& items, b3gfx::FrameParams& fp) {
    items = cv.sky;
    b3view::CarPose pose;
    std::memcpy(pose.sh, kViewerCarSh, sizeof pose.sh);
    b3view::add_car_draws(cv.car, cv.globals, pose, eye, eye, items);
    std::memcpy(fp.sun_colour, cv.globals.sun_colour, sizeof fp.sun_colour);
}

void view_proj(const Camera& cam, float aspect, float fovy, float znear, float out[16]) {
    float f[3], v[16], p[16];
    cam.forward(f);
    const float up[3] = {0, 1, 0};
    b3gfx::mat4_look(cam.pos, f, up, v);
    b3gfx::mat4_perspective_reverse_z(fovy, aspect, znear, p);
    b3gfx::mat4_mul(p, v, out);
}

int run_offscreen(const Args& a) {
    // No visible window: b3gfx::create_headless (SDL's "offscreen" video driver for Vulkan, the platform driver with
    // at most a hidden window for Metal) and a GPU device without a swapchain.
    b3gfx::Options opt;
    opt.msaa = a.msaa;
    opt.driver = a.driver;
    std::string err;
    SDL_Window* hidden = nullptr;
    const char* how = "";
    auto dev = b3gfx::create_headless(opt, &err, &hidden, &how);
    if (!dev) { std::fprintf(stderr, "b3gfx: %s\n", err.c_str()); return 1; }
    b3asset::set_hd_compressed(dev->supports_bc());   // hdgpu: HD replacements stay BC on the GPU
    std::printf("headless device: %s (%s, video driver %s)\n", dev->driver_name(), how, SDL_GetCurrentVideoDriver());
    b3asset::Model model;
    std::string kind;
    if (!b3view::load_model(a.file, {a.lod, a.palette, a.track_lod, a.first, a.exclusive}, model, kind, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    b3view::Scene scene;
    CarView cv;
    if (kind == "car") {
        scene.model = std::move(model);
        if (!load_car_view(*dev, a, a.lod, a.palette, scene, cv, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    } else if (!b3view::build_scene(*dev, std::move(model), kind == "track", scene, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    if (kind == "track") b3view::add_enviro(*dev, b3view::route_dir(a.file), scene);
    b3view::CrashScene crash;
    if (kind == "track" && a.crash_stage >= 0 && load_crash(*dev, a.file, crash) && std::size_t(a.crash_stage) >= crash.stages.size())
        std::fprintf(stderr, "--crash-stage %d: GAMEDATA.BGD has %zu stages\n", a.crash_stage, crash.stages.size());
    const float fovy = 50.0f * 3.14159265f / 180;
    Camera cam;
    frame_bounds(scene.model, a.yaw, a.pitch, fovy, cam);
    if (a.has_eye) look_at(a.eye, a.target, cam);
    else if (a.waypoint >= 0) to_waypoint(scene.model, a.waypoint, cam);
    b3gfx::FrameParams fp;
    view_proj(cam, float(a.w) / a.h, fovy, 0.05f, fp.view_proj);
    {
        float f[3];
        cam.forward(f);
        b3view::frame_params(scene, cam.pos, f, !a.no_fog, !a.no_specular, fp);
        if (cv.active) car_frame(cv, cam.pos, scene.items, fp);
    }
    if (kind == "track" && a.crash_stage >= 0) crash_items(crash, a.crash_stage, cam.yaw, scene.items);
    if (kind == "track") {   // animated materials at --time (anim1)
        b3view::AnimClock clock;
        b3view::animate(scene, clock.at(scene.model, a.time));
    }
    std::vector<std::uint8_t> rgba;
    if (kind == "track") {   // water1: RenderWater's choice for this camera; the sun corona at --time
        ViewWater water;
        load_water(a.file, scene, water);
        scene.items = with_water(scene, water, cam.pos);
        b3view::Sun sun;
        if (!a.no_sun) load_sun(*dev, a.file, scene, sun);
        sun.frame(fp, float(a.w) / float(a.h), 640, 448.0f / 480.0f, float(a.time));
        fp.sun_occlusion = !a.no_sun_test;
    }
    if (!dev->draw_offscreen(a.w, a.h, fp, scene.items, rgba)) { std::fprintf(stderr, "offscreen draw failed: %s\n", SDL_GetError()); return 1; }
    if (!b3asset::write_png_rgba(a.offscreen, a.w, a.h, rgba.data())) { std::fprintf(stderr, "cannot write %s\n", a.offscreen.c_str()); return 1; }
    b3asset::hd_report();
    std::printf("%s: %s; driver %s, MSAA %dx -> %s\n", kind.c_str(), scene.description.c_str(), dev->driver_name(),
                dev->msaa(), a.offscreen.c_str());
    dev.reset();
    if (hidden) SDL_DestroyWindow(hidden);
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) {
        std::fprintf(stderr, "usage: b3view <CARn.BGV|CARn.BTV|TRACKS/<area>/<route>> [--lod N] [--palette K] [--track-lod] [--msaa N]"
                             " [--vsync] [--size WxH] [--fullscreen] [--driver vulkan|metal] [--yaw D --pitch D | --eye x,y,z"
                             " --target x,y,z] [--offscreen out.png [--time S]] [--crash-stage N] [--hd-pack <dir> | --no-hd]\n");
        return 1;
    }
    if (b3asset::hd_enable(a.hd_pack, a.no_hd))
        b3asset::hd_prefetch({b3asset::disc::is_dir(a.file) ? a.file + "/STATIC.DAT" : a.file});
    if (!a.offscreen.empty()) return run_offscreen(a);

    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("b3view", a.w, a.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                                           (a.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    b3gfx::Options opt;
    opt.msaa = a.msaa;
    opt.driver = a.driver;
    opt.present = a.vsync ? b3gfx::PresentMode::kVsync : b3gfx::PresentMode::kUncapped;
    std::string err;
    auto dev = b3gfx::Device::create(win, opt, &err);
    if (!dev) { std::fprintf(stderr, "b3gfx: %s\n", err.c_str()); return 1; }
    b3asset::set_hd_compressed(dev->supports_bc());   // hdgpu: HD replacements stay BC on the GPU

    b3view::LoadOptions lo{a.lod, a.palette, a.track_lod, a.first, a.exclusive};
    b3view::Scene scene;
    CarView cv;
    std::string kind;
    b3view::CrashScene crash;
    ViewWater water;
    b3view::Sun sun;
    bool sun_on = !a.no_sun, sun_test = !a.no_sun_test;
    int crash_stage = a.crash_stage;
    auto load = [&]() -> bool {
        b3asset::Model model;
        dev->clear_resources();
        cv = CarView{};
        bool ok = b3view::load_model(a.file, lo, model, kind, &err);
        if (ok && kind == "car") {
            scene = b3view::Scene{};
            scene.model = std::move(model);
            ok = load_car_view(*dev, a, lo.lod, lo.palette, scene, cv, &err);
        } else if (ok) {
            ok = b3view::build_scene(*dev, std::move(model), kind == "track", scene, &err);
        }
        if (!ok) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return false;
        }
        if (kind == "track" && b3view::add_enviro(*dev, b3view::route_dir(a.file), scene))
            std::printf("ENVIRO.DAT: sky, sun (%.2f %.2f %.2f), fog, specular\n", double(scene.enviro.to_sun[0]),
                        double(scene.enviro.to_sun[1]), double(scene.enviro.to_sun[2]));
        std::printf("%s %s: %s\n", kind.c_str(), a.file.c_str(), scene.description.c_str());
        if (kind == "track" && load_crash(*dev, a.file, crash)) std::printf("GAMEDATA.BGD: %zu stages (K: crash ramps and pickups)\n", crash.stages.size());
        if (kind == "track") load_water(a.file, scene, water);
        if (kind == "track") load_sun(*dev, a.file, scene, sun);
        return true;
    };
    if (!load()) return 1;
    std::printf("driver %s, MSAA %dx, present %s\n", dev->driver_name(), dev->msaa(), dev->present_mode_name());

    const float fovy = 60.0f * 3.14159265f / 180;
    Camera cam;
    int waypoint = a.waypoint > 0 ? a.waypoint : 0;
    auto reset_camera = [&]() {
        if (a.has_eye) look_at(a.eye, a.target, cam);
        else if (!to_waypoint(scene.model, waypoint, cam)) frame_bounds(scene.model, a.yaw, a.pitch, fovy, cam);
    };
    reset_camera();
    bool running = true, looking = false, mouse_look = false, lighting = true, fog = !a.no_fog, specular = !a.no_specular;
    Uint64 last = SDL_GetPerformanceCounter(), title_at = last;
    const double freq = double(SDL_GetPerformanceFrequency());
    b3view::AnimClock anim_clock;   // animated track materials (anim1): wall clock from here, 1/60 s ticks
    const Uint64 anim_start = last;
    int frames = 0;
    int w = a.w, h = a.h;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: running = false; break;
            // Look: hold the right or left button (trackpads: click and drag), or M for mouse look without a button.
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_LEFT) { looking = true; SDL_SetWindowRelativeMouseMode(win, true); }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if ((e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_LEFT) && !mouse_look) { looking = false; SDL_SetWindowRelativeMouseMode(win, false); }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (looking || mouse_look) {
                    cam.yaw -= e.motion.xrel * 0.0025f;
                    cam.pitch = std::fmax(-1.55f, std::fmin(1.55f, cam.pitch - e.motion.yrel * 0.0025f));
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                cam.speed *= e.wheel.y > 0 ? 1.25f : 0.8f;
                break;
            case SDL_EVENT_KEY_DOWN: {
                const SDL_Keycode k = e.key.key;
                if (k == SDLK_ESCAPE) running = false;
                else if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)))
                    SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                else if (k == SDLK_M) { mouse_look = !mouse_look; looking = false; SDL_SetWindowRelativeMouseMode(win, mouse_look); }
                else if (k == SDLK_R) reset_camera();
                else if (k == SDLK_N || k == SDLK_B) { waypoint += k == SDLK_N ? 1 : -1; to_waypoint(scene.model, waypoint, cam); }
                else if (k == SDLK_O) frame_bounds(scene.model, a.yaw, a.pitch, fovy, cam);
                else if (k == SDLK_K && kind == "track" && !crash.stages.empty()) {
                    // the next stage with crash ramps or pickups, then none
                    int n = int(crash.stages.size()), st = crash_stage;
                    do { st = st + 1 >= n ? -1 : st + 1; } while (st >= 0 && crash.stages[std::size_t(st)].ramps.empty() && crash.stages[std::size_t(st)].pickups.empty());
                    crash_stage = st;
                    if (st < 0) std::printf("crash stage: none\n");
                    else std::printf("crash stage %d (id %016llx): %zu ramps, %zu pickups\n", st, static_cast<unsigned long long>(crash.stages[std::size_t(st)].id),
                                     crash.stages[std::size_t(st)].ramps.size(), crash.stages[std::size_t(st)].pickups.size());
                }
                else if (k == SDLK_T && kind == "track") { lo.track_lod = !lo.track_lod; load(); }
                else if (k == SDLK_X && kind == "track") { lo.region_exclusive = !lo.region_exclusive; load(); }
                else if (k == SDLK_F) fog = !fog;
                else if (k == SDLK_H) specular = !specular;
                else if (k == SDLK_G) sun_on = !sun_on;
                else if (k == SDLK_J) sun_test = !sun_test;
                else if (k == SDLK_L) {
                    lighting = !lighting;
                    for (auto& it : scene.items) it.lighting = lighting && !scene.prelit ? 1.0f : 0.0f;
                } else if (k == SDLK_P && kind == "car") {   // next paint, wrapping at the file's count (palettes past it draw the base)
                    const auto np = scene.model.stats.find("palettes");
                    if (++lo.palette >= (np != scene.model.stats.end() ? int(np->second) : 1)) lo.palette = 0;
                    load();
                }
                else if (k >= SDLK_0 && k <= SDLK_3 && kind == "car") { lo.lod = int(k - SDLK_0); load(); }
                break;
            }
            default: break;
            }
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        const float dt = float(double(now - last) / freq);
        last = now;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float f[3];
        cam.forward(f);
        const float right[3] = {-std::cos(cam.yaw), 0, std::sin(cam.yaw)};  // cross(forward, up) on the ground plane
        const float sp = cam.speed * dt * ((keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) ? 4.0f : 1.0f);
        for (int i = 0; i < 3; ++i) {
            if (keys[SDL_SCANCODE_W]) cam.pos[i] += f[i] * sp;
            if (keys[SDL_SCANCODE_S]) cam.pos[i] -= f[i] * sp;
            if (keys[SDL_SCANCODE_D]) cam.pos[i] += right[i] * sp;
            if (keys[SDL_SCANCODE_A]) cam.pos[i] -= right[i] * sp;
        }
        // Arrow keys look (1.5 rad/s).
        cam.yaw += 1.5f * dt * ((keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f));
        cam.pitch = std::fmax(-1.55f, std::fmin(1.55f, cam.pitch + 1.5f * dt * ((keys[SDL_SCANCODE_UP] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f))));
        if (keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_SPACE]) cam.pos[1] += sp;
        if (keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_LCTRL]) cam.pos[1] -= sp;

        b3gfx::FrameParams fp;
        view_proj(cam, float(w) / float(h > 0 ? h : 1), fovy, 0.05f, fp.view_proj);
        b3view::frame_params(scene, cam.pos, f, fog, specular, fp);
        if (cv.active) car_frame(cv, cam.pos, scene.items, fp);
        if (kind == "track") b3view::animate(scene, anim_clock.at(scene.model, double(now - anim_start) / freq));   // anim1: wall clock
        if (kind == "track" && sun_on) {   // water1: the sun corona (G; J: its sky test)
            sun.frame(fp, float(w) / float(h > 0 ? h : 1), 640, 448.0f / 480.0f, float(double(now - anim_start) / freq));
            fp.sun_occlusion = sun_test;
        }
        if (kind == "track") {   // water1: RenderWater's choice for this camera; crash ramps and pickups
            std::vector<b3gfx::DrawItem> items = with_water(scene, water, cam.pos);
            if (crash_stage >= 0) crash_items(crash, crash_stage, cam.yaw, items);
            dev->draw_frame(fp, items, &w, &h);
        } else {
            dev->draw_frame(fp, scene.items, &w, &h);
        }
        ++frames;
        if (double(now - title_at) / freq >= 0.5) {
            const double secs = double(now - title_at) / freq;
            char t[256];
            std::snprintf(t, sizeof t, "b3view - %s%s - at %.0f %.0f %.0f - %.0f fps (%.2f ms) - %dx%d - MSAA %dx - %s - %s", a.file.c_str(),
                          lo.region_exclusive ? " (X: one version of each place)" : "", double(cam.pos[0]), double(cam.pos[1]), double(cam.pos[2]),
                          frames / secs, 1000.0 * secs / frames, w, h, dev->msaa(), dev->present_mode_name(), dev->driver_name());
            SDL_SetWindowTitle(win, t);
            frames = 0;
            title_at = now;
        }
    }
    dev.reset();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
