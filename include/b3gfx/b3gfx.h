// b3gfx: the port's thin GPU layer (docs/native-renderer.md §5 and §10). Backend today: SDL3 SDL_GPU (Vulkan on
// Windows/Linux, Metal on macOS; D3D12 once DXIL shaders are generated). The interface is backend-neutral (opaque
// ids, no SDL types) so a bgfx or raw Vulkan backend stays possible.
//
// Scope of this first cut: static meshes (b3gfx::Vertex), RGBA8 textures with stored or generated mip chains, three
// blend modes, reverse-Z depth (D32F), MSAA resolved into the swapchain, uncapped presentation (IMMEDIATE, else
// MAILBOX, else VSYNC), and an offscreen target with read-back for headless checks.
#ifndef B3GFX_B3GFX_H
#define B3GFX_B3GFX_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace b3gfx {

// Must match b3asset::Vertex (static_assert in the viewer): pos, normal, uv, RGBA8, two u16 (ignored here).
struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
    std::uint32_t rgba;
    std::uint16_t extra[2];
};
static_assert(sizeof(Vertex) == 40, "vertex layout");

enum class Blend : std::uint8_t { kOpaque, kAlphaTest, kAlpha };
enum class PresentMode : std::uint8_t { kUncapped, kVsync };

struct Options {
    int msaa = 4;                              // 1, 2, 4 or 8 (clamped to what the device supports)
    PresentMode present = PresentMode::kUncapped;
    float anisotropy = 16.0f;                  // 0 or 1 = off
    bool point = false, mip_nearest = false;   // settings1: the 3D textures' filter (point; bilinear = mip_nearest); the 2D stays bilinear
    bool debug = false;                        // backend validation layers
    std::string driver;                        // "" = SDL's choice, or "vulkan" / "metal" / "direct3d12"
    int frames_in_flight = 0;                  // lat1: window frames the GPU may queue (1..3; 0 = SDL's default, 2)
    // perf1 (docs/native-renderer.md §33.8): an ordered dither of +-dither/2 output steps at the present pass, against the
    // visible 8-bit steps of shallow gradients (the sky); 0 = off (the default: output byte-identical)
    float dither = 0.0f;
    // effects2: the 3D picture (scene, effects and the full-screen passes) drawn at this share of the output's size, then scaled
    // up into it; the 2D stage stays at the output's size. 1 = the picture as before (the default: output byte-identical).
    // perf4 (supersampling): above 1 (up to 2) drawn larger and brought down with an area (box) filter
    float render_scale = 1.0f;
    // finalfix: SUN SHADOWS and AMBIENT OCCLUSION on at start (b3replay::scale_options): their pipelines are made with the
    // fixed set (warm_pipelines), not at their first draw in the first race. sun_shadow_side: the map's side (texels) the
    // supersampling budget keeps aside for it (0: none). Drawing does not depend on these: the frame's FrameParams decide.
    bool warm_sun_shadows = false, warm_ambient_occlusion = false;
    int sun_shadow_side = 0;
};

// hdgpu: `bc` = 1 / 2 / 3 (b3asset/bc.h kBC1..kBC3): `rgba` points at `bytes` of BC blocks instead (Device::supports_bc)
struct TextureLevel { int w = 0, h = 0; const std::uint8_t* rgba = nullptr; int bc = 0; std::size_t bytes = 0; };

using BufferId = std::uint32_t;
using TextureId = std::uint32_t;
constexpr std::uint32_t kNone = 0xFFFFFFFFu;

// How a draw is shaded. kSimple: tint x vertex colour x texture with an optional N.L light (cars, legacy). kTrack: the
// game's track model (docs/native-renderer.md §11): the track VU1 programs' per-vertex math (types 0/6, 1 specular
// alpha, 2 reflection texgen, instances' colour) and the GS (MODULATE, alpha test, ALPHA/FIX blends), drawn into the
// half-intensity "GS frame" that the present pass scales by FrameParams::present_scale. kSky: kTrack on vertices that
// are directions around the camera (sky dome, docs/sky-enviro.md), at the far plane.
// kCar: the game's car model (docs/car-shading.md, b3asset/carshade.h): VU1 0x46BB80's SH irradiance lighting and
// Fresnel alpha, then the GS: MODULATE, ALPHA_1, and the environment pass (ALPHA_2 by the destination alpha) folded into
// the same draw (see CarParams); drawn among the opaque draws in list order (glass included: the game draws cars before
// the full-screen specular).
enum class Shading : std::uint8_t { kSimple, kTrack, kSky, kCar };

// Per part / mesh entry inputs of the car model, in world space (VU0 0x47F6D0's part-space constants rotated back: the
// lighting and reflection are invariant under the part rotation, b3vucheck_car --shader checks it).
struct CarParams {
    float sh[16] = {};               // column-major 4x4 irradiance matrix (R&H) of the car's 9 SH coefficients, world axes
    float light[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // normal -> lighting axes (the body's rotation for
                                     // panels: RenderObject's bool false reuses the body's SH matrix [C])
    float sh_k = 0.0f;               // b3asset::car_irradiance_k for the lighting axes' at row
    float view_dir[3] = {0, 0, 1};   // normalise(car position - camera position), world (VU1 970)
    float base[4] = {}, full[4] = {};  // VU1 1010 / 1011 in GS units: colour = base + E (full - base), Fresnel alpha
    float env_colour[4] = {};        // VU1 1012: env-pass vertex colour (GS units)
    bool alpha_as = false;           // ALPHA_1 C = As (0x44) instead of FIX (0x64)
    float fade = 1.0f;               // in (0, 1): a screen-door fade (the Time Attack ghost); negative (traffic-popin):
                                     // the car at opacity -fade over the frame (premultiplied blend, Z still written)
    float fix = 128.0f;              // ALPHA_1 FIX byte
    int env_mode = 0;                // 0 no env pass (or ALPHA_2 0x6A), 1 ALPHA_2 0x54 lerp by Ad, 2 ALPHA_2 0x58 add Cs Ad
    std::uint32_t env_texture = 0xFFFFFFFFu;  // the sky backdrop sampled with the env-map dome mapping (kNone: black)
    bool env_frame_units = false;    // fe2: env_texture holds frame units (texel / 255) instead of display units (x 0.5)
    float reflect = 0.0f;            // > 0: the enhanced reflections' strength (car.frag: Fresnel sky / horizon / sun on the paint
                                     // and glass, in place of the game's env pass); 0: the game's pass as it is
    std::uint32_t env_world = 0xFFFFFFFFu;  // reflect-tunnel: a Device::render_env map (the track around the car, in front of
                                     // env_texture's sky), kNone: the sky alone
    float sky_open = 1.0f;           // 0..1: how much sky the car sees (its SH irradiance's upward share; the enhanced reflections'
                                     // gate: low in tunnels and under cover)
    int zones = 0;                   // RenderDeformedObject's zone count (8 body, 2 panel; 0 = RenderObject, undeformed)
    float zone[8][16] = {};          // its zone matrices, part space, column-major (VU1 974 + 4 k)
    // carpaint (docs/remaster-features.md §17, b3asset/carpaint.h): the player's own paint over the atlas, drawn only.
    // paint_mode 0 (the default): the texture as it is. Bit 0: the paint recoloured (main paint -> paint, second colour ->
    // paint2, each at the texel's shade: its brightest channel / paint_ref). Bit 1: decals off (decals and second colours
    // -> paint at the shade the mask holds). Bit 2 / 3 (padpaint): the finish, metallic / pearl (car.frag). paint_mask:
    // the car's CarPaintMap::mask texture (kNone: mode ignored)
    int paint_mode = 0;
    float paint[3] = {}, paint2[3] = {};   // texel units (texel byte / 255)
    float paint_ref = 1.0f;
    std::uint32_t paint_mask = 0xFFFFFFFFu;
};

struct DrawItem {
    BufferId vertices = kNone, indices = kNone;  // index buffer of u32
    std::uint32_t first_index = 0, index_count = 0;
    TextureId texture = kNone;                   // kNone = white
    Blend blend = Blend::kOpaque;
    float tint[4] = {1, 1, 1, 1};
    float model[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
    float lighting = 1.0f;                       // 0 = unlit (prelit vertex colour), 1 = simple N.L
    float alpha_ref = 0.5f;                      // Blend::kAlphaTest: discard below
    // kTrack / kSky only:
    Shading shading = Shading::kSimple;
    int track_type = 0;                          // material type 0..7 (+0x00)
    float track_param[2] = {0, 0};               // material +0x04 / +0x08 (specular strength / exponent)
    float instance[4] = {0, 0, 0, -1};           // instances: rgb = colour in GS bytes / 128, w = alpha in GS bytes; w < 0: none
    std::uint32_t gs_test = 0;                   // GS TEST_1 of the material (alpha test)
    bool textured = true;                        // false: the GS takes the vertex colour as is
    float blend_fix = -1.0f;                     // >= 0: blend by this constant (GS ALPHA with C = FIX), not by As
    bool cull = false;                           // back faces culled (CCW front, right-handed view)
    float uv_scroll = 0.0f;                      // added to u (material flag 0x200)
    // Post-fog stage (crash2, §12.9): CB3World::Render draws CB3GameData::Draw's B3Im3d objects after the fog pass. Such
    // items draw after the effects stage, in list order, unfogged, Z tested and written (B3Im3d keeps the camera's ZBUF).
    bool post_fog = false;
    // Night mode (a remaster feature, off by default; docs/remaster-features.md §17): a headlight pool (shaders/light.frag
    // over mesh.vert's simple model: uv = lateral / forward metres, colour = the lamp's, normal.x = the car's half width),
    // drawn in the post-fog stage after the night darkening, the pre-grade colour x the lamp's blended over the frame
    // with the beam's alpha (FrameParams::Night::spot false only: the presets without the spot pass), Z tested, not written.
    // `night_shape` is the shader's beam (tan of the half angle, range, the near pool's gain, the far throw's gain). Only FrameParams::night.on frames
    // carry such items.
    bool night_light = false;
    float night_shape[4] = {0, 0, 0, 0};
    // Night mode: a post-fog effect that gives light (coronas, sparks, fire, the explosion flash) is drawn after the
    // grade, so it stays bright and feeds the bloom; the other post-fog draws (smoke, dust, debris) are graded with the
    // frame. Ignored by day.
    bool night_glow = false;
    // Night mode (nightlights, §17.2): a track draw whose texture is a light fixture (a tunnel's lamp strips and panels, by
    // texture name per region). At night it is drawn once more after the grade and the night sky (beside the coronas),
    // depth GEQUAL, keeping only its bright texels at their pre-grade colour (1), or every texel (2: a backlit panel, an
    // advertising light box), so the lamps and signs glow and reach the bloom. 0: none. Ignored by day.
    std::uint8_t night_emissive = 0;
    // kCar only. Held by pointer: CarParams is ~700 bytes (zone matrices), and a track item is copied into the draw list
    // ~2,000 times a frame; only the few car items own one. car() reads (defaults when none), car_mut() makes / unshares it.
    std::shared_ptr<CarParams> car_;
    const CarParams& car() const { static const CarParams none; return car_ ? *car_ : none; }
    CarParams& car_mut() {
        if (!car_) car_ = std::make_shared<CarParams>();
        else if (car_.use_count() > 1) car_ = std::make_shared<CarParams>(*car_);
        return *car_;
    }
    // Effects stage (fx1, docs/native-renderer.md §14): drawn after every other draw (after the blended ones), in list
    // order, as CB3World::Render draws shadows and the pre-fog effects after the track, the cars, the full-screen
    // specular and the props. Shaded like kTrack (MODULATE, TEST_1, fog); the GS ALPHA register `gs_alpha` (A, B, C, D
    // in bits 0-7; C = FIX takes `blend_fix`) picks the blend, the frame alpha is kept (FBMSK 0xFF000000), Z is tested
    // (GEQUAL) but not written, and `clamp_uv` is the GS CLAMP_1 region clamp.
    bool effect = false;
    std::uint8_t gs_alpha = 0x44;
    bool clamp_uv = false;
    // HUD / 2D stage (hud1, docs/native-renderer.md §17): drawn last (after the post-fog stage), in list order, into
    // the GS frame like everything else (the game's Gt2d passes draw into the render buffer before its present blit).
    // Vertices are positioned by `model` alone (the 2D space -> clip space map; view_proj is not applied), no depth
    // test or write, no fog. Shaded like kTrack (MODULATE: set shading = kTrack, textured), blended by the GS ALPHA
    // `gs_alpha` (A, B, C, D in bits 0-7: every mode Gt2dRenderer::SetBlendMode selects, 0x44 0x48 0x42 0x2A 0x54 0x58
    // 0x52 0x6A; blend_fix < 0), or not blended (`overlay_blend` false: PRIM ABE clear); RGB written, frame alpha
    // kept, except `alpha_only` (FRAME FBMSK clear with 0x6A: only the frame alpha takes As). `scissor`: x0, y0, x1, y1
    // as fractions of the output (top left origin; x1 <= x0: none); `point_sample`: TEX1 nearest (else bilinear);
    // `repeat_u` / `repeat_v`: CLAMP_1 WMS / WMT REPEAT (else clamped to the edge).
    bool overlay = false;
    bool overlay_blend = true;
    bool alpha_only = false;
    bool point_sample = false;
    bool repeat_u = false, repeat_v = false;
    // `sharp_alpha` (hud2, a display choice, not the GS): the texture's alpha is coverage (the fonts) and the 2D space is
    // magnified: the bilinear coverage is re-thresholded at 0.5 with a one-pixel ramp (a distance-field style edge)
    bool sharp_alpha = false;
    float sharp_level = 0.5f;   // mac5: the coverage threshold (thin fonts lower, so their strokes keep their weight)
    float scissor[4] = {0, 0, 0, 0};
    // fe2 (docs/native-renderer.md §24): a 2D-stage draw with depth, the World Tour globe's B3Im3d batches (the game
    // draws them between Gt2d Suspend / Resume with Gt2dRenderer::EnableZTest(true)): Z tested GEQUAL against the
    // frame's depth (reverse-Z, as the scene), written with `overlay_zwrite`; `model` still maps the vertices to clip.
    bool overlay_ztest = false, overlay_zwrite = false;
    // perf4: the vertices' box in the model's own space (before `model`): lo x, y, z, hi x, y, z (lo > hi: not known). The
    // sun's occlusion pass leaves out an item whose box lies wholly outside FrameParams::sun_box (it could not cover any of
    // it: the result is the same); items without one are always drawn there.
    float bounds[6] = {1, 1, 1, 0, 0, 0};
};

// The enhanced car reflections' look (docs/car-look.md; b3replay's carlook.h edits it live). Presentation only. The defaults
// are the shipped look; the same numbers are the hard-coded block carlook.cpp checks against (tests/test_carlook.cpp).
struct CarLook {
    float strength = 0.5f;          // the gloss layer's strength (0: no gloss; the paint's colour floor stays)
    float fresnel_power = 4.0f;    // paint / glass weight ~ floor + (1 - floor) (1 - n.v)^power
    float fresnel_floor = 0.10f;   // the face-on gloss
    float paint_gain = 1.6f;       // the brightness of the reflection on the paint (the clearcoat contrast)
    float paint_darken = 0.0f;     // how much the paint darkens where it mirrors the ground (0: the game's colour is never scaled away)
    float glint_size = 1.0f;       // the sun glint's size
    float glint_gain = 1.5f;       // and its intensity
    float sky_gain = 0.9f;         // the sky's clouds in the reflection
    float horizon_gain = 0.8f;     // the bright haze hugging the horizon line
    float ground_level = 0.05f;    // the brightness of the ground below the horizon
    float haze = 0.0f;             // a uniform veil added to the environment (off)
    float glass_weight = 0.30f;    // glass: the reflection's weight face-on
    float glass_fresnel = 0.65f;   // and its growth to the edges
    float glass_gain = 1.8f;       // the brightness of the sky in the glass
    float glass_darken = 0.45f;    // how much darker the glass itself goes
    float rim = 0.20f;             // the bright rim of the glass at the edges
    float colour_keep = 1.0f;      // 1: the reflection takes the paint's own colour (never tints); 0: the sky's colour
    float traffic = 0.55f;         // traffic cars' strength against the racers'
};

struct FrameParams {
    CarLook car_look;              // the enhanced car reflections' look

    float view_proj[16];                         // column-major, reverse-Z (near -> depth 1, far -> 0)
    float clear[4] = {0.10f, 0.11f, 0.13f, 1.0f};
    float light_dir[3] = {0.4f, 0.8f, 0.3f};
    // Game frame model (kTrack / kSky draws):
    float eye[3] = {0, 0, 0};                    // camera position, world
    float right[3] = {1, 0, 0};                  // camera right vector, world
    float to_sun[3] = {0, 1, 0};                 // the environment's light 0 (towards the sun)
    bool no_specular = false;                    // RenderMesh's last argument: type-1/2 specular off
    float specular[3] = {0, 0, 0};               // full-screen specular colour in frame units (GS bytes / 255); 0 = off
    float present_scale = 1.0f;                  // 255/128: the game's present blit; 1 for kSimple-only scenes
    // lines1: the camera's far plane for the line meshes' own fog (program 0x476130's F, docs/native-renderer.md §11.8).
    // Line meshes are kTrack draws of track_type 3: quads widened on the GPU to max(1, output rows / 448) pixels.
    float line_far = 10000.0f;
    float fog[4] = {0, 0, 0, 0};                 // z_scale, z_shift, fog_max, fog_sky (b3asset::FogModel)
    float fog_colour[4] = {0, 0, 0, 0};          // rgb in frame units; a = 1 enables fog on kTrack draws
    float sun_colour[4] = {0, 0, 0, 0};          // kCar env map: the sun blob's colour (frame units), a = 1: drawn
    // Sun corona and lens flare (water1, docs/native-renderer.md §21; b3asset/sunflare.h): drawn after the post-fog
    // stage and before the 2D stage, additive (Cs x As + Cd on RGB, frame alpha kept), As = the share of `sun_box` that
    // no Z-writing draw of this frame covers (the game's sky test, OcclusionTest 0x24C7E0), measured on the GPU before
    // the scene pass from the same draw list.
    struct SunSprite { float rect[4]; float uv[4]; float rgb[4]; };   // rect: NDC x0, y0, x1, y1 (+y up); rgb: GS bytes / 255
    std::vector<SunSprite> sun_sprites;          // the corona, then the lens-flare secondaries (at most 13)
    TextureId sun_texture = kNone;
    float sun_box[4] = {0, 0, 0, 0};             // NDC x0, y0, x1, y1 of the occlusion box (clamped to the screen)
    bool sun_occlusion = true;                   // false: As = 1 (no sky test: the corona shows through walls)
    // Boost radial blur (water1, §21; CB3RadialBlur::Render 0x2FA9A0, shaders/blur.frag): after the sun corona, before
    // the 2D stage, on half-size buffers; nothing when amount <= 0 or no mask. Values as CB3RadialBlurParams holds them.
    struct RadialBlur {
        float centre[2] = {0.5f, 0.5f};          // fractions of the view, y down
        float radius[2] = {1, 1};                // the mask's extent (fractions of the view)
        float uv_scale[2] = {1, 1};              // the first pass's zoom towards the centre (then squared twice)
        float colour = 1, amount = 0;
        bool flip = false;
        TextureId mask = kNone;                  // GLOBAL.TXD "radialblurmask"
    } blur;
    // post1 (docs/native-renderer.md §25). CB3World::Render's order: the bloom (after the post-fog effects, before the sun
    // corona), then the sun corona, then one zoom blur (directional or radial), then the 2D stage.
    // Quality: PC (default) runs the zoom blurs at the output size, takes the bloom's bright part from every output pixel
    // and upsamples it smoothly (B-spline); game runs them at the GS's sizes (half / quarter of a 448-row frame, as wide
    // as the output's aspect) with its 2 x 2 bilinear taps. The effect (constants, pass order, the bloom's spread in
    // screen terms) is the same in both.
    bool post_game = false;
    // CB3DirectionalBlur::Render 0x426C10: nothing when amount <= 0 (the caller applies Render's threshold 1/255).
    struct DirectionalBlur {
        float centre[2] = {0.5f, 0.5f};          // fractions of the view, y down (often far outside it)
        float zoom[2] = {1, 1};                  // BlurOut's zoom (BlurIn uses its inverse), squared after each pass
        float amount = 0;                        // CopyToScreen's lerp towards the blurred picture
    } dblur;
    // CB3Bloom::Render 0x2659C0: the bright part of the frame, blurred at a quarter of its size, kept from render to render
    // (the picture P lives in the device, per target size) and added to the frame.
    struct Bloom {
        bool on = false;
        bool soft = false;                       // bloom2: SOFT level: P x 0.6 added as a screen blend (1 - (1 - frame)(1 - P)),
                                                 // so the highlights keep their detail; false: the game's own sum
        float c = 0;                             // threshold colour (frame units)
        float k1 = 0, k2 = 0, k3 = 0;            // FirstDownsample's factors / 128 (b3asset::bloom_gains)
        float intensity = 0;                     // CopyToScreen: P x it is added
        float keep = 0;                          // Blend: P x keep per render (int(118 x feedback) / 128)
        int mode = 0;                            // 0 accumulate, 1 reset (P = Q), 2 freeze (P kept)
        float renders = 1;                       // game renders since the last drawn frame (fractional in PC quality)
        bool restart = false;                    // P starts from black (a jump in the replay, a new segment)
    } bloom;
    // photo2: photo mode's depth of field (src/b3replay/photomode.h; shaders/dof.frag). After the zoom blur, before the 2D
    // stage: a depth pre-pass of the frame's Z-writing draws (the scene's multisampled depth cannot be sampled), then a
    // bokeh-style gather blur at half size, composited where the frame is out of focus. Nothing of it runs while `on` is
    // false or `radius` is 0 (the frame is drawn as before).
    struct Dof {
        bool on = false;
        float radius = 0;                        // the largest blur circle's radius, a fraction of the output height
        float focus = 0;                         // the focus distance (m, along the view axis); <= 0: autofocus
        float focus_scale = 1;                   // autofocus: the depth at `point` x this
        float point[2] = {0.5f, 0.5f};           // autofocus: where (fractions of the view, y down; the median of 5 pixels)
    } dof;
    // photoblur (1.3.0): photo mode's motion blur (src/b3replay/photomode.h PhotoMotion; shaders/motion.frag). At the start
    // of the 2D stage's phase, after the night grade and the headlight pools, before the bloom, the zoom blur, the depth of
    // field and the 2D: from the depth pre-pass (the one the depth of field draws), each pixel's world position is moved
    // over the shutter time and reprojected; the picture is gathered along that streak. A pixel inside a car's box moves
    // with that car (`box`, nearest first; the subject first). The camera moves over the shutter too: at -T/2 and +T/2 it
    // is offset by -`cam` / +`cam` (world) and turned by `rot_m` / `rot_p` (row-major 3 x 3 about `eye`, applied to the
    // world point: what the turned camera sees there). Since the frame is frozen, each window frame adds 8 taps to an
    // accumulation (Device: restarted when the camera or this changes) up to `frames`; an offscreen draw (a screenshot,
    // a shot) takes all of them at once. Off (`on` false): nothing of it runs and the frame is as before.
    struct Motion {
        static constexpr int kBoxes = 8;
        bool on = false;
        float eye[3] = {0, 0, 0};
        float cam[3] = {0, 0, 0};                // the camera's offset at +T/2 (world, m; -cam at -T/2)
        float rot_m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, rot_p[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        float max_streak = 0.12f;                // the half streak's longest, a fraction of the output height
        int frames = 16;                         // accumulated window frames (8 taps each)
        int boxes = 0;
        struct Box {
            float to_local[3][4] = {};           // world -> the car's box space: local[k] = dot(row k xyz, P) + row k w
            float lo[3] = {}, hi[3] = {};        // the box in that space
            float move[3] = {};                  // the car's offset at +T/2 (world, m)
        } box[kBoxes];
    } motion;
    // Night mode (TIME OF DAY: Night, remaster-features §17; off by default, then nothing of it runs and the frame is
    // unchanged). At the start of the post-fog stage, after the post-fog geometry (crash-mode objects) and the post-fog
    // effects that blend over the frame (smoke, dust), the frame so far is copied (resolved) and graded by
    // shaders/night.frag: where something was drawn (depth nearer than the far plane) desaturated to `sat`, multiplied by
    // `world` and bent by the curve pow(c / pivot, gamma) x pivot; then the headlight pools (DrawItem::night_light); then
    // where the sky shows (depth at the far plane) the night sky: a gradient from `horizon` to `zenith`, the original sky
    // x `sky` (clouds, the backdrop's silhouettes), the clouds lit by the moon (`cloud`), `stars`, the moon's disc and
    // halo; then the additive post-fog effects (coronas: the car and traffic lights, sparks, fire, the flash), which so
    // stay bright and feed the bloom. Cars: the SH light x car[0], the env map x car[1], a moon key light car[2] x
    // moon_light x N.L (FrameParams::to_sun is the moon's direction at night). Track: `facing` (> 0, with the surface
    // relief on) brightens the faces turned to the moon and darkens the others.
    struct Night {
        bool on = false;
        float world[3] = {1, 1, 1};
        float sat = 1, gamma = 1, pivot = 0.5f;
        float sky[3] = {1, 1, 1};
        float zenith[3] = {0, 0, 0}, horizon[3] = {0, 0, 0};
        float cloud = 0, stars = 0;
        float moon_dir[3] = {0, 1, 0};           // towards the moon (normalised)
        float moon_colour[3] = {0, 0, 0};        // the disc, frame units (0: no moon)
        float moon_size = 0.03f;                 // the disc's angular radius (rad)
        float halo = 0;                          // the halo's strength
        float moon_light[3] = {0, 0, 0};         // the moonlight's colour (frame units; 0: none)
        float car[3] = {1, 1, 0};                // cars: SH scale, env scale, moon key gain
        float facing = 0;                        // track: moonlit faces (with the relief)
        // nightlights (§17.2): the headlights as projected spot lights. `spot` true: a full-screen light pass after the
        // world's grade (shaders/beam.frag) over a single-sample depth pre-pass (the DOF's), lighting what the lamps
        // point at (road, walls, cars, props) by bringing the pre-grade colour back (tinted `lamp`) with the alpha
        // 1 - prod(1 - a_i); false: the ground quads (DrawItem::night_light, light.frag) with the same blend. A lamp:
        // `pos` (its world position), `dir` (the beam's axis, normalised), `gain` (0..1: the traffic fade).
        struct Lamp { float pos[3] = {0, 0, 0}, dir[3] = {0, 0, 1}, gain = 0; };
        static constexpr int kMaxLamps = 8;
        bool spot = false;
        int lamps = 0;
        Lamp lamp[kMaxLamps];
        float lamp_colour[3] = {1, 0.94f, 0.82f};   // the beam's tint over the pre-grade colour
        float spot_shape[4] = {0.978f, 0.883f, 16.0f, 2.2f};   // the spill: cos of its inner / outer half angle, falloff distance d0 (m), gain
        float spot_core[3] = {0.9986f, 0.9877f, 5.0f};        // the hot core: cos of its inner / outer half angle, its gain
        float spot_range[2] = {45.0f, 75.0f};    // the beam fades out between these distances (m)
        // Tunnels (§17.2): `enclosed` 0 (open) .. 1 (inside a tunnel; the view's car's sky_open, smoothed): the grade
        // keeps 1 - tunnel[0] x enclosed of its strength within tunnel[1] m, back to full at tunnel[2] m (per pixel with
        // the spot pass's depth; else over the whole world, without the distance)
        float enclosed = 0;
        float tunnel[3] = {0.8f, 60.0f, 140.0f};
        // the fixtures (DrawItem::night_emissive): the glow's gain over the pre-grade colour, the ramp on the texel's brightest channel
        float fixture[3] = {1.0f, 0.6f, 0.8f};
        // Street lights (§17.2): the route's lamps found at load (View::night_street_load, a per-region texture list), the
        // nearest in view each frame; in the spot pass a pool of light under each (a downward cone, faded out by
        // street_range), the pre-grade colour brought back tinted `colour` (sodium orange or warm white by region)
        struct Street { float pos[3] = {0, 0, 0}, colour[3] = {1, 1, 1}; };
        static constexpr int kMaxStreet = 24;
        int streets = 0;
        Street street[kMaxStreet];
        float street_shape[4] = {0.707f, 0.309f, 7.0f, 2.2f};   // cos of the pool's inner / outer angle from straight down, d0 (m), gain
        float street_range[2] = {14.0f, 24.0f};                // the pool fades out between these distances from the lamp (m)
        // Hook for SUN SHADOWS (brief B): at night the shadow's receiver term belongs in the spot pass (it has the
        // position): graded x shadow, scaled by (1 - a), so a beam fills the moon's shadow. Not implemented here.
    } night;
    // The scene's key light, for passes that cast shadows or light from one direction (a car shadow map): `dir` towards
    // the light (normalised): the ENVIRO sun by day, the moon at night (Night::moon_dir); `colour` its colour in frame
    // units (0: none, e.g. night with the moon off). Filled for every race frame (View::post_frame); nothing reads it yet.
    struct KeyLight {
        float dir[3] = {0, 1, 0};
        float colour[3] = {0, 0, 0};
        bool night = false;
    } key_light;
    // SUN SHADOWS (a remaster feature, off by default; docs/remaster-features.md §19): the cars' shadows from the key light
    // on the road. Before the scene pass the frame's car draws (DrawItem kCar: racers, traffic, wrecks; not a screen-door
    // ghost) are drawn depth-only through `view_proj` into one map of `size` texels (shaders/shadow.frag), each with its
    // gate (CarParams::sky_open: nothing from a car in a tunnel or under a bridge, fading at the mouths); the opaque and
    // blended track draws then take it (mesh.frag's SUN_SHADOW variant, shaders/sun_shadow.glsl): the faces turned up,
    // multiplied by 1 - strength x shadow. Nothing of it runs while `on` is false (no target, no pipeline variant).
    struct SunShadow {
        bool on = false;
        float view_proj[16] = {};    // world -> the map's clip space: orthographic along the light, x, y -1..1 across the
                                     // box, z 0..1 from the light's side (column-major)
        float depth = 80;            // m: the box's depth along the light (its z 0..1)
        float rise = 1;              // the light's sine of elevation (a distance along the light x rise = the height it drops)
        float strength = 0;          // the darkening at full shadow, of the open road (never below the baked shadows)
        float lit = 0.75f;           // the route's open-road vertex light (luminance of the prelit colour, GS units / 128):
                                     // a receiver darker than this is in a baked shadow and darkens only down to it
        int size = 2048;             // the map's side (texels)
        int filter = 0;              // 0: a 3 x 3 kernel, 1: 5 x 5 (softer, ULTRA)
    } sun_shadow;
    // surface (remaster prototype, docs/remaster-features.md §18; off by default, presentation only): on the opaque track
    // draws (kTrack types 0/1/4-7, not the sky, lines, blended or 2D draws, not the car env map), a normal per pixel from
    // the colour texture's luminance lit by the sun as a factor around 1 (`relief`, the bump height; 0: off) and a
    // sheen from texel heuristics added to the frame alpha that the full-screen specular reads (`sheen`; 0: off).
    // Contact shading: `ao` (> 0: a screen-space ambient occlusion of this strength over the opaque scene, from a depth
    // pre-pass, before the full-screen specular). With every field 0 nothing of it runs and the frame is unchanged.
    struct Surface {
        float relief = 0;                        // bump height per luminance step (texel units); ~3 subtle, 6 strong
        float sheen = 0;                         // the sheen lobe's strength in frame alpha
        float fade = 60;                         // m: relief and sheen fade out from 0.4 x fade to fade
        float sky = 0.35f;                       // the share of light from straight above (faces turned from the sun)
        float grazing = 2.0f;                    // the sheen's Fresnel-like boost at grazing angles
        float ao = 0;                            // ambient occlusion strength (0: off; 1: full)
        float ao_radius = 1.2f;                  // m, the occlusion's reach in the world
        bool debug = false;                      // false colour: the relief factor (red), the sheen (blue), the AO alone
    } surface;
};

// SUN SHADOWS: a caster's gate from its sky share (CarParams::sky_open): nothing below 0.18 (a tunnel reads ~0.2), all from
// 0.32 (a street between tall buildings reads ~0.3, the open road 1), smooth between (the tunnel mouths). Shared by the
// caster pass (b3gfx) and the blob fade (b3replay's View), which must agree.
inline float sun_shadow_gate(float sky_open) {
    float s = (sky_open - 0.18f) / 0.14f;
    s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    return s * s * (3.0f - 2.0f * s);
}

// reflect-tunnel: the car environment map's track part (docs/car-shading.md §9). The game draws the sky, the sun and the
// streamed track's LOD blocks into its env-map cameras around the followed car every frame (CB3World::Render ->
// RenderSkyToEnvironmentMap / RenderSunToEnvironmentMap / RenderBeforeVehicles 0x21C780 cameras 1..3, near 0.01, far 100
// [C/L]); Device::render_env draws `items` into the six faces of a cube around `centre` (an atlas of 3 x 2 faces, face f
// at column f % 3, row f / 3; forward +X, -X, +Y, -Y, +Z, -Z, see env_face) with alpha 1 where the track covers the
// face, 0 where the sky shows (car.frag puts the sky behind it). Frame units, no fog, no specular (RenderMesh's 1).
struct EnvPass {
    float centre[3] = {0, 0, 0};
    float znear = 0.01f, zfar = 100.0f;
    int face = 128;                              // face size in pixels; each face is rendered 1 texel wider on every side
    FrameParams fp;                              // eye / light / sun inputs of the track model (view_proj set per face)
    std::vector<DrawItem> items;                 // kTrack draws
    std::vector<std::uint8_t> faces;             // per item: bit f = drawn into face f (empty: every face)
};
// The cube faces' bases (shared with car.frag's env_world): forward, right, up, world axes.
void env_face(int f, float fwd[3], float right[3], float up[3]);

class Device {
public:
    // `window` = nullptr creates a device without a swapchain (offscreen rendering only).
    static std::unique_ptr<Device> create(SDL_Window* window, const Options& opt, std::string* err);
    virtual ~Device() = default;

    virtual const char* driver_name() const = 0;
    // The SDL_GPUDevice behind it (bug reports: include/b3report/sysinfo.h gpu_description); nullptr: none
    virtual void* sdl_gpu_device() const { return nullptr; }
    virtual int msaa() const = 0;
    virtual const char* present_mode_name() const = 0;
    // lat1: frames the GPU may have queued for the window (SDL_SetGPUAllowedFramesInFlight; 0 = no window)
    virtual int frames_in_flight() const = 0;
    // lat1: blocks until the next window frame can be acquired without waiting (SDL_WaitForGPUSwapchain), so that a frame
    // built right after it is not held behind the GPU's queue. No-op without a window.
    virtual void wait_swapchain() = 0;

    virtual BufferId create_vertex_buffer(const Vertex* v, std::size_t count) = 0;
    virtual BufferId create_index_buffer(const std::uint32_t* idx, std::size_t count) = 0;
    // Stored levels are uploaded as given; a single level gets a generated chain. BC levels (TextureLevel::bc, all the
    // same format) are always uploaded as given: the caller brings the chain (no GPU generation for compressed formats).
    virtual TextureId create_texture(const std::vector<TextureLevel>& levels) = 0;
    // hdgpu: BC1, BC2 and BC3 can be sampled (SDL_GPUTextureSupportsFormat); else create_texture refuses BC levels
    virtual bool supports_bc() const { return false; }
    // A dynamic texture (one level, no mips; menu1's movie pictures): `id` == kNone or another size creates one in a new
    // or the same slot, else the pixels are replaced in place. What the next drawn frame samples.
    virtual void update_texture(TextureId& id, int w, int h, const std::uint8_t* rgba) = 0;
    // Releases every buffer and texture created so far (ids restart from 0).
    virtual void clear_resources() = 0;
    // Counts the clear_resources calls: a panel that keeps buffer / texture ids between frames compares it with the value it
    // made them under and makes them again when it changed (after a clear, ids restart from 0 and name the game's own).
    virtual unsigned resource_epoch() const { return 0; }
    // Dynamic buffers (per-frame effect geometry): `id` == kNone creates one, else its contents are replaced in place
    // (grown when needed; the id stays). The new contents are what the next drawn frame uses.
    virtual void update_vertex_buffer(BufferId& id, const Vertex* v, std::size_t count) = 0;
    virtual void update_index_buffer(BufferId& id, const std::uint32_t* idx, std::size_t count) = 0;

    // Window frame: acquires the swapchain image (false = nothing to draw, e.g. minimized), draws opaque and
    // alpha-test items in order, then blended items in order, resolves and presents.
    virtual bool draw_frame(const FrameParams& fp, const std::vector<DrawItem>& items, int* w = nullptr, int* h = nullptr) = 0;

    // Offscreen frame of w x h, read back as RGBA8 rows (top row first).
    virtual bool draw_offscreen(int w, int h, const FrameParams& fp, const std::vector<DrawItem>& items,
                                std::vector<std::uint8_t>& rgba) = 0;

    // gsoff1 (docs/native-renderer.md §29): one frame of several views (the 2-player split screen). Each view is drawn as a
    // frame of its own (3D, effects, its post passes and their kept pictures; DrawItem::overlay items are left out) at
    // the size of its `rect` (x0, y0, x1, y1 as fractions of the output, top left origin), placed there, then `overlay`
    // (the 2D stage, over the whole output, scissors as fractions of it) and the present pass with `overlay_fp`'s
    // present_scale. One-view frames keep draw_frame / draw_offscreen.
    struct ViewPass { FrameParams fp; std::vector<DrawItem> items; float rect[4] = {0, 0, 1, 1}; };
    virtual bool draw_views(const std::vector<ViewPass>& views, const FrameParams& overlay_fp, const std::vector<DrawItem>& overlay,
                            int* w = nullptr, int* h = nullptr) = 0;
    virtual bool draw_views_offscreen(int w, int h, const std::vector<ViewPass>& views, const FrameParams& overlay_fp,
                                      const std::vector<DrawItem>& overlay, std::vector<std::uint8_t>& rgba) = 0;

    // A one-off picture of the window's frame (F12): between screenshot_begin and screenshot_end the offscreen draws start
    // their kept pictures (the bloom's P) from the window's, not from whatever an earlier offscreen draw left (a fresh one
    // when the window has none); screenshot_end releases the offscreen targets (a second set at the window's size).
    // Offscreen rendering without them (the --offscreen-live run, the golden) is unchanged.
    virtual void screenshot_begin() {}
    virtual void screenshot_end() {}

    // perf1 (docs/native-renderer.md §33): profiling. An offscreen frame drawn and waited for (fence) without the read-back,
    // so its time is the GPU's (plus the submit), and the device's counters since the last call (then reset).
    virtual bool draw_offscreen_wait(int w, int h, const FrameParams& fp, const std::vector<DrawItem>& items) {
        std::vector<std::uint8_t> scratch;
        return draw_offscreen(w, h, fp, items, scratch);
    }
    struct Stats {
        std::uint32_t draws = 0;          // indexed draws (scene, cars, 2D)
        std::uint32_t uploads = 0;        // buffer / texture uploads (create_* and update_*), each its own command buffer
        std::uint64_t upload_bytes = 0;
        std::uint32_t buffer_grows = 0;   // dynamic buffers re-created larger
    };
    virtual Stats take_stats() { return {}; }
    // perf1: the present pass's ordered dither (Options::dither), changed at any time (the PC settings' DITHER row)
    // reflect-tunnel: draws `e` into env map `slot` (0 or 1: a split screen's two views) now, in a command buffer of its own
    // submitted before the frame that samples it; returns the value for CarParams::env_world (kNone: not drawn)
    virtual std::uint32_t render_env(int slot, const EnvPass& e) { (void)slot; (void)e; return kNone; }
    virtual void set_dither(float steps) { (void)steps; }
    // effects2: Options::render_scale, changed at any time (the PC settings' RENDER SCALE row)
    virtual void set_render_scale(float scale) { (void)scale; }
    // 1.1 UPSCALING: RENDER SCALE's picture brought up to the window bilinear (false) or by the sharp upscale (true)
    virtual void set_sharp_upscale(bool on) { (void)on; }

    // perf4 (adaptive frames in flight, include/b3gfx/frames_auto.h): the window's frame queue changed at run time
    // (SDL_SetGPUAllowedFramesInFlight: the GPU's queue is flushed once). false: not changed (no window, or refused).
    virtual bool set_frames_in_flight(int n) { (void)n; return false; }
    // perf4: the time the window frames spent waiting for the GPU since the last call (wait_swapchain, the swapchain acquire and
    // the frame's submit), in ns
    virtual std::uint64_t take_wait_ns() { return 0; }
    // perf4 (pipeline warm-up): the 2D stage's pipeline keys made so far (DrawItem::overlay's GS ALPHA byte and flags), and
    // those made now (the keys a previous run saved: their pipelines are not created at the first draw that needs them)
    virtual std::vector<int> overlay_keys() const { return {}; }
    virtual void warm_overlay_keys(const std::vector<int>& keys) { (void)keys; }
    // perf4 (route loads): while on, create_texture and create_*_buffer share one staging buffer and one command buffer
    // (submitted when it is full, at off, or before anything else reaches the GPU: the call order is kept). Off: each
    // its own, as before.
    virtual void upload_batch(bool on) { (void)on; }

    // 1.2 HD TEXTURES key (hdkey): an HD pack replacement's original decode, uploaded beside it. While HD is hidden
    // (set_hd_shown(false)) every draw that binds `hd` samples `original` instead: the key switches with no reload. The
    // pairs go with clear_resources; the shown / hidden state stays. `bytes`, `hd_bytes`: the two uploads (hd_originals).
    virtual void set_texture_original(TextureId hd, TextureId original, std::size_t bytes, std::size_t hd_bytes) {
        (void)hd; (void)original; (void)bytes; (void)hd_bytes;
    }
    virtual void set_hd_shown(bool on) { (void)on; }
    virtual bool hd_shown() const { return true; }
    struct HdOriginals { std::size_t textures = 0, bytes = 0, hd_bytes = 0; };   // the pairs resident now: originals, replacements
    virtual HdOriginals hd_originals() const { return {}; }
    // create_texture(levels), and the original beside it when `original` has levels (an HD replacement's decode). A
    // replacement the device cannot take (a side past its limit, out of memory) gives the original alone: the game's
    // own picture, not the white of no texture.
    TextureId create_texture_hd(const std::vector<TextureLevel>& levels, const std::vector<TextureLevel>& original) {
        const TextureId id = create_texture(levels);
        if (id == kNone) return original.empty() ? kNone : create_texture(original);
        if (original.empty()) return id;
        const TextureId o = create_texture(original);
        if (o == kNone) return id;
        const auto size = [](const std::vector<TextureLevel>& ls) {
            std::size_t b = 0;
            for (const TextureLevel& l : ls) b += l.bc ? l.bytes : std::size_t(l.w) * std::size_t(l.h) * 4;
            return ls.size() == 1 && !ls[0].bc ? b + b / 3 : b;   // one level: the generated chain
        };
        set_texture_original(id, o, size(original), size(levels));
        return id;
    }
};

// Headless rendering (draw_offscreen only): initialises SDL's video subsystem and creates a device, trying in order
// (1) SDL's "offscreen" video driver without a window (Vulkan on Linux / WSL / Windows: that driver provides Vulkan
// surfaces), (2) the platform's own video driver without a window, (3) the platform driver with a hidden window
// (SDL_WINDOW_HIDDEN) claimed by the device. SDL 3.4.2's Metal backend is only selectable when the video driver
// implements Metal_CreateView (METAL_PrepareDriver), which the "offscreen" driver does not, hence "No supported
// SDL_GPU backend found!" on macOS before; Cocoa provides it, and METAL_CreateDevice itself needs no window [L, read
// from SDL's source; not run on a Mac here]. `hidden` receives the window of step 3 (destroy it after the device),
// `how` which step worked. On failure SDL is shut down again.
std::unique_ptr<Device> create_headless(const Options& opt, std::string* err, SDL_Window** hidden, const char** how = nullptr);

// For a panel that keeps buffer / texture ids between frames: changed(dev) is true once after every clear_resources (a track
// load), when its ids name released resources, or the game's own: it must make them again.
struct ResourceEpoch {
    unsigned seen = 0;
    bool changed(const Device& d) {
        const unsigned e = d.resource_epoch();
        if (e == seen) return false;
        seen = e;
        return true;
    }
};

// Column-major helpers for callers.
void mat4_identity(float m[16]);
void mat4_mul(const float a[16], const float b[16], float out[16]);  // out = a * b
// Right-handed view looking from `eye` along `fwd` with `up`; reverse-Z infinite perspective (depth 1 at znear).
void mat4_look(const float eye[3], const float fwd[3], const float up[3], float out[16]);
void mat4_perspective_reverse_z(float fovy_rad, float aspect, float znear, float out[16]);
// photo2: the general 4x4 inverse (false: singular)
bool mat4_invert(const float m[16], float out[16]);

}  // namespace b3gfx

#endif
