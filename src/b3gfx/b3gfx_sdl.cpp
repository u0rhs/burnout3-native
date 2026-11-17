// b3gfx on SDL3 SDL_GPU (see include/b3gfx/b3gfx.h).
#include "b3gfx/b3gfx.h"

#include <SDL3/SDL.h>
#include <cstdio>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "b3gfx/perf.h"
#include "shaders_gen.h"

// perf3 (include/b3gfx/perf.h): frame timing capture. The SDL_GPU calls that matter are wrapped here, so a pass, a draw, a
// submit and the waits are counted / timed (B3_PERF_LOG; off: a plain bool test) without touching the code around them.
namespace {
inline SDL_GPURenderPass* perf_begin_pass(SDL_GPUCommandBuffer* cmd, const SDL_GPUColorTargetInfo* ci, Uint32 n,
                                          const SDL_GPUDepthStencilTargetInfo* di) {
    b3perf::count(b3perf::kPasses);
    return SDL_BeginGPURenderPass(cmd, ci, n, di);
}
inline void perf_draw(SDL_GPURenderPass* pass, Uint32 vertices, Uint32 first) {
    b3perf::count(b3perf::kDraws);
    SDL_DrawGPUPrimitives(pass, vertices, 1, first, 0);
}
inline void perf_draw_indexed(SDL_GPURenderPass* pass, Uint32 n, Uint32 first) {
    b3perf::count(b3perf::kDraws);
    SDL_DrawGPUIndexedPrimitives(pass, n, 1, first, 0, 0);
}
inline bool perf_submit(SDL_GPUCommandBuffer* cmd) {
    b3perf::count(b3perf::kSubmits);
    const int cur = b3perf::current();
    b3perf::Scope sc(cur == b3perf::kUpload || cur == b3perf::kUploadSubmit ? b3perf::kUploadSubmit : b3perf::kSubmit);
    return SDL_SubmitGPUCommandBuffer(cmd);
}
inline SDL_GPUFence* perf_submit_fence(SDL_GPUCommandBuffer* cmd) {
    b3perf::count(b3perf::kSubmits);
    b3perf::Scope sc(b3perf::kSubmit);
    return SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
}
inline bool perf_acquire(SDL_GPUCommandBuffer* cmd, SDL_Window* w, SDL_GPUTexture** t, Uint32* tw, Uint32* th) {
    b3perf::Scope sc(b3perf::kAcquire);
    return SDL_WaitAndAcquireGPUSwapchainTexture(cmd, w, t, tw, th);
}
inline bool perf_wait_fences(SDL_GPUDevice* d, bool all, SDL_GPUFence* const* f, Uint32 n) {
    b3perf::Scope sc(b3perf::kFence);
    return SDL_WaitForGPUFences(d, all, f, n);
}
inline bool perf_wait_swapchain(SDL_GPUDevice* d, SDL_Window* w) {
    b3perf::Scope sc(b3perf::kWaitSwapchain);
    return SDL_WaitForGPUSwapchain(d, w);
}
}  // namespace
#define SDL_BeginGPURenderPass(cmd, ci, n, di) perf_begin_pass(cmd, ci, n, di)
#define SDL_DrawGPUPrimitives(pass, nv, ninst, fv, fi) perf_draw(pass, nv, fv)
#define SDL_DrawGPUIndexedPrimitives(pass, ni, ninst, fi, vo, fii) perf_draw_indexed(pass, ni, fi)
#define SDL_SubmitGPUCommandBuffer(cmd) perf_submit(cmd)
#define SDL_SubmitGPUCommandBufferAndAcquireFence(cmd) perf_submit_fence(cmd)
#define SDL_WaitAndAcquireGPUSwapchainTexture(cmd, w, t, tw, th) perf_acquire(cmd, w, t, tw, th)
#define SDL_WaitForGPUFences(d, all, f, n) perf_wait_fences(d, all, f, n)
#define SDL_WaitForGPUSwapchain(d, w) perf_wait_swapchain(d, w)

namespace b3gfx {

void mat4_identity(float m[16]) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mat4_mul(const float a[16], const float b[16], float out[16]) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + rr] * b[c * 4 + k];
            r[c * 4 + rr] = s;
        }
    std::memcpy(out, r, sizeof r);
}

void mat4_look(const float eye[3], const float fwd[3], const float up[3], float m[16]) {
    auto norm = [](float v[3]) { const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 0) for (int i = 0; i < 3; ++i) v[i] /= l; };
    float f[3] = {fwd[0], fwd[1], fwd[2]};
    norm(f);
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    norm(s);
    const float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    mat4_identity(m);
    for (int i = 0; i < 3; ++i) {
        m[i * 4 + 0] = s[i];
        m[i * 4 + 1] = u[i];
        m[i * 4 + 2] = -f[i];
    }
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
}

void mat4_perspective_reverse_z(float fovy, float aspect, float znear, float m[16]) {
    std::memset(m, 0, 16 * sizeof(float));
    const float f = 1.0f / std::tan(fovy / 2);
    m[0] = f / aspect;
    m[5] = f;
    m[11] = -1.0f;   // w = -z_view
    m[14] = znear;   // depth = znear / -z_view: 1 at the near plane, 0 at infinity
}

bool mat4_invert(const float m[16], float out[16]) {
    // cofactors in double (the reverse-Z projection's small terms)
    double a[16], inv[16];
    for (int i = 0; i < 16; ++i) a[i] = m[i];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (!(std::fabs(det) > 0.0)) return false;
    for (int i = 0; i < 16; ++i) out[i] = float(inv[i] / det);
    return true;
}

namespace {

struct VertexUniforms {
    float view_proj[16], model[16];
    float eye[4], right[4], sun[4], shade[4], inst[4], flags[4], line[4];
};
struct FragmentUniforms { float tint[4]; float light_dir[4]; float params[4]; float gs[4]; float fog[4]; float fog_colour[4]; };
// car.vert / car.frag (std140: mat4 and vec4 members only)
struct CarVertexUniforms { float view_proj[16], model[16], light[16], sh[16]; float eye[4], view_dir[4], base[4], full[4];
                           float zone[8][16]; float deform[4]; };
struct CarFragmentUniforms { float env_colour[4], mode[4], to_sun[4], sun_colour[4], fog[4], fog_colour[4], refl[4], look0[4], look1[4], look2[4], look3[4]; };
// The GS frame: half intensity like the PS2's render buffer, float so the frame alpha (As / 128, read by the
// full-screen specular pass) is not clamped at 1.
// bloom2: the SOFT level's share of the game's bloom intensity (before the screen blend)
constexpr float kBloomSoft = 0.6f;
constexpr float kUpscaleSharpen = 0.35f;   // 1.1 UPSCALING Sharp: post.frag sharp_upscale amount
constexpr SDL_GPUTextureFormat kSceneFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

class SdlDevice final : public Device {
public:
    ~SdlDevice() override {
        if (!dev_) return;
        flush_uploads();
        SDL_WaitForGPUIdle(dev_);
        if (up_.tb) SDL_ReleaseGPUTransferBuffer(dev_, up_.tb);
        for (auto* b : buffers_) SDL_ReleaseGPUBuffer(dev_, b);
        for (auto* t : textures_) SDL_ReleaseGPUTexture(dev_, t);
        for (auto& kv : pipelines_) for (auto* p : kv.second) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
        release_targets(win_targets_);
        release_targets(off_targets_);
        release_targets(scaled_win_);   // effects2
        release_targets(scaled_off_);
        for (auto* v : {&win_views_, &off_views_}) for (Targets& t : *v) release_targets(t);   // gsoff1
        if (sampler_) SDL_ReleaseGPUSampler(dev_, sampler_);
        if (white_) SDL_ReleaseGPUTexture(dev_, white_);
        for (auto* sh : {vs_, fs_, fsq_vs_, spec_fs_, present_fs_, car_vs_, car_fs_, sun_vs_, sun_fs_, blur_fs_, post_fs_, post_lean_fs_, dof_fs_}) if (sh) SDL_ReleaseGPUShader(dev_, sh);
        for (auto* p : {occ_pipe_[0], occ_pipe_[1], sun_pipe_, blur_pipe_[0], blur_pipe_[1], post_pipe_[0], post_pipe_[1], post_pipe_[2], post_pipe_[3]}) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
        for (auto* p : {dof_depth_pipe_[0], dof_depth_pipe_[1], dof_depth_pipe_[2], dof_pipe_[0], dof_pipe_[1]}) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
        if (occ_) SDL_ReleaseGPUTexture(dev_, occ_);
        for (auto* p : {spec_pipe_, present_pipe_win_, present_pipe_off_, car_pipe_, line_pipe_}) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
        if (clamp_sampler_) SDL_ReleaseGPUSampler(dev_, clamp_sampler_);
        if (effect_sampler_) SDL_ReleaseGPUSampler(dev_, effect_sampler_);
        for (auto* p : effect_pipe_) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
        for (auto& kv : overlay_pipe_) if (kv.second) SDL_ReleaseGPUGraphicsPipeline(dev_, kv.second);
        if (overlay_sampler_) SDL_ReleaseGPUSampler(dev_, overlay_sampler_);
        for (auto* w : overlay_wrap_) if (w) SDL_ReleaseGPUSampler(dev_, w);
        if (window_) SDL_ReleaseWindowFromGPUDevice(dev_, window_);
        SDL_DestroyGPUDevice(dev_);
    }

    bool init(SDL_Window* window, const Options& opt, std::string* err) {
        dither_ = opt.dither;   // perf1
        render_scale_ = std::clamp(opt.render_scale, 0.25f, 1.0f);   // effects2
        SDL_PropertiesID p = SDL_CreateProperties();
        SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
        SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, opt.debug);
        if (!opt.driver.empty()) SDL_SetStringProperty(p, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, opt.driver.c_str());
        // B3GFX_TRACE=1: each start-up call on stdout (the live window's first test hung inside this function)
        const bool trace = SDL_getenv("B3GFX_TRACE") != nullptr;
        auto step = [&](const char* what) { if (trace) { std::printf("[b3gfx] %s\n", what); std::fflush(stdout); } };
        step("SDL_CreateGPUDevice");
        dev_ = SDL_CreateGPUDeviceWithProperties(p);
        SDL_DestroyProperties(p);
        if (!dev_) return fail(err, "SDL_CreateGPUDevice");
        if (window) {
            step("SDL_ClaimWindowForGPUDevice");
            if (!SDL_ClaimWindowForGPUDevice(dev_, window)) return fail(err, "SDL_ClaimWindowForGPUDevice");
            step("swapchain parameters");
            window_ = window;
            SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
            if (opt.present == PresentMode::kUncapped) {
                if (SDL_WindowSupportsGPUPresentMode(dev_, window, SDL_GPU_PRESENTMODE_IMMEDIATE)) mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
                else if (SDL_WindowSupportsGPUPresentMode(dev_, window, SDL_GPU_PRESENTMODE_MAILBOX)) mode = SDL_GPU_PRESENTMODE_MAILBOX;
            }
            if (!SDL_SetGPUSwapchainParameters(dev_, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
                return fail(err, "SDL_SetGPUSwapchainParameters");
            present_ = mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "immediate" : mode == SDL_GPU_PRESENTMODE_MAILBOX ? "mailbox" : "vsync";
            // lat1: the GPU's frame queue (SDL's default 2: the CPU may build a frame while the previous one renders,
            // which holds a GPU-bound frame back by up to one more frame)
            if (opt.frames_in_flight > 0 && SDL_SetGPUAllowedFramesInFlight(dev_, std::uint32_t(std::clamp(opt.frames_in_flight, 1, 3))))
                frames_in_flight_ = std::clamp(opt.frames_in_flight, 1, 3);
            else frames_in_flight_ = 2;
            swap_format_ = SDL_GetGPUSwapchainTextureFormat(dev_, window);
            step("swapchain ready");
        }
        depth_format_ = SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                                     SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                            ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT : SDL_GPU_TEXTUREFORMAT_D24_UNORM;
        // MSAA: the largest supported count <= requested, for both the swapchain and offscreen formats.
        msaa_ = 1;
        for (int s : {8, 4, 2}) {
            if (s > opt.msaa) continue;
            const SDL_GPUSampleCount sc = sample_count(s);
            if ((!window_ || SDL_GPUTextureSupportsSampleCount(dev_, swap_format_, sc)) &&
                SDL_GPUTextureSupportsSampleCount(dev_, kSceneFormat, sc) &&
                SDL_GPUTextureSupportsSampleCount(dev_, depth_format_, sc)) { msaa_ = s; break; }
        }
        if (!make_shaders(err)) return false;
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = si.mag_filter = opt.point ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;   // settings1
        si.mipmap_mode = opt.point || opt.mip_nearest ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.max_lod = 1000.0f;
        si.enable_anisotropy = opt.anisotropy > 1.0f;
        si.max_anisotropy = opt.anisotropy;
        sampler_ = SDL_CreateGPUSampler(dev_, &si);
        if (!sampler_) return fail(err, "SDL_CreateGPUSampler");
        SDL_GPUSamplerCreateInfo ci{};
        ci.min_filter = ci.mag_filter = SDL_GPU_FILTER_NEAREST;
        ci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        ci.address_mode_u = ci.address_mode_v = ci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        clamp_sampler_ = SDL_CreateGPUSampler(dev_, &ci);
        if (!clamp_sampler_) return fail(err, "SDL_CreateGPUSampler");
        // effects with GS CLAMP_1 = clamp (filter mode 1 of the texture cache: bilinear)
        SDL_GPUSamplerCreateInfo ei = si;
        ei.address_mode_u = ei.address_mode_v = ei.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        effect_sampler_ = SDL_CreateGPUSampler(dev_, &ei);
        if (!effect_sampler_) return fail(err, "SDL_CreateGPUSampler");
        // the 2D stage (hud1): bilinear, clamped, no anisotropy (TEX1 MMAG linear; point sampling uses clamp_sampler_)
        SDL_GPUSamplerCreateInfo oi = ei;
        oi.enable_anisotropy = false;
        oi.min_filter = oi.mag_filter = SDL_GPU_FILTER_LINEAR;   // settings1: whatever the 3D filter is
        oi.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        overlay_sampler_ = SDL_CreateGPUSampler(dev_, &oi);
        if (!overlay_sampler_) return fail(err, "SDL_CreateGPUSampler");
        // 2D draws with GS CLAMP_1 REPEAT on an axis (hud2: the boost bar's tread tile, WMS = WMT = 0 [C capture]):
        // index (point << 2) | (repeat_u << 1) | repeat_v; the all-clamp entries stay the two samplers above
        for (int k = 0; k < 8; ++k) {
            if ((k & 3) == 0) continue;
            SDL_GPUSamplerCreateInfo wi = (k & 4) ? ci : oi;
            wi.address_mode_u = (k & 2) ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            wi.address_mode_v = (k & 1) ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            overlay_wrap_[k] = SDL_CreateGPUSampler(dev_, &wi);
            if (!overlay_wrap_[k]) return fail(err, "SDL_CreateGPUSampler");
        }
        const std::uint8_t white[4] = {255, 255, 255, 255};
        const TextureId w = create_texture({TextureLevel{1, 1, white}});
        if (w == kNone) return fail(err, "white texture");
        white_ = textures_[w];
        textures_[w] = nullptr;
        textures_.pop_back();
        return true;
    }

    unsigned resource_epoch() const override { return resource_epoch_; }
    void clear_resources() override {
        ++resource_epoch_;
        flush_uploads();
        SDL_WaitForGPUIdle(dev_);
        for (auto* b : buffers_) SDL_ReleaseGPUBuffer(dev_, b);
        for (auto* t : textures_) SDL_ReleaseGPUTexture(dev_, t);
        dyn_.clear();
        buffers_.clear();
        textures_.clear();
        dyn_tex_size_.clear();
    }

    void update_vertex_buffer(BufferId& id, const Vertex* v, std::size_t count) override {
        update_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, id, v, count * sizeof(Vertex));
    }
    void update_index_buffer(BufferId& id, const std::uint32_t* idx, std::size_t count) override {
        update_buffer(SDL_GPU_BUFFERUSAGE_INDEX, id, idx, count * 4);
    }

    const char* driver_name() const override { return SDL_GetGPUDeviceDriver(dev_); }
    void* sdl_gpu_device() const override { return dev_; }
    int msaa() const override { return msaa_; }
    const char* present_mode_name() const override { return present_; }
    int frames_in_flight() const override { return window_ ? frames_in_flight_ : 0; }
    void wait_swapchain() override { if (window_) SDL_WaitForGPUSwapchain(dev_, window_); }

    BufferId create_vertex_buffer(const Vertex* v, std::size_t count) override {
        return make_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, v, count * sizeof(Vertex));
    }
    BufferId create_index_buffer(const std::uint32_t* idx, std::size_t count) override {
        return make_buffer(SDL_GPU_BUFFERUSAGE_INDEX, idx, count * 4);
    }

    TextureId create_texture(const std::vector<TextureLevel>& levels) override {
        flush_uploads();   // keep the submission order of the immediate uploads (see upload_reserve)
        B3PERF_SCOPE(kUpload);
        if (levels.empty() || levels[0].w <= 0 || levels[0].h <= 0) return kNone;
        const bool generate = levels.size() == 1 && (levels[0].w > 1 || levels[0].h > 1);
        std::uint32_t nlev = std::uint32_t(levels.size());
        if (generate) { nlev = 1; for (int m = std::max(levels[0].w, levels[0].h); m > 1; m >>= 1) ++nlev; }
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (generate ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
        ti.width = std::uint32_t(levels[0].w);
        ti.height = std::uint32_t(levels[0].h);
        ti.layer_count_or_depth = 1;
        ti.num_levels = nlev;
        SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev_, &ti);
        if (!tex) return kNone;
        std::size_t total = 0;
        for (const auto& l : levels) total += std::size_t(l.w) * l.h * 4;
        ++stats_.uploads;
        stats_.upload_bytes += total;
        b3perf::count(b3perf::kUploads);
        b3perf::count(b3perf::kUploadBytes, total);
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, std::uint32_t(total), 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        auto* dst = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(dev_, tb, false));
        std::size_t off = 0;
        for (const auto& l : levels) { std::memcpy(dst + off, l.rgba, std::size_t(l.w) * l.h * 4); off += std::size_t(l.w) * l.h * 4; }
        SDL_UnmapGPUTransferBuffer(dev_, tb);
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        off = 0;
        for (std::size_t i = 0; i < levels.size(); ++i) {
            SDL_GPUTextureTransferInfo src{tb, std::uint32_t(off), std::uint32_t(levels[i].w), std::uint32_t(levels[i].h)};
            SDL_GPUTextureRegion reg{};
            reg.texture = tex;
            reg.mip_level = std::uint32_t(i);
            reg.w = std::uint32_t(levels[i].w);
            reg.h = std::uint32_t(levels[i].h);
            reg.d = 1;
            SDL_UploadToGPUTexture(cp, &src, &reg, false);
            off += std::size_t(levels[i].w) * levels[i].h * 4;
        }
        SDL_EndGPUCopyPass(cp);
        if (generate) SDL_GenerateMipmapsForGPUTexture(cmd, tex);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(dev_, tb);
        textures_.push_back(tex);
        return TextureId(textures_.size() - 1);
    }

    void update_texture(TextureId& id, int w, int h, const std::uint8_t* rgba) override {
        flush_uploads();   // keep the submission order of the immediate uploads (see upload_reserve)
        B3PERF_SCOPE(kUpload);
        if (w <= 0 || h <= 0) return;
        const bool fresh = id == kNone || id >= textures_.size() || dyn_tex_size_[id] != std::make_pair(w, h);
        SDL_GPUTexture* tex = fresh ? nullptr : textures_[id];
        if (fresh) {
            SDL_GPUTextureCreateInfo ti{};
            ti.type = SDL_GPU_TEXTURETYPE_2D;
            ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            ti.width = std::uint32_t(w);
            ti.height = std::uint32_t(h);
            ti.layer_count_or_depth = 1;
            ti.num_levels = 1;
            tex = SDL_CreateGPUTexture(dev_, &ti);
            if (!tex) return;
            if (id != kNone && id < textures_.size()) {
                if (textures_[id]) SDL_ReleaseGPUTexture(dev_, textures_[id]);   // released once the GPU is done with it
                textures_[id] = tex;
            } else {
                textures_.push_back(tex);
                id = TextureId(textures_.size() - 1);
            }
            dyn_tex_size_[id] = {w, h};
        }
        const std::uint32_t bytes = std::uint32_t(w) * std::uint32_t(h) * 4U;
        ++stats_.uploads;
        stats_.upload_bytes += bytes;
        b3perf::count(b3perf::kUploads);
        b3perf::count(b3perf::kUploadBytes, bytes);
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes, 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        if (!tb) return;
        auto* dst = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(dev_, tb, false));
        std::memcpy(dst, rgba, bytes);
        SDL_UnmapGPUTransferBuffer(dev_, tb);
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src{tb, 0, std::uint32_t(w), std::uint32_t(h)};
        SDL_GPUTextureRegion reg{};
        reg.texture = tex;
        reg.w = std::uint32_t(w);
        reg.h = std::uint32_t(h);
        reg.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &reg, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(dev_, tb);
    }

    bool draw_frame(const FrameParams& fp, const std::vector<DrawItem>& items, int* w, int* h) override {
        if (!window_) return false;
        flush_uploads();
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd) return false;
        SDL_GPUTexture* sw = nullptr;
        Uint32 sw_w = 0, sw_h = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &sw, &sw_w, &sw_h) || !sw) {
            SDL_SubmitGPUCommandBuffer(cmd);
            return false;
        }
        if (w) *w = int(sw_w);
        if (h) *h = int(sw_h);
        if (!ensure_targets(win_targets_, swap_format_, sw_w, sw_h, false)) { SDL_SubmitGPUCommandBuffer(cmd); return false; }
        render(cmd, win_targets_, sw, swap_format_, fp, items);
        return SDL_SubmitGPUCommandBuffer(cmd);
    }

    bool draw_views(const std::vector<ViewPass>& views, const FrameParams& ofp, const std::vector<DrawItem>& overlay, int* w, int* h) override {
        if (!window_) return false;
        flush_uploads();
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd) return false;
        SDL_GPUTexture* sw = nullptr;
        Uint32 sw_w = 0, sw_h = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &sw, &sw_w, &sw_h) || !sw) {
            SDL_SubmitGPUCommandBuffer(cmd);
            return false;
        }
        if (w) *w = int(sw_w);
        if (h) *h = int(sw_h);
        if (!ensure_targets(win_targets_, swap_format_, sw_w, sw_h, false)) { SDL_SubmitGPUCommandBuffer(cmd); return false; }
        render_views(cmd, win_targets_, win_views_, sw, swap_format_, views, ofp, overlay);
        return SDL_SubmitGPUCommandBuffer(cmd);
    }

    bool draw_views_offscreen(int w, int h, const std::vector<ViewPass>& views, const FrameParams& ofp, const std::vector<DrawItem>& overlay,
                              std::vector<std::uint8_t>& rgba) override {
        const SDL_GPUTextureFormat fmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        flush_uploads();
        if (!ensure_targets(off_targets_, fmt, std::uint32_t(w), std::uint32_t(h), true)) return false;
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        render_views(cmd, off_targets_, off_views_, off_targets_.resolve, fmt, views, ofp, overlay);
        return read_back(cmd, w, h, rgba);
    }

    bool draw_offscreen(int w, int h, const FrameParams& fp, const std::vector<DrawItem>& items,
                        std::vector<std::uint8_t>& rgba) override {
        const SDL_GPUTextureFormat fmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        flush_uploads();
        if (!ensure_targets(off_targets_, fmt, std::uint32_t(w), std::uint32_t(h), true)) return false;
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        render(cmd, off_targets_, off_targets_.resolve, fmt, fp, items);
        return read_back(cmd, w, h, rgba);
    }

    // perf1: the frame drawn and waited for, no read-back
    bool draw_offscreen_wait(int w, int h, const FrameParams& fp, const std::vector<DrawItem>& items) override {
        const SDL_GPUTextureFormat fmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        flush_uploads();
        if (!ensure_targets(off_targets_, fmt, std::uint32_t(w), std::uint32_t(h), true)) return false;
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        render(cmd, off_targets_, off_targets_.resolve, fmt, fp, items);
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (!fence) return false;
        SDL_WaitForGPUFences(dev_, true, &fence, 1);
        SDL_ReleaseGPUFence(dev_, fence);
        return true;
    }
    Stats take_stats() override { const Stats r = stats_; stats_ = Stats{}; return r; }
    void set_dither(float steps) override { dither_ = steps; }
    void set_sharp_upscale(bool on) override { sharp_upscale_ = on; }   // 1.1
    void set_render_scale(float scale) override {   // effects2: at 100% the extra targets are freed (the GPU may still read them: wait first)
        render_scale_ = std::clamp(scale, 0.25f, 1.0f);
        if (render_scale_ >= 0.999f && (scaled_win_.depth || scaled_off_.depth)) {
            flush_uploads();
            SDL_WaitForGPUIdle(dev_);
            release_targets(scaled_win_);
            release_targets(scaled_off_);
        }
    }

    // offscreen: the resolve target downloaded into `rgba` (submits `cmd` and waits)
    bool read_back(SDL_GPUCommandBuffer* cmd, int w, int h, std::vector<std::uint8_t>& rgba) {
        const std::uint32_t bytes = std::uint32_t(w) * std::uint32_t(h) * 4;
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes, 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion reg{};
        reg.texture = off_targets_.resolve;
        reg.w = std::uint32_t(w);
        reg.h = std::uint32_t(h);
        reg.d = 1;
        SDL_GPUTextureTransferInfo dst{tb, 0, std::uint32_t(w), std::uint32_t(h)};
        SDL_DownloadFromGPUTexture(cp, &reg, &dst);
        SDL_EndGPUCopyPass(cp);
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (!fence) { SDL_ReleaseGPUTransferBuffer(dev_, tb); return false; }
        SDL_WaitForGPUFences(dev_, true, &fence, 1);
        SDL_ReleaseGPUFence(dev_, fence);
        const auto* src = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(dev_, tb, false));
        rgba.assign(src, src + bytes);
        SDL_UnmapGPUTransferBuffer(dev_, tb);
        SDL_ReleaseGPUTransferBuffer(dev_, tb);
        return true;
    }

private:
    struct Targets {
        std::uint32_t w = 0, h = 0;
        SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
        SDL_GPUTexture* scene = nullptr;    // the GS frame (kSceneFormat, MSAA when enabled)
        SDL_GPUTexture* scene_resolve = nullptr;  // its single-sample copy the present pass samples (MSAA only)
        SDL_GPUTexture* bloom_src = nullptr;  // MSAA + PC bloom: the 3D picture resolved before the bloom (no MSAA store / reload)
        SDL_GPUTexture* depth = nullptr;
        SDL_GPUTexture* resolve = nullptr;  // offscreen single-sample result
        // the post passes' buffers (kSceneFormat, made on first use, remade when their size changes): water1 / post1's
        // zoom blur ping-pong, post1's bloom H (half), Q (quarter, ping-pong) and the kept picture P (ping-pong; `bp_cur`
        // holds the current one, `bp_valid` whether it holds a picture yet)
        struct Buf { SDL_GPUTexture* t = nullptr; std::uint32_t w = 0, h = 0; };
        Buf zb[2], bh, bq[2], bp[2];
        int bp_cur = 0;
        bool bp_valid = false;
        // photo2 (FrameParams::Dof): the depth pre-pass (single-sample, sampled), its colour target (writes off), the
        // half-size buffers W (prep) and G (gather); made on first use
        SDL_GPUTexture *dof_depth = nullptr, *dof_colour = nullptr;
        Buf dw, dg;
    };

    static bool fail(std::string* err, const char* what) {
        if (err) *err = std::string(what) + ": " + SDL_GetError();
        return false;
    }
    static SDL_GPUSampleCount sample_count(int s) {
        return s >= 8 ? SDL_GPU_SAMPLECOUNT_8 : s >= 4 ? SDL_GPU_SAMPLECOUNT_4 : s >= 2 ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_1;
    }

    SDL_GPUShader* shader(const std::uint8_t* spv, std::size_t spv_size, const char* msl, SDL_GPUShaderStage stage,
                          std::uint32_t samplers, std::uint32_t uniforms) {
        const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(dev_);
        SDL_GPUShaderCreateInfo si{};
        if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
            si.format = SDL_GPU_SHADERFORMAT_SPIRV;
            si.code = spv;
            si.code_size = spv_size;
            si.entrypoint = "main";
        } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
            si.format = SDL_GPU_SHADERFORMAT_MSL;
            si.code = reinterpret_cast<const Uint8*>(msl);
            si.code_size = std::strlen(msl);
            si.entrypoint = "main0";
        } else {
            return nullptr;
        }
        si.stage = stage;
        si.num_samplers = samplers;
        si.num_uniform_buffers = uniforms;
        return SDL_CreateGPUShader(dev_, &si);
    }

    bool make_shaders(std::string* err) {
        const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(dev_);
        if (!(formats & (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL))) {
            if (err) *err = "the GPU driver accepts neither SPIR-V nor MSL (D3D12 needs DXIL: not generated yet)";
            return false;
        }
        using namespace shaders;
        vs_ = shader(mesh_vert_spv, sizeof mesh_vert_spv, mesh_vert_msl, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
        fs_ = shader(mesh_frag_spv, sizeof mesh_frag_spv, mesh_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
        fsq_vs_ = shader(fullscreen_vert_spv, sizeof fullscreen_vert_spv, fullscreen_vert_msl, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
        spec_fs_ = shader(specular_frag_spv, sizeof specular_frag_spv, specular_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
        present_fs_ = shader(present_frag_spv, sizeof present_frag_spv, present_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
        car_vs_ = shader(car_vert_spv, sizeof car_vert_spv, car_vert_msl, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
        car_fs_ = shader(car_frag_spv, sizeof car_frag_spv, car_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
        sun_vs_ = shader(sun_vert_spv, sizeof sun_vert_spv, sun_vert_msl, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
        sun_fs_ = shader(sun_frag_spv, sizeof sun_frag_spv, sun_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
        blur_fs_ = shader(blur_frag_spv, sizeof blur_frag_spv, blur_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
        post_fs_ = shader(post_frag_spv, sizeof post_frag_spv, post_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
        post_lean_fs_ = shader(post_lean_frag_spv, sizeof post_lean_frag_spv, post_lean_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);   // the one-draw composite: a failure only turns that form off
        if (!vs_ || !fs_ || !fsq_vs_ || !spec_fs_ || !present_fs_ || !car_vs_ || !car_fs_ || !sun_vs_ || !sun_fs_ || !blur_fs_ || !post_fs_)
            return fail(err, "SDL_CreateGPUShader");
        return true;
    }

    // Full-screen passes: the specular add inside the scene pass, the present into the output.
    SDL_GPUGraphicsPipeline* fullscreen_pipeline(SDL_GPUTextureFormat fmt, bool specular) {
        SDL_GPUColorTargetDescription ct{};
        ct.format = fmt;
        if (specular) {  // GS ALPHA 0x58: Cd + Cs * Ad (Ad = frame alpha As / 128); frame alpha unchanged
            ct.blend_state.enable_blend = true;
            ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_ALPHA;
            ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = fsq_vs_;
        pi.fragment_shader = specular ? spec_fs_ : present_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = specular ? sample_count(msaa_) : SDL_GPU_SAMPLECOUNT_1;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        if (specular) {
            pi.target_info.depth_stencil_format = depth_format_;
            pi.target_info.has_depth_stencil_target = true;
        }
        SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: full-screen pipeline: %s", SDL_GetError());
        return p;
    }

    // zwrite: blended draws that write Z (the post-fog stage); slots 6 / 7 (culled)
    SDL_GPUGraphicsPipeline* pipeline(SDL_GPUTextureFormat fmt, Blend blend, bool cull, bool car = false, bool zwrite = false) {
        auto& slot = pipelines_[fmt];
        zwrite = zwrite && blend == Blend::kAlpha;
        SDL_GPUGraphicsPipeline*& p = car ? car_pipe_ : slot[zwrite ? 6 + (cull ? 1 : 0) : int(blend) + (cull ? 3 : 0)];
        if (p) return p;
        SDL_GPUVertexBufferDescription vb{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[5] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 32},
            {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, 36},
        };
        SDL_GPUColorTargetDescription ct{};
        ct.format = fmt;
        if (car) {
            // car.frag: X + (1 - alpha) Cd (both GS passes folded); frame alpha := 0 (see car.frag)
            ct.blend_state.enable_blend = true;
            ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        } else if (blend == Blend::kAlpha) {
            ct.blend_state.enable_blend = true;
            ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            // The GS writes the source alpha into the frame alpha, unblended.
            ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = car ? car_vs_ : vs_;
        pi.fragment_shader = car ? car_fs_ : fs_;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = at;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        // Track materials without flag 0x20 cull back faces in their VU1 program; with the emitted strip order and a
        // right-handed view those are the clockwise ones (docs/native-renderer.md §11.3).
        // Cars: VU1 0x46BB80 has no back-face test (ADC comes from the data and the guard band only), the GS draws
        // both sides [C docs/car-shading.md §4.4].
        pi.rasterizer_state.cull_mode = cull && !car ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.multisample_state.sample_count = sample_count(msaa_);
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = car || zwrite || blend != Blend::kAlpha;  // car TEST_1 0x50003: Z GEQUAL, written
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;  // reverse-Z
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: pipeline: %s", SDL_GetError());
        return p;
    }

    // Effects stage (DrawItem::effect): the GS ALPHA (A - B) C + D as GPU blend factors. The shader's alpha carries C
    // (As, or FIX / 128 for C = FIX), so only the destination factor varies: B = Cd, D = Cd -> 1 - C; B = 0, D = Cd ->
    // ONE (additive); D = 0 -> ZERO. RGB only (FRAME FBMSK 0xFF000000 keeps the frame alpha); Z GEQUAL, not written.
    static int effect_mode(std::uint8_t alpha) {
        const int b = (alpha >> 2) & 3, d = (alpha >> 6) & 3;
        if (d == 2) return 2;
        return b == 1 ? 0 : 1;
    }
    SDL_GPUGraphicsPipeline* effect_pipeline(int mode) {
        SDL_GPUGraphicsPipeline*& p = effect_pipe_[mode];
        if (p) return p;
        SDL_GPUVertexBufferDescription vb{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[5] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 32},
            {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, 36},
        };
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ct.blend_state.dst_color_blendfactor = mode == 0 ? SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
                                               : mode == 1 ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ZERO;
        ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        ct.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
        ct.blend_state.enable_color_write_mask = true;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs_;
        pi.fragment_shader = fs_;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = at;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;   // the GS has no culling
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.multisample_state.sample_count = sample_count(msaa_);
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = false;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;  // reverse-Z GEQUAL
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: effect pipeline: %s", SDL_GetError());
        return p;
    }

    // lines1 (docs/native-renderer.md §11.8): the track's line meshes (material type 3: TEST_1 0x5100A, ALPHA_1 0x42 =
    // Cd - Cs x As with As = AA1's coverage). mesh.frag outputs Cs x coverage, blended as Cd - src; the frame alpha is
    // left as it was (the full-screen specular reads it; with the coverage there the wires came out light). Z tested GEQUAL, not written (the GS writes no Z
    // for AA1 lines: GS User's Manual §3.6.2), no culling (the GS has none).
    static bool is_line(const DrawItem& it) {
        return it.shading == Shading::kTrack && it.track_type == 3 && !it.overlay && !it.effect && !it.post_fog;
    }
    SDL_GPUGraphicsPipeline* line_pipeline() {
        if (line_pipe_) return line_pipe_;
        SDL_GPUVertexBufferDescription vb{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[5] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 32},
            {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, 36},
        };
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_REVERSE_SUBTRACT;   // dst - src
        // The frame alpha is kept (not the GS's alpha = coverage): the full-screen specular reads it, and with the
        // coverage there it lit the wires up, lighter than the sky on routes with a strong specular. Wires stay dark.
        ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs_;
        pi.fragment_shader = fs_;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = at;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.multisample_state.sample_count = sample_count(msaa_);
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = false;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;  // reverse-Z GEQUAL (ZTST 2)
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        line_pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!line_pipe_) SDL_Log("b3gfx: line pipeline: %s", SDL_GetError());
        return line_pipe_;
    }

    // The 2D stage (hud1): the GS ALPHA (A - B) C + D with A, B in {Cs, Cd, 0}, C in {As, Ad, FIX}, D in {Cs, Cd, 0} as
    // GPU blend factors; the shader's alpha is As / 128, the frame alpha holds Ad / 128 (both may exceed 1: 0x80 = 1).
    // Covers every mode Gt2d selects (C = FIX only with A = B: the FIX value then drops out). key: alpha byte, bit 8 =
    // alpha only (FBMSK clear: the frame alpha takes As, colour kept), bit 9 = no blending (PRIM ABE clear).
    SDL_GPUGraphicsPipeline* overlay_pipeline(int key) {
        SDL_GPUGraphicsPipeline*& p = overlay_pipe_[key];
        if (p) return p;
        const int a = key & 3, b = (key >> 2) & 3, c = (key >> 4) & 3, d = (key >> 6) & 3;
        const bool alpha_only = (key >> 8) & 1, no_blend = (key >> 9) & 1;
        const bool ztest = (key >> 10) & 1, zwrite = (key >> 11) & 1;   // fe2: the globe's B3Im3d batches
        SDL_GPUVertexBufferDescription vb{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[5] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 32},
            {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, 36},
        };
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        auto& bs = ct.blend_state;
        bs.enable_blend = true;
        const SDL_GPUBlendFactor cf = c == 1 ? SDL_GPU_BLENDFACTOR_DST_ALPHA : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        const SDL_GPUBlendFactor cf1 = c == 1 ? SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        bs.color_blend_op = SDL_GPU_BLENDOP_ADD;
        if (no_blend || (a == 2 && b == 2 && d == 0)) {             // Cs
            bs.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        } else if (a == b) {                                        // D alone
            bs.src_color_blendfactor = d == 0 ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ZERO;
            bs.dst_color_blendfactor = d == 1 ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ZERO;
        } else if (a == 0 && b == 1 && d == 1) {                    // (Cs - Cd) C + Cd
            bs.src_color_blendfactor = cf;
            bs.dst_color_blendfactor = cf1;
        } else if (a == 0 && b == 2 && d == 1) {                    // Cs C + Cd
            bs.src_color_blendfactor = cf;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        } else if (a == 2 && b == 0 && d == 1) {                    // Cd - Cs C
            bs.src_color_blendfactor = cf;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            bs.color_blend_op = SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        } else if (a == 0 && b == 2 && d == 2) {                    // Cs C
            bs.src_color_blendfactor = cf;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        } else {                                                    // not used by Gt2d: plain alpha blending
            bs.src_color_blendfactor = cf;
            bs.dst_color_blendfactor = cf1;
        }
        bs.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;         // alpha only: the frame alpha takes As
        bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        bs.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        bs.color_write_mask = alpha_only ? SDL_GPU_COLORCOMPONENT_A
                                         : (SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B);
        bs.enable_color_write_mask = true;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs_;
        pi.fragment_shader = fs_;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = at;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.multisample_state.sample_count = sample_count(msaa_);
        pi.depth_stencil_state.enable_depth_test = ztest;
        pi.depth_stencil_state.enable_depth_write = ztest && zwrite;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;  // reverse-Z GEQUAL (the GS's ZTST 2)
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: overlay pipeline: %s", SDL_GetError());
        return p;
    }

    // Sun occlusion (water1): mesh shaders into the occlusion target; every fragment that survives the alpha test
    // writes 0 (blend ZERO, ZERO), the clear value 1 stays where nothing covers: the GS sprites' "Z == 0" sky test.
    SDL_GPUGraphicsPipeline* occ_pipeline(bool cull) {
        SDL_GPUGraphicsPipeline*& p = occ_pipe_[cull ? 1 : 0];
        if (p) return p;
        SDL_GPUVertexBufferDescription vb{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[5] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 32},
            {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, 36},
        };
        SDL_GPUColorTargetDescription ct{};
        ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        auto& bs = ct.blend_state;
        bs.enable_blend = true;
        bs.src_color_blendfactor = bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        bs.src_alpha_blendfactor = bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        bs.color_blend_op = bs.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs_;
        pi.fragment_shader = fs_;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = at;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = cull ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: occlusion pipeline: %s", SDL_GetError());
        return p;
    }
    // The sun sprites: additive on RGB (ALPHA 0x48 with As from the sun.frag), the frame alpha kept, no depth test
    // (the game's TEST_1 for them is the sky's: ZTST ALWAYS [L skyTest_1 restored by OcclusionTest]).
    SDL_GPUGraphicsPipeline* sun_pipeline() {
        if (sun_pipe_) return sun_pipe_;
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        auto& bs = ct.blend_state;
        bs.enable_blend = true;
        bs.src_color_blendfactor = bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        bs.color_blend_op = SDL_GPU_BLENDOP_ADD;
        bs.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        bs.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        bs.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
        bs.enable_color_write_mask = true;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = sun_vs_;
        pi.fragment_shader = sun_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = sample_count(msaa_);
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        sun_pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!sun_pipe_) SDL_Log("b3gfx: sun pipeline: %s", SDL_GetError());
        return sun_pipe_;
    }
    // The occlusion pass (before the scene pass): the draws that write Z in the game (opaque and alpha-tested track,
    // cars, the post-fog B3Im3d objects; not the sky, the blended track, effects or the 2D stage) through a projection
    // that maps fp.sun_box onto the target. Then the mip chain, whose 1 x 1 level is the uncovered share.
    bool sun_occlusion_pass(SDL_GPUCommandBuffer* cmd, const FrameParams& fp, const std::vector<DrawItem>& items) {
        const float* b = fp.sun_box;
        if (!(b[2] > b[0] && b[3] > b[1])) return false;
        if (!occ_) {
            SDL_GPUTextureCreateInfo ti{};
            ti.type = SDL_GPU_TEXTURETYPE_2D;
            ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            ti.width = ti.height = kOccSize;
            ti.layer_count_or_depth = 1;
            ti.num_levels = kOccLevels;
            ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
            occ_ = SDL_CreateGPUTexture(dev_, &ti);
            if (!occ_) return false;
        }
        // clip' = C clip: x' = (x - cx w) / hx, y' = (y - cy w) / hy (column-major C)
        const float cx = 0.5f * (b[0] + b[2]), cy = 0.5f * (b[1] + b[3]), hx = 0.5f * (b[2] - b[0]), hy = 0.5f * (b[3] - b[1]);
        const float C[16] = {1 / hx, 0, 0, 0, 0, 1 / hy, 0, 0, 0, 0, 1, 0, -cx / hx, -cy / hy, 0, 1};
        FrameParams op = fp;
        op.sun_sprites.clear();
        mat4_mul(C, fp.view_proj, op.view_proj);
        op.fog_colour[3] = 0;
        SDL_GPUColorTargetInfo ci{};
        ci.texture = occ_;
        ci.clear_color = SDL_FColor{1, 1, 1, 1};
        ci.load_op = SDL_GPU_LOADOP_CLEAR;
        ci.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);
        const float ld[3] = {0, 1, 0};
        SDL_GPUGraphicsPipeline* bound = nullptr;
        BufferId bvb = kNone, bib = kNone;
        for (const DrawItem& it : items) {
            if (it.overlay || it.effect || it.shading == Shading::kSky) continue;
            if (it.blend == Blend::kAlpha && it.shading != Shading::kCar && !it.post_fog) continue;
            draw_item(pass, cmd, op, it, ld, bound, bvb, bib, true);
        }
        SDL_EndGPURenderPass(pass);
        SDL_GenerateMipmapsForGPUTexture(cmd, occ_);
        return true;
    }
    void draw_sun(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const FrameParams& fp, bool occ_ok,
                  SDL_GPUGraphicsPipeline*& bound) {
        if (fp.sun_sprites.empty() || fp.sun_texture >= textures_.size()) return;
        SDL_GPUGraphicsPipeline* p = sun_pipeline();
        if (!p) return;
        SDL_BindGPUGraphicsPipeline(pass, p);
        bound = p;
        struct { float rect[13][4], uv[13][4], rgb[13][4]; } vu{};
        const std::size_t n = std::min<std::size_t>(fp.sun_sprites.size(), 13);
        for (std::size_t k = 0; k < n; ++k) {
            std::memcpy(vu.rect[k], fp.sun_sprites[k].rect, 16);
            std::memcpy(vu.uv[k], fp.sun_sprites[k].uv, 16);
            std::memcpy(vu.rgb[k], fp.sun_sprites[k].rgb, 16);
        }
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
        const bool test = fp.sun_occlusion && occ_ok && occ_;
        const float fu[4] = {float(kOccLevels - 1), test ? 1.0f : 0.0f, 0, 0};
        SDL_PushGPUFragmentUniformData(cmd, 0, fu, sizeof fu);
        SDL_GPUTextureSamplerBinding tsb[2] = {{textures_[fp.sun_texture], effect_sampler_}, {occ_ ? occ_ : white_, effect_sampler_}};
        SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
        SDL_DrawGPUPrimitives(pass, std::uint32_t(6 * n), 1, 0, 0);
    }

    SDL_GPUGraphicsPipeline* blur_pipeline(bool composite) {
        SDL_GPUGraphicsPipeline*& p = blur_pipe_[composite ? 1 : 0];
        if (p) return p;
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        if (composite) {
            auto& bs = ct.blend_state;
            bs.enable_blend = true;
            bs.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            bs.color_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            bs.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
            bs.enable_color_write_mask = true;
        }
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = fsq_vs_;
        pi.fragment_shader = blur_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = composite ? sample_count(msaa_) : SDL_GPU_SAMPLECOUNT_1;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        if (composite) {   // drawn in the resumed scene pass, which has the depth target attached
            pi.target_info.depth_stencil_format = depth_format_;
            pi.target_info.has_depth_stencil_target = true;
        }
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: radial blur pipeline: %s", SDL_GetError());
        return p;
    }
    struct BlurUniforms { float a[4], b[4], c[4]; };
    // One zoom blur for the frame (post1): the directional blur when its amount is set, else the radial one.
    struct ZoomBlur {
        bool radial = false;
        float centre[2] = {0.5f, 0.5f}, radius[2] = {1, 1};
        float zoom[3][2] = {};       // per pass: BlurOut's zoom, or BlurIn's inverse
        float gain = 1, colour = 1, amount = 0;
        bool flip = false;
        TextureId mask = kNone;
    };
    ZoomBlur zoom_blur(const FrameParams& fp) const {
        ZoomBlur z;
        if (fp.dblur.amount > 0) {   // CB3DirectionalBlur::Render: BlurOut (s), BlurIn (1 / s^2), BlurOut (s^4)
            float sx = fp.dblur.zoom[0], sy = fp.dblur.zoom[1];
            for (int k = 0; k < 3; ++k) {
                const bool in = k == 1;
                z.zoom[k][0] = in ? 1.0f / sx : sx;
                z.zoom[k][1] = in ? 1.0f / sy : sy;
                sx *= sx;
                sy *= sy;
            }
            z.centre[0] = fp.dblur.centre[0];
            z.centre[1] = fp.dblur.centre[1];
            z.amount = std::min(fp.dblur.amount, 1.0f);
            return z;
        }
        if (fp.blur.amount > 0 && fp.blur.mask < textures_.size()) {   // CB3RadialBlur::Render: BlurOut (z, z^2, z^4)
            float zx = fp.blur.uv_scale[0], zy = fp.blur.uv_scale[1];
            for (int k = 0; k < 3; ++k) { z.zoom[k][0] = zx; z.zoom[k][1] = zy; zx *= zx; zy *= zy; }
            z.radial = true;
            for (int k = 0; k < 2; ++k) { z.centre[k] = fp.blur.centre[k]; z.radius[k] = fp.blur.radius[k]; }
            z.gain = 136.0f / 128.0f;
            z.colour = fp.blur.colour;
            z.amount = fp.blur.amount;
            z.flip = fp.blur.flip;
            z.mask = fp.blur.mask;
        }
        return z;
    }
    static BlurUniforms blur_uniforms(const ZoomBlur& z, int mode, int pass) {
        const float zx = pass >= 0 ? z.zoom[pass][0] : 0, zy = pass >= 0 ? z.zoom[pass][1] : 0;
        return {{float(mode), z.amount, z.colour, z.flip ? 1.0f : 0.0f}, {z.centre[0], z.centre[1], z.radius[0], z.radius[1]},
                {zx, zy, z.gain, z.radial ? 1.0f : 0.0f}};
    }
    bool ensure_buf(Targets::Buf& b, std::uint32_t w, std::uint32_t h) {
        w = std::max<std::uint32_t>(1, w);
        h = std::max<std::uint32_t>(1, h);
        if (b.t && b.w == w && b.h == h) return true;
        if (b.t) SDL_ReleaseGPUTexture(dev_, b.t);
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = kSceneFormat;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = w;
        ti.height = h;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
        b.t = SDL_CreateGPUTexture(dev_, &ti);
        b.w = b.t ? w : 0;
        b.h = b.t ? h : 0;
        return b.t != nullptr;
    }
    // The GS's buffer of a 448-row frame scaled by `div` (2: half, 4: quarter), as wide as the output's aspect.
    static void game_size(const Targets& t, int div, std::uint32_t& w, std::uint32_t& h) {
        h = 448u / std::uint32_t(div);
        w = std::max<std::uint32_t>(1, std::uint32_t(std::lround(double(t.w) * double(h) / double(std::max<std::uint32_t>(1, t.h)))));
    }
    // One full-target pass of `fs` into `dst` (no blend) reading tex0 / tex1.
    void post_pass(SDL_GPUCommandBuffer* cmd, SDL_GPUGraphicsPipeline* p, SDL_GPUTexture* dst, SDL_GPUTexture* tex0, SDL_GPUTexture* tex1,
                   const void* u, std::size_t un, SDL_GPUSampler* smp) {
        B3PERF_SCOPE(kCmdPost);
        SDL_GPUColorTargetInfo ci{};
        ci.texture = dst;
        ci.load_op = SDL_GPU_LOADOP_DONT_CARE;
        ci.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(rp, p);
        SDL_PushGPUFragmentUniformData(cmd, 0, u, std::uint32_t(un));
        SDL_GPUTextureSamplerBinding tsb[2] = {{tex0, smp}, {tex1 ? tex1 : tex0, smp}};
        SDL_BindGPUFragmentSamplers(rp, 0, tsb, 2);
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }
    // The zoom blur's chain: downsample (+ mask) into zb[0], then the three zoom passes ping-pong (the GS does them in
    // place; its strips are ordered so each reads pixels it has not written yet [L]) [G that ping-pong is equal]. Its
    // buffer: the GS's half size (game quality) or the output size (PC). Returns the buffer with the result.
    SDL_GPUTexture* zoom_blur_chain(SDL_GPUCommandBuffer* cmd, Targets& t, const FrameParams& fp, const ZoomBlur& z) {
        std::uint32_t w = t.w, h = t.h;
        if (fp.post_game) game_size(t, 2, w, h);
        if (!ensure_buf(t.zb[0], w, h) || !ensure_buf(t.zb[1], w, h)) return nullptr;
        SDL_GPUGraphicsPipeline* p = blur_pipeline(false);
        if (!p) return nullptr;
        SDL_GPUTexture* src = msaa_ > 1 ? t.scene_resolve : t.scene;
        SDL_GPUTexture* mask = z.radial ? textures_[z.mask] : white_;
        for (int pass = 0; pass < 4; ++pass) {
            SDL_GPUColorTargetInfo ci{};
            ci.texture = t.zb[pass & 1].t;
            ci.load_op = SDL_GPU_LOADOP_DONT_CARE;
            ci.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);
            SDL_BindGPUGraphicsPipeline(rp, p);
            const BlurUniforms u = blur_uniforms(z, pass == 0 ? 0 : 1, pass == 0 ? -1 : pass - 1);
            SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
            SDL_GPUTextureSamplerBinding tsb[2] = {{pass == 0 ? src : t.zb[(pass - 1) & 1].t, effect_sampler_}, {mask, effect_sampler_}};
            SDL_BindGPUFragmentSamplers(rp, 0, tsb, 2);
            SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
            SDL_EndGPURenderPass(rp);
        }
        return t.zb[1].t;
    }

    // The bloom (post1, shaders/post.frag): H (half, the bright part), Q (quarter) blurred by Blur's four passes, then P =
    // Blend(Q, P) kept from frame to frame. Returns the new P (nullptr: nothing). H / Q at the GS's sizes in both qualities
    // (the effect's spread is set by them); PC takes H's taps from every output pixel.
    struct PostUniforms { float a[4], b[4], c[4]; };
    SDL_GPUGraphicsPipeline* post_pipeline(bool composite, bool soft = false, bool one_draw = false) {
        SDL_GPUGraphicsPipeline*& p = post_pipe_[one_draw ? 3 : composite ? (soft ? 2 : 1) : 0];
        if (p) return p;
        if (one_draw) composite = false;   // no blend state: the shader adds the glow itself
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        if (composite) {   // CopyToScreen: Cs + Cd on RGB (ALPHA 0x29 FIX 0x80), frame alpha kept
            auto& bs = ct.blend_state;
            bs.enable_blend = true;
            // bloom2 SOFT: a screen blend, Cs (1 - Cd) + Cd: the sum never passes 1, the highlights keep their detail
            bs.src_color_blendfactor = soft ? SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR : SDL_GPU_BLENDFACTOR_ONE;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            bs.color_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            bs.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
            bs.enable_color_write_mask = true;
        }
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = fsq_vs_;
        pi.fragment_shader = one_draw ? post_lean_fs_ : post_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = composite || one_draw ? sample_count(msaa_) : SDL_GPU_SAMPLECOUNT_1;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        if (composite || one_draw) {
            pi.target_info.depth_stencil_format = depth_format_;
            pi.target_info.has_depth_stencil_target = true;
        }
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: bloom pipeline: %s", SDL_GetError());
        return p;
    }
    SDL_GPUTexture* bloom_chain(SDL_GPUCommandBuffer* cmd, Targets& t, const FrameParams& fp, SDL_GPUTexture* src) {
        const auto& bl = fp.bloom;
        std::uint32_t hw, hh, qw, qh;
        game_size(t, 2, hw, hh);
        game_size(t, 4, qw, qh);
        if (!ensure_buf(t.bh, hw, hh) || !ensure_buf(t.bq[0], qw, qh) || !ensure_buf(t.bq[1], qw, qh)) return nullptr;
        if (t.bp[0].w != qw || t.bp[0].h != qh || bl.restart) t.bp_valid = false;
        if (!ensure_buf(t.bp[0], qw, qh) || !ensure_buf(t.bp[1], qw, qh)) return nullptr;
        SDL_GPUGraphicsPipeline* p = post_pipeline(false);
        if (!p) return nullptr;
        const float hx = 1.0f / float(hw), hy = 1.0f / float(hh), qx = 1.0f / float(qw), qy = 1.0f / float(qh);
        // FirstDownsample: game: the H texel's 2 x 2 GS frame pixels (bilinear taps at their centres); PC: n x n taps,
        // n = the output pixels per H texel (at most 8)
        const int n = fp.post_game ? 2 : std::clamp(int(std::ceil(float(t.h) / float(hh))), 2, 8);
        PostUniforms u{{0, float(n), 0, 0}, {bl.c, bl.k1, bl.k2, bl.k3}, {hx, hy, 1.0f / float(t.w), 1.0f / float(t.h)}};
        post_pass(cmd, p, t.bh.t, src, nullptr, &u, sizeof u, effect_sampler_);
        u = {{1, 0, 0, 0}, {0, 0, 0, 0}, {qx, qy, hx, hy}};   // SecondDownsample: one bilinear tap = the 2 x 2 average
        post_pass(cmd, p, t.bq[0].t, t.bh.t, nullptr, &u, sizeof u, effect_sampler_);
        static const float kOff[4][2] = {{1.5f, 0}, {-1.5f, 0}, {0, 1.5f}, {0, -1.5f}};   // Blur's four passes
        for (int k = 0; k < 4; ++k) {
            u = {{2, 0, 0, 0}, {kOff[k][0], kOff[k][1], 0, 0}, {qx, qy, qx, qy}};
            post_pass(cmd, p, t.bq[(k + 1) & 1].t, t.bq[k & 1].t, nullptr, &u, sizeof u, effect_sampler_);
        }
        SDL_GPUTexture* q = t.bq[0].t;   // after four passes the result is back in bq[0]
        // Blend: over `renders` renders of this Q, P keep^n and Q (1 - keep^n) / (1 - keep); none yet: P = that sum alone
        const float n_r = std::max(0.0f, bl.renders);
        if (!t.bp_valid || n_r > 0 || bl.mode != 0) {
            const float kn = std::pow(std::clamp(bl.keep, 0.0f, 0.9999f), n_r > 0 ? n_r : 1.0f);
            const float w = bl.keep < 0.9999f ? (1.0f - kn) / (1.0f - bl.keep) : n_r;
            const bool reset = bl.mode == 1 || !t.bp_valid, freeze = bl.mode == 2 && t.bp_valid;
            const int dst = t.bp_valid ? 1 - t.bp_cur : t.bp_cur;
            u = {{3, 0, 0, 0}, {t.bp_valid ? w : 1.0f, t.bp_valid ? kn : 0.0f, reset ? 1.0f : 0.0f, freeze ? 1.0f : 0.0f}, {qx, qy, qx, qy}};
            post_pass(cmd, p, t.bp[dst].t, q, t.bp[t.bp_cur].t, &u, sizeof u, effect_sampler_);
            t.bp_cur = dst;
            t.bp_valid = true;
        }
        return t.bp[t.bp_cur].t;
    }

    // ---- photo2: the depth of field (FrameParams::Dof, shaders/dof.frag) ----
    struct DofUniforms { float a[4], b[4], c[4], d[4], e[4]; };
    // dof.frag and the pre-pass's depth format, on first use. False (logged once): no depth of field on this device; the
    // frame is drawn without it.
    bool dof_ready() {
        if (dof_failed_) return false;
        if (dof_fs_) return true;
        for (SDL_GPUTextureFormat f : {SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D16_UNORM}) {
            if (SDL_GPUTextureSupportsFormat(dev_, f, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
                dof_depth_format_ = f;
                break;
            }
        }
        using namespace shaders;
        if (dof_depth_format_ != SDL_GPU_TEXTUREFORMAT_INVALID)
            dof_fs_ = shader(dof_frag_spv, sizeof dof_frag_spv, dof_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 1);
        if (!dof_fs_) {
            SDL_Log("b3gfx: depth of field unavailable: %s", dof_depth_format_ == SDL_GPU_TEXTUREFORMAT_INVALID ? "no sampled depth format" : SDL_GetError());
            dof_failed_ = true;
            return false;
        }
        return true;
    }
    // The pre-pass: [0] mesh shaders, [1] culled, [2] the car shaders; Z GEQUAL written into the sampled depth, the
    // fragment shaders' alpha tests and screen-door discards kept, colour writes off
    SDL_GPUGraphicsPipeline* dof_depth_pipeline(int k) {
        SDL_GPUGraphicsPipeline*& p = dof_depth_pipe_[k];
        if (p) return p;
        SDL_GPUVertexBufferDescription vb{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[5] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 32},
            {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, 36},
        };
        SDL_GPUColorTargetDescription ct{};
        ct.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        ct.blend_state.enable_color_write_mask = true;
        ct.blend_state.color_write_mask = 0;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = k == 2 ? car_vs_ : vs_;
        pi.fragment_shader = k == 2 ? car_fs_ : fs_;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = at;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = k == 1 ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;   // reverse-Z
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = dof_depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: depth of field pre-pass pipeline: %s", SDL_GetError());
        return p;
    }
    // dof.frag: [0] its passes into W / G (no blend), [1] the composite into the resumed scene pass (SRC_ALPHA, RGB only)
    SDL_GPUGraphicsPipeline* dof_pipeline(bool composite) {
        SDL_GPUGraphicsPipeline*& p = dof_pipe_[composite ? 1 : 0];
        if (p) return p;
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        if (composite) {
            auto& bs = ct.blend_state;
            bs.enable_blend = true;
            bs.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            bs.color_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            bs.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
            bs.enable_color_write_mask = true;
        }
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = fsq_vs_;
        pi.fragment_shader = dof_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = composite ? sample_count(msaa_) : SDL_GPU_SAMPLECOUNT_1;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        if (composite) {   // drawn in the resumed scene pass, which has the depth target attached
            pi.target_info.depth_stencil_format = depth_format_;
            pi.target_info.has_depth_stencil_target = true;
        }
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: depth of field pipeline: %s", SDL_GetError());
        return p;
    }
    // The uniforms of the three dof.frag passes (a.x, the mode, set per pass); false: nothing to blur (a circle under
    // half a W pixel, or a singular view-projection)
    bool dof_uniforms(const Targets& t, const FrameParams& fp, DofUniforms& u) const {
        float inv[16];
        if (!mat4_invert(fp.view_proj, inv)) return false;
        const float ww = float(t.dg.w), wh = float(t.dg.h);
        if (!(ww > 0 && wh > 0)) return false;
        const float r = std::clamp(fp.dof.radius, 0.0f, 0.1f) * wh;   // W pixels
        if (r < 0.5f) return false;
        // the spiral takes about r^2 / (2 step) taps: at most ~256 (beyond 16 W pixels the spiral is coarser)
        const float step = std::max(0.5f, r * r / 512.0f);
        u = {{0, r, step, fp.dof.focus > 0 ? fp.dof.focus : 0.0f}, {inv[3], inv[7], inv[11], inv[15]},
             {1.0f / ww, 1.0f / wh, 1.0f / float(t.w), 1.0f / float(t.h)}, {fp.dof.focus_scale > 0 ? fp.dof.focus_scale : 1.0f, 600, dof_debug_ ? 1.0f : 0.0f, 0},
             {std::clamp(fp.dof.point[0], 0.0f, 1.0f), std::clamp(fp.dof.point[1], 0.0f, 1.0f), 0, 0}};
        return true;
    }
    // The pre-pass (before the scene pass): the draws that write Z (as the sun's occlusion pass: not the sky, the blended
    // track, effects or the 2D stage) at the output size, into t.dof_depth
    bool dof_depth_pass(SDL_GPUCommandBuffer* cmd, Targets& t, const FrameParams& fp, const std::vector<DrawItem>& items) {
        if (!t.dof_depth) {
            SDL_GPUTextureCreateInfo ti{};
            ti.type = SDL_GPU_TEXTURETYPE_2D;
            ti.width = t.w;
            ti.height = t.h;
            ti.layer_count_or_depth = 1;
            ti.num_levels = 1;
            ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
            ti.format = dof_depth_format_;
            ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            t.dof_depth = SDL_CreateGPUTexture(dev_, &ti);
            ti.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            t.dof_colour = SDL_CreateGPUTexture(dev_, &ti);
            if (!t.dof_depth || !t.dof_colour) return false;
        }
        SDL_GPUColorTargetInfo ci{};
        ci.texture = t.dof_colour;
        ci.load_op = SDL_GPU_LOADOP_DONT_CARE;
        ci.store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPUDepthStencilTargetInfo di{};
        di.texture = t.dof_depth;
        di.clear_depth = 0.0f;   // reverse-Z: far
        di.load_op = SDL_GPU_LOADOP_CLEAR;
        di.store_op = SDL_GPU_STOREOP_STORE;
        di.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        di.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
        const float ld[3] = {0, 1, 0};
        SDL_GPUGraphicsPipeline* bound = nullptr;
        BufferId bvb = kNone, bib = kNone;
        for (const DrawItem& it : items) {
            if (it.overlay || it.effect || it.shading == Shading::kSky) continue;
            if (it.blend == Blend::kAlpha && it.shading != Shading::kCar && !it.post_fog) continue;
            draw_item(pass, cmd, fp, it, ld, bound, bvb, bib, false, true);
        }
        SDL_EndGPURenderPass(pass);
        return true;
    }
    // Prep (the frame -> W) and gather (W -> G); returns G (nullptr: nothing)
    SDL_GPUTexture* dof_chain(SDL_GPUCommandBuffer* cmd, Targets& t, const DofUniforms& base) {
        SDL_GPUGraphicsPipeline* p = dof_pipeline(false);
        if (!p) return nullptr;
        SDL_GPUTexture* src = msaa_ > 1 ? t.scene_resolve : t.scene;
        for (int mode = 0; mode < 2; ++mode) {
            DofUniforms u = base;
            u.a[0] = float(mode);
            SDL_GPUColorTargetInfo ci{};
            ci.texture = mode == 0 ? t.dw.t : t.dg.t;
            ci.load_op = SDL_GPU_LOADOP_DONT_CARE;
            ci.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);
            SDL_BindGPUGraphicsPipeline(rp, p);
            SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
            SDL_GPUTexture* in = mode == 0 ? src : t.dw.t;
            SDL_GPUTextureSamplerBinding tsb[3] = {{in, effect_sampler_}, {in, clamp_sampler_}, {t.dof_depth, clamp_sampler_}};
            SDL_BindGPUFragmentSamplers(rp, 0, tsb, 3);
            SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
            SDL_EndGPURenderPass(rp);
        }
        return t.dg.t;
    }

    bool ensure_bloom_src(Targets& t) {
        if (t.bloom_src) return true;
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.width = t.w;
        ti.height = t.h;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ti.format = kSceneFormat;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        t.bloom_src = SDL_CreateGPUTexture(dev_, &ti);
        return t.bloom_src != nullptr;
    }

    void release_targets(Targets& t) {
        for (auto* x : {t.scene, t.scene_resolve, t.bloom_src, t.depth, t.resolve, t.dof_depth, t.dof_colour}) if (x) SDL_ReleaseGPUTexture(dev_, x);
        for (auto* b : {&t.zb[0], &t.zb[1], &t.bh, &t.bq[0], &t.bq[1], &t.bp[0], &t.bp[1], &t.dw, &t.dg}) if (b->t) SDL_ReleaseGPUTexture(dev_, b->t);
        t = Targets{};
    }

    bool ensure_targets(Targets& t, SDL_GPUTextureFormat fmt, std::uint32_t w, std::uint32_t h, bool offscreen) {
        if (t.w == w && t.h == h && t.format == fmt && t.depth) return true;
        release_targets(t);
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.width = w;
        ti.height = h;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        ti.sample_count = sample_count(msaa_);
        ti.format = kSceneFormat;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | (msaa_ > 1 ? 0 : SDL_GPU_TEXTUREUSAGE_SAMPLER);
        t.scene = SDL_CreateGPUTexture(dev_, &ti);
        if (!t.scene) return false;
        ti.format = depth_format_;
        ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        t.depth = SDL_CreateGPUTexture(dev_, &ti);
        if (!t.depth) return false;
        ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
        if (msaa_ > 1) {
            ti.format = kSceneFormat;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            t.scene_resolve = SDL_CreateGPUTexture(dev_, &ti);
            if (!t.scene_resolve) return false;
        }
        if (offscreen) {
            ti.format = fmt;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            t.resolve = SDL_CreateGPUTexture(dev_, &ti);
            if (!t.resolve) return false;
        }
        t.w = w;
        t.h = h;
        t.format = fmt;
        return true;
    }

    // occ (water1): the sun's occlusion pass: coverage only (occ_pipe_), cars through the mesh shaders' simple model.
    // dof (photo2): the depth of field's pre-pass: depth only (dof_depth_pipeline), cars through their own shaders
    void draw_item(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const FrameParams& fp, const DrawItem& it,
                   const float ld[3], SDL_GPUGraphicsPipeline*& bound, BufferId& bound_vb, BufferId& bound_ib, bool occ = false,
                   bool dof = false) {
        if (!it.index_count || it.vertices >= buffers_.size() || it.indices >= buffers_.size()) return;
        if (it.shading == Shading::kCar && !occ) { draw_car(pass, cmd, fp, it, bound, bound_vb, bound_ib, dof); return; }
        const Shading shading = it.shading == Shading::kCar ? Shading::kSimple : it.shading;
        const bool game = shading != Shading::kSimple;
        SDL_GPUGraphicsPipeline* p = dof ? dof_depth_pipeline(game && it.cull ? 1 : 0)
                                   : occ ? occ_pipeline(game && it.cull)
                                   : it.overlay ? overlay_pipeline(it.gs_alpha | (it.alpha_only ? 0x100 : 0) | (it.overlay_blend ? 0 : 0x200) |
                                                                   (it.overlay_ztest ? 0x400 : 0) | (it.overlay_zwrite ? 0x800 : 0))
                                   : it.effect ? effect_pipeline(effect_mode(it.gs_alpha))
                                   : is_line(it) ? line_pipeline() : pipeline(kSceneFormat, it.blend, game && it.cull, false, it.post_fog);
        if (!p) return;
        if (p != bound) { SDL_BindGPUGraphicsPipeline(pass, p); bound = p; }
        if (it.vertices != bound_vb) {
            SDL_GPUBufferBinding b{buffers_[it.vertices], 0};
            SDL_BindGPUVertexBuffers(pass, 0, &b, 1);
            bound_vb = it.vertices;
        }
        if (it.indices != bound_ib) {
            SDL_GPUBufferBinding b{buffers_[it.indices], 0};
            SDL_BindGPUIndexBuffer(pass, &b, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            bound_ib = it.indices;
        }
        SDL_GPUTextureSamplerBinding tsb{it.texture < textures_.size() ? textures_[it.texture] : white_,
                                         it.overlay ? (it.repeat_u || it.repeat_v
                                                       ? overlay_wrap_[(it.point_sample ? 4 : 0) | (it.repeat_u ? 2 : 0) | (it.repeat_v ? 1 : 0)]
                                                       : it.point_sample ? clamp_sampler_ : overlay_sampler_)
                                         : it.effect && it.clamp_uv ? effect_sampler_ : sampler_};
        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        VertexUniforms vu{};
        if (it.overlay) mat4_identity(vu.view_proj);
        else std::memcpy(vu.view_proj, fp.view_proj, sizeof vu.view_proj);
        std::memcpy(vu.model, it.model, sizeof vu.model);
        for (int k = 0; k < 3; ++k) {
            vu.eye[k] = fp.eye[k];
            vu.right[k] = fp.right[k];
            vu.sun[k] = fp.to_sun[k];
        }
        vu.shade[0] = float(int(shading));
        vu.shade[1] = float(it.track_type);
        vu.shade[2] = it.track_param[0];
        vu.shade[3] = it.track_param[1];
        std::memcpy(vu.inst, it.instance, sizeof vu.inst);
        vu.flags[0] = fp.no_specular ? 1.0f : 0.0f;
        vu.flags[1] = it.uv_scroll;
        vu.line[0] = line_vp_[0];
        vu.line[1] = line_vp_[1];
        vu.line[2] = line_width_;
        vu.line[3] = fp.line_far;
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
        FragmentUniforms fu{{it.tint[0], it.tint[1], it.tint[2], game ? it.blend_fix : it.tint[3]},
                            {ld[0], ld[1], ld[2], 0},
                            {it.blend == Blend::kAlphaTest ? it.alpha_ref : 0.0f, it.lighting,
                             it.blend == Blend::kAlpha ? 1.0f : 0.0f, is_line(it) && !occ && !dof ? 4.0f : float(int(shading))},
                            {float(it.gs_test), it.textured ? 1.0f : 0.0f, it.blend == Blend::kAlpha || it.effect ? 1.0f : 0.0f,
                             it.overlay && it.sharp_alpha ? it.sharp_level : 0.0f},
                            {fp.fog[0], fp.fog[1], fp.fog[2], fp.fog[3]},
                            {fp.fog_colour[0], fp.fog_colour[1], fp.fog_colour[2], it.post_fog || it.overlay ? 0.0f : fp.fog_colour[3]}};
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
        ++stats_.draws;
        SDL_DrawGPUIndexedPrimitives(pass, it.index_count, 1, it.first_index, 0, 0);
    }

    void bind_buffers(SDL_GPURenderPass* pass, const DrawItem& it, BufferId& bound_vb, BufferId& bound_ib) {
        if (it.vertices != bound_vb) {
            SDL_GPUBufferBinding b{buffers_[it.vertices], 0};
            SDL_BindGPUVertexBuffers(pass, 0, &b, 1);
            bound_vb = it.vertices;
        }
        if (it.indices != bound_ib) {
            SDL_GPUBufferBinding b{buffers_[it.indices], 0};
            SDL_BindGPUIndexBuffer(pass, &b, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            bound_ib = it.indices;
        }
    }

    void draw_car(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const FrameParams& fp, const DrawItem& it,
                  SDL_GPUGraphicsPipeline*& bound, BufferId& bound_vb, BufferId& bound_ib, bool dof = false) {
        SDL_GPUGraphicsPipeline* p = dof ? dof_depth_pipeline(2) : pipeline(kSceneFormat, Blend::kOpaque, false, true);
        if (!p) return;
        if (p != bound) { SDL_BindGPUGraphicsPipeline(pass, p); bound = p; }
        bind_buffers(pass, it, bound_vb, bound_ib);
        const CarParams& c = it.car();
        const bool env = c.env_texture < textures_.size();
        SDL_GPUTextureSamplerBinding tsb[2] = {{it.texture < textures_.size() ? textures_[it.texture] : white_, sampler_},
                                               {env ? textures_[c.env_texture] : white_, sampler_}};
        SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
        CarVertexUniforms vu{};
        std::memcpy(vu.view_proj, fp.view_proj, sizeof vu.view_proj);
        std::memcpy(vu.model, it.model, sizeof vu.model);
        std::memcpy(vu.light, c.light, sizeof vu.light);
        std::memcpy(vu.sh, c.sh, sizeof vu.sh);
        for (int k = 0; k < 3; ++k) { vu.eye[k] = fp.eye[k]; vu.view_dir[k] = c.view_dir[k]; }
        vu.view_dir[3] = c.sh_k;
        std::memcpy(vu.base, c.base, sizeof vu.base);
        std::memcpy(vu.full, c.full, sizeof vu.full);
        if (c.zones > 0) std::memcpy(vu.zone, c.zone, sizeof vu.zone);
        vu.deform[0] = float(c.zones < 8 ? c.zones : 8);
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
        CarFragmentUniforms fu{};
        std::memcpy(fu.env_colour, c.env_colour, sizeof fu.env_colour);
        fu.mode[0] = c.alpha_as ? 1.0f : 0.0f;
        fu.mode[1] = c.fix / 128.0f;
        fu.mode[2] = float(c.env_mode);
        fu.mode[3] = env ? (c.env_frame_units ? 2.0f : 1.0f) : 0.0f;
        for (int k = 0; k < 3; ++k) fu.to_sun[k] = fp.to_sun[k];
        fu.to_sun[3] = fp.sun_colour[3];
        std::memcpy(fu.sun_colour, fp.sun_colour, sizeof fu.sun_colour);
        fu.sun_colour[3] = c.fade;   // the screen-door fade / -opacity (car.frag; the shader reads sun_colour.rgb only)
        std::memcpy(fu.fog, fp.fog, sizeof fu.fog);
        std::memcpy(fu.fog_colour, fp.fog_colour, sizeof fu.fog_colour);
        const CarLook& cl = fp.car_look;
        fu.refl[0] = c.reflect;   // (the mode and the traffic factor; the gloss strength is look3.w)
        fu.refl[1] = c.sky_open;
        fu.refl[2] = cl.fresnel_power;
        fu.refl[3] = cl.fresnel_floor;
        const float l0[4] = {cl.paint_gain, cl.paint_darken, cl.glint_size, cl.glint_gain}, l1[4] = {cl.sky_gain, cl.horizon_gain, cl.ground_level, cl.haze},
                    l2[4] = {cl.glass_weight, cl.glass_fresnel, cl.glass_gain, cl.glass_darken}, l3[4] = {cl.rim, cl.colour_keep, 0, cl.strength};
        std::memcpy(fu.look0, l0, sizeof l0);
        std::memcpy(fu.look1, l1, sizeof l1);
        std::memcpy(fu.look2, l2, sizeof l2);
        std::memcpy(fu.look3, l3, sizeof l3);
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
        ++stats_.draws;
        SDL_DrawGPUIndexedPrimitives(pass, it.index_count, 1, it.first_index, 0, 0);
    }

    // Scene pass into the GS frame (sky, opaque and alpha-tested draws, the full-screen specular, blended draws), then
    // the present pass into `out` (scale and clamp).
    void render(SDL_GPUCommandBuffer* cmd, Targets& t, SDL_GPUTexture* out, SDL_GPUTextureFormat fmt,
                const FrameParams& fp, const std::vector<DrawItem>& items) {
        if (render_scale_ < 0.999f && render_scaled(cmd, t, out, fmt, fp, items)) return;
        render_scene(cmd, t, fp, items, true);
        present(cmd, t, out, fmt, fp);
    }

    // effects2: RENDER SCALE. The 3D picture (the scene, the effects, the post passes, the sun) is drawn into targets at the scale
    // (its depth, its kept bloom picture) and copied up into the output-sized frame with a bilinear filter; the 2D stage (the HUD
    // and the menus, DrawItem::overlay) is drawn over that at the output's size. 2D items that test depth against the scene
    // (overlay_ztest) go with the 3D, which is all the depth there is. false: the targets could not be made (the caller draws as usual)
    bool render_scaled(SDL_GPUCommandBuffer* cmd, Targets& t, SDL_GPUTexture* out, SDL_GPUTextureFormat fmt, const FrameParams& fp,
                       const std::vector<DrawItem>& items) {
        Targets& sc = &t == &win_targets_ ? scaled_win_ : scaled_off_;
        const std::uint32_t sw = std::max<std::uint32_t>(1, std::uint32_t(std::floor(float(t.w) * render_scale_ + 0.5f)));
        const std::uint32_t sh = std::max<std::uint32_t>(1, std::uint32_t(std::floor(float(t.h) * render_scale_ + 0.5f)));
        if (!ensure_targets(sc, kSceneFormat, sw, sh, false)) return false;
        line_min_ = render_scale_;   // the line width as at 100%: output pixels, so a scaled target may hold less than one of its own
        render_scene(cmd, sc, fp, items, true, float(t.h) * render_scale_, 1);
        line_min_ = 1.0f;   // the 3D picture (and the depth-tested 2D) in sc.scene (sc.scene_resolve with MSAA)
        SDL_GPUColorTargetInfo ci{};
        ci.clear_color = SDL_FColor{0, 0, 0, 0};
        ci.load_op = SDL_GPU_LOADOP_CLEAR;
        ci.texture = t.scene;
        if (msaa_ > 1) {
            ci.store_op = SDL_GPU_STOREOP_RESOLVE;
            ci.resolve_texture = t.scene_resolve;
        } else {
            ci.store_op = SDL_GPU_STOREOP_STORE;
        }
        SDL_GPUDepthStencilTargetInfo di{};
        di.texture = t.depth;
        di.clear_depth = 0.0f;
        di.load_op = SDL_GPU_LOADOP_CLEAR;
        di.store_op = SDL_GPU_STOREOP_DONT_CARE;
        di.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        di.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
        SDL_GPUGraphicsPipeline* bound = nullptr;
        if (SDL_GPUGraphicsPipeline* cp = post_pipeline(true)) {
            SDL_BindGPUGraphicsPipeline(pass, cp);
            bound = cp;
            const PostUniforms u{{4, 0, sharp_upscale_ ? 2.0f : 0.0f, 0}, {1, kUpscaleSharpen, 0, 0}, {0, 0, 1.0f / float(sc.w), 1.0f / float(sc.h)}};
            SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
            SDL_GPUTexture* src = msaa_ > 1 ? sc.scene_resolve : sc.scene;
            SDL_GPUTextureSamplerBinding tsb[2] = {{src, overlay_sampler_}, {src, overlay_sampler_}};
            SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        }
        float ld[3] = {fp.light_dir[0], fp.light_dir[1], fp.light_dir[2]};
        const float ll = std::sqrt(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);
        if (ll > 0) for (float& c : ld) c /= ll;
        BufferId bound_vb = kNone, bound_ib = kNone;
        draw_overlay(pass, cmd, t, fp, items, ld, bound, bound_vb, bound_ib, 2);   // the rest of the 2D stage, at the output's size
        SDL_EndGPURenderPass(pass);
        present(cmd, t, out, fmt, fp);
        return true;
    }

    // The 2D stage (hud1): DrawItem::overlay, in list order, with its scissor
    void draw_overlay(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Targets& t, const FrameParams& fp,
                      const std::vector<DrawItem>& items, const float ld[3], SDL_GPUGraphicsPipeline*& bound, BufferId& bound_vb,
                      BufferId& bound_ib, int only = 0) {
        B3PERF_SCOPE(kCmdOverlay);
        bool clipped = false;
        for (const DrawItem& it : items) {
            if (!it.overlay) continue;
            if (only == 1 && !it.overlay_ztest) continue;    // effects2 (render scale): only the depth-tested 2D, with the 3D
            if (only == 2 && it.overlay_ztest) continue;     // ... and the rest, over the upscaled picture

            if (it.scissor[2] > it.scissor[0] && it.scissor[3] > it.scissor[1]) {
                const int x0 = int(std::floor(it.scissor[0] * float(t.w) + 0.5f)), y0 = int(std::floor(it.scissor[1] * float(t.h) + 0.5f));
                const int x1 = int(std::floor(it.scissor[2] * float(t.w) + 0.5f)), y1 = int(std::floor(it.scissor[3] * float(t.h) + 0.5f));
                const SDL_Rect r{std::max(0, x0), std::max(0, y0), std::max(0, std::min(int(t.w), x1) - std::max(0, x0)),
                                 std::max(0, std::min(int(t.h), y1) - std::max(0, y0))};
                SDL_SetGPUScissor(pass, &r);
                clipped = true;
            } else if (clipped) {
                const SDL_Rect r{0, 0, int(t.w), int(t.h)};
                SDL_SetGPUScissor(pass, &r);
                clipped = false;
            }
            draw_item(pass, cmd, fp, it, ld, bound, bound_vb, bound_ib);
        }
    }

    // gsoff1 (§29): the views of a split-screen frame, each in its own targets (`vt[k]`, sized to its rect), placed into
    // `t`'s scene (the post pipeline's copy: Cs + Cd on RGB over a cleared frame), the 2D stage over the whole frame, then
    // the present pass
    void render_views(SDL_GPUCommandBuffer* cmd, Targets& t, std::vector<Targets>& vt, SDL_GPUTexture* out, SDL_GPUTextureFormat fmt,
                      const std::vector<ViewPass>& views, const FrameParams& ofp, const std::vector<DrawItem>& overlay) {
        if (vt.size() < views.size()) vt.resize(views.size());
        struct Px { int x, y, w, h; };
        std::vector<Px> px(views.size());
        for (std::size_t k = 0; k < views.size(); ++k) {
            const float* r = views[k].rect;
            const int x0 = int(std::floor(r[0] * float(t.w) + 0.5f)), y0 = int(std::floor(r[1] * float(t.h) + 0.5f));
            const int x1 = int(std::floor(r[2] * float(t.w) + 0.5f)), y1 = int(std::floor(r[3] * float(t.h) + 0.5f));
            px[k] = {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
            // effects2: RENDER SCALE: the view's picture at the scale (1: the view's own size, as before), copied up below
            const bool scl = render_scale_ < 0.999f;
            const std::uint32_t vw = scl ? std::max<std::uint32_t>(1, std::uint32_t(std::floor(float(px[k].w) * render_scale_ + 0.5f))) : std::uint32_t(px[k].w);
            const std::uint32_t vh = scl ? std::max<std::uint32_t>(1, std::uint32_t(std::floor(float(px[k].h) * render_scale_ + 0.5f))) : std::uint32_t(px[k].h);
            if (!ensure_targets(vt[k], kSceneFormat, vw, vh, false)) return;
            line_min_ = scl ? render_scale_ : 1.0f;
            render_scene(cmd, vt[k], views[k].fp, views[k].items, false, scl ? float(t.h) * render_scale_ : float(t.h));
            line_min_ = 1.0f;   // lines1: a GS pixel of the whole output
        }
        SDL_GPUColorTargetInfo ci{};
        ci.clear_color = SDL_FColor{0, 0, 0, 0};
        ci.load_op = SDL_GPU_LOADOP_CLEAR;
        ci.texture = t.scene;
        if (msaa_ > 1) {
            ci.store_op = SDL_GPU_STOREOP_RESOLVE;
            ci.resolve_texture = t.scene_resolve;
        } else {
            ci.store_op = SDL_GPU_STOREOP_STORE;
        }
        SDL_GPUDepthStencilTargetInfo di{};
        di.texture = t.depth;
        di.clear_depth = 0.0f;
        di.load_op = SDL_GPU_LOADOP_CLEAR;
        di.store_op = SDL_GPU_STOREOP_DONT_CARE;
        di.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        di.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
        SDL_GPUGraphicsPipeline* bound = nullptr;
        if (SDL_GPUGraphicsPipeline* cp = post_pipeline(true)) {
            SDL_BindGPUGraphicsPipeline(pass, cp);
            bound = cp;
            for (std::size_t k = 0; k < views.size(); ++k) {
                const SDL_GPUViewport vp{float(px[k].x), float(px[k].y), float(px[k].w), float(px[k].h), 0.0f, 1.0f};
                SDL_SetGPUViewport(pass, &vp);
                const PostUniforms u{{4, 0, sharp_upscale_ ? 2.0f : 0.0f, 0}, {1, kUpscaleSharpen, 0, 0}, {0, 0, 1.0f / float(vt[k].w), 1.0f / float(vt[k].h)}};
                SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
                SDL_GPUTexture* src = msaa_ > 1 ? vt[k].scene_resolve : vt[k].scene;
                SDL_GPUSampler* smp = vt[k].w != std::uint32_t(px[k].w) || vt[k].h != std::uint32_t(px[k].h) ? overlay_sampler_ : clamp_sampler_;   // effects2: bilinear up
                SDL_GPUTextureSamplerBinding tsb[2] = {{src, smp}, {src, smp}};
                SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
                SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            }
            const SDL_GPUViewport full{0, 0, float(t.w), float(t.h), 0.0f, 1.0f};
            SDL_SetGPUViewport(pass, &full);
        }
        float ld[3] = {ofp.light_dir[0], ofp.light_dir[1], ofp.light_dir[2]};
        const float ll = std::sqrt(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);
        if (ll > 0) for (float& c : ld) c /= ll;
        BufferId bound_vb = kNone, bound_ib = kNone;
        draw_overlay(pass, cmd, t, ofp, overlay, ld, bound, bound_vb, bound_ib);
        SDL_EndGPURenderPass(pass);
        present(cmd, t, out, fmt, ofp);
    }

    // The scene pass (and the 2D stage when `overlay`), leaving the GS frame in t.scene (t.scene_resolve with MSAA)
    // `rows`: the output's height the line width follows (0: this target's; a split-screen view passes the whole output's)
    void render_scene(SDL_GPUCommandBuffer* cmd, Targets& t, const FrameParams& fp, const std::vector<DrawItem>& items, bool overlay,
                      float rows = 0, int overlay_only = 0) {
        B3PERF_SCOPE(kCmdScene);
        line_vp_[0] = float(t.w);
        line_vp_[1] = float(t.h);
        line_width_ = std::max(line_min_, (rows > 0 ? rows : float(t.h)) / 448.0f);   // b3asset::track_line_width
        const bool occ_ok = !fp.sun_sprites.empty() && fp.sun_occlusion && sun_occlusion_pass(cmd, fp, items);
        SDL_GPUColorTargetInfo ci{};
        const float inv = fp.present_scale > 0 ? 1.0f / fp.present_scale : 1.0f;
        ci.clear_color = SDL_FColor{fp.clear[0] * inv, fp.clear[1] * inv, fp.clear[2] * inv, 0.0f};
        ci.load_op = SDL_GPU_LOADOP_CLEAR;
        ci.texture = t.scene;
        const ZoomBlur zb = zoom_blur(fp);
        const bool blur = zb.amount > 0;
        const bool bloom = fp.bloom.on && fp.bloom.intensity > 0;
        // photo2: the depth of field (its pre-pass now, its passes after the zoom blur); off: none of it runs
        DofUniforms dofu{};
        const bool dof = fp.dof.on && fp.dof.radius > 0 && dof_ready() && ensure_buf(t.dw, (t.w + 1) / 2, (t.h + 1) / 2) &&
                         ensure_buf(t.dg, (t.w + 1) / 2, (t.h + 1) / 2) && dof_uniforms(t, fp, dofu) && dof_depth_pass(cmd, t, fp, items);
        // MSAA + the PC bloom: the pass before the bloom resolves into bloom_src and does not store its samples; the resumed
        // pass starts with one draw (the resolved picture + the glow) instead of reloading them (a 4-sample 46 MB scene
        // is 184 MB each way at 3200 x 1800). The GS-sized path (post_game, golden) keeps the original split.
        // Measured in a real window on an RTX 4080 SUPER (5120x1440, MSAA 8): the lean form is SLOWER than the original split
        // (218 fps against 238 in the same race replay), so the split is the default; $B3_BLOOM_LEAN=1 opts into the lean form.
        static const bool lean_env = std::getenv("B3_BLOOM_LEAN") != nullptr;
        const bool lean = bloom && msaa_ > 1 && !fp.post_game && lean_env && post_lean_fs_ && ensure_bloom_src(t);
        if (msaa_ > 1) {
            ci.store_op = !lean && (blur || bloom || dof) ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
            ci.resolve_texture = lean ? t.bloom_src : t.scene_resolve;
        } else {
            ci.store_op = SDL_GPU_STOREOP_STORE;
        }
        SDL_GPUDepthStencilTargetInfo di{};
        di.texture = t.depth;
        di.clear_depth = 0.0f;  // reverse-Z: far
        di.load_op = SDL_GPU_LOADOP_CLEAR;
        di.store_op = SDL_GPU_STOREOP_DONT_CARE;
        di.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        di.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
        float ld[3] = {fp.light_dir[0], fp.light_dir[1], fp.light_dir[2]};
        const float ll = std::sqrt(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);
        if (ll > 0) for (float& c : ld) c /= ll;
        SDL_GPUGraphicsPipeline* bound = nullptr;
        BufferId bound_vb = kNone, bound_ib = kNone;
        // a post pass reads the 3D picture: end the scene pass (keeping the MSAA samples), run its chain, resume the pass
        auto resume = [&]() {
            ci.load_op = SDL_GPU_LOADOP_LOAD;
            di.load_op = SDL_GPU_LOADOP_DONT_CARE;
            pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
            bound = nullptr;
            bound_vb = bound_ib = kNone;
        };
        for (int phase = 0; phase < 7; ++phase) {
            if (phase == 6 && lean) {   // post1, one-draw form
                SDL_EndGPURenderPass(pass);
                SDL_GPUTexture* pb = bloom_chain(cmd, t, fp, t.bloom_src);
                ci.load_op = SDL_GPU_LOADOP_DONT_CARE;   // the draw below covers every pixel
                ci.store_op = blur || dof ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
                ci.resolve_texture = t.scene_resolve;
                di.load_op = SDL_GPU_LOADOP_DONT_CARE;
                pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
                bound_vb = bound_ib = kNone;
                bound = nullptr;
                if (SDL_GPUGraphicsPipeline* cp = post_pipeline(false, false, true)) {
                    SDL_BindGPUGraphicsPipeline(pass, cp);
                    bound = cp;
                    SDL_GPUTexture* g = pb ? pb : t.bloom_src;
                    const float wgl = pb ? 1.0f / float(t.bp[t.bp_cur].w) : 1.0f, hgl = pb ? 1.0f / float(t.bp[t.bp_cur].h) : 1.0f;
                    const PostUniforms u{{5, 0, 0, fp.bloom.soft ? 1.0f : 0.0f}, {pb ? (fp.bloom.soft ? fp.bloom.intensity * kBloomSoft : fp.bloom.intensity) : 0.0f, 0, 0, 0}, {0, 0, wgl, hgl}};
                    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
                    SDL_GPUTextureSamplerBinding tsb[2] = {{t.bloom_src, clamp_sampler_}, {g, effect_sampler_}};
                    SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
                    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                }
            } else if (phase == 6 && bloom) {   // post1: CB3Bloom::Render, before the sun corona
                SDL_EndGPURenderPass(pass);
                SDL_GPUTexture* pb = bloom_chain(cmd, t, fp, msaa_ > 1 ? t.scene_resolve : t.scene);
                resume();
                if (SDL_GPUGraphicsPipeline* cp = pb ? post_pipeline(true, fp.bloom.soft) : nullptr) {
                    SDL_BindGPUGraphicsPipeline(pass, cp);
                    bound = cp;
                    const PostUniforms u{{4, 0, fp.post_game ? 0.0f : 1.0f, 0}, {fp.bloom.soft ? fp.bloom.intensity * kBloomSoft : fp.bloom.intensity, 0, 0, 0},
                                         {0, 0, 1.0f / float(t.bp[t.bp_cur].w), 1.0f / float(t.bp[t.bp_cur].h)}};
                    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
                    SDL_GPUTextureSamplerBinding tsb[2] = {{pb, effect_sampler_}, {pb, effect_sampler_}};
                    SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
                    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                }
            }
            if (phase == 6) draw_sun(pass, cmd, fp, occ_ok, bound);   // water1: after the 3D, before the 2D stage
            if (phase == 6 && blur) {   // water1 / post1: the zoom blur (directional or radial), after the sun corona
                SDL_EndGPURenderPass(pass);
                SDL_GPUTexture* h = zoom_blur_chain(cmd, t, fp, zb);
                resume();
                if (SDL_GPUGraphicsPipeline* cp = h ? blur_pipeline(true) : nullptr) {
                    SDL_BindGPUGraphicsPipeline(pass, cp);
                    bound = cp;
                    const BlurUniforms u = blur_uniforms(zb, 2, -1);
                    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
                    SDL_GPUTextureSamplerBinding tsb[2] = {{h, effect_sampler_}, {h, effect_sampler_}};
                    SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
                    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                }
            }
            if (phase == 6 && dof) {   // photo2: the depth of field, after the zoom blur, before the 2D stage
                SDL_EndGPURenderPass(pass);
                SDL_GPUTexture* g = dof_chain(cmd, t, dofu);
                resume();
                if (SDL_GPUGraphicsPipeline* cp = g ? dof_pipeline(true) : nullptr) {
                    SDL_BindGPUGraphicsPipeline(pass, cp);
                    bound = cp;
                    DofUniforms u = dofu;
                    u.a[0] = 2;
                    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
                    SDL_GPUTextureSamplerBinding tsb[3] = {{g, effect_sampler_}, {g, clamp_sampler_}, {t.dof_depth, clamp_sampler_}};
                    SDL_BindGPUFragmentSamplers(pass, 0, tsb, 3);
                    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                }
            }
            if (phase == 6) {   // the 2D stage (hud1): DrawItem::overlay, in list order, with its scissor
                if (overlay) draw_overlay(pass, cmd, t, fp, items, ld, bound, bound_vb, bound_ib, overlay_only);
                continue;
            }
            if (phase == 2) {
                if (fp.specular[0] <= 0 && fp.specular[1] <= 0 && fp.specular[2] <= 0) continue;
                if (!spec_pipe_) spec_pipe_ = fullscreen_pipeline(kSceneFormat, true);
                if (!spec_pipe_) continue;
                SDL_BindGPUGraphicsPipeline(pass, spec_pipe_);
                bound = spec_pipe_;
                const float c[4] = {fp.specular[0], fp.specular[1], fp.specular[2], 0};
                SDL_PushGPUFragmentUniformData(cmd, 0, c, sizeof c);
                SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                continue;
            }
            for (const DrawItem& it : items) {
                if (it.overlay) continue;
                // cars (glass too) draw with the opaque ones, before the full-screen specular, as in CB3World::Render
                // phase 4: the effects stage (DrawItem::effect), then phase 5: the post-fog stage (DrawItem::post_fog), in list order
                if (it.post_fog != (phase == 5)) continue;
                if (phase == 5) { draw_item(pass, cmd, fp, it, ld, bound, bound_vb, bound_ib); continue; }
                if (it.effect != (phase == 4)) continue;
                const bool sky = it.shading == Shading::kSky, blended = it.blend == Blend::kAlpha && it.shading != Shading::kCar;
                // lines1: the line meshes draw with the opaque ones, in list order, before the full-screen specular, as
                // the game draws them among the units' before-vehicles slots (§11.8; they write no Z, so this order is
                // what the cars drawn after them see)
                if (phase < 4 && is_line(it)) {
                    if (phase == 1) draw_item(pass, cmd, fp, it, ld, bound, bound_vb, bound_ib);
                    continue;
                }
                if (phase < 4 && (phase == 0 ? !sky : (sky || blended != (phase == 3)))) continue;
                draw_item(pass, cmd, fp, it, ld, bound, bound_vb, bound_ib);
            }
        }
        SDL_EndGPURenderPass(pass);
    }

    // The present pass: the GS frame scaled (PresentDrawBuffer) into `out`
    void present(SDL_GPUCommandBuffer* cmd, Targets& t, SDL_GPUTexture* out, SDL_GPUTextureFormat fmt, const FrameParams& fp) {
        B3PERF_SCOPE(kCmdPresent);
        SDL_GPUColorTargetInfo oi{};
        oi.texture = out;
        oi.load_op = SDL_GPU_LOADOP_DONT_CARE;
        oi.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pp = SDL_BeginGPURenderPass(cmd, &oi, 1, nullptr);
        SDL_GPUGraphicsPipeline*& present = fmt == swap_format_ ? present_pipe_win_ : present_pipe_off_;
        if (!present) present = fullscreen_pipeline(fmt, false);
        if (present) {
            SDL_BindGPUGraphicsPipeline(pp, present);
            SDL_GPUTextureSamplerBinding tsb{msaa_ > 1 ? t.scene_resolve : t.scene, clamp_sampler_};
            SDL_BindGPUFragmentSamplers(pp, 0, &tsb, 1);
            const float sc[4] = {fp.present_scale, dither_, 0, 0};   // perf1: y = the ordered dither (output steps)
            SDL_PushGPUFragmentUniformData(cmd, 0, sc, sizeof sc);
            SDL_DrawGPUPrimitives(pp, 3, 1, 0, 0);
        }
        SDL_EndGPURenderPass(pp);
    }

    void update_buffer(SDL_GPUBufferUsageFlags usage, BufferId& id, const void* data, std::size_t bytes) {
        if (!bytes) return;
        B3PERF_SCOPE(kUpload);
        b3perf::count(b3perf::kUploads);
        b3perf::count(b3perf::kUploadBytes, bytes);
        ++stats_.uploads;
        stats_.upload_bytes += bytes;
        if (id == kNone || id >= buffers_.size() || dyn_.find(id) == dyn_.end()) {
            id = BufferId(buffers_.size());
            buffers_.push_back(nullptr);
            dyn_[id] = Dyn{};
        }
        Dyn& d = dyn_[id];
        if (d.cap < bytes) {
            ++stats_.buffer_grows;
            const std::uint32_t cap = std::uint32_t(std::max<std::size_t>(bytes * 2, 4096));
            if (buffers_[id]) SDL_ReleaseGPUBuffer(dev_, buffers_[id]);
            SDL_GPUBufferCreateInfo bi{usage, cap, 0};
            buffers_[id] = SDL_CreateGPUBuffer(dev_, &bi);
            d.cap = buffers_[id] ? cap : 0;
            if (!d.cap) return;
        }
        // cycle = true: a frame still reading the old contents keeps them
        std::uint32_t off = 0;
        std::uint8_t* dst = upload_reserve(std::uint32_t(bytes), off);
        if (!dst) return;
        std::memcpy(dst, data, bytes);
        SDL_GPUTransferBufferLocation src{up_.tb, off};
        SDL_GPUBufferRegion region{buffers_[id], 0, std::uint32_t(bytes)};
        SDL_UploadToGPUBuffer(up_.cp, &src, &region, true);
    }

    // perf3: the frame's dynamic buffer uploads (update_vertex_buffer / update_index_buffer) share one staging buffer, one copy
    // pass and one command buffer, submitted by flush_uploads() before the next draw. The staging buffer is mapped with
    // cycle = true once per batch.
    // The first try (perf3, reverted for a golden shot, crashnav live_11000: the showroom car's reflection came out wrong)
    // left the batch open across create_texture / make_buffer / update_texture, which submit at once: the batch then went to
    // the GPU AFTER the command buffers of the calls that came later. The pictures are identical only when every immediate
    // upload first submits the open batch (flush_uploads() at their top), so the submission order stays the call order.
    // Buffers uploaded in a row (a frame's effects and HUD) still share one submit.
    std::uint8_t* upload_reserve(std::uint32_t bytes, std::uint32_t& off) {
        const std::uint32_t at = (up_.used + 255U) & ~255U;
        if (up_.tb && at + bytes > up_.cap) flush_uploads();   // full: this batch goes first
        if (!up_.tb || up_.cap < bytes) {
            if (up_.tb) SDL_ReleaseGPUTransferBuffer(dev_, up_.tb);   // (flushed above: released once the GPU is done with it)
            up_.cap = std::max<std::uint32_t>({bytes + 255U, up_.cap * 2U, 1U << 20});
            SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, up_.cap, 0};
            up_.tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
            if (!up_.tb) { up_.cap = 0; return nullptr; }
        }
        if (!up_.map) {
            up_.map = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(dev_, up_.tb, true));
            if (!up_.map) return nullptr;
            up_.used = 0;
        }
        if (!up_.cmd) {
            up_.cmd = SDL_AcquireGPUCommandBuffer(dev_);
            up_.cp = up_.cmd ? SDL_BeginGPUCopyPass(up_.cmd) : nullptr;
            if (!up_.cp) {
                if (up_.cmd) SDL_CancelGPUCommandBuffer(up_.cmd);
                up_.cmd = nullptr;
                SDL_UnmapGPUTransferBuffer(dev_, up_.tb);
                up_.map = nullptr;
                return nullptr;
            }
        }
        off = (up_.used + 255U) & ~255U;
        up_.used = off + bytes;
        return up_.map + off;
    }
    void flush_uploads() {
        if (up_.map) { SDL_UnmapGPUTransferBuffer(dev_, up_.tb); up_.map = nullptr; }
        up_.used = 0;
        if (!up_.cmd) return;
        SDL_EndGPUCopyPass(up_.cp);
        SDL_SubmitGPUCommandBuffer(up_.cmd);
        up_.cmd = nullptr;
        up_.cp = nullptr;
    }

    BufferId make_buffer(SDL_GPUBufferUsageFlags usage, const void* data, std::size_t bytes) {
        flush_uploads();   // keep the submission order of the immediate uploads (see upload_reserve)
        if (!bytes) return kNone;
        B3PERF_SCOPE(kUpload);
        b3perf::count(b3perf::kUploads);
        b3perf::count(b3perf::kUploadBytes, bytes);
        ++stats_.uploads;
        stats_.upload_bytes += bytes;
        SDL_GPUBufferCreateInfo bi{usage, std::uint32_t(bytes), 0};
        SDL_GPUBuffer* b = SDL_CreateGPUBuffer(dev_, &bi);
        if (!b) return kNone;
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, std::uint32_t(bytes), 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        std::memcpy(SDL_MapGPUTransferBuffer(dev_, tb, false), data, bytes);
        SDL_UnmapGPUTransferBuffer(dev_, tb);
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src{tb, 0};
        SDL_GPUBufferRegion dst{b, 0, std::uint32_t(bytes)};
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(dev_, tb);
        buffers_.push_back(b);
        return BufferId(buffers_.size() - 1);
    }

    SDL_GPUDevice* dev_ = nullptr;
    SDL_Window* window_ = nullptr;
    const char* present_ = "none";
    int frames_in_flight_ = 0;   // lat1
    SDL_GPUTextureFormat swap_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    int msaa_ = 1;
    SDL_GPUShader *vs_ = nullptr, *fs_ = nullptr, *fsq_vs_ = nullptr, *spec_fs_ = nullptr, *present_fs_ = nullptr;
    SDL_GPUShader *car_vs_ = nullptr, *car_fs_ = nullptr;
    // Sun corona (water1): the occlusion target (kOccSize square, RGBA8, a full mip chain: its 1 x 1 level is the sky
    // share), its pipelines (mesh shaders, every fragment writes 0; [1] culls back faces), the sprite pipeline
    static constexpr std::uint32_t kOccSize = 32, kOccLevels = 6;
    SDL_GPUShader *sun_vs_ = nullptr, *sun_fs_ = nullptr;
    SDL_GPUTexture* occ_ = nullptr;
    SDL_GPUGraphicsPipeline *occ_pipe_[2] = {}, *sun_pipe_ = nullptr;
    // Radial blur (water1): [0] the half-size passes (no blend), [1] the composite into the scene (SRC_ALPHA, RGB only)
    SDL_GPUShader* blur_fs_ = nullptr;
    SDL_GPUGraphicsPipeline* blur_pipe_[2] = {};
    // Bloom (post1): [0] its passes into its buffers (no blend), [1] the composite into the scene (additive, RGB only)
    SDL_GPUShader* post_fs_ = nullptr;
    SDL_GPUShader* post_lean_fs_ = nullptr;   // shaders/post_lean.frag
    SDL_GPUGraphicsPipeline* post_pipe_[4] = {};   // [2]: the SOFT composite (bloom2); [3]: the one-draw composite (mode 5, no blend)
    // photo2 (FrameParams::Dof): dof.frag (made on first use; a failure turns the effect off, nothing else), its pipelines
    // ([0] its passes, [1] the composite), the depth pre-pass's ([0] mesh, [1] culled, [2] cars) and its depth format
    SDL_GPUShader* dof_fs_ = nullptr;
    bool dof_failed_ = false;
    bool dof_debug_ = SDL_getenv("B3GFX_DOF_DEBUG") != nullptr;   // the circles in false colour (diagnostics)
    SDL_GPUGraphicsPipeline* dof_pipe_[2] = {};
    SDL_GPUGraphicsPipeline* dof_depth_pipe_[3] = {};
    SDL_GPUTextureFormat dof_depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUGraphicsPipeline *spec_pipe_ = nullptr, *present_pipe_win_ = nullptr, *present_pipe_off_ = nullptr, *car_pipe_ = nullptr;
    SDL_GPUGraphicsPipeline* line_pipe_ = nullptr;   // lines1: the track's line meshes (line_pipeline)
    // lines1: the scene target being drawn (pixels) and the line width there (b3asset::track_line_width of the output's rows)
    float line_vp_[2] = {1, 1}, line_width_ = 1;
    SDL_GPUSampler* sampler_ = nullptr;
    SDL_GPUSampler* clamp_sampler_ = nullptr;
    SDL_GPUSampler* effect_sampler_ = nullptr;
    SDL_GPUGraphicsPipeline* effect_pipe_[3] = {};
    std::map<int, SDL_GPUGraphicsPipeline*> overlay_pipe_;   // the 2D stage, by GS ALPHA byte | flags (hud1)
    SDL_GPUSampler* overlay_sampler_ = nullptr;
    SDL_GPUSampler* overlay_wrap_[8] = {};
    struct Dyn { std::uint32_t cap = 0; };
    struct Upload { SDL_GPUTransferBuffer* tb = nullptr; std::uint8_t* map = nullptr; std::uint32_t cap = 0, used = 0;
                    SDL_GPUCommandBuffer* cmd = nullptr; SDL_GPUCopyPass* cp = nullptr; } up_;   // perf3: the frame's batched uploads
    Stats stats_;   // perf1: take_stats()
    float dither_ = 0.0f;   // perf1: Options::dither
    float render_scale_ = 1.0f;   // effects2: Options::render_scale
    bool sharp_upscale_ = false;  // 1.1 UPSCALING: post.frag mode 4 z 2 for the scaled picture
    float line_min_ = 1.0f;       // effects2: the least line width (pixels of the target being drawn)
    Targets scaled_win_, scaled_off_;   // effects2: the 3D picture's targets at the scale (single-view frames)
    std::map<BufferId, Dyn> dyn_;   // dynamic buffers (update_*_buffer): their upload buffer and capacity
    SDL_GPUTexture* white_ = nullptr;
    std::vector<SDL_GPUBuffer*> buffers_;
    std::vector<SDL_GPUTexture*> textures_;
    unsigned resource_epoch_ = 0;   // clear_resources calls (Device::resource_epoch)
    std::map<TextureId, std::pair<int, int>> dyn_tex_size_;   // update_texture's textures (menu1)
    std::map<int, std::array<SDL_GPUGraphicsPipeline*, 8>> pipelines_;
    Targets win_targets_, off_targets_;
    std::vector<Targets> win_views_, off_views_;   // gsoff1 (§29): the split screen's per-view targets
};

}  // namespace

std::unique_ptr<Device> create_headless(const Options& opt, std::string* err, SDL_Window** hidden, const char** how) {
    *hidden = nullptr;
    std::string e1, e2, e3;
    // test aid: B3GFX_HEADLESS_STEP=2 or 3 starts at that step (exercises the macOS paths on Linux / Windows)
    const char* st = SDL_getenv("B3GFX_HEADLESS_STEP");
    const int first = st ? SDL_atoi(st) : 1;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (first > 1) {
        e1 = "step 1 skipped";
    } else if (SDL_Init(SDL_INIT_VIDEO)) {
        if (auto d = Device::create(nullptr, opt, &e1)) { if (how) *how = "offscreen video driver"; return d; }
        SDL_Quit();
    } else {
        e1 = std::string("SDL_Init (offscreen): ") + SDL_GetError();
    }
    SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        if (err) *err = e1 + "; SDL_Init: " + SDL_GetError();
        return nullptr;
    }
    if (first > 2) e2 = "step 2 skipped";
    else if (auto d = Device::create(nullptr, opt, &e2)) { if (how) *how = "platform video driver, no window"; return d; }
    if (SDL_Window* w = SDL_CreateWindow("b3gfx offscreen", 64, 64, SDL_WINDOW_HIDDEN)) {
        if (auto d = Device::create(w, opt, &e3)) { *hidden = w; if (how) *how = "platform video driver, hidden window"; return d; }
        SDL_DestroyWindow(w);
    } else {
        e3 = std::string("SDL_CreateWindow (hidden): ") + SDL_GetError();
    }
    SDL_Quit();
    if (err) *err = e1 + "; " + e2 + "; " + e3;
    return nullptr;
}

std::unique_ptr<Device> Device::create(SDL_Window* window, const Options& opt, std::string* err) {
    auto d = std::make_unique<SdlDevice>();
    if (!d->init(window, opt, err)) return nullptr;
    return d;
}

}  // namespace b3gfx
