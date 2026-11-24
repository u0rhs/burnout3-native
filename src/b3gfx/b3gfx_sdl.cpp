// b3gfx on SDL3 SDL_GPU (see include/b3gfx/b3gfx.h).
#include "b3gfx/b3gfx.h"

#include <mutex>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>   // perf4: vulkan_device_local_mb
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi.h>   // texpack review: the D3D12 adapter's memory (dxgi_device_local_mb; dxgi.dll loaded at run time)
#undef far    // (windows.h's old 16-bit pointer words: this file has locals of those names)
#undef near
#endif
#include <cstdio>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <new>
#include <unordered_map>
#include <vector>

#include "b3gfx/perf.h"
#include "b3gfx/render_scale.h"   // perf4: supersampling
#include "b3gfx/scene_depth.h"
#include "b3gfx/upload_batch.h"   // uploadbatch
#include "b3gfx/driver_order.h"   // d3d12
#include "shaders_gen.h"

// perf3 (include/b3gfx/perf.h): frame timing capture. The SDL_GPU calls that matter are wrapped here, so a pass, a draw, a
// submit and the waits are counted / timed (B3_PERF_LOG; off: a plain bool test) without touching the code around them.
namespace {
// scenedepth: the scene pass keeps its depth in a second colour target (R32F at the scene's sample count, cleared to 0:
// reverse-Z far; each fragment of a draw the depth is made of writes gl_FragCoord.z, blended MAX: the nearest whatever the
// draw order), resolved at each end of the pass, for the night lights, the AO, the depth of field and the motion blur to
// read instead of a depth pre-pass (the scene's MSAA depth cannot be resolved or sampled through SDL GPU). The scene
// code is not changed for it: while `scene` is set (a frame that reads the depth), every pass begun on `scene` gets the
// target (cleared at the first begin, loaded at the later ones), and every pipeline bound in such a pass is swapped for
// its two-target form, made on first use from its create info (kept for every pipeline drawing into a kSceneFormat
// target): the depth-writing fragment shader (depth_fs) with the target written, any other with it masked off.
struct ScenePipeInfo {
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    std::vector<SDL_GPUVertexBufferDescription> vb;
    std::vector<SDL_GPUVertexAttribute> at;
    SDL_GPUColorTargetDescription ct{};
};
struct SceneDepthState {
    SDL_GPUTexture* scene = nullptr;     // the frame's colour target 0 (null: no scene depth this frame)
    SDL_GPUTexture* target = nullptr;    // attachment 1
    SDL_GPUTexture* resolve = nullptr;   // its single-sample resolve (MSAA), else null
    bool begun = false, in_pass = false;
    SDL_GPUDevice* dev = nullptr;
    std::unordered_map<SDL_GPUGraphicsPipeline*, ScenePipeInfo> info;
    std::unordered_map<SDL_GPUGraphicsPipeline*, SDL_GPUGraphicsPipeline*> mrt, mrt_off;   // the target written / masked off
    // the depth-only forms (the frame and the Z buffer untouched, no depth test: the MAX keeps the nearest), for the
    // post-fog draws' depth at the AO's break (they are drawn later, but the depth the AO reads has them, as the pre-pass)
    std::unordered_map<SDL_GPUGraphicsPipeline*, SDL_GPUGraphicsPipeline*> only;
    bool depth_only = false;   // binds take the depth-only form
    // the draw being bound writes the depth (draw_item / draw_car set it: the target is written unblended, the draws that
    // write it also write Z, so the last fragment written is the nearest; the others have it masked off)
    bool mask_on = false, bound_mask = false;
    bool keep = true;          // the scene pass's samples of it stored at this segment's end (a later reader needs them)
    // with the scene depth on (the device's choice), a scene pipeline's two-target forms are made with it, not at a frame's
    // first use; `samples`: the scene's sample count
    bool eager = false, forming = false;
    int late = 0;
    SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1;
    std::unordered_map<SDL_GPUShader*, SDL_GPUShader*> depth_fs;   // a fragment shader -> its SCENE_DEPTH variant
    int failed = 0;
    bool on = false;      // the device's switch (Device::scene_depth_on_): create infos are kept only then
    bool blend = false;   // R32F blends (MAX) on this device: the depth-only forms (the AO's break) can be made
    // a two-target form could not be made: no later frame takes the scene depth (the pre-pass, as off); `skip`: the bound
    // form is missing, so the scene pass's draws until the next bind are left out (never a single-target pipeline in it)
    bool broken = false, skip = false;
};
// One per device (Device::sd_; a process can hold two: the launcher's previews and showroom, b3view and a headless
// renderer). Pipeline creation finds its device's by the SDL device; the binds and begins of a frame's scene pass use
// t_sd, the state of the device drawing it on this thread (set by render_scene around its scene pass, else null)
std::mutex g_sd_mu;
std::unordered_map<SDL_GPUDevice*, SceneDepthState*> g_sd_dev;
thread_local SceneDepthState* t_sd = nullptr;
constexpr SDL_GPUTextureFormat kSceneDepthFormat = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;

SDL_GPUGraphicsPipeline* scene_depth_variant(SceneDepthState& g_sd, SDL_GPUGraphicsPipeline* p);
// every two-target form of `p` (the masked one; the depth-writing one when its shader has a SCENE_DEPTH form, and the
// depth-only one too where R32F blends); the number made
int scene_depth_forms(SceneDepthState& g_sd, SDL_GPUGraphicsPipeline* p) {
    const auto i = g_sd.info.find(p);
    if (i == g_sd.info.end()) return 0;
    const bool depth = g_sd.depth_fs.count(i->second.pi.fragment_shader) != 0;
    const bool mask = g_sd.mask_on, only = g_sd.depth_only;
    g_sd.forming = true;
    int n = 0;
    for (int form = 0; form < (depth ? (g_sd.blend ? 3 : 2) : 1); ++form) {
        g_sd.mask_on = form == 1;
        g_sd.depth_only = form == 2;
        if (scene_depth_variant(g_sd, p)) ++n;
    }
    g_sd.mask_on = mask;
    g_sd.depth_only = only;
    g_sd.forming = false;
    return n;
}
void scene_depth_keep(SceneDepthState& g_sd, SDL_GPUGraphicsPipeline* p, const SDL_GPUGraphicsPipelineCreateInfo* pi) {
    if (!p || pi->target_info.num_color_targets != 1 || pi->target_info.color_target_descriptions[0].format != SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT)
        return;
    ScenePipeInfo k;
    k.pi = *pi;
    k.vb.assign(pi->vertex_input_state.vertex_buffer_descriptions, pi->vertex_input_state.vertex_buffer_descriptions + pi->vertex_input_state.num_vertex_buffers);
    k.at.assign(pi->vertex_input_state.vertex_attributes, pi->vertex_input_state.vertex_attributes + pi->vertex_input_state.num_vertex_attributes);
    k.ct = pi->target_info.color_target_descriptions[0];
    g_sd.info[p] = std::move(k);
    if (g_sd.eager && pi->multisample_state.sample_count == g_sd.samples) scene_depth_forms(g_sd, p);
}
// `p`'s two-target form for the draw being bound (null when it cannot be made: the state is then broken)
SDL_GPUGraphicsPipeline* scene_depth_variant(SceneDepthState& g_sd, SDL_GPUGraphicsPipeline* p) {
    if (!p) return p;
    const auto dfs = g_sd.depth_fs.find(g_sd.info.count(p) ? g_sd.info[p].pi.fragment_shader : nullptr);
    const bool writes = dfs != g_sd.depth_fs.end() && (g_sd.depth_only || g_sd.mask_on);
    auto& cache = g_sd.depth_only ? g_sd.only : writes ? g_sd.mrt : g_sd.mrt_off;
    if (const auto f = cache.find(p); f != cache.end()) return f->second;
    const auto i = g_sd.info.find(p);
    if (i == g_sd.info.end() || (g_sd.depth_only && !g_sd.blend)) {
        if (++g_sd.failed <= 4) SDL_Log("b3gfx: scene depth: no two-target form for a pipeline bound in the scene pass: the pre-pass from the next frame");
        g_sd.broken = true;
        return nullptr;
    }
    SDL_GPUGraphicsPipelineCreateInfo pi = i->second.pi;
    pi.vertex_input_state.vertex_buffer_descriptions = i->second.vb.data();
    pi.vertex_input_state.vertex_attributes = i->second.at.data();
    SDL_GPUColorTargetDescription ct[2] = {i->second.ct, {}};
    ct[1].format = kSceneDepthFormat;
    if (g_sd.depth_only) {   // (no depth test there: the MAX keeps the nearest)
        ct[1].blend_state.enable_blend = true;
        ct[1].blend_state.src_color_blendfactor = ct[1].blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct[1].blend_state.src_alpha_blendfactor = ct[1].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct[1].blend_state.color_blend_op = ct[1].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_MAX;
    }
    ct[1].blend_state.enable_color_write_mask = true;
    ct[1].blend_state.color_write_mask = writes ? SDL_GPU_COLORCOMPONENT_R : SDL_GPUColorComponentFlags(0);
    if (dfs != g_sd.depth_fs.end()) pi.fragment_shader = dfs->second;
    if (g_sd.depth_only) {
        ct[0].blend_state.enable_color_write_mask = true;
        ct[0].blend_state.color_write_mask = SDL_GPUColorComponentFlags(0);
        pi.depth_stencil_state.enable_depth_test = false;
        pi.depth_stencil_state.enable_depth_write = false;
        pi.depth_stencil_state.enable_stencil_test = false;
    }
    pi.target_info.color_target_descriptions = ct;
    pi.target_info.num_color_targets = 2;
    SDL_GPUGraphicsPipeline* m = SDL_CreateGPUGraphicsPipeline(g_sd.dev, &pi);
    if (!m) {
        if (++g_sd.failed <= 4) SDL_Log("b3gfx: scene depth pipeline: %s (the pre-pass from the next frame)", SDL_GetError());
        g_sd.broken = true;
        return nullptr;   // (not cached: nothing to release)
    }
    cache[p] = m;
    if (g_sd.eager && !g_sd.forming && ++g_sd.late <= 8)   // (with the switch on: one not made with its pipeline, a hitch)
        SDL_Log("b3gfx: scene depth: a two-target pipeline made at a draw (%d so far)", g_sd.late);
    return m;
}

inline SDL_GPURenderPass* perf_begin_pass(SDL_GPUCommandBuffer* cmd, const SDL_GPUColorTargetInfo* ci, Uint32 n,
                                          const SDL_GPUDepthStencilTargetInfo* di) {
    b3perf::count(b3perf::kPasses);
    SceneDepthState* const g = t_sd;
    if (g && g->scene && n == 1 && ci[0].texture == g->scene) {   // scenedepth: the scene pass, with its depth target
        SDL_GPUColorTargetInfo two[2] = {ci[0], {}};
        SDL_GPUColorTargetInfo& d = two[1];
        d.texture = g->target;
        d.clear_color = SDL_FColor{0, 0, 0, 0};
        d.load_op = g->begun ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        d.store_op = !g->resolve ? SDL_GPU_STOREOP_STORE : g->keep ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
        d.resolve_texture = g->resolve;
        g->begun = g->in_pass = true;
        g->skip = false;
        return SDL_BeginGPURenderPass(cmd, two, 2, di);
    }
    if (g) g->in_pass = false;
    return SDL_BeginGPURenderPass(cmd, ci, n, di);
}
inline void perf_end_pass(SDL_GPURenderPass* pass) {
    if (t_sd) t_sd->in_pass = false;
    SDL_EndGPURenderPass(pass);
}
inline void perf_bind(SDL_GPURenderPass* pass, SDL_GPUGraphicsPipeline* p) {
    SceneDepthState* const g = t_sd;
    if (g && g->in_pass) {
        SDL_GPUGraphicsPipeline* const v = scene_depth_variant(*g, p);
        g->skip = !v;   // (none: its draws are left out, not drawn with a single-target pipeline in a two-target pass)
        if (v) SDL_BindGPUGraphicsPipeline(pass, v);
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, p);
}
inline bool scene_depth_skip() { return t_sd && t_sd->in_pass && t_sd->skip; }
inline void perf_draw(SDL_GPURenderPass* pass, Uint32 vertices, Uint32 first) {
    if (scene_depth_skip()) return;
    b3perf::count(b3perf::kDraws);
    SDL_DrawGPUPrimitives(pass, vertices, 1, first, 0);
}
inline void perf_draw_indexed(SDL_GPURenderPass* pass, Uint32 n, Uint32 first) {
    if (scene_depth_skip()) return;
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
// perf4: a pipeline made after the start (at the first draw that needs it: the driver compiles it then) is logged with its
// time, so a first-race hitch from one shows in the run log
bool g_pipe_log = false;
int g_pipe_late = 0;
double g_pipe_late_ms = 0;
inline SDL_GPUGraphicsPipeline* perf_create_pipeline(SDL_GPUDevice* d, const SDL_GPUGraphicsPipelineCreateInfo* pi) {
    const Uint64 t0 = SDL_GetTicksNS();
    SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(d, pi);
    {   // scenedepth: with the device's scene depth on, the create info kept (its two-target forms made from it)
        std::lock_guard<std::mutex> lk(g_sd_mu);
        if (const auto f = g_sd_dev.find(d); f != g_sd_dev.end() && f->second->on) scene_depth_keep(*f->second, p, pi);
    }
    if (g_pipe_log) {
        const double ms = double(SDL_GetTicksNS() - t0) * 1e-6;
        g_pipe_late_ms += ms;
        if (++g_pipe_late <= 64) SDL_Log("b3gfx: pipeline made at its first draw: %.1f ms (%d so far, %.0f ms)", ms, g_pipe_late, g_pipe_late_ms);
    }
    return p;
}
}  // namespace
#define SDL_CreateGPUGraphicsPipeline(d, pi) perf_create_pipeline(d, pi)
#define SDL_BeginGPURenderPass(cmd, ci, n, di) perf_begin_pass(cmd, ci, n, di)
#define SDL_EndGPURenderPass(pass) perf_end_pass(pass)
#define SDL_BindGPUGraphicsPipeline(pass, p) perf_bind(pass, p)
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

void env_face(int f, float fwd[3], float right[3], float up[3]) {
    // mat4_look's basis for forward +X, -X, +Y, -Y, +Z, -Z with up +Y (+Z for the Y faces): right = fwd x up, up = right x fwd
    static const float kF[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const float kR[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {1, 0, 0}};
    static const float kU[6][3] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
    f = f < 0 ? 0 : f > 5 ? 5 : f;
    for (int i = 0; i < 3; ++i) { fwd[i] = kF[f][i]; right[i] = kR[f][i]; up[i] = kU[f][i]; }
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
struct FragmentUniforms { float tint[4]; float light_dir[4]; float params[4]; float gs[4]; float fog[4]; float fog_colour[4];
                         float surf[4], surf_sun[4], surf_eye[4], surf_night[4]; };   // surface (FrameParams::Surface); night: the moonlit faces
// SUN SHADOWS: mesh.frag's SUN_SHADOW variant reads these after FragmentUniforms (the map's view-projection; side, depth,
// kernel, strength)
struct ShadowFragmentUniforms { FragmentUniforms base; float sun_vp[16]; float sun_p[4]; float sun_q[4]; };
// car.vert / car.frag (std140: mat4 and vec4 members only)
struct CarVertexUniforms { float view_proj[16], model[16], light[16], sh[16]; float eye[4], view_dir[4], base[4], full[4];
                           float zone[8][16]; float deform[4]; };
struct CarFragmentUniforms { float env_colour[4], mode[4], to_sun[4], sun_colour[4], fog[4], fog_colour[4], refl[4], look0[4], look1[4], look2[4], look3[4], paint[4], paint2[4], night[4], moon[4], paint3[4], paint4[4]; };
// The GS frame: half intensity like the PS2's render buffer, float so the frame alpha (As / 128, read by the
// full-screen specular pass) is not clamped at 1.
// bloom2: the SOFT level's share of the game's bloom intensity (before the screen blend)
constexpr float kBloomSoft = 0.6f;
constexpr float kUpscaleSharpen = 0.35f;   // 1.1 UPSCALING Sharp: post.frag sharp_upscale amount
constexpr SDL_GPUTextureFormat kSceneFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
constexpr int kMaxTextureSide = 16384;   // create_texture: the largest image side taken

// perf4 (supersampling's budget): the device-local memory of the Vulkan device SDL picked (MB; 0: not known). SDL_GPU does not
// report it, so a throwaway Vulkan instance asks: the physical device whose name is SDL's SDL_PROP_GPU_DEVICE_NAME_STRING (or
// the only one), its largest DEVICE_LOCAL heap. The few Vulkan types are declared here (no Vulkan headers in the build); the
// properties struct is read only up to deviceName, into a buffer larger than the whole struct (824 bytes in Vulkan 1.x).
#ifdef _WIN32
// texpack review (HD TEXTURE MEMORY's Auto on Direct3D 12): the DXGI adapter SDL renders with (its description is SDL's
// SDL_PROP_GPU_DEVICE_NAME_STRING; with one hardware adapter, that one), its dedicated video memory (MB) and its kind as
// Vulkan names them (2 discrete: 512 MB or more of its own; 1 integrated; 4 the software adapter). 0: not known.
double dxgi_device_local_mb(SDL_GPUDevice* dev, int* kind) {
    const char* want = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(dev), SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
    HMODULE lib = LoadLibraryW(L"dxgi.dll");
    if (!lib) return 0;
    using Create = HRESULT(WINAPI*)(REFIID, void**);
    auto create = reinterpret_cast<Create>(reinterpret_cast<void*>(GetProcAddress(lib, "CreateDXGIFactory1")));
    double mb = 0;
    IDXGIFactory1* f = nullptr;
    if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&f))) && f) {
        int hardware = 0, match = -1, only = -1;
        DXGI_ADAPTER_DESC1 descs[16] = {};
        for (UINT i = 0; i < 16; ++i) {
            IDXGIAdapter1* a = nullptr;
            if (f->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND || !a) break;
            a->GetDesc1(&descs[i]);
            a->Release();
            if (!(descs[i].Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { ++hardware; only = int(i); }
            char name[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, descs[i].Description, -1, name, sizeof name - 1, nullptr, nullptr);
            if (match < 0 && want && std::strncmp(name, want, 255) == 0) match = int(i);
        }
        const int use = match >= 0 ? match : hardware == 1 ? only : -1;
        if (use >= 0) {
            const DXGI_ADAPTER_DESC1& d = descs[use];
            mb = double(d.DedicatedVideoMemory) / 1048576.0;
            if (kind) *kind = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? 4 : mb >= 512.0 ? 2 : 1;
        }
        f->Release();
    }
    FreeLibrary(lib);
    return mb;
}
#endif

double vulkan_device_local_mb(SDL_GPUDevice* dev, int* kind = nullptr) {   // texpack: `kind` the VkPhysicalDeviceType
    const char* drv = SDL_GetGPUDeviceDriver(dev);
    if (!drv || std::strcmp(drv, "vulkan") != 0) return 0;
    const char* want = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(dev), SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
    if (!SDL_Vulkan_LoadLibrary(nullptr)) return 0;
    double best = 0;
    using Fn = void (*)();
    using GetProc = Fn (*)(void*, const char*);
    if (auto gip = reinterpret_cast<GetProc>(SDL_Vulkan_GetVkGetInstanceProcAddr())) {
        struct AppInfo { int s_type; const void* next; const char* app; std::uint32_t app_ver; const char* eng; std::uint32_t eng_ver, api; };
        struct InstInfo { int s_type; const void* next; std::uint32_t flags; const AppInfo* app; std::uint32_t nl; const char* const* l;
                          std::uint32_t ne; const char* const* e; };
        struct MemProps { std::uint32_t ntypes; struct { std::uint32_t flags, heap; } types[32]; std::uint32_t nheaps;
                          struct { std::uint64_t size; std::uint32_t flags; } heaps[16]; };
        using Create = int (*)(const InstInfo*, const void*, void**);
        using Destroy = void (*)(void*, const void*);
        using Enum = int (*)(void*, std::uint32_t*, void**);
        using Props = void (*)(void*, void*);
        const AppInfo ai{0, nullptr, "b3", 1, "b3", 1, 1u << 22};
        const InstInfo ii{1, nullptr, 0, &ai, 0, nullptr, 0, nullptr};
        void* inst = nullptr;
        auto create = reinterpret_cast<Create>(gip(nullptr, "vkCreateInstance"));
        if (create && create(&ii, nullptr, &inst) == 0 && inst) {
            auto destroy = reinterpret_cast<Destroy>(gip(inst, "vkDestroyInstance"));
            auto enumerate = reinterpret_cast<Enum>(gip(inst, "vkEnumeratePhysicalDevices"));
            auto props = reinterpret_cast<Props>(gip(inst, "vkGetPhysicalDeviceProperties"));
            auto mem = reinterpret_cast<Props>(gip(inst, "vkGetPhysicalDeviceMemoryProperties"));
            std::uint32_t n = 0;
            if (enumerate && props && mem && enumerate(inst, &n, nullptr) == 0 && n > 0 && n < 64) {
                std::vector<void*> pd(n);
                if (enumerate(inst, &n, pd.data()) == 0)
                    for (std::uint32_t i = 0; i < n; ++i) {
                        alignas(16) unsigned char pp[4096] = {};
                        props(pd[i], pp);
                        const char* name = reinterpret_cast<const char*>(pp + 20);   // after apiVersion .. deviceType
                        if (n > 1 && !(want && std::strncmp(name, want, 255) == 0)) continue;
                        if (kind) std::memcpy(kind, pp + 16, 4);   // deviceType (after apiVersion, driverVersion, vendorID, deviceID)
                        MemProps mp{};
                        mem(pd[i], &mp);
                        for (std::uint32_t h = 0; h < mp.nheaps && h < 16; ++h)
                            if (mp.heaps[h].flags & 1u) best = std::max(best, double(mp.heaps[h].size) / 1048576.0);   // DEVICE_LOCAL
                        break;
                    }
            }
            if (destroy) destroy(inst, nullptr);
        }
    }
    SDL_Vulkan_UnloadLibrary();
    return best;
}

class SdlDevice final : public Device {
public:
    ~SdlDevice() override {
        if (!dev_) return;
        if (occ_frames_)   // perf4
            SDL_Log("b3gfx: sun occlusion pass: %llu frames, %.1f draws a frame (%.1f left out: outside the sun's box)", (unsigned long long)occ_frames_,
                    double(occ_drawn_) / double(occ_frames_), double(occ_skipped_) / double(occ_frames_));
        flush_uploads();
        SDL_WaitForGPUIdle(dev_);
        release_pictures();   // paint3
        if (last_fence_) SDL_ReleaseGPUFence(dev_, last_fence_);   // fifauto
        if (up_.tb) SDL_ReleaseGPUTransferBuffer(dev_, up_.tb);
        if (bat_.tb) SDL_ReleaseGPUTransferBuffer(dev_, bat_.tb);
        for (auto* b : buffers_) SDL_ReleaseGPUBuffer(dev_, b);
        for (auto* t : textures_) SDL_ReleaseGPUTexture(dev_, t);
        for (auto& kv : pipelines_) for (auto* p : kv.second) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
        release_targets(win_targets_);
        release_targets(off_targets_);
        release_targets(pic_targets_);
        release_targets(scaled_win_);   // effects2
        release_targets(scaled_off_);
        for (auto* v : {&win_views_, &off_views_}) for (Targets& t : *v) release_targets(t);   // gsoff1
        if (sampler_) SDL_ReleaseGPUSampler(dev_, sampler_);
        if (white_) SDL_ReleaseGPUTexture(dev_, white_);
        for (auto* p : {night_pipe_[0], night_pipe_[1], light_pipe_, beam_pipe_, emissive_pipe_}) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
        for (auto* sh : {vs_, fs_, fsq_vs_, spec_fs_, present_fs_, car_vs_, car_fs_, sun_vs_, sun_fs_, blur_fs_, post_fs_, post_lean_fs_, dof_fs_, light_fs_, night_fs_, beam_fs_, emissive_fs_, motion_fs_, drops_fs_}) if (sh) SDL_ReleaseGPUShader(dev_, sh);
        if (drops_pipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, drops_pipe_);
        for (auto* p : {occ_pipe_[0], occ_pipe_[1], sun_pipe_, blur_pipe_[0], blur_pipe_[1], post_pipe_[0], post_pipe_[1], post_pipe_[2], post_pipe_[3]}) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
        for (auto* p : {dof_depth_pipe_[0], dof_depth_pipe_[1], dof_depth_pipe_[2], dof_pipe_[0], dof_pipe_[1], motion_pipe_[0], motion_pipe_[1], motion_pipe_[2]}) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
        for (auto* p : ao_pipe_) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
        if (sdfix_pipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, sdfix_pipe_);   // scenedepth
        if (sdfix_fs_) SDL_ReleaseGPUShader(dev_, sdfix_fs_);
        if (ao_fs_) SDL_ReleaseGPUShader(dev_, ao_fs_);
        for (auto* p : sh_recv_pipe_) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);   // SUN SHADOWS
        if (sh_pipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, sh_pipe_);
        if (sh_prop_pipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, sh_prop_pipe_);   // propshadow1
        for (auto* sh : {sh_fs_, sh_recv_fs_, sh_prop_fs_}) if (sh) SDL_ReleaseGPUShader(dev_, sh);
        // scenedepth: the two-target forms (every pipeline is released here, with the device, never before: so the forms
        // cached by their pipeline need no erasing elsewhere)
        for (auto* m : {&sd_.mrt, &sd_.mrt_off, &sd_.only})
            for (auto& kv : *m) if (kv.second) SDL_ReleaseGPUGraphicsPipeline(dev_, kv.second);
        for (auto& kv : sd_.depth_fs) if (kv.second) SDL_ReleaseGPUShader(dev_, kv.second);
        {
            std::lock_guard<std::mutex> lk(g_sd_mu);
            g_sd_dev.erase(dev_);
        }
        sd_ = SceneDepthState{};
        if (env_glow_src_) SDL_ReleaseGPUTexture(dev_, env_glow_src_);   // reflect2 G
        env_glow_src_ = nullptr;
        release_sun_shadow_map();
        if (occ_) SDL_ReleaseGPUTexture(dev_, occ_);
        for (EnvTarget& e : env_) release_env(e);   // reflect-tunnel
        for (auto* p : {spec_pipe_, present_pipe_win_, present_pipe_off_, car_pipe_, line_pipe_, clamp_pipe_}) if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
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
        render_scale_ = std::clamp(opt.render_scale, 0.25f, 2.0f);   // effects2; perf4: above 1 supersampled
        warm_sun_shadows_ = opt.warm_sun_shadows;   // finalfix: warm_pipelines, effective_scale
        warm_ao_ = opt.warm_ambient_occlusion;
        upload_batch_opt_ = opt.upload_batch;   // uploadbatch (pc_settings upload_batch)
        sun_shadow_side_opt_ = std::uint32_t(std::clamp(opt.sun_shadow_side, 0, 4096));
        // B3GFX_TRACE=1: each start-up call on stdout (the live window's first test hung inside this function)
        const bool trace = SDL_getenv("B3GFX_TRACE") != nullptr;
        auto step = [&](const char* what) { if (trace) { std::printf("[b3gfx] %s\n", what); std::fflush(stdout); } };
        auto create = [&](const std::string& name) {
            SDL_PropertiesID p = SDL_CreateProperties();
            SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
            SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
#if defined(_WIN32)
            SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);   // d3d12: shaders_gen.h kDxil
#endif
            SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, opt.debug);
            if (!name.empty()) SDL_SetStringProperty(p, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, name.c_str());
            step("SDL_CreateGPUDevice");
            SDL_GPUDevice* d = SDL_CreateGPUDeviceWithProperties(p);
            SDL_DestroyProperties(p);
            return d;
        };
        // d3d12: exactly the driver asked for ("": SDL's choice); Device::create tries the GRAPHICS API's drivers in order
        dev_ = create(opt.driver);
        if (!dev_) return fail(err, "SDL_CreateGPUDevice");
        // scenedepth: $B3_SCENE_DEPTH, else pc_settings scene_depth, else the backend's default
        // (nightdepth: the default prepares it, for night's frames only: scene_depth_mode_)
        scene_depth_mode_ = scene_depth_mode(std::getenv("B3_SCENE_DEPTH"), opt.scene_depth, SDL_GetGPUDeviceDriver(dev_));
        scene_depth_on_ = scene_depth_mode_ != SceneDepthMode::kOff;
        if (window) {
            step("SDL_ClaimWindowForGPUDevice");
            if (!SDL_ClaimWindowForGPUDevice(dev_, window)) return fail(err, "SDL_ClaimWindowForGPUDevice");
            step("swapchain parameters");
            window_ = window;
            if (!apply_present(opt.present)) return fail(err, "SDL_SetGPUSwapchainParameters");
            // lat1: the GPU's frame queue (SDL's default 2: the CPU may build a frame while the previous one renders,
            // which holds a GPU-bound frame back by up to one more frame)
            if (opt.frames_soft) {   // fifauto: SDL keeps its 2; the device holds 1 itself (wait_swapchain)
                soft_ = true;
                frames_in_flight_ = std::clamp(opt.frames_in_flight, 1, 2);
            } else if (opt.frames_in_flight > 0 && SDL_SetGPUAllowedFramesInFlight(dev_, std::uint32_t(std::clamp(opt.frames_in_flight, 1, 3))))
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
        // scenedepth: its target at the scene's sample count (else the pre-pass, as off)
        if (scene_depth_on_ && !(SDL_GPUTextureSupportsFormat(dev_, kSceneDepthFormat, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) &&
                                 (msaa_ == 1 || SDL_GPUTextureSupportsSampleCount(dev_, kSceneDepthFormat, sample_count(msaa_))))) {
            scene_depth_on_ = false;
            SDL_Log("b3gfx: scene depth: R32F at MSAA %d not available: the depth pre-pass", msaa_);
        }
        // MAX blending of R32F (the AO break's depth-only forms): not queryable through SDL 3.4, the backend's rule
        // (scene_depth.h scene_depth_blend); without it a frame with the AO keeps the pre-pass
        sd_.blend = scene_depth_on_ && scene_depth_blend(SDL_GetGPUDeviceDriver(dev_), SDL_GetStringProperty(SDL_GetGPUDeviceProperties(dev_), SDL_PROP_GPU_DEVICE_NAME_STRING, ""));
        // (nightdepth: the forms at start for every frame's scene depth, or night's when night can be on from the start;
        // else at the first night race's load, prepare_scene_depth; sd_.on keeps the create infos for that)
        sd_.on = scene_depth_on_;
        sd_.eager = scene_depth_on_ && scene_depth_forms_at_start(scene_depth_mode_, opt.scene_depth_warm_night);
        sd_.samples = sample_count(msaa_);
        sd_.dev = dev_;
        {
            std::lock_guard<std::mutex> lk(g_sd_mu);
            g_sd_dev[dev_] = &sd_;
        }
        SDL_Log("b3gfx: scene depth %s%s", !scene_depth_on_ ? "off (the depth pre-pass)"
                                           : sd_.blend ? "on (the scene pass keeps its depth)"
                                                       : "on (the scene pass keeps its depth; the AO's frames the pre-pass: R32F does not blend here)",
                !scene_depth_on_ || scene_depth_mode_ != SceneDepthMode::kNight ? ""
                : sd_.eager ? ", for night's frames (by day the pre-pass; its pipelines at start: NIGHT on)"
                            : ", for night's frames (by day the pre-pass; its pipelines at the first night race's load)");
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
        bc_ok_ = std::getenv("B3_HD_RGBA") == nullptr;   // hdgpu: $B3_HD_RGBA keeps the HD pack on the RGBA8 path
        for (SDL_GPUTextureFormat f : {SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM, SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM})
            bc_ok_ = bc_ok_ && SDL_GPUTextureSupportsFormat(dev_, f, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        SDL_Log("b3gfx: BC (DXT) textures: %s", bc_ok_ ? "yes (HD packs stay compressed on the GPU)"
                                                : std::getenv("B3_HD_RGBA") ? "no (B3_HD_RGBA: HD packs go up as RGBA8)"
                                                                            : "no (the GPU / driver does not take BC1-3: HD packs go up as RGBA8)");
        const std::uint8_t white[4] = {255, 255, 255, 255};
        const TextureId w = create_texture({TextureLevel{1, 1, white}});
        if (w == kNone) return fail(err, "white texture");
        white_ = textures_[w];
        textures_[w] = nullptr;
        textures_.pop_back();
        warm_pipelines();
        // d3d12: a driver that makes a device but not the track's own pipeline (an old, broken Vulkan driver) fails here,
        // so Device::create tries the next GRAPHICS API (D3D12) instead of a window with nothing in it
        if (!pipeline(kSceneFormat, Blend::kOpaque, false)) return fail(err, "the first pipelines (the track's opaque pipeline)");
        // test aid: B3GFX_FAIL_DRIVER=<driver> fails the start-up of that driver here, after its device, window claim,
        // shaders and pipelines exist (Device::create's destroy-and-retry path, run on a working PC)
        if (const char* f = SDL_getenv("B3GFX_FAIL_DRIVER"); f && *f && opt.driver == f) return fail(err, "B3GFX_FAIL_DRIVER (test aid)");
        g_pipe_log = true;
        return true;
    }

    // perf4: every fixed pipeline made now, not at the first draw that needs it (a driver compiles it then: a hitch in the
    // race's first seconds). The 2D stage's keyed ones come from warm_overlay_keys; the depth of field's passes stay on demand
    // (photo mode), its depth pre-pass is made here (night mode draws it every frame). B3_PIPELINE_WARM=0: all on demand, as before.
    // finalfix: SUN SHADOWS' caster and receivers and AMBIENT OCCLUSION's two passes too when the setting is on at start
    // (Options::warm_*; turned on later, they are made at their first draw). Photo mode's motion blur stays on demand.
    void warm_pipelines() {
        if (const char* e = std::getenv("B3_PIPELINE_WARM"); e && e[0] == '0') return;
        const Uint64 t0 = SDL_GetTicksNS();
        for (Blend b : {Blend::kOpaque, Blend::kAlphaTest, Blend::kAlpha})
            for (bool cull : {false, true}) pipeline(kSceneFormat, b, cull);
        for (bool cull : {false, true}) pipeline(kSceneFormat, Blend::kAlpha, cull, false, true);   // the post-fog stage (Z written)
        for (bool cull : {false, true}) pipeline(kSceneFormat, Blend::kAlpha, cull, false, false, nullptr, true);   // whitesky: the clouds
        pipeline(kSceneFormat, Blend::kOpaque, false, true);   // cars
        for (int m = 0; m < 3; ++m) effect_pipeline(m);
        line_pipeline();
        clamp_pipeline();
        occ_pipeline(false);
        occ_pipeline(true);
        sun_pipeline();
        blur_pipeline(false);
        blur_pipeline(true);
        post_pipeline(false);
        post_pipeline(true, false);
        post_pipeline(true, true);
        if (std::getenv("B3_BLOOM_LEAN") && post_lean_fs_) post_pipeline(false, false, true);
        if (!spec_pipe_) spec_pipe_ = fullscreen_pipeline(kSceneFormat, true);
        // nightlights (§17.2): night mode's grade, beams, pools and fixtures, and the depth pre-pass they read (the depth
        // of field's, drawn every night frame: so made here too, not at the first night frame)
        night_pipeline(false);
        night_pipeline(true);
        light_pipeline();
        beam_pipeline();
        emissive_pipeline();
        if (dof_ready()) for (int k = 0; k < 3; ++k) dof_depth_pipeline(k);
        if (warm_sun_shadows_ && sun_shadow_ready()) {
            sun_caster_pipeline();
            sun_prop_pipeline();   // propshadow1 (made with the rest: PROP SHADOWS may turn on in a race)
            for (Blend b : {Blend::kOpaque, Blend::kAlphaTest, Blend::kAlpha})
                for (bool cull : {false, true}) recv_pipeline(b, cull);
        }
        if (warm_ao_ && dof_ready() && ao_ready()) {
            ao_pipeline(0);
            ao_pipeline(1);   // (2, its debug form, stays on demand)
        }
        if (window_ && !present_pipe_win_) present_pipe_win_ = fullscreen_pipeline(swap_format_, false);
        if (!present_pipe_off_ && swap_format_ != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
            present_pipe_off_ = fullscreen_pipeline(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false);   // offscreen and F12 shots
        if (sd_.eager) scene_depth_warm("at start");   // scenedepth: their two-target forms too (no compile at the first night frame)
        warm_ms_ = double(SDL_GetTicksNS() - t0) * 1e-6;
        SDL_Log("b3gfx: pipelines made at start: %.0f ms", warm_ms_);
    }
    std::vector<int> overlay_keys() const override {
        std::vector<int> k;
        for (const auto& kv : overlay_pipe_) if (kv.second) k.push_back(kv.first);
        return k;
    }
    void warm_overlay_keys(const std::vector<int>& keys) override {
        if (const char* e = std::getenv("B3_PIPELINE_WARM"); e && e[0] == '0') return;
        const Uint64 t0 = SDL_GetTicksNS();
        int n = 0;
        g_pipe_log = false;
        for (int k : keys)
            if (k >= 0 && k < 0x1000 && !overlay_pipe_.count(k)) { overlay_pipeline(k); ++n; }
        g_pipe_log = true;
        if (n) SDL_Log("b3gfx: 2D pipelines made at start: %d (saved by an earlier run) in %.0f ms", n, double(SDL_GetTicksNS() - t0) * 1e-6);
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
        tex_gen_.clear();
        dyn_tex_size_.clear();
        original_.clear();   // hdkey (the pairs go; hd_shown_ stays)
        originals_ = {};
        release_sun_shadow_map();   // SUN SHADOWS: made again when a frame next asks for it
    }

    // hdkey (Device::set_texture_original): the texture a draw samples for `id` (white for none)
    SDL_GPUTexture* tex_of(TextureId id) const {
        if (id >= textures_.size()) return white_;
        if (!hd_shown_ && id < original_.size() && original_[id] < textures_.size()) return textures_[original_[id]];
        return textures_[id];
    }
    void set_texture_original(TextureId hd, TextureId original, std::size_t bytes, std::size_t hd_bytes) override {
        if (hd >= textures_.size() || original >= textures_.size()) return;
        if (original_.size() <= hd) original_.resize(std::size_t(hd) + 1, kNone);
        if (original_[hd] == kNone) { ++originals_.textures; originals_.bytes += bytes; originals_.hd_bytes += hd_bytes; }
        original_[hd] = original;
    }
    void set_hd_shown(bool on) override { hd_shown_ = on; }
    bool hd_shown() const override { return hd_shown_; }
    HdOriginals hd_originals() const override { return originals_; }

    void update_vertex_buffer(BufferId& id, const Vertex* v, std::size_t count) override {
        update_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, id, v, count * sizeof(Vertex));
    }
    void update_index_buffer(BufferId& id, const std::uint32_t* idx, std::size_t count) override {
        update_buffer(SDL_GPU_BUFFERUSAGE_INDEX, id, idx, count * 4);
    }

    bool supports_bc() const override { return bc_ok_; }
    const char* driver_name() const override { return SDL_GetGPUDeviceDriver(dev_); }
    // texpack: asked once (a throwaway Vulkan instance; other drivers: not known)
    double device_local_mb() const override { gpu_info(); return local_mb_; }
    int gpu_kind() const override { gpu_info(); return gpu_kind_; }
    void gpu_info() const {
        if (gpu_asked_) return;
        gpu_asked_ = true;
        const char* drv = SDL_GetGPUDeviceDriver(dev_);
        if (drv && std::strcmp(drv, "vulkan") == 0) {
            local_mb_ = vulkan_device_local_mb(dev_, &gpu_kind_);
        } else if (drv && std::strcmp(drv, "direct3d12") == 0) {
#ifdef _WIN32
            local_mb_ = dxgi_device_local_mb(dev_, &gpu_kind_);
#endif
        } else if (drv && std::strcmp(drv, "metal") == 0) {
            gpu_kind_ = 1;   // texpack review: Apple's GPUs here share the system's memory (Apple silicon); Auto packs
        }
    }
    mutable bool gpu_asked_ = false;
    mutable double local_mb_ = 0;
    mutable int gpu_kind_ = 0;
    void* sdl_gpu_device() const override { return dev_; }
    int msaa() const override { return msaa_; }
    const char* present_mode_name() const override { return present_; }
    // The swapchain's present mode: vsync, or uncapped (immediate where the window has it, else mailbox)
    bool apply_present(PresentMode want) {
        SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
        if (want == PresentMode::kUncapped) {
            if (SDL_WindowSupportsGPUPresentMode(dev_, window_, SDL_GPU_PRESENTMODE_IMMEDIATE)) mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
            else if (SDL_WindowSupportsGPUPresentMode(dev_, window_, SDL_GPU_PRESENTMODE_MAILBOX)) mode = SDL_GPU_PRESENTMODE_MAILBOX;
        }
        if (!SDL_SetGPUSwapchainParameters(dev_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) return false;
        present_ = mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "immediate" : mode == SDL_GPU_PRESENTMODE_MAILBOX ? "mailbox" : "vsync";
        return true;
    }
    bool set_present(PresentMode want) override {   // nextstart: VSYNC live
        if (!window_ || !dev_) return false;
        const bool ok = apply_present(want);
        if (!ok) std::fprintf(stderr, "b3gfx: SDL_SetGPUSwapchainParameters: %s\n", SDL_GetError());
        return ok;
    }
    int frames_in_flight() const override { return window_ ? frames_in_flight_ : 0; }
    void wait_swapchain() override {
        if (!window_) return;
        const Uint64 t0 = SDL_GetTicksNS();
        if (soft_ && frames_in_flight_ == 1 && last_fence_) {   // fifauto: one frame in flight: the last one done first
            SDL_WaitForGPUFences(dev_, true, &last_fence_, 1);
            SDL_ReleaseGPUFence(dev_, last_fence_);
            last_fence_ = nullptr;
        }
        SDL_WaitForGPUSwapchain(dev_, window_);
        wait_ns_ += SDL_GetTicksNS() - t0;
    }
    // perf4: frames in flight at run time (the adaptive Auto, include/b3gfx/frames_auto.h)
    bool set_frames_in_flight(int n) override {
        if (!window_) return false;
        n = std::clamp(n, 1, soft_ ? 2 : 3);
        if (n == frames_in_flight_) return true;
        if (soft_) {   // fifauto: a flag (the next frame's start waits, or does not wait, for the last frame's fence)
            frames_in_flight_ = n;
            return true;
        }
        flush_uploads();
        if (!SDL_SetGPUAllowedFramesInFlight(dev_, Uint32(n))) return false;
        frames_in_flight_ = n;
        return true;
    }
    std::uint64_t take_wait_ns() override { const std::uint64_t r = wait_ns_; wait_ns_ = 0; return r; }
    std::uint64_t take_acquire_ns() override { const std::uint64_t r = acquire_ns_; acquire_ns_ = 0; return r; }   // fifauto
    void set_gpu_timing(int every) override { gpu_every_ = every > 0 ? every : 0; }
    void set_bloom_lean(int on) override { bloom_lean_ = on < 0 ? -1 : (on ? 1 : 0); }
    double take_gpu_ms() override { const double r = gpu_ms_; gpu_ms_ = -1.0; return r; }
    // perf4: acquire / submit of a window frame, timed into wait_ns_
    bool acquire_timed(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture** sw, Uint32* w, Uint32* h) {
        const Uint64 t0 = SDL_GetTicksNS();
        const bool ok = SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, sw, w, h);
        const Uint64 dt = SDL_GetTicksNS() - t0;
        wait_ns_ += dt;
        acquire_ns_ += dt;   // fifauto: the presentation side's part (a free image / drawable)
        return ok;
    }
    bool submit_timed(SDL_GPUCommandBuffer* cmd) {
        const Uint64 t0 = SDL_GetTicksNS();
        bool ok;
        const bool timed = gpu_every_ > 0 && ++gpu_frame_ % std::uint64_t(gpu_every_) == 0;   // fifauto: a diagnostic sample
        if (soft_ || timed) {   // fifauto: the window frame's fence kept for the next frame's start (one frame in flight)
            SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            ok = f != nullptr;
            if (last_fence_) SDL_ReleaseGPUFence(dev_, last_fence_);
            last_fence_ = f;
            wait_ns_ += SDL_GetTicksNS() - t0;
            if (timed && f) {   // the GPU's time on this frame: from the submit's start to the fence (the fence wait itself is
                // not counted as a wait; a driver that renders inside the submit, lavapipe, has it all in the submit)
                SDL_WaitForGPUFences(dev_, true, &last_fence_, 1);
                gpu_ms_ = double(SDL_GetTicksNS() - t0) * 1e-6;
            }
            if (!soft_ && last_fence_) {
                SDL_ReleaseGPUFence(dev_, last_fence_);
                last_fence_ = nullptr;
            }
            return ok;
        }
        ok = SDL_SubmitGPUCommandBuffer(cmd);
        wait_ns_ += SDL_GetTicksNS() - t0;
        return ok;
    }

    BufferId create_vertex_buffer(const Vertex* v, std::size_t count) override {
        return make_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, v, count * sizeof(Vertex));
    }
    BufferId create_index_buffer(const std::uint32_t* idx, std::size_t count) override {
        return make_buffer(SDL_GPU_BUFFERUSAGE_INDEX, idx, count * 4);
    }

    TextureId create_texture(const std::vector<TextureLevel>& levels) override {
        if (bat_.on) flush_dyn();   // perf4: into the route's batch (after the frame's dynamic uploads, which came first)
        else flush_uploads();   // keep the submission order of the immediate uploads (see upload_reserve)
        B3PERF_SCOPE(kUpload);
        if (levels.empty() || levels[0].w <= 0 || levels[0].h <= 0) return kNone;
        // past what every backend takes (D3D12 and Metal 16384; Vulkan's dimension limit is not queried through SDL): a
        // replacement this large falls back to its original (Device::create_texture_hd)
        const int bc = levels[0].bc;
        if (levels[0].w > kMaxTextureSide || levels[0].h > kMaxTextureSide) {
            texture_refused(kRefuseSize, bc, levels[0].w, levels[0].h);
            return kNone;
        }
        // hdgpu: BC levels go up as they are (every level the same format, the chain given)
        for (const auto& l : levels)
            if (l.bc != bc || (bc && (!bc_ok_ || !l.rgba))) {
                texture_refused(l.bc != bc || !l.rgba ? kRefuseChain : kRefuseFormat, bc, levels[0].w, levels[0].h);
                return kNone;
            }
        const bool generate = !bc && levels.size() == 1 && (levels[0].w > 1 || levels[0].h > 1);
        std::uint32_t nlev = std::uint32_t(levels.size());
        if (generate) { nlev = 1; for (int m = std::max(levels[0].w, levels[0].h); m > 1; m >>= 1) ++nlev; }
        auto level_bytes = [&](const TextureLevel& l) {
            return bc ? std::size_t((l.w + 3) / 4) * std::size_t((l.h + 3) / 4) * (bc == 1 ? 8u : 16u) : std::size_t(l.w) * l.h * 4;
        };
        for (const auto& l : levels)
            if (bc && l.bytes < level_bytes(l)) {
                texture_refused(kRefuseShort, bc, levels[0].w, levels[0].h);
                return kNone;
            }
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = bc == 1 ? SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM : bc == 2 ? SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM
                  : bc == 3 ? SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (generate ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
        ti.width = std::uint32_t(levels[0].w);
        ti.height = std::uint32_t(levels[0].h);
        ti.layer_count_or_depth = 1;
        ti.num_levels = nlev;
        std::size_t total = 0;
        for (const auto& l : levels) total += level_bytes(l);
        if (total > 0xFFFFFFFFu) {
            texture_refused(kRefuseSize, bc, levels[0].w, levels[0].h);
            return kNone;
        }
        SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev_, &ti);
        if (!tex) { texture_failed("texture", ti.width, ti.height); return kNone; }
        if (bat_.on) {   // perf4: the route's batch (one staging buffer, one copy pass; the mip chains at its submit)
            std::uint32_t at = 0;
            if (std::uint8_t* dst = batch_reserve(std::uint32_t(total), at)) {
                std::size_t o = 0;
                for (const auto& l : levels) { std::memcpy(dst + o, l.rgba, level_bytes(l)); o += level_bytes(l); }
                ++stats_.uploads;
                stats_.upload_bytes += total;
                b3perf::count(b3perf::kUploads);
                b3perf::count(b3perf::kUploadBytes, total);
                o = 0;
                for (std::size_t i = 0; i < levels.size(); ++i) {
                    SDL_GPUTextureTransferInfo src{bat_.tb, at + std::uint32_t(o), std::uint32_t(levels[i].w), std::uint32_t(levels[i].h)};
                    SDL_GPUTextureRegion reg{};
                    reg.texture = tex;
                    reg.mip_level = std::uint32_t(i);
                    reg.w = std::uint32_t(levels[i].w);
                    reg.h = std::uint32_t(levels[i].h);
                    reg.d = 1;
                    SDL_UploadToGPUTexture(bat_.cp, &src, &reg, false);
                    bat_.ops.push_back({tex, nullptr, std::uint32_t(i), reg.w, reg.h, src.offset, 0, generate && i == 0});
                    o += level_bytes(levels[i]);
                }
                if (generate) bat_.mips.push_back(tex);
                ++bat_.items;
                textures_.push_back(tex);
                return TextureId(textures_.size() - 1);
            }
            flush_batch();   // too large for the batch (or its buffer failed): on its own below, after what the batch holds
        }
        // every allocation checked: a failure releases what was made and returns kNone (no half-made texture)
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, std::uint32_t(total), 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        auto* dst = tb ? static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(dev_, tb, false)) : nullptr;
        if (!dst) {
            if (tb) SDL_ReleaseGPUTransferBuffer(dev_, tb);
            SDL_ReleaseGPUTexture(dev_, tex);
            texture_failed("upload buffer for a texture", ti.width, ti.height);
            return kNone;
        }
        std::size_t off = 0;
        for (const auto& l : levels) { std::memcpy(dst + off, l.rgba, level_bytes(l)); off += level_bytes(l); }
        SDL_UnmapGPUTransferBuffer(dev_, tb);
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        SDL_GPUCopyPass* cp = cmd ? SDL_BeginGPUCopyPass(cmd) : nullptr;
        if (!cp) {
            if (cmd) SDL_CancelGPUCommandBuffer(cmd);
            SDL_ReleaseGPUTransferBuffer(dev_, tb);
            SDL_ReleaseGPUTexture(dev_, tex);
            texture_failed("command buffer for a texture", ti.width, ti.height);
            return kNone;
        }
        ++stats_.uploads;
        stats_.upload_bytes += total;
        b3perf::count(b3perf::kUploads);
        b3perf::count(b3perf::kUploadBytes, total);
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
            off += level_bytes(levels[i]);
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
        if (tex_gen_.size() <= id) tex_gen_.resize(std::size_t(id) + 1, 0);   // photoblur: its content changed
        ++tex_gen_[id];
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
        if (!acquire_timed(cmd, &sw, &sw_w, &sw_h) || !sw) {
            SDL_SubmitGPUCommandBuffer(cmd);
            return false;
        }
        if (w) *w = int(sw_w);
        if (h) *h = int(sw_h);
        if (!ensure_targets(win_targets_, swap_format_, sw_w, sw_h, false)) {
            no_frame(cmd, win_targets_, sw, fp, "the window's targets");
            return false;
        }
        render(cmd, win_targets_, sw, swap_format_, fp, items);
        return submit_timed(cmd);
    }

    bool draw_views(const std::vector<ViewPass>& views, const FrameParams& ofp, const std::vector<DrawItem>& overlay, int* w, int* h) override {
        if (!window_) return false;
        flush_uploads();
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd) return false;
        SDL_GPUTexture* sw = nullptr;
        Uint32 sw_w = 0, sw_h = 0;
        if (!acquire_timed(cmd, &sw, &sw_w, &sw_h) || !sw) {
            SDL_SubmitGPUCommandBuffer(cmd);
            return false;
        }
        if (w) *w = int(sw_w);
        if (h) *h = int(sw_h);
        if (!ensure_targets(win_targets_, swap_format_, sw_w, sw_h, false)) {
            no_frame(cmd, win_targets_, sw, ofp, "the window's targets");
            return false;
        }
        if (!render_views(cmd, win_targets_, win_views_, sw, swap_format_, views, ofp, overlay)) {
            no_frame(cmd, win_targets_, sw, ofp, "a split-screen view's targets");
            return false;
        }
        return submit_timed(cmd);
    }

    bool draw_views_offscreen(int w, int h, const std::vector<ViewPass>& views, const FrameParams& ofp, const std::vector<DrawItem>& overlay,
                              std::vector<std::uint8_t>& rgba) override {
        const SDL_GPUTextureFormat fmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        flush_uploads();
        if (!ensure_targets(off_targets_, fmt, std::uint32_t(w), std::uint32_t(h), true)) return false;
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd) return false;
        if (!render_views(cmd, off_targets_, off_views_, off_targets_.resolve, fmt, views, ofp, overlay)) {
            SDL_CancelGPUCommandBuffer(cmd);   // nothing recorded (no swapchain here)
            return false;
        }
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

    bool draw_picture(int w, int h, const FrameParams& fp, const std::vector<DrawItem>& items, std::vector<std::uint8_t>& rgba) override {
        const SDL_GPUTextureFormat fmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;   // paint3: pic_targets_ (the shots' stay)
        flush_uploads();
        if (!ensure_targets(pic_targets_, fmt, std::uint32_t(w), std::uint32_t(h), true)) return false;
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        // (at 100 %, never the render-scale path: its targets are the shots', and MIRROR MODE's state is the frames')
        render_scene(cmd, pic_targets_, fp, items, true);
        present(cmd, pic_targets_, pic_targets_.resolve, fmt, fp);
        return read_back(cmd, w, h, rgba, pic_targets_.resolve);
    }

    // paint3: draw_picture's drawing submitted with its read-back; picture_take maps it once its fence has passed
    int picture_begin(int w, int h, const FrameParams& fp, const std::vector<DrawItem>& items) override {
        const SDL_GPUTextureFormat fmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
        flush_uploads();
        if (!ensure_targets(pic_targets_, fmt, std::uint32_t(w), std::uint32_t(h), true)) return 0;
        const std::uint32_t bytes = std::uint32_t(w) * std::uint32_t(h) * 4u;
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes, 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        if (!tb) return 0;
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd) { SDL_ReleaseGPUTransferBuffer(dev_, tb); return 0; }
        render_scene(cmd, pic_targets_, fp, items, true);
        present(cmd, pic_targets_, pic_targets_.resolve, fmt, fp);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        if (!cp) { SDL_SubmitGPUCommandBuffer(cmd); SDL_ReleaseGPUTransferBuffer(dev_, tb); return 0; }
        SDL_GPUTextureRegion reg{};
        reg.texture = pic_targets_.resolve;
        reg.w = std::uint32_t(w);
        reg.h = std::uint32_t(h);
        reg.d = 1;
        SDL_GPUTextureTransferInfo dst{tb, 0, std::uint32_t(w), std::uint32_t(h)};
        SDL_DownloadFromGPUTexture(cp, &reg, &dst);
        SDL_EndGPUCopyPass(cp);
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (!fence) { SDL_ReleaseGPUTransferBuffer(dev_, tb); return 0; }
        pictures_[++picture_id_] = PendingPicture{fence, tb, bytes};
        return picture_id_;
    }
    int picture_take(int id, std::vector<std::uint8_t>& rgba, bool wait) override {
        const auto it = pictures_.find(id);
        if (it == pictures_.end()) return -1;
        PendingPicture& p = it->second;
        if (!wait && !SDL_QueryGPUFence(dev_, p.fence)) return 0;
        if (wait) SDL_WaitForGPUFences(dev_, true, &p.fence, 1);
        int r = -1;
        if (const auto* src = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(dev_, p.tb, false))) {
            rgba.assign(src, src + p.bytes);
            SDL_UnmapGPUTransferBuffer(dev_, p.tb);
            r = 1;
        }
        SDL_ReleaseGPUFence(dev_, p.fence);
        SDL_ReleaseGPUTransferBuffer(dev_, p.tb);
        pictures_.erase(it);
        return r;
    }
    void release_pictures() {
        for (auto& [id, p] : pictures_) {
            SDL_WaitForGPUFences(dev_, true, &p.fence, 1);
            SDL_ReleaseGPUFence(dev_, p.fence);
            SDL_ReleaseGPUTransferBuffer(dev_, p.tb);
        }
        pictures_.clear();
    }
    void release_buffer(BufferId id) override {   // paint3 (SDL frees it once the GPU is done with it)
        if (id < buffers_.size() && buffers_[id]) {
            SDL_ReleaseGPUBuffer(dev_, buffers_[id]);
            buffers_[id] = nullptr;
        }
    }
    void release_texture(TextureId id) override {   // (and an HD replacement's original: create_texture_hd's pair)
        if (id < original_.size() && original_[id] != kNone) {
            const TextureId o = original_[id];
            original_[id] = kNone;
            if (o < textures_.size() && textures_[o] && textures_[o] != white_) {
                SDL_ReleaseGPUTexture(dev_, textures_[o]);
                textures_[o] = nullptr;
            }
        }
        if (id < textures_.size() && textures_[id] && textures_[id] != white_) {
            SDL_ReleaseGPUTexture(dev_, textures_[id]);
            textures_[id] = nullptr;
        }
    }
    void picture_drop(int id) override {   // paint3: given up (its fence never passed): released without waiting
        const auto it = pictures_.find(id);
        if (it == pictures_.end()) return;
        SDL_ReleaseGPUFence(dev_, it->second.fence);
        SDL_ReleaseGPUTransferBuffer(dev_, it->second.tb);
        pictures_.erase(it);
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
    // create_texture's failures: the first few logged (an HD pack can have many), the rest counted silently
    void texture_failed(const char* what, std::uint32_t w, std::uint32_t h) {
        if (++texture_failures_ <= 4)
            SDL_Log("b3gfx: no %s (%ux%u): %s%s", what, unsigned(w), unsigned(h), SDL_GetError(),
                    texture_failures_ == 4 ? " (further failures not logged)" : "");
    }
    // create_texture's refusals before any GPU call (an HD replacement then falls back to its original: "F4 does nothing"):
    // the first of each reason and format logged, with the count so far
    enum Refusal { kRefuseFormat, kRefuseChain, kRefuseShort, kRefuseSize, kRefusals };
    void texture_refused(Refusal why, int bc, int w, int h) {
        static constexpr const char* kWhy[kRefusals] = {
            "this GPU / driver does not take BC (DXT) textures (or B3_HD_RGBA is set)",
            "its mip chain mixes formats or a level has no data", "a level's data is shorter than its size needs",
            "larger than the GPU takes"};
        ++texture_refusals_;
        const unsigned bit = 1u << (unsigned(why) * 4u + unsigned(bc & 3));
        if (refusals_logged_ & bit) return;
        refusals_logged_ |= bit;
        SDL_Log("b3gfx: a %dx%d %s%s texture refused: %s; it falls back to its original (%u refused so far; logged once per "
                "reason and format)", w, h, bc ? "BC" : "RGBA8", bc ? std::to_string(bc).c_str() : "", kWhy[why], texture_refusals_);
    }
    void set_dither(float steps) override { dither_ = steps; }
    void set_sharp_upscale(bool on) override { sharp_upscale_ = on; }   // 1.1
    void screenshot_begin() override {
        shot_seed_ = true;
        off_targets_.bp_valid = scaled_off_.bp_valid = false;   // never an earlier offscreen draw's picture
        for (Targets& t : off_views_) t.bp_valid = false;
    }
    void screenshot_end() override {
        shot_seed_ = false;
        flush_uploads();
        SDL_WaitForGPUIdle(dev_);   // read_back waited for the frame; nothing else reads these
        release_targets(off_targets_);
        release_targets(pic_targets_);
        release_targets(scaled_off_);
        for (Targets& t : off_views_) release_targets(t);
        off_views_.clear();
    }
    void set_render_scale(float scale) override {   // effects2: at 100% the extra targets are freed (the GPU may still read them: wait first)
        const float prev = render_scale_;
        render_scale_ = std::clamp(scale, 0.25f, 2.0f);   // perf4: above 1 supersampled (render_scaled)
        for (Targets* t : {&win_targets_, &off_targets_}) t->ss_failed = false;
        if (render_scale_ != prev) {   // a change: no target set's kept bloom carries over (the 100% set's is from before the scaling)
            for (Targets* t : {&win_targets_, &off_targets_, &scaled_win_, &scaled_off_}) t->bp_valid = false;
            for (auto* v : {&win_views_, &off_views_}) for (Targets& t : *v) t.bp_valid = false;
        }
        // gameopts: not while MIRROR MODE draws through them (every settings edit comes here: they would be freed and made
        // again with a GPU wait each time); render() frees them when the mirrored frames end
        if (std::fabs(render_scale_ - 1.0f) < 1e-3f && !mirror_held_ && (scaled_win_.depth || scaled_off_.depth)) {
            flush_uploads();
            SDL_WaitForGPUIdle(dev_);
            release_targets(scaled_win_);
            release_targets(scaled_off_);
        }
    }

    // offscreen: the resolve target downloaded into `rgba` (submits `cmd` and waits)
    bool read_back(SDL_GPUCommandBuffer* cmd, int w, int h, std::vector<std::uint8_t>& rgba, SDL_GPUTexture* from = nullptr) {
        if (!cmd) return false;
        const std::uint64_t bytes64 = std::uint64_t(std::max(w, 0)) * std::uint64_t(std::max(h, 0)) * 4;
        if (bytes64 == 0 || bytes64 > 0xFFFFFFFFu) { SDL_CancelGPUCommandBuffer(cmd); return false; }
        const std::uint32_t bytes = std::uint32_t(bytes64);
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes, 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        SDL_GPUCopyPass* cp = tb ? SDL_BeginGPUCopyPass(cmd) : nullptr;
        if (!cp) {   // the frame's passes are recorded: submit them (the next frame expects their results), no read-back
            SDL_SubmitGPUCommandBuffer(cmd);
            if (tb) SDL_ReleaseGPUTransferBuffer(dev_, tb);
            return false;
        }
        SDL_GPUTextureRegion reg{};
        reg.texture = from ? from : off_targets_.resolve;   // (paint3: draw_picture's own)
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
        if (!src) { SDL_ReleaseGPUTransferBuffer(dev_, tb); return false; }
        bool ok = true;
        try {
            rgba.assign(src, src + bytes);
        } catch (const std::bad_alloc&) {
            ok = false;
        }
        SDL_UnmapGPUTransferBuffer(dev_, tb);
        SDL_ReleaseGPUTransferBuffer(dev_, tb);
        return ok;
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
        bool shown = false;   // the scene holds a finished frame (present ran; no_frame shows it again)
        bool ss_failed = false;   // perf4: this output set's supersampled targets could not be made (drawn at 100% until the
                                  // size or the scale changes; per set: an F12 shot's failure leaves the window's alone)
        // photo2 (FrameParams::Dof): the depth pre-pass (single-sample, sampled), its colour target (writes off), the
        // half-size buffers W (prep) and G (gather); made on first use
        SDL_GPUTexture *dof_depth = nullptr, *dof_colour = nullptr;
        // scenedepth: the scene pass's depth target (R32F, the scene's sample count) and its single-sample resolve (MSAA)
        SDL_GPUTexture *sd = nullptr, *sd_res = nullptr, *sd_fix = nullptr;   // sd_fix: the resolve with its edges fixed (sdfix.frag)
        Buf dw, dg;
        Buf ao;   // surface (FrameParams::Surface::ao): the half-size visibility (r) and view distance (g)
        Buf ao2;  // nightfix: the same after the half-size blur (ao.frag mode 3), what the composite reads
        Buf nc;   // night mode without MSAA: the copy of the frame the grade reads (with MSAA it reads the resolve)
        // photoblur (FrameParams::Motion): the streaks V and the accumulation A (full size), the tile max and the
        // neighbour max (one texel per MotionTile pixels), the frames A holds and what they were drawn from (motion_sig)
        Buf mv, ma, mt, mn;
        Buf mir;   // gameopts (FrameParams::mirror_x): the 3D picture reversed left to right, what the composite copies
        int ma_frames = 0;
        std::uint64_t ma_sig = 0;
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
#if defined(_WIN32)
        } else if (formats & SDL_GPU_SHADERFORMAT_DXIL) {   // d3d12: the DXIL made from this SPIR-V (shaders_gen.h kDxil)
            const shaders::Dxil* d = nullptr;
            for (const shaders::Dxil& x : shaders::kDxil)
                if (x.spv == spv) { d = &x; break; }
            if (!d) return nullptr;
            si.format = SDL_GPU_SHADERFORMAT_DXIL;
            si.code = d->code;
            si.code_size = d->size;
            si.entrypoint = "main";
#endif
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
#if defined(_WIN32)
        const SDL_GPUShaderFormat ours = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL;
#else
        const SDL_GPUShaderFormat ours = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
#endif
        if (!(formats & ours)) {
            if (err) *err = "the GPU driver accepts none of the shader formats this build has (SPIR-V, MSL, DXIL on Windows)";
            return false;
        }
        using namespace shaders;
        vs_ = shader(mesh_vert_spv, sizeof mesh_vert_spv, mesh_vert_msl, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
        fs_ = shader(mesh_frag_spv, sizeof mesh_frag_spv, mesh_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
        fsq_vs_ = shader(fullscreen_vert_spv, sizeof fullscreen_vert_spv, fullscreen_vert_msl, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
        spec_fs_ = shader(specular_frag_spv, sizeof specular_frag_spv, specular_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
        present_fs_ = shader(present_frag_spv, sizeof present_frag_spv, present_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
        car_vs_ = shader(car_vert_spv, sizeof car_vert_spv, car_vert_msl, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
        car_fs_ = shader(car_frag_spv, sizeof car_frag_spv, car_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 1);   // reflect-tunnel: + the world env map; carpaint: + the paint mask
        // scenedepth: the scene pass's depth-writing forms of the track and car shaders (made now; used only on frames whose
        // scene pass keeps its depth: sd_)
        if (SDL_GPUShader* d = fs_ ? shader(mesh_depth_frag_spv, sizeof mesh_depth_frag_spv, mesh_depth_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1) : nullptr)
            sd_.depth_fs[fs_] = d;
        if (SDL_GPUShader* d = car_fs_ ? shader(car_depth_frag_spv, sizeof car_depth_frag_spv, car_depth_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 1) : nullptr)
            sd_.depth_fs[car_fs_] = d;
        sun_vs_ = shader(sun_vert_spv, sizeof sun_vert_spv, sun_vert_msl, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
        sun_fs_ = shader(sun_frag_spv, sizeof sun_frag_spv, sun_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
        blur_fs_ = shader(blur_frag_spv, sizeof blur_frag_spv, blur_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
        post_fs_ = shader(post_frag_spv, sizeof post_frag_spv, post_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
        post_lean_fs_ = shader(post_lean_frag_spv, sizeof post_lean_frag_spv, post_lean_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);   // the one-draw composite: a failure only turns that form off
        light_fs_ = shader(light_frag_spv, sizeof light_frag_spv, light_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);   // night mode's headlight pools: a failure only leaves them out
        emissive_fs_ = shader(emissive_frag_spv, sizeof emissive_frag_spv, emissive_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);   // nightlights' fixtures: a failure leaves them as graded
        beam_fs_ = shader(beam_frag_spv, sizeof beam_frag_spv, beam_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 1);   // nightlights' projected beams: a failure leaves no beams (the pools are the presets' fallback)
        night_fs_ = shader(night_frag_spv, sizeof night_frag_spv, night_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);   // night mode's grade and sky: a failure leaves the frame as the day's
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

    // Night mode (remaster-features §17). The grade and the night sky: shaders/night.frag over the full-screen triangle
    // (z = 0: the far plane of the reverse-Z projection), reading a single-sample copy of the frame and writing RGB only
    // (the frame alpha kept), Z tested and not written: `sky` = GEQUAL passes where the depth is still the far plane (the
    // sky, the clear), else LESS passes where something was drawn. The headlight pools: mesh.vert + light.frag, the
    // restore blend (restore_blend: frame alpha kept), Z GEQUAL, not written, no culling. The projected beams
    // (nightlights): fullscreen.vert + beam.frag, the restore blend, Z LESS (not the sky), not written.
    SDL_GPUGraphicsPipeline* night_pipeline(bool sky) {
        SDL_GPUGraphicsPipeline*& p = night_pipe_[sky ? 1 : 0];
        if (p || !night_fs_) return p;
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        ct.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
        ct.blend_state.enable_color_write_mask = true;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = fsq_vs_;
        pi.fragment_shader = night_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = sample_count(msaa_);
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = false;
        pi.depth_stencil_state.compare_op = sky ? SDL_GPU_COMPAREOP_GREATER_OR_EQUAL : SDL_GPU_COMPAREOP_LESS;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: night pipeline: %s", SDL_GetError());
        return p;
    }
    // nightlights' blend: Cs x As + Cd x (1 - As) on RGB (the pre-grade colour x the lamp over the graded frame), the frame
    // alpha kept
    static void restore_blend(SDL_GPUColorTargetDescription& ct) {
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        ct.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
        ct.blend_state.enable_color_write_mask = true;
    }
    SDL_GPUGraphicsPipeline* beam_pipeline() {
        if (beam_pipe_ || !beam_fs_) return beam_pipe_;
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        restore_blend(ct);
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = fsq_vs_;
        pi.fragment_shader = beam_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = sample_count(msaa_);
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = false;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;   // reverse-Z, z = 0: where something was drawn
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        beam_pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!beam_pipe_) SDL_Log("b3gfx: headlight beam pipeline: %s", SDL_GetError());
        return beam_pipe_;
    }
    SDL_GPUGraphicsPipeline* light_pipeline() {
        if (light_pipe_ || !light_fs_) return light_pipe_;
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
        restore_blend(ct);
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs_;
        pi.fragment_shader = light_fs_;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = at;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = sample_count(msaa_);
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = false;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;   // reverse-Z
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        light_pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!light_pipe_) SDL_Log("b3gfx: headlight pipeline: %s", SDL_GetError());
        return light_pipe_;
    }
    // nightlights' fixtures: mesh.vert + emissive.frag, the restore blend, Z GEQUAL (their own first draw), not written
    SDL_GPUGraphicsPipeline* emissive_pipeline() {
        if (emissive_pipe_ || !emissive_fs_) return emissive_pipe_;
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
        restore_blend(ct);
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs_;
        pi.fragment_shader = emissive_fs_;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = at;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = sample_count(msaa_);
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = false;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;   // reverse-Z
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        emissive_pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!emissive_pipe_) SDL_Log("b3gfx: night fixture pipeline: %s", SDL_GetError());
        return emissive_pipe_;
    }
    // night mode: a post-fog effect that gives light (DrawItem::night_glow: coronas, sparks, fire, the flash) keeps it;
    // the rest of the post-fog stage (smoke and dust are additive too) is graded with the frame
    static bool night_additive(const DrawItem& it) { return it.effect && it.night_glow; }
    struct NightUniforms { float inv[16]; float a[4], tint[4], sky[4], zenith[4], horizon[4], moon[4], moon_col[4], eye[4]; };
    // One of the two grade draws (sky: the night sky where the far plane shows, else the world's grade) reading `src`.
    // `tunnel`: the world's grade taken back by this share over the whole world (the tunnel fade without the spot pass)
    void draw_night_grade(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const FrameParams& fp, SDL_GPUTexture* src, bool sky,
                          SDL_GPUGraphicsPipeline*& bound, float tunnel = 0.0f) {
        SDL_GPUGraphicsPipeline* p = night_pipeline(sky);
        if (!p || !src) return;
        const FrameParams::Night& n = fp.night;
        NightUniforms u{};
        if (!mat4_invert(fp.view_proj, u.inv)) mat4_identity(u.inv);
        const float a[4] = {sky ? 1.0f : 0.0f, n.sat, n.gamma, std::max(n.pivot, 0.01f)};
        std::memcpy(u.a, a, sizeof a);
        const bool moon = n.moon_colour[0] + n.moon_colour[1] + n.moon_colour[2] > 0;
        for (int k = 0; k < 3; ++k) {
            u.tint[k] = n.world[k];
            u.sky[k] = n.sky[k];
            u.zenith[k] = n.zenith[k];
            u.horizon[k] = n.horizon[k];
            u.moon[k] = n.moon_dir[k];
            u.moon_col[k] = n.moon_colour[k];
            u.eye[k] = fp.eye[k];
        }
        u.tint[3] = n.cloud;
        u.sky[3] = n.stars;
        u.moon[3] = moon ? n.moon_size : 0.0f;
        u.moon_col[3] = n.halo;
        u.eye[3] = sky ? 0.0f : std::clamp(tunnel, 0.0f, 1.0f);
        SDL_BindGPUGraphicsPipeline(pass, p);
        bound = p;
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
        SDL_GPUTextureSamplerBinding tsb{src, clamp_sampler_};
        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    }
    // The headlight pools (DrawItem::night_light): the pre-grade copy `src` x the lamp, blended by the beam's alpha.
    void draw_night_lights(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const FrameParams& fp, const std::vector<DrawItem>& items,
                           SDL_GPUTexture* src, SDL_GPUGraphicsPipeline*& bound, BufferId& bound_vb, BufferId& bound_ib) {
        SDL_GPUGraphicsPipeline* lp = light_pipeline();
        if (!lp || !src) return;
        for (const DrawItem& it : items) {
            if (it.overlay || !it.night_light || !it.index_count || it.vertices >= buffers_.size() || it.indices >= buffers_.size()) continue;
            if (lp != bound) {
                SDL_BindGPUGraphicsPipeline(pass, lp);
                bound = lp;
                SDL_GPUTextureSamplerBinding tsb{src, clamp_sampler_};
                SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
            }
            bind_buffers(pass, it, bound_vb, bound_ib);
            VertexUniforms vu{};
            std::memcpy(vu.view_proj, fp.view_proj, sizeof vu.view_proj);
            std::memcpy(vu.model, it.model, sizeof vu.model);
            vu.inst[3] = -1.0f;
            SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
            SDL_PushGPUFragmentUniformData(cmd, 0, it.night_shape, sizeof it.night_shape);
            ++stats_.draws;
            SDL_DrawGPUIndexedPrimitives(pass, it.index_count, 1, it.first_index, 0, 0);
        }
    }

    // nightlights (§17.2): the projected beams and the tunnel's fade, one full-screen draw (beam.frag) reading the pre-grade
    // copy `src` and the depth pre-pass, scissored to the beams' screen bounds (the whole target while the tunnel fade
    // is on, or when a beam's hull reaches behind the eye)
    // nightlights (§17.2): the fixtures (DrawItem::night_emissive) drawn again after the grade and the night sky: their
    // bright texels at the pre-grade colour (emissive.frag), over mesh.vert with the same vertex uniforms as draw_item
    // `day` (TUNNEL LIGHTS, §35.4): the DrawItem::day_glow draws instead, by day (emissive.frag mode 8: no frame copy read)
    void draw_night_emissive(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const FrameParams& fp, const std::vector<DrawItem>& items,
                             SDL_GPUTexture* src, SDL_GPUGraphicsPipeline*& bound, BufferId& bound_vb, BufferId& bound_ib, bool day = false) {
        for (const DrawItem& it : items)
            if (!draw_emissive_item(pass, cmd, fp, it, src, bound, bound_vb, bound_ib, day)) return;
    }
    // one item of draw_night_emissive (skipped when it is not one; false: no pipeline, stop)
    bool draw_emissive_item(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const FrameParams& fp, const DrawItem& it,
                            SDL_GPUTexture* src, SDL_GPUGraphicsPipeline*& bound, BufferId& bound_vb, BufferId& bound_ib, bool day) {
        {
            // (by day: the tunnels' fittings)
            if (!(day ? it.day_glow == 1 : it.night_emissive != 0) || it.overlay || it.effect || it.post_fog || !it.textured || !it.index_count ||
                it.vertices >= buffers_.size() || it.indices >= buffers_.size() || it.shading != Shading::kTrack)
                return true;
            SDL_GPUGraphicsPipeline* p = emissive_pipeline();
            if (!p) return false;
            if (p != bound) { SDL_BindGPUGraphicsPipeline(pass, p); bound = p; }
            bind_buffers(pass, it, bound_vb, bound_ib);
            SDL_GPUTextureSamplerBinding tsb[2] = {{tex_of(it.texture), sampler_}, {src, clamp_sampler_}};
            SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
            VertexUniforms vu{};
            std::memcpy(vu.view_proj, fp.view_proj, sizeof vu.view_proj);
            std::memcpy(vu.model, it.model, sizeof vu.model);
            for (int k = 0; k < 3; ++k) {
                vu.eye[k] = fp.eye[k];
                vu.right[k] = fp.right[k];
                vu.sun[k] = fp.to_sun[k];
            }
            vu.shade[0] = float(int(it.shading));
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
            // 2: a backlit panel, 3: a front-lit billboard (nightfix): every texel (the ramp below any texel)
            const bool whole = it.night_emissive >= 2, board = it.night_emissive == 3;
            const float* bt = fp.night.board;
            const float a[16] = {day ? fp.day_glow : fp.night.fixture[0], whole ? -2.0f : fp.night.fixture[1], whole ? -1.0f : fp.night.fixture[2],
                                 it.blend == Blend::kAlphaTest ? it.alpha_ref : 0.0f,
                                 board ? bt[0] : 1.0f, board ? bt[1] : 1.0f, board ? bt[2] : 1.0f, board ? std::clamp(bt[3], 0.0f, 1.0f) : 1.0f,
                                 // round 3: the mode (4 windows, 5 shop front), the texture's mean light, its windows' grid
                                 day ? 8.0f : float(it.night_emissive), float(it.night_mean) / 255.0f, float(std::max<int>(it.night_cols, 1)),
                                 float(std::max<int>(it.night_rows, 1)),
                                 // round 3b: the camera (retro-reflective signs); xboxglow (§35.5): w 1 the texture's alpha is the
                                 // glow mask, 2 that and the Xbox look (by day: self-lit, opaque)
                                 fp.eye[0], fp.eye[1], fp.eye[2],
                                 it.glow_mask ? (day && fp.day_glow_mode >= 2 ? 2.0f : 1.0f) : (day && fp.day_glow_mode >= 2 ? 3.0f : 0.0f)};
            SDL_PushGPUFragmentUniformData(cmd, 0, a, sizeof a);
            ++stats_.draws;
            SDL_DrawGPUIndexedPrimitives(pass, it.index_count, 1, it.first_index, 0, 0);
        }
        return true;
    }
    // reflect2 G: the env pass's lamps at night: the fixtures of `ep` in face `f` through the emissive pipeline (as
    // draw_night_emissive), their glow from a constant warm source (env_glow_src_, the size of the atlas: emissive.frag
    // fetches it at the pixel) x ep.night_glow
    // (before render_env's pass: its upload goes with flush_uploads)
    void ensure_env_glow(const EnvPass& ep) {
        const std::uint32_t w = 3u * std::uint32_t(ep.face), h = 2u * std::uint32_t(ep.face);
        if (env_glow_src_ && env_glow_w_ >= w && env_glow_h_ >= h) return;
        if (env_glow_src_) SDL_ReleaseGPUTexture(dev_, env_glow_src_);
        env_glow_src_ = nullptr;
        std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
        for (std::size_t i = 0; i < px.size(); i += 4) {
            for (int c = 0; c < 3; ++c) px[i + std::size_t(c)] = std::uint8_t(std::clamp(ep.glow_colour[c], 0.0f, 1.0f) * 255.0f + 0.5f);
            px[i + 3] = 255;
        }
        const TextureId id = create_texture({TextureLevel{int(w), int(h), px.data()}});
        if (id == kNone) return;
        env_glow_src_ = textures_[id];
        textures_[id] = nullptr;
        if (std::size_t(id) + 1 == textures_.size()) textures_.pop_back();
        env_glow_w_ = w;
        env_glow_h_ = h;
    }
    void env_night_glow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const FrameParams& fp, const EnvPass& ep, int f,
                        SDL_GPUGraphicsPipeline*& bound, BufferId& bound_vb, BufferId& bound_ib) {
        if (!env_glow_src_) return;   // (made before the pass: ensure_env_glow)
        FrameParams g = fp;
        g.night.fixture[0] = ep.night_glow;
        g.night.fixture[1] = ep.glow_ramp[0];
        g.night.fixture[2] = ep.glow_ramp[1];
        std::memcpy(g.night.board, ep.glow_board, sizeof g.night.board);
        // (the fixtures, backlit panels and front-lit boards: modes 1-3; the lit windows and shop fronts keep their look)
        for (std::size_t i = 0; i < ep.items.size(); ++i) {
            const DrawItem& it = ep.items[i];
            if (it.night_emissive < 1 || it.night_emissive > 3 || !(ep.faces.empty() || (i < ep.faces.size() && (ep.faces[i] >> f & 1)))) continue;
            if (!draw_emissive_item(pass, cmd, g, it, env_glow_src_, bound, bound_vb, bound_ib, false)) return;
        }
    }
    struct BeamUniforms { float inv[16], eye[4], shape[4], core[4], range[4], tint[4], tun[4], lpos[8][4], ldir[8][4], sun_vp[16], sun_p[4], sshape[4], srange[4], spos[32][4], scol[32][4], bshape[4], bpos[16][4], bdir[16][4]; };
    // `moon_shadow`: SUN SHADOWS' map is live this frame and its receiver term is this pass's (night: the track draws took none)
    void draw_night_beams(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Targets& t, const FrameParams& fp, SDL_GPUTexture* src,
                          SDL_GPUGraphicsPipeline*& bound, bool moon_shadow = false) {
        SDL_GPUGraphicsPipeline* p = beam_pipeline();
        const FrameParams::Night& n = fp.night;
        const int lamps = std::clamp(n.lamps, 0, int(FrameParams::Night::kMaxLamps));
        const float tun = std::clamp(n.tunnel[0] * n.enclosed, 0.0f, 1.0f);
        const int streets = std::clamp(n.streets, 0, int(FrameParams::Night::kMaxStreet));
        const int boards = std::clamp(n.boards, 0, int(FrameParams::Night::kMaxBoard));
        if (!p || !src || !depth_src_ || (lamps == 0 && streets == 0 && boards == 0 && tun <= 1e-3f && !moon_shadow)) return;
        BeamUniforms u{};
        if (!mat4_invert(fp.view_proj, u.inv)) return;
        for (int k = 0; k < 3; ++k) {
            u.eye[k] = fp.eye[k];
            u.tint[k] = n.lamp_colour[k];
        }
        u.eye[3] = float(lamps);
        std::memcpy(u.shape, n.spot_shape, sizeof u.shape);
        std::memcpy(u.core, n.spot_core, sizeof n.spot_core);
        u.range[0] = n.spot_range[0];
        u.range[1] = n.spot_range[1];
        u.range[2] = 1.0f / float(std::max<std::uint32_t>(t.w, 1));
        u.range[3] = 1.0f / float(std::max<std::uint32_t>(t.h, 1));
        u.tun[0] = tun;
        u.tun[1] = n.tunnel[1];
        u.tun[2] = n.tunnel[2];
        u.tun[3] = std::clamp(fp.surface.wet, 0.0f, 1.0f);   // weather (§38): the wet road's glints in the pools
        if (moon_shadow && sh_map_) {   // SUN SHADOWS at night (as draw_item's receiver uniforms)
            const FrameParams::SunShadow& ss = fp.sun_shadow;
            std::memcpy(u.sun_vp, ss.view_proj, sizeof u.sun_vp);
            u.sun_p[0] = float(sh_side_);
            u.sun_p[1] = ss.depth * std::clamp(ss.rise, 0.05f, 1.0f);
            u.sun_p[2] = ss.filter == 1 ? 1.0f : 0.0f;
            u.sun_p[3] = std::clamp(ss.strength, 0.0f, 1.0f);
        }
        // the scissor: the beams' hulls (the lamp, the far end's square) projected
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        // street lights (their pools), each lamp's position and tint
        std::memcpy(u.sshape, n.street_shape, sizeof u.sshape);
        u.srange[0] = n.street_range[0];
        u.srange[1] = n.street_range[1];
        u.srange[2] = float(streets);
        for (int i = 0; i < streets; ++i) {
            for (int k = 0; k < 3; ++k) {
                u.spos[i][k] = n.street[i].pos[k];
                u.scol[i][k] = n.street[i].colour[k];
            }
            u.spos[i][3] = std::clamp(n.street[i].gain, 0.0f, 1.0f);   // nightfix: the fade at the cap
            u.scol[i][3] = std::clamp(n.street[i].scale, 0.25f, 4.0f);  // nightfix: the pool's size (the lamp's height)
        }
        // billboard floodlights (nightfix): position (w: the board's height), axis (w: gain), the cone and the tint
        u.bshape[0] = n.board_shape[0];
        u.bshape[1] = n.board_shape[1];
        u.bshape[2] = n.board_shape[2];
        u.bshape[3] = float(boards);
        for (int i = 0; i < boards; ++i) {
            for (int k = 0; k < 3; ++k) {
                u.bpos[i][k] = n.board_lamp[i].pos[k];
                u.bdir[i][k] = n.board_lamp[i].axis[k];
            }
            u.bpos[i][3] = std::max(n.board_lamp[i].size, 0.5f);
            u.bdir[i][3] = std::clamp(n.board_lamp[i].gain, 0.0f, 1.0f);
        }
        bool full = tun > 1e-3f || u.sun_p[3] > 0.0f || streets > 0 || boards > 0;   // the tunnel fade, the moon shadow, street lights, boards: the whole view
        const float wide = n.spot_range[1] * std::sqrt(std::max(0.0f, 1.0f - n.spot_shape[1] * n.spot_shape[1])) / std::max(n.spot_shape[1], 0.05f);
        for (int i = 0; i < lamps; ++i) {
            const FrameParams::Night::Lamp& l = n.lamp[i];
            for (int k = 0; k < 3; ++k) {
                u.lpos[i][k] = l.pos[k];
                u.ldir[i][k] = l.dir[k];
            }
            u.lpos[i][3] = std::clamp(l.gain, 0.0f, 2.0f);   // (1.3.0: the players' beams up to 1.5x)
            if (full) continue;
            // two axes across the beam
            const float* d = l.dir;
            float a[3] = {std::fabs(d[1]) < 0.9f ? 0.0f : 1.0f, std::fabs(d[1]) < 0.9f ? 1.0f : 0.0f, 0.0f};
            float r[3] = {a[1] * d[2] - a[2] * d[1], a[2] * d[0] - a[0] * d[2], a[0] * d[1] - a[1] * d[0]};
            const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            for (float& c : r) c /= std::max(rl, 1e-6f);
            const float q[3] = {d[1] * r[2] - d[2] * r[1], d[2] * r[0] - d[0] * r[2], d[0] * r[1] - d[1] * r[0]};
            for (int c = 0; c < 5; ++c) {
                float w[3];
                for (int k = 0; k < 3; ++k) {
                    w[k] = l.pos[k];
                    if (c > 0) w[k] += d[k] * n.spot_range[1] + (r[k] * ((c & 1) ? 1.0f : -1.0f) + q[k] * ((c & 2) ? 1.0f : -1.0f)) * wide;
                }
                const float* m = fp.view_proj;
                const float cx = m[0] * w[0] + m[4] * w[1] + m[8] * w[2] + m[12];
                const float cy = m[1] * w[0] + m[5] * w[1] + m[9] * w[2] + m[13];
                const float cw = m[3] * w[0] + m[7] * w[1] + m[11] * w[2] + m[15];
                if (cw < 0.05f) { full = true; break; }
                const float sx = (cx / cw * 0.5f + 0.5f) * float(t.w), sy = (0.5f - cy / cw * 0.5f) * float(t.h);
                x0 = std::min(x0, sx); x1 = std::max(x1, sx);
                y0 = std::min(y0, sy); y1 = std::max(y1, sy);
            }
        }
        SDL_Rect r{0, 0, int(t.w), int(t.h)};
        if (!full) {
            const int ix0 = std::clamp(int(std::floor(x0)) - 2, 0, int(t.w)), iy0 = std::clamp(int(std::floor(y0)) - 2, 0, int(t.h));
            const int ix1 = std::clamp(int(std::ceil(x1)) + 2, 0, int(t.w)), iy1 = std::clamp(int(std::ceil(y1)) + 2, 0, int(t.h));
            if (ix1 <= ix0 || iy1 <= iy0) return;
            r = SDL_Rect{ix0, iy0, ix1 - ix0, iy1 - iy0};
        }
        SDL_BindGPUGraphicsPipeline(pass, p);
        bound = p;
        if (!full) SDL_SetGPUScissor(pass, &r);
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
        SDL_GPUTextureSamplerBinding tsb[3] = {{src, clamp_sampler_}, {depth_src_, clamp_sampler_},
                                               {moon_shadow && sh_map_ ? sh_map_ : depth_src_, clamp_sampler_}};
        SDL_BindGPUFragmentSamplers(pass, 0, tsb, 3);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        ++stats_.draws;
        if (!full) {
            const SDL_Rect all{0, 0, int(t.w), int(t.h)};
            SDL_SetGPUScissor(pass, &all);
        }
    }

    // zwrite: blended draws that write Z (the post-fog stage); slots 6 / 7 (culled)
    // `fs` (SUN SHADOWS: the receivers' mesh.frag variant): a new pipeline with that fragment shader, not cached here
    // (recv_pipeline keeps it)
    // `keep_alpha` (whitesky, keeps_frame_alpha): a blended draw that leaves the frame alpha as it is (the GS's FBMSK alpha:
    // the sky's clouds), slots 8 / 9
    SDL_GPUGraphicsPipeline* pipeline(SDL_GPUTextureFormat fmt, Blend blend, bool cull, bool car = false, bool zwrite = false,
                                      SDL_GPUShader* fs = nullptr, bool keep_alpha = false) {
        auto& slot = pipelines_[fmt];
        zwrite = zwrite && blend == Blend::kAlpha;
        keep_alpha = keep_alpha && blend == Blend::kAlpha && !zwrite && !car && !fs;
        SDL_GPUGraphicsPipeline* made = nullptr;
        SDL_GPUGraphicsPipeline*& p = fs ? made : car ? car_pipe_ : slot[keep_alpha ? 8 + (cull ? 1 : 0) : zwrite ? 6 + (cull ? 1 : 0) : int(blend) + (cull ? 3 : 0)];
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
            // The GS writes the source alpha into the frame alpha, unblended; with FBMSK alpha (keep_alpha: the clouds)
            // the frame alpha stays as it is
            ct.blend_state.src_alpha_blendfactor = keep_alpha ? SDL_GPU_BLENDFACTOR_ZERO : SDL_GPU_BLENDFACTOR_ONE;
            ct.blend_state.dst_alpha_blendfactor = keep_alpha ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ZERO;
            ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = car ? car_vs_ : vs_;
        pi.fragment_shader = fs ? fs : car ? car_fs_ : fs_;
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
    // A draw whose blend subtracts (Cd - Cs): the line meshes, and the 2D stage's ALPHA A = Cd, B = Cs, D = Cd (0x42 ...).
    // The GS clamps each blend's result at 0 (COLCLAMP); the RGBA16F frame keeps the negative colour, which the next blend,
    // the bloom (it reads the frame) or the upscale would then see: clamp_frame before the next draw that reads it.
    static bool subtracts(const DrawItem& it) {
        if (is_line(it)) return true;
        if (!it.overlay || !it.overlay_blend || it.alpha_only) return false;
        const int a = it.gs_alpha & 3, b = (it.gs_alpha >> 2) & 3, d = (it.gs_alpha >> 6) & 3;
        return a == 2 && b == 0 && d == 1;
    }
    // Cd = max(Cd, 0) on RGB in place: a full-screen draw of colour 0 (specular.frag) with the MAX blend; no pass break.
    // B3_SUB_CLAMP=0 leaves the negative colour (the look before the clamp).
    void clamp_frame(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, SDL_GPUGraphicsPipeline*& bound) {
        static const bool off = [] { const char* e = std::getenv("B3_SUB_CLAMP"); return e && e[0] == '0'; }();
        if (off) return;
        if (!clamp_pipeline()) return;
        SDL_BindGPUGraphicsPipeline(pass, clamp_pipe_);
        bound = clamp_pipe_;
        const float zero[4] = {0, 0, 0, 0};
        SDL_PushGPUFragmentUniformData(cmd, 0, zero, sizeof zero);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    }
    SDL_GPUGraphicsPipeline* clamp_pipeline() {
        if (!clamp_pipe_) {
            SDL_GPUColorTargetDescription ct{};
            ct.format = kSceneFormat;
            ct.blend_state.enable_blend = true;
            ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_MAX;
            ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            ct.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
            ct.blend_state.enable_color_write_mask = true;
            SDL_GPUGraphicsPipelineCreateInfo pi{};
            pi.vertex_shader = fsq_vs_;
            pi.fragment_shader = spec_fs_;
            pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            pi.multisample_state.sample_count = sample_count(msaa_);
            pi.target_info.color_target_descriptions = &ct;
            pi.target_info.num_color_targets = 1;
            pi.target_info.depth_stencil_format = depth_format_;
            pi.target_info.has_depth_stencil_target = true;
            clamp_pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
            if (!clamp_pipe_) SDL_Log("b3gfx: clamp pipeline: %s", SDL_GetError());
        }
        return clamp_pipe_;
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
        static const bool cull_off = [] { const char* e = std::getenv("B3_SUN_CULL"); return e && e[0] == '0'; }();
        for (const DrawItem& it : items) {
            if (it.overlay || it.effect || it.shading == Shading::kSky) continue;
            if (it.blend == Blend::kAlpha && it.shading != Shading::kCar && !it.post_fog) continue;
            if (!cull_off && outside_box(op.view_proj, it)) { ++occ_skipped_; continue; }   // perf4: it cannot cover any of the box
            draw_item(pass, cmd, op, it, ld, bound, bvb, bib, true);
            ++occ_drawn_;
        }
        ++occ_frames_;
        SDL_EndGPURenderPass(pass);
        SDL_GenerateMipmapsForGPUTexture(cmd, occ_);
        return true;
    }
    // perf4: the item's box (DrawItem::bounds through its model) wholly outside one plane of `vp`'s clip volume (x, y = +-w,
    // or behind the eye): no fragment of it lands on the target. Conservative: a margin of 1e-3 w (a fraction of an
    // occlusion texel), items without a box and the line meshes (widened on the GPU by pixels) are never left out.
    static bool outside_box(const float vp[16], const DrawItem& it) {
        if (!(it.bounds[0] <= it.bounds[3] && it.bounds[1] <= it.bounds[4] && it.bounds[2] <= it.bounds[5]) || is_line(it)) return false;
        // cars: the box padded by 0.5 m (a crash-deformed car at the box's edge; this pass draws them undeformed, but the
        // margin costs nothing)
        const float pad = it.shading == Shading::kCar ? 0.5f : 0.0f;
        const float b[6] = {it.bounds[0] - pad, it.bounds[1] - pad, it.bounds[2] - pad, it.bounds[3] + pad, it.bounds[4] + pad, it.bounds[5] + pad};
        float m[16];
        mat4_mul(vp, it.model, m);
        int out[5] = {0, 0, 0, 0, 0};
        for (int c = 0; c < 8; ++c) {
            const float x = b[(c & 1) ? 3 : 0], y = b[(c & 2) ? 4 : 1], z = b[(c & 4) ? 5 : 2];
            const float cx = m[0] * x + m[4] * y + m[8] * z + m[12], cy = m[1] * x + m[5] * y + m[9] * z + m[13];
            const float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
            if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cw)) return false;
            const float e = 1e-3f * std::fabs(cw) + 1e-6f;
            out[0] += cx < -cw - e; out[1] += cx > cw + e; out[2] += cy < -cw - e; out[3] += cy > cw + e; out[4] += cw < -e;
        }
        for (int k = 0; k < 5; ++k) if (out[k] == 8) return true;
        return false;
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
        SDL_GPUTextureSamplerBinding tsb[2] = {{tex_of(fp.sun_texture), effect_sampler_}, {occ_ ? occ_ : white_, effect_sampler_}};
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
    // screenshot_begin: the window's targets that `t` (an offscreen set) stands for this frame (nullptr: none)
    Targets* window_twin(const Targets& t) {
        if (&t == &off_targets_) return &win_targets_;
        if (&t == &scaled_off_) return &scaled_win_;
        for (std::size_t k = 0; k < off_views_.size(); ++k)
            if (&t == &off_views_[k]) return k < win_views_.size() ? &win_views_[k] : nullptr;
        return nullptr;
    }
    SDL_GPUTexture* bloom_chain(SDL_GPUCommandBuffer* cmd, Targets& t, const FrameParams& fp, SDL_GPUTexture* src) {
        const auto& bl = fp.bloom;
        std::uint32_t hw, hh, qw, qh;
        game_size(t, 2, hw, hh);
        game_size(t, 4, qw, qh);
        if (!ensure_buf(t.bh, hw, hh) || !ensure_buf(t.bq[0], qw, qh) || !ensure_buf(t.bq[1], qw, qh)) return nullptr;
        if (t.bp[0].w != qw || t.bp[0].h != qh || bl.restart) t.bp_valid = false;
        if (!ensure_buf(t.bp[0], qw, qh) || !ensure_buf(t.bp[1], qw, qh)) return nullptr;
        if (shot_seed_ && !t.bp_valid && !bl.restart) {   // screenshot_begin: P from the window's (the frame on screen)
            const Targets* wt = window_twin(t);
            if (wt && wt->bp_valid && wt->bp[wt->bp_cur].t && wt->bp[wt->bp_cur].w == qw && wt->bp[wt->bp_cur].h == qh) {
                if (SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd)) {
                    SDL_GPUTextureLocation from{wt->bp[wt->bp_cur].t, 0, 0, 0, 0, 0}, to{t.bp[t.bp_cur].t, 0, 0, 0, 0, 0};
                    SDL_CopyGPUTextureToTexture(cp, &from, &to, qw, qh, 1, false);
                    SDL_EndGPUCopyPass(cp);
                    t.bp_valid = true;
                }
            }
        }
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
    // (round 3e) the pre-pass's targets (the single-sample depth the passes read, its scratch colour), made once
    bool ensure_dof_targets(Targets& t) {
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
        return true;
    }
    // scenedepth: the two-target forms of every pipeline drawing into the scene's target at its sample count (the masked
    // form of each; the depth-writing and depth-only forms of those with a SCENE_DEPTH shader), made now instead of at a
    // frame's first use
    void scene_depth_warm(const char* when) {
        const std::uint64_t t0 = SDL_GetTicksNS();
        std::vector<SDL_GPUGraphicsPipeline*> scene;
        for (const auto& kv : sd_.info)
            if (kv.second.pi.multisample_state.sample_count == sample_count(msaa_)) scene.push_back(kv.first);
        int n = 0;
        for (SDL_GPUGraphicsPipeline* p : scene) n += scene_depth_forms(sd_, p);
        SDL_Log("b3gfx: scene depth: %d two-target pipelines made %s (%.0f ms)%s", n, when, double(SDL_GetTicksNS() - t0) * 1e-6,
                sd_.broken ? "; one could not be made: the depth pre-pass from now on" : "");
    }
    // nightdepth: at a night race's load (View::load_prepared), the forms not made at start
    void prepare_scene_depth() override {
        if (!scene_depth_on_ || sd_.eager || sd_.broken) return;
        sd_.eager = true;   // (and the pipelines made from now on get theirs with them, scene_depth_keep)
        scene_depth_warm("at a night race's load");
    }
    // scenedepth: the scene depth's targets, made once (false: not on this device: the pre-pass instead)
    bool ensure_scene_depth(Targets& t) {
        if (t.sd) return true;
        if (sd_failed_) return false;
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.width = t.w;
        ti.height = t.h;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        ti.format = kSceneDepthFormat;
        ti.sample_count = sample_count(msaa_);
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | (msaa_ > 1 ? 0 : SDL_GPU_TEXTUREUSAGE_SAMPLER);
        if (!SDL_GPUTextureSupportsFormat(dev_, ti.format, ti.type, ti.usage) ||
            (msaa_ > 1 && !SDL_GPUTextureSupportsSampleCount(dev_, ti.format, ti.sample_count))) {
            sd_failed_ = true;
            SDL_Log("b3gfx: scene depth: R32F %s not available: the depth pre-pass instead", msaa_ > 1 ? "with MSAA" : "target");
            return false;
        }
        t.sd = SDL_CreateGPUTexture(dev_, &ti);
        if (t.sd && msaa_ > 1) {
            ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            t.sd_res = SDL_CreateGPUTexture(dev_, &ti);
            if (scene_depth_edge() > 0) t.sd_fix = SDL_CreateGPUTexture(dev_, &ti);
        }
        if (!t.sd || (msaa_ > 1 && (!t.sd_res || (scene_depth_edge() > 0 && (!t.sd_fix || !sdfix_pipeline()))))) {
            SDL_Log("b3gfx: scene depth targets: %s (the depth pre-pass instead)", SDL_GetError());
            sd_failed_ = true;
            for (auto* x : {t.sd, t.sd_res, t.sd_fix}) if (x) SDL_ReleaseGPUTexture(dev_, x);
            t.sd = t.sd_res = t.sd_fix = nullptr;
            return false;
        }
        return true;
    }
    // scenedepth, MSAA: the resolved depth's edges fixed (sdfix.frag) into t.sd_fix, the depth the passes then read
    SDL_GPUGraphicsPipeline* sdfix_pipeline() {
        if (sdfix_pipe_) return sdfix_pipe_;
        using namespace shaders;
        if (!sdfix_fs_) sdfix_fs_ = shader(sdfix_frag_spv, sizeof sdfix_frag_spv, sdfix_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
        if (!sdfix_fs_) return nullptr;
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneDepthFormat;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = fsq_vs_;
        pi.fragment_shader = sdfix_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        sdfix_pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!sdfix_pipe_) SDL_Log("b3gfx: scene depth fix pipeline: %s", SDL_GetError());
        return sdfix_pipe_;
    }
    // (opt-in, $B3_SCENE_DEPTH_EDGE=<share>: measured against the pre-pass's pictures at MSAA 4, every form of this fix --
    // the nearest, or the closer of the nearest and farthest, at 2 % .. 25 % -- differed more than the plain resolve, which
    // the passes read by default: their own filtering smooths the averaged edge, a snapped one jumps)
    static float scene_depth_edge() {
        static const float edge = [] { const char* e = std::getenv("B3_SCENE_DEPTH_EDGE"); return e ? float(std::atof(e)) : 0.0f; }();
        return edge;
    }
    void scene_depth_fix(SDL_GPUCommandBuffer* cmd, Targets& t) {
        if (msaa_ <= 1 || scene_depth_edge() <= 0 || !t.sd_res || !t.sd_fix || !sdfix_pipeline()) return;
        const float edge = scene_depth_edge();
        static const float nearest = [] { const char* e = std::getenv("B3_SCENE_DEPTH_NEAREST"); return e && *e == '1' ? 1.0f : 0.0f; }();
        const float u[4] = {1.0f / float(t.w), 1.0f / float(t.h), edge, nearest};
        post_pass(cmd, sdfix_pipe_, t.sd_fix, t.sd_res, nullptr, u, sizeof u, clamp_sampler_);
    }
    bool dof_depth_pass(SDL_GPUCommandBuffer* cmd, Targets& t, const FrameParams& fp, const std::vector<DrawItem>& items) {
        if (!ensure_dof_targets(t)) return false;
        depth_src_ = t.dof_depth;
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

    // ---- SUN SHADOWS (FrameParams::SunShadow, shaders/shadow.frag, mesh.frag's SUN_SHADOW variant) ----
    // The shaders on first use. False (logged once): no sun shadows on this device; the frame is drawn without them.
    bool sun_shadow_ready() {
        if (sh_failed_) return false;
        if (sh_fs_ && sh_recv_fs_) return true;
        using namespace shaders;
        const bool fmt = SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_R32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                                      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        if (fmt) {
            sh_fs_ = shader(shadow_frag_spv, sizeof shadow_frag_spv, shadow_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
            sh_recv_fs_ = shader(mesh_shadow_frag_spv, sizeof mesh_shadow_frag_spv, mesh_shadow_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
            if (SDL_GPUShader* d = sh_recv_fs_ ? shader(mesh_shadow_depth_frag_spv, sizeof mesh_shadow_depth_frag_spv, mesh_shadow_depth_frag_msl,
                                                        SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1) : nullptr)
                sd_.depth_fs[sh_recv_fs_] = d;   // scenedepth
        }
        if (!sh_fs_ || !sh_recv_fs_) {
            SDL_Log("b3gfx: sun shadows unavailable: %s", fmt ? SDL_GetError() : "no R32F colour target");
            sh_failed_ = true;
            return false;
        }
        return true;
    }
    void release_sun_shadow_map() {
        for (SDL_GPUTexture* t : {sh_map_, sh_depth_}) if (t) SDL_ReleaseGPUTexture(dev_, t);
        sh_map_ = sh_depth_ = nullptr;
        sh_side_ = 0;
    }
    // The map (R32F: the caster's light depth + 2 x its gate) and its depth target, at `side` texels; made when first
    // needed or when the side changes, released by clear_resources
    bool ensure_sun_shadow_map(std::uint32_t side) {
        if (sh_map_ && sh_side_ == side) return true;
        release_sun_shadow_map();
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.width = ti.height = side;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ti.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        sh_map_ = SDL_CreateGPUTexture(dev_, &ti);
        ti.format = depth_format_;
        ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        sh_depth_ = SDL_CreateGPUTexture(dev_, &ti);
        if (!sh_map_ || !sh_depth_) {
            static bool told = false;
            if (!told) { SDL_Log("b3gfx: sun shadow map (%u texels) could not be made: %s (no sun shadows)", unsigned(side), SDL_GetError()); told = true; }
            release_sun_shadow_map();
            return false;
        }
        sh_side_ = side;
        return true;
    }
    // The caster pipeline: car.vert + shadow.frag, depth LESS_OR_EQUAL (cleared to 1: the light's far side), a
    // slope-scaled bias, both sides drawn
    SDL_GPUGraphicsPipeline* sun_caster_pipeline() {
        if (sh_pipe_) return sh_pipe_;
        SDL_GPUVertexBufferDescription vb{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[5] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 32},
            {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, 36},
        };
        SDL_GPUColorTargetDescription ct{};
        ct.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = car_vs_;
        pi.fragment_shader = sh_fs_;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = at;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.rasterizer_state.enable_depth_bias = true;
        pi.rasterizer_state.depth_bias_constant_factor = 1.0f;
        pi.rasterizer_state.depth_bias_slope_factor = 1.5f;
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        sh_pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!sh_pipe_) SDL_Log("b3gfx: sun shadow caster pipeline: %s", SDL_GetError());
        return sh_pipe_;
    }
    // propshadow1: the track casters' pipeline: mesh.vert's simple path (shade.x 0: view_proj x model x pos, the uv and
    // the vertex colour through) + shadow_mesh.frag, otherwise as the cars' (the same map, depth test and bias)
    SDL_GPUGraphicsPipeline* sun_prop_pipeline() {
        if (sh_prop_pipe_ || sh_prop_failed_) return sh_prop_pipe_;
        using namespace shaders;
        if (!sh_prop_fs_) sh_prop_fs_ = shader(shadow_mesh_frag_spv, sizeof shadow_mesh_frag_spv, shadow_mesh_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
        SDL_GPUVertexBufferDescription vb{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[5] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 32},
            {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, 36},
        };
        SDL_GPUColorTargetDescription ct{};
        ct.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs_;
        pi.fragment_shader = sh_prop_fs_;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = at;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.rasterizer_state.enable_depth_bias = true;
        pi.rasterizer_state.depth_bias_constant_factor = 1.0f;
        pi.rasterizer_state.depth_bias_slope_factor = 1.5f;
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        if (sh_prop_fs_) sh_prop_pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!sh_prop_pipe_) {
            SDL_Log("b3gfx: prop shadow caster pipeline: %s (props cast nothing)", SDL_GetError());
            sh_prop_failed_ = true;
        }
        return sh_prop_pipe_;
    }
    // A caster's gate (0..255): its sky_open (nothing below 0.18, all from 0.32: a car in a tunnel, ~0.2, casts nothing and
    // the mouth fades; a street between tall buildings, ~0.3, still casts: the baked shadows keep the rest from darkening
    // twice), its traffic fade-in (CarParams::fade < 0); a screen-door ghost (0 < fade < 1) casts nothing
    static int sun_gate(const CarParams& c) {
        if (c.fade > 0.0f && c.fade < 1.0f) return 0;
        const float g = sun_shadow_gate(c.sky_open) * (c.fade < 0.0f ? -c.fade : 1.0f);
        return int(std::floor(g * 255.0f + 0.5f));
    }
    // The caster pass (before the scene pass): the frame's car draws whose position lies in the box (with a car's
    // length to spare), depth-only through fp.sun_shadow.view_proj. False: no map this frame (the scene draws unshadowed)
    bool sun_shadow_pass(SDL_GPUCommandBuffer* cmd, const FrameParams& fp, const std::vector<DrawItem>& items) {
        const FrameParams::SunShadow& ss = fp.sun_shadow;
        const std::uint32_t side = std::uint32_t(std::clamp(ss.size, 256, 4096));
        if (!ensure_sun_shadow_map(side) || !sun_caster_pipeline()) return false;
        SDL_GPUColorTargetInfo ci{};
        ci.texture = sh_map_;
        ci.clear_color = SDL_FColor{1, 0, 0, 0};   // no caster: gate 0, the far side
        ci.load_op = SDL_GPU_LOADOP_CLEAR;
        ci.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo di{};
        di.texture = sh_depth_;
        di.clear_depth = 1.0f;
        di.load_op = SDL_GPU_LOADOP_CLEAR;
        di.store_op = SDL_GPU_STOREOP_DONT_CARE;
        di.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        di.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
        SDL_BindGPUGraphicsPipeline(pass, sh_pipe_);
        BufferId bvb = kNone, bib = kNone;
        const float* m = ss.view_proj;
        int casters = 0;
        for (const DrawItem& it : items) {
            if (it.shading != Shading::kCar || it.overlay || !it.index_count || it.vertices >= buffers_.size() || it.indices >= buffers_.size()) continue;
            const CarParams& c = it.car();
            const int gate = sun_gate(c);
            if (gate < 1) continue;
            const float x = m[0] * it.model[12] + m[4] * it.model[13] + m[8] * it.model[14] + m[12];
            const float y = m[1] * it.model[12] + m[5] * it.model[13] + m[9] * it.model[14] + m[13];
            if (std::fabs(x) > 1.25f || std::fabs(y) > 1.25f) continue;
            bind_buffers(pass, it, bvb, bib);
            CarVertexUniforms vu{};
            car_vertex_uniforms(ss.view_proj, fp.eye, it, vu);
            SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
            const float g[4] = {float(gate), 0, 0, 0};
            SDL_PushGPUFragmentUniformData(cmd, 0, g, sizeof g);
            ++stats_.draws;
            ++casters;
            SDL_DrawGPUIndexedPrimitives(pass, it.index_count, 1, it.first_index, 0, 0);
        }
        // propshadow1: the marked track draws (props, instances) whose bounds (DrawItem::sun_radius round the origin) reach
        // the box, so a long barrier or gantry with its origin outside still casts where it crosses the box
        int prop_casters = 0;
        const float kx = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]), ky = std::sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
        if (ss.props && sun_prop_pipeline()) {
            bool bound = false;
            for (const DrawItem& it : items) {
                if (!it.sun_caster || it.shading != Shading::kTrack || it.overlay || it.effect || is_line(it) || !it.index_count ||
                    it.vertices >= buffers_.size() || it.indices >= buffers_.size())
                    continue;
                const float x = m[0] * it.model[12] + m[4] * it.model[13] + m[8] * it.model[14] + m[12];
                const float y = m[1] * it.model[12] + m[5] * it.model[13] + m[9] * it.model[14] + m[13];
                if (std::fabs(x) > 1.0f + kx * it.sun_radius || std::fabs(y) > 1.0f + ky * it.sun_radius) continue;
                if (!bound) { SDL_BindGPUGraphicsPipeline(pass, sh_prop_pipe_); bound = true; }
                bind_buffers(pass, it, bvb, bib);
                const SDL_GPUTextureSamplerBinding tsb{tex_of(it.texture), sampler_};
                SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
                VertexUniforms vu{};
                std::memcpy(vu.view_proj, ss.view_proj, sizeof vu.view_proj);
                std::memcpy(vu.model, it.model, sizeof vu.model);
                vu.inst[3] = -1.0f;   // (not an instance colour: the simple path does not read it)
                SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
                const float g[4] = {255.0f, it.blend == Blend::kOpaque ? 0.0f : 1.0f, 0, 0};
                SDL_PushGPUFragmentUniformData(cmd, 0, g, sizeof g);
                ++stats_.draws;
                ++prop_casters;
                SDL_DrawGPUIndexedPrimitives(pass, it.index_count, 1, it.first_index, 0, 0);
            }
        }
        SDL_EndGPURenderPass(pass);
        static bool told = false;   // once, what it works with
        if (!told) {
            told = true;
            char props[48] = "";
            if (ss.props) std::snprintf(props, sizeof props, " and %d prop casters (PROP SHADOWS)", prop_casters);
            SDL_Log("b3gfx: sun shadows: %u texels, kernel %s, strength %.2f, light (%.2f %.2f %.2f)%s, %d caster draws%s in the first frame",
                    unsigned(side), ss.filter == 1 ? "5x5" : "3x3", double(ss.strength), double(fp.key_light.dir[0]),
                    double(fp.key_light.dir[1]), double(fp.key_light.dir[2]), fp.key_light.night ? " (the moon)" : "", casters, props);
        }
        return true;
    }
    // The receivers' pipelines: pipeline()'s track variants with mesh.frag's SUN_SHADOW variant (blend 0..2, x 2 culled)
    SDL_GPUGraphicsPipeline* recv_pipeline(Blend blend, bool cull) {
        SDL_GPUGraphicsPipeline*& p = sh_recv_pipe_[int(blend) + (cull ? 3 : 0)];
        if (!p) p = pipeline(kSceneFormat, blend, cull, false, false, sh_recv_fs_);
        return p;
    }

    // ---- surface: screen-space ambient occlusion (FrameParams::Surface::ao, shaders/ao.frag) ----
    struct AoUniforms { float inv[16], vp[16], a[4], b[4], right[4], eye[4]; };
    bool ao_ready() {
        if (ao_failed_) return false;
        if (ao_fs_) return true;
        using namespace shaders;
        ao_fs_ = shader(ao_frag_spv, sizeof ao_frag_spv, ao_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
        if (!ao_fs_) {
            SDL_Log("b3gfx: ambient occlusion unavailable: %s", SDL_GetError());
            ao_failed_ = true;
        }
        return ao_fs_ != nullptr;
    }
    // [0] the half-size pass (no blend), [1] the composite into the scene pass (RGB x src, alpha kept), [2] its debug form
    SDL_GPUGraphicsPipeline* ao_pipeline(int k) {
        SDL_GPUGraphicsPipeline*& p = ao_pipe_[k];
        if (p) return p;
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        if (k > 0) {
            auto& bs = ct.blend_state;
            bs.enable_blend = true;
            bs.src_color_blendfactor = k == 1 ? SDL_GPU_BLENDFACTOR_DST_COLOR : SDL_GPU_BLENDFACTOR_ONE;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            bs.color_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            bs.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
            bs.enable_color_write_mask = true;
        }
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = fsq_vs_;
        pi.fragment_shader = ao_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = k > 0 ? sample_count(msaa_) : SDL_GPU_SAMPLECOUNT_1;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        if (k > 0) {
            pi.target_info.depth_stencil_format = depth_format_;
            pi.target_info.has_depth_stencil_target = true;
        }
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: ambient occlusion pipeline: %s", SDL_GetError());
        return p;
    }
    bool ao_uniforms(const Targets& t, const FrameParams& fp, int mode, AoUniforms& u) const {
        if (!mat4_invert(fp.view_proj, u.inv)) return false;
        std::memcpy(u.vp, fp.view_proj, sizeof u.vp);
        const FrameParams::Surface& sf = fp.surface;
        u.a[0] = float(mode); u.a[1] = std::clamp(sf.ao, 0.0f, 2.0f); u.a[2] = std::max(0.05f, sf.ao_radius); u.a[3] = 0;
        u.b[0] = 1.0f / float(t.w); u.b[1] = 1.0f / float(t.h);
        u.b[2] = 1.0f / float(std::max<std::uint32_t>(1, t.ao.w)); u.b[3] = 1.0f / float(std::max<std::uint32_t>(1, t.ao.h));
        for (int k = 0; k < 3; ++k) { u.right[k] = fp.right[k]; u.eye[k] = fp.eye[k]; }
        u.right[3] = u.eye[3] = 0;
        return true;
    }
    // after the depth pre-pass, before the scene pass: the half-size visibility into t.ao, blurred into t.ao2 (nightfix:
    // the 4 x 4 box that cancels the disc's ordered rotations)
    bool ao_pass(SDL_GPUCommandBuffer* cmd, Targets& t, const FrameParams& fp) {
        SDL_GPUGraphicsPipeline* p = ao_pipeline(0);
        AoUniforms u{};
        if (!p || !ao_pipeline(fp.surface.debug ? 2 : 1) || !ao_uniforms(t, fp, 0, u)) return false;
        post_pass(cmd, p, t.ao.t, depth_src_, depth_src_, &u, sizeof u, clamp_sampler_);
        u.a[0] = 3.0f;
        post_pass(cmd, p, t.ao2.t, depth_src_, t.ao.t, &u, sizeof u, clamp_sampler_);
        return true;
    }
    void draw_ao(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, Targets& t, const FrameParams& fp, SDL_GPUGraphicsPipeline*& bound) {
        SDL_GPUGraphicsPipeline* p = ao_pipeline(fp.surface.debug ? 2 : 1);
        AoUniforms u{};
        if (!p || !ao_uniforms(t, fp, fp.surface.debug ? 2 : 1, u)) return;
        SDL_BindGPUGraphicsPipeline(pass, p);
        bound = p;
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
        SDL_GPUTextureSamplerBinding tsb[2] = {{depth_src_, clamp_sampler_}, {t.ao2.t, clamp_sampler_}};
        SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
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
            SDL_GPUTextureSamplerBinding tsb[3] = {{in, effect_sampler_}, {in, clamp_sampler_}, {depth_src_, clamp_sampler_}};
            SDL_BindGPUFragmentSamplers(rp, 0, tsb, 3);
            SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
            SDL_EndGPURenderPass(rp);
        }
        return t.dg.t;
    }

    // ---- photoblur: photo mode's motion blur (FrameParams::Motion, shaders/motion.frag) ----
    struct MotionUniforms { float vp[16], inv[16], rm[12], rp[12], eye[4], cam[4], a[4], b[4], c[4], bx[FrameParams::Motion::kBoxes * 6][4]; };
    static constexpr std::uint32_t kMotionTile = 32;   // the tile max's tile (pixels)
    bool motion_ready() {
        if (motion_failed_) return false;
        if (motion_fs_) return true;
        using namespace shaders;
        motion_fs_ = shader(motion_frag_spv, sizeof motion_frag_spv, motion_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 1);
        if (!motion_fs_) {
            SDL_Log("b3gfx: motion blur unavailable: %s", SDL_GetError());
            motion_failed_ = true;
        }
        return motion_fs_ != nullptr;
    }
    // weather (§38): the lens drops' pipeline: full screen in the resumed scene pass (its depth target attached, not
    // tested), Cs x As + Cd x (1 - As) on RGB, the frame alpha kept
    struct DropUniforms { float a[4], g[4], tint[4], d[FrameParams::Drops::kMax][4]; };
    SDL_GPUGraphicsPipeline* drops_pipeline() {
        if (drops_pipe_ || drops_failed_) return drops_pipe_;
        using namespace shaders;
        if (!drops_fs_) drops_fs_ = shader(drops_frag_spv, sizeof drops_frag_spv, drops_frag_msl, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
        if (!drops_fs_) {
            SDL_Log("b3gfx: lens drops unavailable: %s", SDL_GetError());
            drops_failed_ = true;
            return nullptr;
        }
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        restore_blend(ct);
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = fsq_vs_;
        pi.fragment_shader = drops_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = sample_count(msaa_);
        pi.depth_stencil_state.enable_depth_test = false;
        pi.depth_stencil_state.enable_depth_write = false;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth_format_;
        pi.target_info.has_depth_stencil_target = true;
        drops_pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!drops_pipe_) {
            SDL_Log("b3gfx: lens drops pipeline: %s", SDL_GetError());
            drops_failed_ = true;
        }
        return drops_pipe_;
    }
    SDL_GPUGraphicsPipeline* motion_pipeline(int k) {
        SDL_GPUGraphicsPipeline*& p = motion_pipe_[k];
        if (p) return p;
        SDL_GPUColorTargetDescription ct{};
        ct.format = kSceneFormat;
        auto& bs = ct.blend_state;
        if (k == 1) {   // A = mix(A, this frame's gather, 1 / (frames + 1))
            bs.enable_blend = true;
            bs.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            bs.color_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            bs.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        if (k == 2) {   // the frame's alpha (the specular's sheen) kept
            bs.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
            bs.enable_color_write_mask = true;
        }
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = fsq_vs_;
        pi.fragment_shader = motion_fs_;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.multisample_state.sample_count = k == 2 ? sample_count(msaa_) : SDL_GPU_SAMPLECOUNT_1;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        if (k == 2) {   // drawn in the resumed scene pass, which has the depth target attached
            pi.target_info.depth_stencil_format = depth_format_;
            pi.target_info.has_depth_stencil_target = true;
        }
        p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!p) SDL_Log("b3gfx: motion blur pipeline: %s", SDL_GetError());
        return p;
    }
    bool motion_uniforms(const Targets& t, const FrameParams& fp, MotionUniforms& u) const {
        u = MotionUniforms{};
        if (!mat4_invert(fp.view_proj, u.inv)) return false;
        std::memcpy(u.vp, fp.view_proj, sizeof u.vp);
        const FrameParams::Motion& m = fp.motion;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                u.rm[4 * r + c] = m.rot_m[3 * r + c];
                u.rp[4 * r + c] = m.rot_p[3 * r + c];
            }
        for (int k = 0; k < 3; ++k) { u.eye[k] = m.eye[k]; u.cam[k] = m.cam[k]; }
        const int n = std::clamp(m.boxes, 0, FrameParams::Motion::kBoxes);
        u.a[1] = float(n);
        u.b[0] = std::clamp(m.max_streak, 0.0f, 0.5f) * float(t.h);
        u.b[1] = float(t.w);
        u.b[2] = float(t.h);
        u.b[3] = 8;
        u.c[0] = float(kMotionTile);
        u.c[1] = std::ceil(u.b[0] / float(kMotionTile));
        u.c[2] = float((t.w + kMotionTile - 1) / kMotionTile);
        u.c[3] = float((t.h + kMotionTile - 1) / kMotionTile);
        for (int k = 0; k < n; ++k) {
            const FrameParams::Motion::Box& b = m.box[k];
            for (int r = 0; r < 3; ++r) std::memcpy(u.bx[6 * k + r], b.to_local[r], sizeof u.bx[0]);
            for (int c = 0; c < 3; ++c) {
                u.bx[6 * k + 3][c] = b.lo[c];
                u.bx[6 * k + 4][c] = b.hi[c];
                u.bx[6 * k + 5][c] = b.move[c];
            }
        }
        return true;
    }
    // What the accumulation was drawn from (a change restarts it): the target, the view, the motion, everything else of
    // the frame's parameters that changes the picture before the blur (fields one by one: no padding bytes), and the 3D
    // draw list with what each draw samples (the texture as drawn: the HD pack or the original, its content's writes)
    // and its car's paint and light; the effects' own matrices are left out (an effect that moves while paused)
    std::uint64_t motion_sig(const Targets& t, const FrameParams& fp, const std::vector<DrawItem>& items) const {
        std::uint64_t h = 1469598103934665603ull;
        const auto mix = [&](const void* p, std::size_t n) {
            const auto* b = static_cast<const std::uint8_t*>(p);
            for (std::size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
        };
        const auto mixv = [&](auto v) { mix(&v, sizeof v); };
        const auto mixa = [&](const auto& a) { mix(&a, sizeof a); };   // arrays of plain numbers
        mixv(t.w); mixv(t.h); mixv(msaa_);
        mixa(fp.view_proj);
        const FrameParams::Motion& m = fp.motion;
        mixa(m.eye); mixa(m.cam); mixa(m.rot_m); mixa(m.rot_p);
        mixv(m.max_streak); mixv(m.frames); mixv(m.boxes);
        for (int k = 0; k < std::clamp(m.boxes, 0, FrameParams::Motion::kBoxes); ++k) {
            mixa(m.box[k].to_local); mixa(m.box[k].lo); mixa(m.box[k].hi); mixa(m.box[k].move);
        }
        {   // the frame's look (CarLook: floats only)
            static_assert(sizeof(CarLook) % sizeof(float) == 0, "CarLook: floats only");
            mix(&fp.car_look, sizeof fp.car_look);
            mixa(fp.clear); mixa(fp.light_dir); mixa(fp.right); mixa(fp.to_sun); mixv(fp.no_specular); mixa(fp.specular);
            mixv(fp.present_scale); mixv(fp.line_far); mixa(fp.fog); mixa(fp.fog_colour); mixa(fp.sun_colour); mixv(fp.post_game);
            const FrameParams::Night& n = fp.night;
            mixv(n.on); mixa(n.world); mixv(n.sat); mixv(n.gamma); mixv(n.pivot); mixa(n.sky); mixa(n.zenith); mixa(n.horizon);
            mixv(n.cloud); mixv(n.stars); mixa(n.moon_dir); mixa(n.moon_colour); mixv(n.moon_size); mixv(n.halo); mixa(n.moon_light);
            mixa(n.car); mixv(n.facing); mixv(n.lit);   // (weather: the rain's day grade)
            mixv(n.spot); mixv(n.lamps);   // nightlights: the beams, the tunnels, the fixtures' glow
            for (int k = 0; k < std::clamp(n.lamps, 0, FrameParams::Night::kMaxLamps); ++k) {
                mixa(n.lamp[k].pos); mixa(n.lamp[k].dir); mixv(n.lamp[k].gain);
            }
            mixa(n.lamp_colour); mixa(n.spot_shape); mixa(n.spot_core); mixa(n.spot_range); mixv(n.enclosed); mixa(n.tunnel); mixa(n.fixture);
            mixv(n.streets);   // street lights: their pools restart the blur too
            for (int k = 0; k < std::clamp(n.streets, 0, FrameParams::Night::kMaxStreet); ++k) { mixa(n.street[k].pos); mixa(n.street[k].colour); mixv(n.street[k].gain); mixv(n.street[k].scale); }
            mixa(n.street_shape); mixa(n.street_range); mixv(n.flat); mixa(n.board);   // (nightfix)
            mixv(n.boards);
            for (int k = 0; k < std::clamp(n.boards, 0, FrameParams::Night::kMaxBoard); ++k) { mixa(n.board_lamp[k].pos); mixa(n.board_lamp[k].axis); mixv(n.board_lamp[k].gain); }
            mixa(fp.key_light.dir); mixa(fp.key_light.colour); mixv(fp.key_light.night);
            const FrameParams::Surface& sf = fp.surface;
            mixv(sf.relief); mixv(sf.sheen); mixv(sf.fade); mixv(sf.sky); mixv(sf.grazing); mixv(sf.ao); mixv(sf.ao_radius); mixv(sf.debug);
            mixv(hd_shown_);
            const FrameParams::SunShadow& ss = fp.sun_shadow;   // (dev 1.3.0: SUN SHADOWS)
            mixv(ss.on); mixa(ss.view_proj); mixv(ss.depth); mixv(ss.rise); mixv(ss.strength); mixv(ss.lit); mixv(ss.size); mixv(ss.filter);
        }
        std::size_t n3d = 0;   // (the 2D stage, photo mode's own plates among it, is drawn after: not counted)
        for (const DrawItem& it : items) {
            if (it.overlay) continue;
            ++n3d;
            mixv(it.vertices); mixv(it.indices); mixv(it.texture); mixv(it.first_index); mixv(it.index_count);
            mixv(tex_of(it.texture));
            mixv(it.texture < tex_gen_.size() ? tex_gen_[it.texture] : 0u);
            mixa(it.tint); mixa(it.instance); mixv(it.uv_scroll); mixv(it.blend); mixv(it.shading); mixv(it.night_emissive); mixv(it.day_glow); mixv(it.glow_mask); mixv(it.night_mean); mixv(it.night_cols); mixv(it.night_rows);
            if (!it.effect && !it.post_fog) mixa(it.model);
            if (it.car_) {
                const CarParams& c = *it.car_;
                mixa(c.sh); mixa(c.base); mixa(c.full); mixa(c.env_colour); mixv(c.fade); mixv(c.reflect); mixv(c.env_texture);
                mixv(c.env_world); mixv(c.paint_mode); mixa(c.paint); mixa(c.paint2); mixv(c.paint_ref); mixv(c.paint_mask);
                mixa(c.paint3); mixa(c.paint4);   // paint2
                mixv(c.zones); mixa(c.zone);
            }
        }
        mixv(n3d);
        return h;
    }
    // The streaks and this frame's gather (all of them at once offscreen: a screenshot or a shot is the finished picture);
    // returns A (nullptr: nothing). The window's frames add to A until it holds `frames`.
    bool motion_window(const Targets& t) const { return &t == &win_targets_ || &t == &scaled_win_; }
    static int motion_frames(const FrameParams& fp) { return std::clamp(fp.motion.frames, 1, 64); }
    SDL_GPUTexture* motion_chain(SDL_GPUCommandBuffer* cmd, Targets& t, const FrameParams& fp, std::uint64_t sig) {
        SDL_GPUGraphicsPipeline* pv = motion_pipeline(0);
        SDL_GPUGraphicsPipeline* pa = motion_pipeline(1);
        MotionUniforms u;
        if (!pv || !pa || !motion_pipeline(2) || !motion_uniforms(t, fp, u)) return nullptr;
        const bool window = motion_window(t);
        const int frames = motion_frames(fp);
        if (!window || sig != t.ma_sig) {
            t.ma_frames = 0;
            t.ma_sig = sig;
        }
        if (t.ma_frames >= frames) return t.ma.t;   // converged: A as it is
        SDL_GPUTexture* src = msaa_ > 1 ? t.scene_resolve : t.scene;
        // the streaks (V), the tile max (V -> Tm), the neighbour max (Tm -> Tn)
        const struct { int mode; SDL_GPUTexture *dst, *in1, *in2; } prep[3] = {
            {0, t.mv.t, src, depth_src_}, {3, t.mt.t, t.mv.t, t.mv.t}, {4, t.mn.t, t.mt.t, t.mt.t}};
        for (const auto& s : prep) {
            u.a[0] = float(s.mode);
            SDL_GPUColorTargetInfo ci{};
            ci.texture = s.dst;
            ci.load_op = SDL_GPU_LOADOP_DONT_CARE;
            ci.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);
            SDL_BindGPUGraphicsPipeline(rp, pv);
            SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
            SDL_GPUTextureSamplerBinding tsb[3] = {{src, effect_sampler_}, {s.in1, clamp_sampler_}, {s.in2, clamp_sampler_}};
            SDL_BindGPUFragmentSamplers(rp, 0, tsb, 3);
            SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
            SDL_EndGPURenderPass(rp);
        }
        do {
            const int k = t.ma_frames;
            int r = 0;   // frame k's sub-step: the bit-reversed k (mod 16), so any 2^n frames in a row spread evenly
            for (int b = 0; b < 4; ++b) r |= ((k >> b) & 1) << (3 - b);
            u.a[0] = 1;
            u.a[2] = (float(r) + 0.5f) / 16.0f;
            u.a[3] = 1.0f / float(k + 1);
            SDL_GPUColorTargetInfo ci{};
            ci.texture = t.ma.t;
            ci.clear_color = SDL_FColor{0, 0, 0, 0};
            ci.load_op = k == 0 ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            ci.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);
            SDL_BindGPUGraphicsPipeline(rp, pa);
            SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
            SDL_GPUTextureSamplerBinding tsb[3] = {{src, effect_sampler_}, {t.mv.t, clamp_sampler_}, {t.mn.t, clamp_sampler_}};
            SDL_BindGPUFragmentSamplers(rp, 0, tsb, 3);
            SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
            SDL_EndGPURenderPass(rp);
            ++t.ma_frames;
        } while (!window && t.ma_frames < frames);
        return t.ma.t;
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
        for (auto* x : {t.scene, t.scene_resolve, t.bloom_src, t.depth, t.resolve, t.dof_depth, t.dof_colour, t.sd, t.sd_res, t.sd_fix}) if (x) SDL_ReleaseGPUTexture(dev_, x);
        for (auto* b : {&t.zb[0], &t.zb[1], &t.bh, &t.bq[0], &t.bq[1], &t.bp[0], &t.bp[1], &t.dw, &t.dg, &t.ao, &t.ao2, &t.nc, &t.mv, &t.ma, &t.mt, &t.mn, &t.mir}) if (b->t) SDL_ReleaseGPUTexture(dev_, b->t);
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
        // SUN SHADOWS: the track draws of the scene pass take the map (not the sky, lines, reflection texgen, effects,
        // the post-fog stage, the 2D, the env map or the pre-passes)
        const bool recv = sh_live_ && !occ && !dof && !env_face_ && shading == Shading::kTrack && !it.effect && !it.overlay &&
                          !it.post_fog && !is_line(it) && it.track_type != 2;
        SDL_GPUGraphicsPipeline* p = dof ? dof_depth_pipeline(game && it.cull ? 1 : 0)
                                   : recv ? recv_pipeline(it.blend, it.cull)
                                   : occ ? occ_pipeline(game && it.cull)
                                   : it.overlay ? overlay_pipeline(it.gs_alpha | (it.alpha_only ? 0x100 : 0) | (it.overlay_blend ? 0 : 0x200) |
                                                                   (it.overlay_ztest ? 0x400 : 0) | (it.overlay_zwrite ? 0x800 : 0))
                                   : it.effect ? effect_pipeline(effect_mode(it.gs_alpha))
                                   : is_line(it) ? line_pipeline() : pipeline(kSceneFormat, it.blend, game && it.cull, false, it.post_fog, nullptr, keeps_frame_alpha(it));
        if (!p) return;
        // scenedepth: the draws the scene depth is made of, as the depth pre-pass draws them (dof_depth_pass): not the 2D,
        // the effects, the sky, or a blended track draw (other than the post-fog stage's)
        const bool sdw = sd_.scene && !occ && !dof && !env_face_ && !(it.overlay || it.effect || it.shading == Shading::kSky) &&
                         !(it.blend == Blend::kAlpha && it.shading != Shading::kCar && !it.post_fog);
        if (sd_.scene) {
            if (sdw != sd_.bound_mask) bound = nullptr;   // (another form of the same pipeline)
            sd_.mask_on = sd_.bound_mask = sdw;
        }
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
        SDL_GPUTextureSamplerBinding tsb{tex_of(it.texture),
                                         it.overlay ? (it.repeat_u || it.repeat_v
                                                       ? overlay_wrap_[(it.point_sample ? 4 : 0) | (it.repeat_u ? 2 : 0) | (it.repeat_v ? 1 : 0)]
                                                       : it.point_sample ? clamp_sampler_ : overlay_sampler_)
                                         : it.effect && it.clamp_uv ? effect_sampler_ : sampler_};
        if (recv) {
            const SDL_GPUTextureSamplerBinding two[2] = {tsb, {sh_map_, clamp_sampler_}};
            SDL_BindGPUFragmentSamplers(pass, 0, two, 2);
        } else {
            SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        }
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
                            {ld[0], ld[1], ld[2], env_face_ ? 1.0f : 0.0f},   // reflect-tunnel: opaque track writes alpha 1
                            {it.blend == Blend::kAlphaTest ? it.alpha_ref : 0.0f, it.lighting,
                             it.blend == Blend::kAlpha ? 1.0f : 0.0f, is_line(it) && !occ && !dof ? 4.0f : float(int(shading))},
                            {float(it.gs_test), it.textured ? 1.0f : 0.0f, it.blend == Blend::kAlpha || it.effect ? 1.0f : 0.0f,
                             it.overlay && it.sharp_alpha ? it.sharp_level : 0.0f},
                            {fp.fog[0], fp.fog[1], fp.fog[2], fp.fog[3]},
                            {fp.fog_colour[0], fp.fog_colour[1], fp.fog_colour[2], it.post_fog || it.overlay ? 0.0f : fp.fog_colour[3]},
                            {}, {}, {}};
        // surface: the opaque track draws of the main view only (not the env map, the pre-passes, the effects, lines, 2D)
        const FrameParams::Surface& sf = fp.surface;
        if ((sf.relief > 0 || sf.sheen > 0 || sf.wet > 0) && shading == Shading::kTrack && it.textured && it.blend != Blend::kAlpha && !it.effect &&
            !it.overlay && !it.post_fog && !occ && !dof && !env_face_ && !is_line(it) && it.track_type != 2) {
            float s[3] = {fp.to_sun[0], fp.to_sun[1], fp.to_sun[2]};
            const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
            if (sl > 0) {
                fu.surf[0] = sf.relief; fu.surf[1] = sf.sheen; fu.surf[2] = std::max(1.0f, sf.fade); fu.surf[3] = sf.sky;
                for (int k = 0; k < 3; ++k) { fu.surf_sun[k] = s[k] / sl; fu.surf_eye[k] = fp.eye[k]; }
                fu.surf_sun[3] = sf.grazing;
                fu.surf_eye[3] = sf.debug ? 1.0f : 0.0f;
                if (fp.night.on) fu.surf_night[0] = fp.night.facing;   // night mode: to_sun is the moon's direction
                fu.surf_night[2] = sf.wet * (1.0f - std::clamp(it.dry, 0.0f, 1.0f));   // weather (§38): the wet road (dry under a roof)
            }
        }
        fu.surf_night[3] = it.overcast;   // weather (§38): the rain's grey sky (sky dome, backdrops)
        // night (nightfix): every track draw of the main view flattens its baked daylight (mesh.frag, surf_night.y)
        if (fp.night.on && fp.night.flat > 0 && shading == Shading::kTrack && !it.effect && !it.overlay && !it.post_fog && !occ && !dof &&
            !env_face_ && !is_line(it))
            fu.surf_night[1] = fp.night.flat;
        if (sdw) fu.light_dir[3] = -1.0f;   // scenedepth (above; mesh.frag's SCENE_DEPTH variant: never an env-map face's 1)
        if (recv) {   // SUN SHADOWS (surf_eye.xyz: the camera, which the variant reads to orient the face)
            ShadowFragmentUniforms su{};
            su.base = fu;
            for (int k = 0; k < 3; ++k) su.base.surf_eye[k] = fp.eye[k];
            const FrameParams::SunShadow& ss = fp.sun_shadow;
            std::memcpy(su.sun_vp, ss.view_proj, sizeof su.sun_vp);
            su.sun_p[0] = float(sh_side_);
            su.sun_p[1] = ss.depth * std::clamp(ss.rise, 0.05f, 1.0f);   // vertical metres per unit of map depth
            su.sun_p[2] = ss.filter == 1 ? 1.0f : 0.0f;
            static const char* const debug = SDL_getenv("B3_SUN_SHADOW_DEBUG");   // false colour (mesh.frag): 1 the shadow, 2 the vertex light
            su.sun_q[0] = ss.lit > 0 ? ss.lit : 0.75f;
            su.sun_p[3] = debug && *debug ? (*debug == '2' ? -2.0f : -1.0f) : std::clamp(ss.strength, 0.0f, 1.0f);
            SDL_PushGPUFragmentUniformData(cmd, 0, &su, sizeof su);
        } else {
            SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
        }
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

    // car.vert's inputs for a car draw through `view_proj` (the frame's, or SUN SHADOWS' map)
    static void car_vertex_uniforms(const float view_proj[16], const float eye[3], const DrawItem& it, CarVertexUniforms& vu) {
        const CarParams& c = it.car();
        std::memcpy(vu.view_proj, view_proj, sizeof vu.view_proj);
        std::memcpy(vu.model, it.model, sizeof vu.model);
        std::memcpy(vu.light, c.light, sizeof vu.light);
        std::memcpy(vu.sh, c.sh, sizeof vu.sh);
        for (int k = 0; k < 3; ++k) { vu.eye[k] = eye[k]; vu.view_dir[k] = c.view_dir[k]; }
        vu.view_dir[3] = c.sh_k;
        std::memcpy(vu.base, c.base, sizeof vu.base);
        std::memcpy(vu.full, c.full, sizeof vu.full);
        if (c.zones > 0) std::memcpy(vu.zone, c.zone, sizeof vu.zone);
        vu.deform[0] = float(c.zones < 8 ? c.zones : 8);
    }

    void draw_car(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const FrameParams& fp, const DrawItem& it,
                  SDL_GPUGraphicsPipeline*& bound, BufferId& bound_vb, BufferId& bound_ib, bool dof = false) {
        SDL_GPUGraphicsPipeline* p = dof ? dof_depth_pipeline(2) : pipeline(kSceneFormat, Blend::kOpaque, false, true);
        if (!p) return;
        if (sd_.scene && !dof) {   // scenedepth: every car draw writes the depth (as the pre-pass draws them)
            if (!sd_.bound_mask) bound = nullptr;
            sd_.mask_on = sd_.bound_mask = true;
        }
        if (p != bound) { SDL_BindGPUGraphicsPipeline(pass, p); bound = p; }
        bind_buffers(pass, it, bound_vb, bound_ib);
        const CarParams& c = it.car();
        const bool env = c.env_texture < textures_.size();
        SDL_GPUTexture* world = env_world_texture(c.env_world);   // reflect-tunnel
        const bool paint = c.paint_mode != 0 && c.paint_mask < textures_.size();   // carpaint
        SDL_GPUTextureSamplerBinding tsb[4] = {{tex_of(it.texture), sampler_},
                                               {env ? tex_of(c.env_texture) : white_, sampler_},
                                               {world ? world : white_, overlay_sampler_},
                                               {paint ? tex_of(c.paint_mask) : white_, sampler_}};
        SDL_BindGPUFragmentSamplers(pass, 0, tsb, 4);
        CarVertexUniforms vu{};
        car_vertex_uniforms(fp.view_proj, fp.eye, it, vu);
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
                    l2[4] = {cl.glass_weight, cl.glass_fresnel, cl.glass_gain, cl.glass_darken},
                    l3[4] = {cl.rim, cl.colour_keep, world ? 1.0f : 0.0f, cl.strength};   // look3.z: the world env map bound
        std::memcpy(fu.look0, l0, sizeof l0);
        std::memcpy(fu.look1, l1, sizeof l1);
        std::memcpy(fu.look2, l2, sizeof l2);
        std::memcpy(fu.look3, l3, sizeof l3);
        if (paint) {   // carpaint: paint.w the mode (0: the texture as is), paint2.w the reference level
            for (int k = 0; k < 3; ++k) { fu.paint[k] = c.paint[k]; fu.paint2[k] = c.paint2[k]; fu.paint3[k] = c.paint3[k]; fu.paint4[k] = c.paint4[k]; }
            fu.paint[3] = float(c.paint_mode);
            fu.paint2[3] = c.paint_ref;
        }
        if (fp.night.on) {   // night mode (§17): the SH and the env map dimmed, the moon as a key light (to_sun: the moon)
            const FrameParams::Night& n = fp.night;
            const float nv[4] = {n.car[0], n.car[1], n.car[2], 1.0f};
            std::memcpy(fu.night, nv, sizeof nv);
            for (int k = 0; k < 3; ++k) fu.moon[k] = n.moon_light[k];
        }
        fu.moon[3] = std::clamp(fp.surface.wet, 0.0f, 1.0f);   // weather (§38): the wet car (gloss, beads); 0 dry
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
        ++stats_.draws;
        SDL_DrawGPUIndexedPrimitives(pass, it.index_count, 1, it.first_index, 0, 0);
    }

    // reflect-tunnel: the car's world env map (EnvPass). CarParams::env_world = kEnvWorldBase + slot.
    static constexpr std::uint32_t kEnvWorldBase = 0xFFFFFF00u;
    struct EnvTarget { SDL_GPUTexture *ms = nullptr, *res = nullptr, *depth = nullptr; std::uint32_t w = 0, h = 0; bool drawn = false; };
    SDL_GPUTexture* env_world_texture(std::uint32_t id) const {
        if (id < kEnvWorldBase || id - kEnvWorldBase >= 2) return nullptr;
        const EnvTarget& e = env_[id - kEnvWorldBase];
        return e.drawn ? (e.res ? e.res : e.ms) : nullptr;
    }
    void release_env(EnvTarget& e) {
        for (SDL_GPUTexture* t : {e.ms, e.res, e.depth}) if (t) SDL_ReleaseGPUTexture(dev_, t);
        e = EnvTarget{};
    }
    bool ensure_env(EnvTarget& e, std::uint32_t w, std::uint32_t h) {
        if (e.w == w && e.h == h && e.ms && e.depth) return true;
        release_env(e);
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.width = w;
        ti.height = h;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        ti.sample_count = sample_count(msaa_);   // the track pipelines' sample count
        ti.format = kSceneFormat;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | (msaa_ > 1 ? 0 : SDL_GPU_TEXTUREUSAGE_SAMPLER);
        e.ms = SDL_CreateGPUTexture(dev_, &ti);
        ti.format = depth_format_;
        ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        e.depth = SDL_CreateGPUTexture(dev_, &ti);
        if (msaa_ > 1) {
            ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
            ti.format = kSceneFormat;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            e.res = SDL_CreateGPUTexture(dev_, &ti);
        }
        if (!e.ms || !e.depth || (msaa_ > 1 && !e.res)) { release_env(e); return false; }
        e.w = w;
        e.h = h;
        return true;
    }
    std::uint32_t render_env(int slot, const EnvPass& ep) override {
        if (slot < 0 || slot > 1 || ep.face < 8) return kNone;
        EnvTarget& e = env_[slot];
        e.drawn = false;
        if (ep.night_glow > 0.0f) ensure_env_glow(ep);   // reflect2 G (its upload with this frame's)
        flush_uploads();   // this frame's buffer updates go first (the submission order, as update_texture keeps it)
        const std::uint32_t T = std::uint32_t(ep.face);
        if (!ensure_env(e, 3 * T, 2 * T)) return kNone;
        static const bool timed = SDL_getenv("B3_ENV_TIME") != nullptr;   // diagnostics: the pass alone, waited for
        const std::uint64_t t0 = timed ? SDL_GetTicksNS() : 0;
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd) return kNone;
        SDL_GPUColorTargetInfo ci{};
        ci.clear_color = SDL_FColor{0, 0, 0, 0};   // alpha 0: the sky (car.frag draws it behind the track)
        ci.load_op = SDL_GPU_LOADOP_CLEAR;
        ci.texture = e.ms;
        ci.cycle = true;   // a frame still in flight may be sampling the previous map
        if (msaa_ > 1) {
            ci.store_op = SDL_GPU_STOREOP_RESOLVE;
            ci.resolve_texture = e.res;
            ci.cycle_resolve_texture = true;
        } else {
            ci.store_op = SDL_GPU_STOREOP_STORE;
        }
        SDL_GPUDepthStencilTargetInfo di{};
        di.texture = e.depth;
        di.clear_depth = 0.0f;   // reverse-Z: far
        di.load_op = SDL_GPU_LOADOP_CLEAR;
        di.store_op = SDL_GPU_STOREOP_DONT_CARE;
        di.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        di.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        di.cycle = true;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
        if (!pass) { SDL_SubmitGPUCommandBuffer(cmd); return kNone; }
        FrameParams fp = ep.fp;
        fp.no_specular = true;          // RenderMesh(..., 1) for the env cameras [L draft 0x21C780]
        fp.fog_colour[3] = 0.0f;        // CB3Fog::Render runs on the main frame only
        float ld[3] = {fp.light_dir[0], fp.light_dir[1], fp.light_dir[2]};
        const float ll = std::sqrt(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);
        if (ll > 0) for (float& c : ld) c /= ll;
        // each face one texel wider on every side (tan of the half angle T / (T - 2)), so car.frag's bilinear lookup
        // never reads across a face's edge
        const float k = float(T) / float(T - 2), n = ep.znear, fr = ep.zfar;
        float proj[16] = {};
        proj[0] = proj[5] = 1.0f / k;
        proj[10] = n / (fr - n);        // reverse-Z with a far plane: depth 1 at znear, 0 at zfar, clipped beyond
        proj[11] = -1.0f;
        proj[14] = n * fr / (fr - n);
        env_face_ = true;
        SDL_GPUGraphicsPipeline* bound = nullptr;
        BufferId bound_vb = kNone, bound_ib = kNone;
        for (int f = 0; f < 6; ++f) {
            float F[3], R[3], U[3], view[16];
            env_face(f, F, R, U);
            mat4_identity(view);
            for (int i = 0; i < 3; ++i) { view[i * 4 + 0] = R[i]; view[i * 4 + 1] = U[i]; view[i * 4 + 2] = -F[i]; }
            view[12] = -(R[0] * ep.centre[0] + R[1] * ep.centre[1] + R[2] * ep.centre[2]);
            view[13] = -(U[0] * ep.centre[0] + U[1] * ep.centre[1] + U[2] * ep.centre[2]);
            view[14] = F[0] * ep.centre[0] + F[1] * ep.centre[1] + F[2] * ep.centre[2];
            mat4_mul(proj, view, fp.view_proj);
            const SDL_GPUViewport vp{float(T * std::uint32_t(f % 3)), float(T * std::uint32_t(f / 3)), float(T), float(T), 0.0f, 1.0f};
            SDL_SetGPUViewport(pass, &vp);
            const SDL_Rect r{int(T) * (f % 3), int(T) * (f / 3), int(T), int(T)};
            SDL_SetGPUScissor(pass, &r);
            // the opaque draws, then the blended ones, in list order (the main pass's phases 1 and 3)
            for (int phase = 0; phase < 2; ++phase)
                for (std::size_t i = 0; i < ep.items.size(); ++i) {
                    const DrawItem& it = ep.items[i];
                    if (!ep.faces.empty() && !(i < ep.faces.size() && (ep.faces[i] >> f & 1))) continue;
                    // (reflect2 C: the View adds car draws too, their env lookup off)
                    if (it.overlay || it.effect || (it.shading != Shading::kTrack && it.shading != Shading::kCar) || is_line(it)) continue;
                    if ((it.blend == Blend::kAlpha) != (phase == 1)) continue;
                    draw_item(pass, cmd, fp, it, ld, bound, bound_vb, bound_ib);
                }
            if (ep.night_glow > 0.0f) env_night_glow(pass, cmd, fp, ep, f, bound, bound_vb, bound_ib);   // reflect2 G
        }
        env_face_ = false;
        SDL_EndGPURenderPass(pass);
        e.drawn = true;
        if (const char* dump = SDL_getenv("B3_ENV_DUMP")) {   // diagnostics: the atlas as a PPM in display units (x 2), alpha below
            const std::uint32_t w = e.w, h = e.h, bytes = w * h * 8;
            SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes, 0};
            SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureRegion reg{};
            reg.texture = e.res ? e.res : e.ms;
            reg.w = w;
            reg.h = h;
            reg.d = 1;
            SDL_GPUTextureTransferInfo dst{tb, 0, w, h};
            SDL_DownloadFromGPUTexture(cp, &reg, &dst);
            SDL_EndGPUCopyPass(cp);
            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            if (fence) {
                SDL_WaitForGPUFences(dev_, true, &fence, 1);
                SDL_ReleaseGPUFence(dev_, fence);
                const auto* src = static_cast<const std::uint16_t*>(SDL_MapGPUTransferBuffer(dev_, tb, false));
                auto half = [](std::uint16_t v) {
                    const int ex = (v >> 10) & 31, m = v & 1023;
                    const float f = ex == 0 ? std::ldexp(float(m), -24) : std::ldexp(float(m | 1024), ex - 25);
                    return (v & 0x8000) ? -f : f;
                };
                if (FILE* fo = std::fopen(dump, "wb")) {
                    std::fprintf(fo, "P6\n%u %u\n255\n", w, 2 * h);
                    for (int pass2 = 0; pass2 < 2; ++pass2)
                        for (std::uint32_t i = 0; i < w * h; ++i)
                            for (int c = 0; c < 3; ++c) {
                                const float v = pass2 == 0 ? 2.0f * half(src[4 * i + std::uint32_t(c)]) : half(src[4 * i + 3]);
                                std::fputc(int(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f), fo);
                            }
                    std::fclose(fo);
                }
                SDL_UnmapGPUTransferBuffer(dev_, tb);
            }
            SDL_ReleaseGPUTransferBuffer(dev_, tb);
        } else if (timed) {
            if (SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd)) {
                SDL_WaitForGPUFences(dev_, true, &fence, 1);
                SDL_ReleaseGPUFence(dev_, fence);
            }
            std::size_t n = 0;
            for (std::size_t i = 0; i < ep.items.size(); ++i)
                for (int f = 0; f < 6; ++f) n += ep.faces.empty() || (i < ep.faces.size() && (ep.faces[i] >> f & 1));
            std::fprintf(stderr, "env time: %.3f ms (record + submit + GPU), %zu items, %zu face draws\n", double(SDL_GetTicksNS() - t0) * 1e-6, ep.items.size(), n);
        } else {
            SDL_SubmitGPUCommandBuffer(cmd);
        }
        return kEnvWorldBase + std::uint32_t(slot);
    }

    // Scene pass into the GS frame (sky, opaque and alpha-tested draws, the full-screen specular, blended draws), then
    // the present pass into `out` (scale and clamp).
    void render(SDL_GPUCommandBuffer* cmd, Targets& t, SDL_GPUTexture* out, SDL_GPUTextureFormat fmt,
                const FrameParams& fp, const std::vector<DrawItem>& items) {
        // gameopts: MIRROR MODE takes the render-scale path (the 3D apart from the 2D stage) at any scale, 100 % included
        mirror_now_ = fp.mirror_x;   // (effective_scale counts its copy in the budget)
        if (fp.mirror_x) mirror_held_ = true;
        else if (mirror_held_) {   // the mirrored frames are over: at 100 % their target set goes (SDL frees it once the GPU is done)
            mirror_held_ = false;
            if (std::fabs(render_scale_ - 1.0f) < 1e-3f) {
                release_targets(scaled_win_);
                release_targets(scaled_off_);
            }
        }
        if ((std::fabs(render_scale_ - 1.0f) >= 1e-3f || fp.mirror_x) && render_scaled(cmd, t, out, fmt, fp, items)) return;
        render_scene(cmd, t, fp, items, true);
        present(cmd, t, out, fmt, fp);
    }

    // effects2: RENDER SCALE. The 3D picture (the scene, the effects, the post passes, the sun) is drawn into targets at the scale
    // (its depth, its kept bloom picture) and copied up into the output-sized frame with a bilinear filter; the 2D stage (the HUD
    // and the menus, DrawItem::overlay) is drawn over that at the output's size. 2D items that test depth against the scene
    // (overlay_ztest) go with the 3D, which is all the depth there is, and with them the 2D drawn before them (the list's order:
    // render_scale.h scaled_overlay_end). false: the targets could not be made (the caller draws as usual)
    bool render_scaled(SDL_GPUCommandBuffer* cmd, Targets& t, SDL_GPUTexture* out, SDL_GPUTextureFormat fmt, const FrameParams& fp,
                       const std::vector<DrawItem>& items) {
        Targets& sc = &t == &win_targets_ ? scaled_win_ : scaled_off_;
        const float scale = effective_scale(t);
        if (std::fabs(scale - 1.0f) < 1e-3f && !fp.mirror_x) return false;   // perf4: no supersampled target fits: drawn at 100 %
        const std::uint32_t sw = scaled_side(t.w, scale), sh = scaled_side(t.h, scale);
        if (!ensure_targets(sc, kSceneFormat, sw, sh, false)) {
            if (scale > 1.0f) {   // perf4: out of GPU memory for the larger set: 100 % for this output size (logged once)
                release_targets(sc);
                t.ss_failed = true;
                SDL_Log("b3gfx: RENDER SCALE %.0f%%: no %ux%u targets (%s): drawn at 100%%", double(scale) * 100.0, unsigned(sw), unsigned(sh), SDL_GetError());
            }
            return false;
        }
        line_min_ = scale;   // the line width as at 100%: output pixels, so a scaled target may hold less than one of its own
        render_scene(cmd, sc, fp, items, true, float(t.h) * scale, 1);
        line_min_ = 1.0f;   // the 3D picture (and the depth-tested 2D) in sc.scene (sc.scene_resolve with MSAA)
        SDL_GPUTexture* picture = mirrored(cmd, sc, fp.mirror_x);   // gameopts: reversed when MIRROR MODE's
        const bool same = sc.w == t.w && sc.h == t.h;   // (MIRROR MODE at 100 %: a plain copy)
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
            // perf4: above 100% the box filter's downscale (texel-centre taps: the point sampler), below the upscale as before
            const bool down = scale > 1.0f && !same;
            const PostUniforms u{{4, 0, down ? 3.0f : sharp_upscale_ && !same ? 2.0f : 0.0f, 0}, {1, kUpscaleSharpen, 0, 0},
                                 {1.0f / float(t.w), 1.0f / float(t.h), 1.0f / float(sc.w), 1.0f / float(sc.h)}};
            SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
            SDL_GPUTexture* src = picture;
            SDL_GPUSampler* smp = down || same ? clamp_sampler_ : overlay_sampler_;
            SDL_GPUTextureSamplerBinding tsb[2] = {{src, smp}, {src, smp}};
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

    // gameopts (FrameParams::mirror_x): the picture `t` holds after render_scene (its resolve with MSAA), or with `mirror` its
    // copy reversed left to right (t.mir, made on first use; a blit outside any pass). The copy failing: the picture as it is
    SDL_GPUTexture* mirrored(SDL_GPUCommandBuffer* cmd, Targets& t, bool mirror) {
        SDL_GPUTexture* src = msaa_ > 1 ? t.scene_resolve : t.scene;
        if (!mirror) return src;
        if (!ensure_buf(t.mir, t.w, t.h)) {
            static bool told = false;
            if (!told) { told = true; SDL_Log("b3gfx: MIRROR MODE: no %ux%u copy (%s): drawn unmirrored", unsigned(t.w), unsigned(t.h), SDL_GetError()); }
            return src;
        }
        SDL_GPUBlitInfo bi{};
        bi.source = SDL_GPUBlitRegion{src, 0, 0, 0, 0, t.w, t.h};
        bi.destination = SDL_GPUBlitRegion{t.mir.t, 0, 0, 0, 0, t.w, t.h};
        bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
        bi.flip_mode = SDL_FLIP_HORIZONTAL;
        bi.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(cmd, &bi);
        return t.mir.t;
    }

    // perf4: the render scale used for output set `t` (the window's or the offscreen / F12 one): below 1 as set; above 1 capped
    // (b3gfx/render_scale.h) to what keeps the larger set within the side limit and the memory budget (from the card's
    // device-local memory, $B3_SUPERSAMPLE_MB overrides; 1 GB when not known). The offscreen set while the window has a
    // supersampled one (an F12 shot) gets what the budget has left beside it, and is drawn at 100% when the window's scale
    // does not fit there. 1 after this set's targets could not be made. A cap is logged once per output size.
    float effective_scale(const Targets& t) {
        if (render_scale_ <= 1.0f) return render_scale_;
        if (t.ss_failed) return 1.0f;
        if (ss_budget_mb_ < 0) {
            if (const char* e = std::getenv("B3_SUPERSAMPLE_MB"); e && std::atof(e) > 0) ss_budget_mb_ = std::atof(e);
            else {
                const double vram = device_local_mb();   // texpack: Vulkan's heap, or DXGI's dedicated memory on Direct3D 12
                ss_budget_mb_ = supersample_budget_mb(vram);
                SDL_Log("b3gfx: supersampling budget %.0f MB (device-local memory %s)", ss_budget_mb_,
                        vram > 0 ? (std::to_string(int(vram)) + " MB").c_str() : "not known");
            }
        }
        static const bool lean = std::getenv("B3_BLOOM_LEAN") != nullptr;
        // finalfix: photo mode's motion blur (two full-size buffers) while it can run here; the sun-shadow map's fixed size
        const double bpp = scaled_bytes_per_pixel(msaa_, lean, !motion_failed_, mirror_now_);   // gameopts: Targets::mir
        double budget = ss_budget_mb_;
        if (const std::uint32_t side = std::max(sh_side_, warm_sun_shadows_ ? sun_shadow_side_opt_ : 0u))
            budget = std::max(1.0, budget - sun_shadow_map_mb(side));   // (> 0: capped_scale reads 0 as no budget)
        const bool second = &t != &win_targets_ && scaled_win_.depth;   // an F12 / offscreen set beside the window's
        if (second) budget -= double(scaled_win_.w) * double(scaled_win_.h) * bpp / 1048576.0;
        const float s = capped_scale(render_scale_, t.w, t.h, bpp, budget, kMaxTextureSide);
        if (second) {
            const float win = capped_scale(render_scale_, win_targets_.w, win_targets_.h, bpp, ss_budget_mb_, kMaxTextureSide);
            if (s < win - 0.005f) {
                SDL_Log("b3gfx: RENDER SCALE %.0f%%: the shot's second set does not fit beside the window's (%.0f MB left): shot at 100%%",
                        double(win) * 100.0, budget);
                return 1.0f;
            }
        }
        if (s < render_scale_ - 0.005f && (ss_logged_.first != t.w || ss_logged_.second != t.h)) {
            ss_logged_ = {t.w, t.h};
            SDL_Log("b3gfx: RENDER SCALE %.0f%% at %ux%u (MSAA %d) capped to %.0f%%: %ux%u targets (~%.0f MB; limits %d px a side, %.0f MB)",
                    double(render_scale_) * 100.0, unsigned(t.w), unsigned(t.h), msaa_, double(s) * 100.0, unsigned(scaled_side(t.w, s)),
                    unsigned(scaled_side(t.h, s)), double(scaled_side(t.w, s)) * double(scaled_side(t.h, s)) * bpp / 1048576.0, kMaxTextureSide, budget);
        }
        return s;
    }

    // The 2D stage (hud1): DrawItem::overlay, in list order, with its scissor
    void draw_overlay(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Targets& t, const FrameParams& fp,
                      const std::vector<DrawItem>& items, const float ld[3], SDL_GPUGraphicsPipeline*& bound, BufferId& bound_vb,
                      BufferId& bound_ib, int only = 0) {
        B3PERF_SCOPE(kCmdOverlay);
        bool clipped = false, neg = false;   // neg: a subtractive draw since the last clamp_frame
        const auto clamp = [&] {
            if (clipped) {
                const SDL_Rect r{0, 0, int(t.w), int(t.h)};
                SDL_SetGPUScissor(pass, &r);
                clipped = false;
            }
            clamp_frame(pass, cmd, bound);
            neg = false;
        };
        // effects2 (render scale): the depth-tested 2D goes with the 3D (only == 1), the rest over the upscaled picture
        // (only == 2). The split keeps the list's order (render_scale.h scaled_overlay_end): 1.3.0 drew only the depth-tested
        // items with the 3D, under the 2D drawn before them in the game's order, so the Crash Nav backdrop hid the globe
        const std::size_t split = only ? scaled_overlay_end(items) : 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const DrawItem& it = items[i];
            if (only == 1 && i >= split) break;
            if (only == 2 && i < split) continue;
            if (!it.overlay) continue;
            const bool sub = subtracts(it);
            if (neg && !sub) clamp();

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
            neg = neg || sub;
        }
        if (neg) clamp();
    }

    // gsoff1 (§29): the views of a split-screen frame, each in its own targets (`vt[k]`, sized to its rect), placed into
    // `t`'s scene (the post pipeline's copy: Cs + Cd on RGB over a cleared frame), the 2D stage over the whole frame, then
    // the present pass
    // false: a view's targets could not be made; nothing was recorded (the caller does not present this frame's picture)
    bool render_views(SDL_GPUCommandBuffer* cmd, Targets& t, std::vector<Targets>& vt, SDL_GPUTexture* out, SDL_GPUTextureFormat fmt,
                      const std::vector<ViewPass>& views, const FrameParams& ofp, const std::vector<DrawItem>& overlay) {
        if (vt.size() < views.size()) vt.resize(views.size());
        mirror_now_ = false;   // gameopts: a mirrored view's copy counted in effective_scale's budget
        for (const ViewPass& v : views) mirror_now_ = mirror_now_ || v.fp.mirror_x;
        struct Px { int x, y, w, h; };
        std::vector<Px> px(views.size());
        float vscale = effective_scale(t);   // perf4: above 1 capped for the whole output's size
        bool scl = std::fabs(vscale - 1.0f) >= 1e-3f;
        for (std::size_t k = 0; k < views.size(); ++k) {   // every view's targets before anything is recorded
            const float* r = views[k].rect;
            const int x0 = int(std::floor(r[0] * float(t.w) + 0.5f)), y0 = int(std::floor(r[1] * float(t.h) + 0.5f));
            const int x1 = int(std::floor(r[2] * float(t.w) + 0.5f)), y1 = int(std::floor(r[3] * float(t.h) + 0.5f));
            px[k] = {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
            // effects2: RENDER SCALE: the view's picture at the scale (1: the view's own size, as before), copied up below
            const std::uint32_t vw = scl ? scaled_side(std::uint32_t(px[k].w), vscale) : std::uint32_t(px[k].w);
            const std::uint32_t vh = scl ? scaled_side(std::uint32_t(px[k].h), vscale) : std::uint32_t(px[k].h);
            if (!ensure_targets(vt[k], kSceneFormat, vw, vh, false)) {
                if (!(vscale > 1.0f)) return false;
                // perf4: the supersampled views do not fit: this set at 100% (logged once), the views made again
                for (std::size_t j = 0; j <= k; ++j) release_targets(vt[j]);
                t.ss_failed = true;
                SDL_Log("b3gfx: RENDER SCALE %.0f%%: no %ux%u split-screen targets (%s): drawn at 100%%", double(vscale) * 100.0, unsigned(vw), unsigned(vh), SDL_GetError());
                vscale = 1.0f;
                scl = false;
                k = std::size_t(-1);   // (the loop's ++k: from the first view again)
                continue;
            }
        }
        std::vector<SDL_GPUTexture*> mirror_src(views.size(), nullptr);
        for (std::size_t k = 0; k < views.size(); ++k) {
            line_min_ = scl ? vscale : 1.0f;
            render_scene(cmd, vt[k], views[k].fp, views[k].items, false, scl ? float(t.h) * vscale : float(t.h));
            line_min_ = 1.0f;   // lines1: a GS pixel of the whole output
            mirror_src[k] = mirrored(cmd, vt[k], views[k].fp.mirror_x);   // gameopts: each view about its own middle
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
                const bool down = vt[k].w > std::uint32_t(px[k].w);   // perf4: supersampled: the box filter's downscale
                const PostUniforms u{{4, 0, down ? 3.0f : sharp_upscale_ ? 2.0f : 0.0f, 0}, {1, kUpscaleSharpen, 0, 0},
                                     {1.0f / float(px[k].w), 1.0f / float(px[k].h), 1.0f / float(vt[k].w), 1.0f / float(vt[k].h)}};
                SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
                SDL_GPUTexture* src = mirror_src[k];   // gameopts: the view's picture, reversed when MIRROR MODE's
                SDL_GPUSampler* smp = !down && (vt[k].w != std::uint32_t(px[k].w) || vt[k].h != std::uint32_t(px[k].h)) ? overlay_sampler_ : clamp_sampler_;   // effects2: bilinear up
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
        return true;
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
        // scenedepth: with scene_depth_on_, a frame with a pass that reads the depth (the depth of field, the AO, the motion
        // blur, night's lights) keeps the scene pass's own (sd_, set below) instead of drawing the depth pre-pass
        // (night: what night_lights below needs: the spot pass with something it lights; a rain frame by day has none)
        const bool sd_night = fp.night.on && fp.night.spot &&
                              (fp.night.lamps > 0 || fp.night.streets > 0 || fp.night.boards > 0 || fp.night.tunnel[0] * fp.night.enclosed > 1e-3f ||
                               (fp.sun_shadow.on && fp.sun_shadow.strength > 0));
        // (the AO's break needs R32F's MAX blend: without it, a frame with the AO takes the pre-pass for all its readers)
        // (nightdepth: in the default mode only night's frames: scene_depth_frame)
        const bool sdepth = scene_depth_on_ && scene_depth_frame(scene_depth_mode_, sd_night) && !sd_.broken &&
                            (fp.dof.on || fp.surface.ao > 0 || fp.motion.on || sd_night) &&
                            (fp.surface.ao <= 0 || sd_.blend) && dof_ready() && ensure_scene_depth(t);
        depth_src_ = sdepth ? (msaa_ > 1 ? (scene_depth_edge() > 0 ? t.sd_fix : t.sd_res) : t.sd) : t.dof_depth;
        const auto depth_fix = [&]() { if (sdepth) scene_depth_fix(cmd, t); };   // (after an end of the scene pass, before a reader)
        // photo2: the depth of field (its pre-pass now, its passes after the zoom blur); off: none of it runs
        DofUniforms dofu{};
        const bool dof = fp.dof.on && fp.dof.radius > 0 && dof_ready() && ensure_buf(t.dw, (t.w + 1) / 2, (t.h + 1) / 2) &&
                         ensure_buf(t.dg, (t.w + 1) / 2, (t.h + 1) / 2) && dof_uniforms(t, fp, dofu) && (sdepth || dof_depth_pass(cmd, t, fp, items));
        if (fp.surface.relief > 0 || fp.surface.sheen > 0 || fp.surface.ao > 0) {   // surface: once, what it works with
            static bool told = false;
            if (!told) {
                told = true;
                SDL_Log("b3gfx: surface relief %.2f sheen %.2f ao %.2f (radius %.2f m); sun (%.2f %.2f %.2f), specular (%.3f %.3f %.3f)",
                        fp.surface.relief, fp.surface.sheen, fp.surface.ao, fp.surface.ao_radius, fp.to_sun[0], fp.to_sun[1], fp.to_sun[2],
                        fp.specular[0], fp.specular[1], fp.specular[2]);
            }
        }
        // surface: the ambient occlusion (the same depth pre-pass, then its half-size pass); off: none of it runs
        bool depth_drawn = dof;   // the depth pre-pass (once for all who read it)
        const auto depth_pass = [&]() { return sdepth || depth_drawn || (depth_drawn = dof_depth_pass(cmd, t, fp, items)); };
        const bool ao = fp.surface.ao > 0 && dof_ready() && ao_ready() && ensure_buf(t.ao, (t.w + 1) / 2, (t.h + 1) / 2) && ensure_buf(t.ao2, (t.w + 1) / 2, (t.h + 1) / 2) &&
                        depth_pass() && (sdepth || ao_pass(cmd, t, fp));
        // scenedepth: the AO's passes read the opaque scene's depth: the scene pass ends after its opaque draws (colour and
        // depth stored), runs them and resumes
        const bool ao_split = ao && sdepth;
        // photoblur: photo mode's motion blur (the same depth pre-pass; its passes at the start of the 2D stage's phase)
        const std::uint32_t mtw = (t.w + kMotionTile - 1) / kMotionTile, mth = (t.h + kMotionTile - 1) / kMotionTile;
        // (converged in the window: A is the picture, no pre-pass or streaks needed)
        const std::uint64_t msig = fp.motion.on ? motion_sig(t, fp, items) : 0;
        const bool mdone = fp.motion.on && motion_window(t) && msig == t.ma_sig && t.ma_frames >= motion_frames(fp) && t.ma.t;
        bool motion = fp.motion.on && dof_ready() && motion_ready();
        if (motion && !(ensure_buf(t.mv, t.w, t.h) && ensure_buf(t.ma, t.w, t.h) && ensure_buf(t.mt, mtw, mth) && ensure_buf(t.mn, mtw, mth))) {
            static bool told = false;   // finalfix: a missing blur is diagnosable
            if (!told) {
                told = true;
                SDL_Log("b3gfx: photo motion blur: its buffers (%ux%u) could not be made: %s (photo mode without the blur)", unsigned(t.w),
                        unsigned(t.h), SDL_GetError());
            }
            motion = false;
        }
        motion = motion && (mdone || depth_pass());
        // SUN SHADOWS: the caster pass (before the scene pass); off: none of it runs and no track draw takes the variant
        const bool sun_shadow = fp.sun_shadow.on && fp.sun_shadow.strength > 0 && sun_shadow_ready() && sun_shadow_pass(cmd, fp, items);
        // MSAA + the PC bloom: the pass before the bloom resolves into bloom_src and does not store its samples; the resumed
        // pass starts with one draw (the resolved picture + the glow) instead of reloading them (a 4-sample 46 MB scene
        // is 184 MB each way at 3200 x 1800). The GS-sized path (post_game, golden) keeps the original split.
        // Measured in a real window on an RTX 4080 SUPER (5120x1440, MSAA 8): the lean form is SLOWER than the original split
        // (218 fps against 238 in the same race replay), so the split is the default; $B3_BLOOM_LEAN=1 opts into the lean form.
        static const bool lean_env = std::getenv("B3_BLOOM_LEAN") != nullptr;
        const bool lean_on = bloom_lean_ >= 0 ? bloom_lean_ != 0 : lean_env;   // fifauto: B3_PERF_BISECT's step
        const bool lean = bloom && msaa_ > 1 && !fp.post_game && lean_on && !motion && post_lean_fs_ && ensure_bloom_src(t);
        // night mode (§17): the scene pass ends before the grade (its samples kept, resolved for the grade to read) and
        // resumes with its depth (the grade's sky / world split, the headlight pools, the coronas' depth test)
        const bool night = fp.night.on && night_fs_ && ensure_buf(t.nc, t.w, t.h);
        // nightlights (§17.2): the projected beams and the tunnel's per-pixel fade need the single-sample depth (the DOF's
        // pre-pass: the scene's MSAA depth cannot be sampled through SDL GPU), drawn here unless the DOF, the AO or the motion
        // blur did
        // (round 3e) the floodlights count too (a frame with boards alone drew none); scenedepth: the scene pass's own depth
        // (with scene_depth_on_)
        const bool night_lights = night && fp.night.spot &&
                                  (fp.night.lamps > 0 || fp.night.streets > 0 || fp.night.boards > 0 || fp.night.tunnel[0] * fp.night.enclosed > 1e-3f || sun_shadow) &&
                                  beam_fs_ && dof_ready();
        if (night_lights && !sdepth && !depth_drawn) depth_drawn = dof_depth_pass(cmd, t, fp, items);
        const bool night_spot = night_lights && (sdepth || depth_drawn);   // (photoblur: the one pre-pass of the frame)
        // SUN SHADOWS at night (brief D): with the spot pass the moon shadow's receiver term is that pass's (a beam fills
        // the shadow); the track draws take none, so the pre-grade copy is unshadowed. Without it (LOW, PS2 ORIGINAL) the
        // shadow stays in mesh.frag as by day.
        const bool moon_shadow = sun_shadow && night_spot;
        SDL_GPUTexture* const resolve_to = lean ? t.bloom_src : t.scene_resolve;
        if (msaa_ > 1) {
            ci.store_op = (!lean && (blur || bloom || dof || motion)) || night || ao_split ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
            ci.resolve_texture = night ? t.nc.t : resolve_to;   // night: the part before the grade resolves into its copy
        } else {
            ci.store_op = SDL_GPU_STOREOP_STORE;
        }
        // the depth survives a post pass's break only when the 2D stage after it tests depth (fe2: the World Tour globe's
        // batches, DrawItem::overlay_ztest); otherwise it is not stored (no depth traffic on tile GPUs)
        const bool keep_depth = overlay && overlay_only != 2 && (blur || bloom || dof || motion) &&
                                std::any_of(items.begin(), items.end(), [](const DrawItem& it) { return it.overlay && it.overlay_ztest; });
        SDL_GPUDepthStencilTargetInfo di{};
        di.texture = t.depth;
        di.clear_depth = 0.0f;  // reverse-Z: far
        di.load_op = SDL_GPU_LOADOP_CLEAR;
        di.store_op = night || keep_depth || ao_split ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;   // night: the grade's sky / world split; rfix: depth-tested 2D
        di.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        di.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        // scenedepth: every begin of the scene pass below gets the depth target (perf_begin_pass), until the pass is done
        t_sd = &sd_;   // (this thread's scene pass: perf_begin_pass / perf_bind; cleared after it)
        sd_.scene = sdepth ? t.scene : nullptr;
        sd_.target = t.sd;
        sd_.resolve = msaa_ > 1 ? t.sd_res : nullptr;
        sd_.begun = false;
        sd_.mask_on = sd_.bound_mask = false;
        // the last pass that reads it: the target is not stored at that break and is left out after it
        const int sd_last = dof || motion ? 6 : night_spot ? 5 : ao_split ? 2 : -1;
        sd_.keep = !(sd_last == 2 || (sd_last == 5 && !ao_split));
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
        sh_live_ = sun_shadow && !moon_shadow;   // the track draws below take the map (draw_item); at night the spot pass does
        float ld[3] = {fp.light_dir[0], fp.light_dir[1], fp.light_dir[2]};
        const float ll = std::sqrt(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);
        if (ll > 0) for (float& c : ld) c /= ll;
        SDL_GPUGraphicsPipeline* bound = nullptr;
        BufferId bound_vb = kNone, bound_ib = kNone;
        // a post pass reads the 3D picture: end the scene pass (keeping the MSAA samples), run its chain, resume the pass
        auto resume = [&]() {
            ci.load_op = SDL_GPU_LOADOP_LOAD;
            di.load_op = keep_depth ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
            pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
            bound = nullptr;
            bound_vb = bound_ib = kNone;
        };
        bool neg = false;   // a line mesh (Cd - Cs) drawn since the last clamp_frame
        for (int phase = 0; phase < 7; ++phase) {
            if (neg && phase >= 2) { clamp_frame(pass, cmd, bound); neg = false; }   // the specular and on read the frame
            if (phase == 6 && motion) {   // photoblur: after the night grade and its pools, before the bloom and the rest
                SDL_EndGPURenderPass(pass);
                depth_fix();
                SDL_GPUTexture* a = motion_chain(cmd, t, fp, msig);
                resume();
                if (SDL_GPUGraphicsPipeline* cp = a ? motion_pipeline(2) : nullptr) {
                    SDL_BindGPUGraphicsPipeline(pass, cp);
                    bound = cp;
                    MotionUniforms u{};
                    if (motion_debug_) motion_uniforms(t, fp, u);
                    u.a[0] = 2;
                    u.a[2] = motion_debug_ ? 1.0f : 0.0f;
                    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
                    SDL_GPUTextureSamplerBinding tsb[3] = {{a, clamp_sampler_}, {t.mv.t, clamp_sampler_}, {t.mn.t, clamp_sampler_}};
                    SDL_BindGPUFragmentSamplers(pass, 0, tsb, 3);
                    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                }
            }
            if (phase == 6 && lean) {   // post1, one-draw form
                SDL_EndGPURenderPass(pass);
                SDL_GPUTexture* pb = bloom_chain(cmd, t, fp, t.bloom_src);
                ci.load_op = SDL_GPU_LOADOP_DONT_CARE;   // the draw below covers every pixel
                ci.store_op = blur || dof ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
                ci.resolve_texture = t.scene_resolve;
                di.load_op = keep_depth ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
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
                depth_fix();
                SDL_GPUTexture* g = dof_chain(cmd, t, dofu);
                resume();
                if (SDL_GPUGraphicsPipeline* cp = g ? dof_pipeline(true) : nullptr) {
                    SDL_BindGPUGraphicsPipeline(pass, cp);
                    bound = cp;
                    DofUniforms u = dofu;
                    u.a[0] = 2;
                    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
                    SDL_GPUTextureSamplerBinding tsb[3] = {{g, effect_sampler_}, {g, clamp_sampler_}, {depth_src_, clamp_sampler_}};
                    SDL_BindGPUFragmentSamplers(pass, 0, tsb, 3);
                    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                }
            }
            if (phase == 6 && night && fp.drops.count > 0 && t.nc.t) {   // weather (§38): the lens drops, before the 2D stage
                if (SDL_GPUGraphicsPipeline* dp = drops_pipeline()) {
                    DropUniforms u{};
                    const FrameParams::Night& n = fp.night;
                    const float a[4] = {float(std::min(fp.drops.count, FrameParams::Drops::kMax)), fp.drops.aspect, n.lit ? 1.0f : 0.0f, 0.0f};
                    const float g[4] = {n.sat, n.gamma, std::max(n.pivot, 0.01f), 0.0f};
                    std::memcpy(u.a, a, sizeof a);
                    std::memcpy(u.g, g, sizeof g);
                    for (int k = 0; k < 3; ++k) u.tint[k] = n.world[k];
                    std::memcpy(u.d, fp.drops.drop, sizeof u.d);
                    SDL_BindGPUGraphicsPipeline(pass, dp);
                    bound = dp;
                    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
                    SDL_GPUTextureSamplerBinding tsb{t.nc.t, clamp_sampler_};
                    SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
                    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                }
            }
            if (phase == 6) {   // the 2D stage (hud1): DrawItem::overlay, in list order, with its scissor
                if (overlay) draw_overlay(pass, cmd, t, fp, items, ld, bound, bound_vb, bound_ib, overlay_only);
                continue;
            }
            if (phase == 2 && ao) {   // surface: over the opaque scene, before the specular
                if (ao_split) {   // scenedepth: its passes now, on the opaque scene's depth (resolved at this end)
                    // the post-fog draws' depth first (drawn for real later; the pre-pass had them): the depth target only
                    sd_.depth_only = true;
                    bound = nullptr;
                    for (const DrawItem& it : items)
                        if (it.post_fog && !it.overlay && !it.effect && it.shading != Shading::kSky && !it.night_light)
                            draw_item(pass, cmd, fp, it, ld, bound, bound_vb, bound_ib);
                    sd_.depth_only = false;
                    bound = nullptr;
                    SDL_EndGPURenderPass(pass);
                    depth_fix();
                    ao_pass(cmd, t, fp);
                    if (sd_last == 2) sd_.scene = nullptr;   // (no later reader)
                    sd_.keep = sd_last != 5;
                    ci.load_op = SDL_GPU_LOADOP_LOAD;
                    di.load_op = SDL_GPU_LOADOP_LOAD;
                    pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
                    bound = nullptr;
                    bound_vb = bound_ib = kNone;
                }
                draw_ao(pass, cmd, t, fp, bound);
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
            if (phase == 5 && night) {   // night mode (remaster-features §17)
                // the post-fog draws that blend over the frame (smoke, dust, crash objects) are graded with it
                for (const DrawItem& it : items)
                    if (!it.overlay && it.post_fog && !it.night_light && !night_additive(it)) draw_item(pass, cmd, fp, it, ld, bound, bound_vb, bound_ib);
                SDL_EndGPURenderPass(pass);
                if (night_spot) depth_fix();
                if (sd_last == 5) sd_.scene = nullptr;   // scenedepth: no later reader
                SDL_GPUTexture* src = t.nc.t;   // MSAA: the pass resolved into it; else copied here
                if (msaa_ > 1) {
                    ci.resolve_texture = resolve_to;
                } else {
                    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
                    SDL_GPUTextureLocation from{}, to{};
                    from.texture = t.scene;
                    to.texture = t.nc.t;
                    SDL_CopyGPUTextureToTexture(cp, &from, &to, t.w, t.h, 1, false);
                    SDL_EndGPUCopyPass(cp);
                }
                ci.load_op = SDL_GPU_LOADOP_LOAD;
                di.load_op = SDL_GPU_LOADOP_LOAD;
                pass = SDL_BeginGPURenderPass(cmd, &ci, 1, &di);
                bound = nullptr;
                bound_vb = bound_ib = kNone;
                // the world's grade; nightlights: then the projected beams and the tunnel's fade (or, on the presets
                // without them, the ground pools and the tunnel's fade over the whole world)
                draw_night_grade(pass, cmd, fp, src, false, bound, night_spot ? 0.0f : fp.night.tunnel[0] * fp.night.enclosed);
                if (night_spot) draw_night_beams(pass, cmd, t, fp, src, bound, moon_shadow);
                else draw_night_lights(pass, cmd, fp, items, src, bound, bound_vb, bound_ib);
                draw_night_grade(pass, cmd, fp, src, true, bound);   // after the pools: none of their light on the sky
                if (fp.night.lit) draw_night_emissive(pass, cmd, fp, items, src, bound, bound_vb, bound_ib);   // nightlights: the tunnels' fixtures
                else if (fp.day_glow > 0) draw_night_emissive(pass, cmd, fp, items, white_, bound, bound_vb, bound_ib, true);   // weather: rain by day
                for (const DrawItem& it : items)
                    if (!it.overlay && it.post_fog && !it.night_light && night_additive(it)) draw_item(pass, cmd, fp, it, ld, bound, bound_vb, bound_ib);
                continue;
            }
            // TUNNEL LIGHTS by day (§35.4): the fittings' glow over the opaque world, before the post-fog effects
            if (phase == 5 && !night && fp.day_glow > 0) draw_night_emissive(pass, cmd, fp, items, white_, bound, bound_vb, bound_ib, true);
            for (const DrawItem& it : items) {
                if (it.overlay || it.night_light) continue;   // (night mode's pools: only in its stage above)
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
                    if (phase == 1) { draw_item(pass, cmd, fp, it, ld, bound, bound_vb, bound_ib); neg = true; }
                    continue;
                }
                if (phase < 4 && (phase == 0 ? !sky : (sky || blended != (phase == 3)))) continue;
                // the opaque ones overwrite; a car (its glass blends) reads the frame
                if (neg && (it.shading == Shading::kCar || it.blend == Blend::kAlpha)) { clamp_frame(pass, cmd, bound); neg = false; }
                draw_item(pass, cmd, fp, it, ld, bound, bound_vb, bound_ib);
            }
        }
        SDL_EndGPURenderPass(pass);
        sh_live_ = false;
        sd_.scene = nullptr;   // scenedepth
        t_sd = nullptr;
    }

    // A window frame that could not be drawn (its targets could not be made: out of memory). The swapchain texture is
    // already acquired, so the command buffer must be submitted, and it then presents: the last frame again (the targets
    // still hold it) or black, never the texture's undefined contents. Logged once; the next frame tries again.
    void no_frame(SDL_GPUCommandBuffer* cmd, Targets& t, SDL_GPUTexture* sw, const FrameParams& fp, const char* what) {
        static bool told = false;
        if (!told) { SDL_Log("b3gfx: no frame drawn (%s: %s); retrying every frame", what, SDL_GetError()); told = true; }
        if (t.shown && t.scene) present(cmd, t, sw, swap_format_, fp);
        else {
            SDL_GPUColorTargetInfo oi{};
            oi.texture = sw;
            oi.clear_color = SDL_FColor{0, 0, 0, 1};
            oi.load_op = SDL_GPU_LOADOP_CLEAR;
            oi.store_op = SDL_GPU_STOREOP_STORE;
            SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, &oi, 1, nullptr));
        }
        SDL_SubmitGPUCommandBuffer(cmd);
    }

    // The present pass: the GS frame scaled (PresentDrawBuffer) into `out`
    void present(SDL_GPUCommandBuffer* cmd, Targets& t, SDL_GPUTexture* out, SDL_GPUTextureFormat fmt, const FrameParams& fp) {
        B3PERF_SCOPE(kCmdPresent);
        SDL_GPUColorTargetInfo oi{};
        oi.texture = out;
        oi.load_op = SDL_GPU_LOADOP_DONT_CARE;
        oi.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pp = SDL_BeginGPURenderPass(cmd, &oi, 1, nullptr);
        t.shown = true;
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
        flush_batch();   // perf4: the route batch's uploads were called first
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
    // Everything recorded for the GPU and not submitted yet: the frame's dynamic uploads, the route's batch (at most one of
    // the two is open: each submits the other before it records)
    void flush_uploads() {
        flush_dyn();
        flush_batch();
    }
    // perf4 (Device::upload_batch): while on, create_texture / make_buffer copy into bat_'s staging buffer (kBatchBytes, mapped
    // with cycle = true once per batch) and record into its one copy pass; the generated mip chains go after the copy pass,
    // then one submit. A full buffer submits and starts again (the order kept); off submits and frees the buffer.
    // Window devices only. SDL 3.4.2's Vulkan backend defragments its memory at a submit, recording the copies (old ->
    // new texture or buffer) into the command buffer being submitted, with no check for resources made and uploaded in it:
    // textures created and uploaded in one batch after a route's resources were freed (fragmented memory) came out as noise.
    // A device with a window defragments only in a command buffer that acquired the swapchain (a frame, after the batch
    // went up); one without (offscreen, the golden) at every submit, so there each upload keeps its own submit. Measured with
    // a standalone SDL test: 20 / 290 of 1500 levels wrong offscreen, 0 with a claimed window.
    // uploadbatch (2026-11-22, read in SDL 3.4.2's src/gpu/vulkan/SDL_gpu_vulkan.c; SDL's API documents none of this, so the
    // version check below stays exact and a newer SDL must be read again before it is widened):
    //   - VULKAN_Submit: performCleanups = (claimedWindowCount > 0 && swapchainRequested) || claimedWindowCount == 0, and
    //     VULKAN_INTERNAL_DefragmentMemory runs only when performCleanups (and allocations are marked, nothing in flight);
    //   - allocations are only marked for it when an allocation finds no free region (the sub-allocator), never moved there.
    // SDL's code, not the driver's: the same on every Vulkan driver (AMD, NVIDIA, Intel, Mesa). And no frame can run while a
    // batch is open: a window frame's first step (draw_frame / draw_views: flush_uploads, before the swapchain is acquired)
    // submits it, and the route loads on this thread between
    // frames, so a frame's defragmentation moves only batch resources whose uploads were submitted before it (SDL's own
    // barriers, the submission order). B3_UPLOAD_BATCH / pc_settings upload_batch: include/b3gfx/upload_batch.h.
    static constexpr std::uint32_t kBatchBytes = 64u << 20;
    void upload_batch(bool on) override {
        const bool wanted = upload_batch_wanted(std::getenv("B3_UPLOAD_BATCH"), upload_batch_opt_, window_ != nullptr);
        if (on && !wanted && !bat_.off_logged && window_) {   // (offscreen: never batched, nothing to say)
            bat_.off_logged = true;
            SDL_Log("b3gfx: route upload batching off (%s): one submit per upload", std::getenv("B3_UPLOAD_BATCH") ? "B3_UPLOAD_BATCH" : "pc_settings upload_batch = false");
        }
        if (!wanted) on = false;
        // only the tested combination (SDL 3.4.2, Vulkan: its defragmentation read in its source, see above); Metal, D3D12 and
        // other SDL versions keep one submit per upload. Not again after a batch's submit failed (flush_batch).
        static const bool tested = SDL_GetVersion() == SDL_VERSIONNUM(3, 4, 2) && std::strcmp(SDL_GetGPUDeviceDriver(dev_), "vulkan") == 0;
        if (!tested || bat_.broken) on = false;
        if (on == bat_.on) return;
        flush_uploads();
        bat_.on = on;
        if (on) {
            bat_.t0 = SDL_GetTicksNS();
            bat_.submits = bat_.total = 0;
            return;
        }
        if (bat_.tb) { SDL_ReleaseGPUTransferBuffer(dev_, bat_.tb); bat_.tb = nullptr; }   // (released once the GPU is done with it)
        if (bat_.total)
            SDL_Log("b3gfx: route uploads batched: %u uploads in %u submits, %.0f ms", bat_.total, bat_.submits, double(SDL_GetTicksNS() - bat_.t0) * 1e-6);
    }
    std::uint8_t* batch_reserve(std::uint32_t bytes, std::uint32_t& off) {
        if (bytes > kBatchBytes || bat_.broken) return nullptr;   // (the caller uploads on its own)
        const std::uint32_t at = (bat_.used + 255U) & ~255U;
        if (bat_.cmd && std::uint64_t(at) + bytes > kBatchBytes) flush_batch();
        if (!bat_.tb) {
            SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, kBatchBytes, 0};
            bat_.tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
            if (!bat_.tb) return nullptr;
        }
        if (!bat_.cmd) {
            bat_.map = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(dev_, bat_.tb, true));
            if (!bat_.map) return nullptr;
            bat_.cmd = SDL_AcquireGPUCommandBuffer(dev_);
            bat_.cp = bat_.cmd ? SDL_BeginGPUCopyPass(bat_.cmd) : nullptr;
            if (!bat_.cp) {
                if (bat_.cmd) SDL_CancelGPUCommandBuffer(bat_.cmd);
                bat_.cmd = nullptr;
                SDL_UnmapGPUTransferBuffer(dev_, bat_.tb);
                bat_.map = nullptr;
                return nullptr;
            }
            bat_.used = 0;
        }
        off = (bat_.used + 255U) & ~255U;
        bat_.used = off + bytes;
        return bat_.map + off;
    }
    void flush_batch() {
        if (!bat_.cmd) return;
        SDL_UnmapGPUTransferBuffer(dev_, bat_.tb);
        bat_.map = nullptr;
        SDL_EndGPUCopyPass(bat_.cp);
        for (SDL_GPUTexture* t : bat_.mips) SDL_GenerateMipmapsForGPUTexture(bat_.cmd, t);
        bat_.mips.clear();
        const bool ok = SDL_SubmitGPUCommandBuffer(bat_.cmd);
        bat_.cmd = nullptr;
        bat_.cp = nullptr;
        bat_.used = 0;
        bat_.total += bat_.items;
        bat_.items = 0;
        ++bat_.submits;
        if (!ok) {   // the batch did not reach the GPU: each upload again from the staging buffer, one submit each; no batch again
            SDL_Log("b3gfx: route upload batch not submitted (%s): %zu uploads again one by one, batching off", SDL_GetError(), bat_.ops.size());
            bat_.broken = true;
            int lost = 0;
            for (const Batch::Op& op : bat_.ops) {
                SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
                SDL_GPUCopyPass* cp = cmd ? SDL_BeginGPUCopyPass(cmd) : nullptr;
                if (!cp) { if (cmd) SDL_CancelGPUCommandBuffer(cmd); ++lost; continue; }
                if (op.tex) {
                    SDL_GPUTextureTransferInfo src{bat_.tb, op.off, op.w, op.h};
                    SDL_GPUTextureRegion reg{};
                    reg.texture = op.tex;
                    reg.mip_level = op.level;
                    reg.w = op.w;
                    reg.h = op.h;
                    reg.d = 1;
                    SDL_UploadToGPUTexture(cp, &src, &reg, false);
                } else {
                    SDL_GPUTransferBufferLocation src{bat_.tb, op.off};
                    SDL_GPUBufferRegion dst{op.buf, 0, op.bytes};
                    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
                }
                SDL_EndGPUCopyPass(cp);
                if (op.mips) SDL_GenerateMipmapsForGPUTexture(cmd, op.tex);
                if (!SDL_SubmitGPUCommandBuffer(cmd)) ++lost;
            }
            if (lost) SDL_Log("b3gfx: %d uploads could not be submitted (%s)", lost, SDL_GetError());
        }
        bat_.ops.clear();
    }
    void flush_dyn() {
        if (up_.map) { SDL_UnmapGPUTransferBuffer(dev_, up_.tb); up_.map = nullptr; }
        up_.used = 0;
        if (!up_.cmd) return;
        SDL_EndGPUCopyPass(up_.cp);
        SDL_SubmitGPUCommandBuffer(up_.cmd);
        up_.cmd = nullptr;
        up_.cp = nullptr;
    }

    BufferId make_buffer(SDL_GPUBufferUsageFlags usage, const void* data, std::size_t bytes) {
        if (bat_.on) flush_dyn();   // perf4: into the route's batch (see create_texture)
        else flush_uploads();   // keep the submission order of the immediate uploads (see upload_reserve)
        if (!bytes) return kNone;
        B3PERF_SCOPE(kUpload);
        b3perf::count(b3perf::kUploads);
        b3perf::count(b3perf::kUploadBytes, bytes);
        ++stats_.uploads;
        stats_.upload_bytes += bytes;
        SDL_GPUBufferCreateInfo bi{usage, std::uint32_t(bytes), 0};
        SDL_GPUBuffer* b = SDL_CreateGPUBuffer(dev_, &bi);
        if (!b) return kNone;
        if (bat_.on) {
            std::uint32_t at = 0;
            if (std::uint8_t* dst = bytes <= 0xFFFFFFFFu ? batch_reserve(std::uint32_t(bytes), at) : nullptr) {
                std::memcpy(dst, data, bytes);
                SDL_GPUTransferBufferLocation src{bat_.tb, at};
                SDL_GPUBufferRegion dstr{b, 0, std::uint32_t(bytes)};
                SDL_UploadToGPUBuffer(bat_.cp, &src, &dstr, false);
                bat_.ops.push_back({nullptr, b, 0, 0, 0, at, std::uint32_t(bytes), false});
                ++bat_.items;
                buffers_.push_back(b);
                return BufferId(buffers_.size() - 1);
            }
            flush_batch();
        }
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
    bool soft_ = false;                     // fifauto: DeviceOptions::frames_soft (1 / 2 held by the device)
    SDL_GPUFence* last_fence_ = nullptr;    // fifauto: the last window frame's (soft_ only)
    std::uint64_t acquire_ns_ = 0;          // fifauto: the swapchain acquire's waits (take_acquire_ns)
    int gpu_every_ = 0;                     // fifauto: GPU timing every n-th window frame (0 off)
    std::uint64_t gpu_frame_ = 0;
    double gpu_ms_ = -1.0;
    int bloom_lean_ = -1;                   // fifauto: set_bloom_lean (-1: $B3_BLOOM_LEAN)
    std::uint64_t wait_ns_ = 0;   // perf4: take_wait_ns
    double warm_ms_ = 0;          // perf4: warm_pipelines' time
    std::uint64_t occ_frames_ = 0, occ_drawn_ = 0, occ_skipped_ = 0;   // perf4: the sun occlusion pass
    SDL_GPUTextureFormat swap_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    int msaa_ = 1;
    SDL_GPUGraphicsPipeline* clamp_pipe_ = nullptr;   // clamp_frame
    SDL_GPUShader *vs_ = nullptr, *fs_ = nullptr, *fsq_vs_ = nullptr, *spec_fs_ = nullptr, *present_fs_ = nullptr;
    // night mode (remaster-features §17): the grade and sky shader and the beam shader (made at start with the others;
    // a failure only turns night mode off), the grade's two pipelines (0 where something was drawn, 1 where the sky
    // shows) and the beams' pipeline, each made on its first use (night_pipeline, light_pipeline)
    SDL_GPUShader* light_fs_ = nullptr;
    SDL_GPUShader* night_fs_ = nullptr;
    SDL_GPUGraphicsPipeline* night_pipe_[2] = {nullptr, nullptr};
    SDL_GPUGraphicsPipeline* light_pipe_ = nullptr;
    SDL_GPUShader* beam_fs_ = nullptr;             // nightlights (§17.2): the projected beams, beam_pipeline on first use
    SDL_GPUGraphicsPipeline* beam_pipe_ = nullptr;
    SDL_GPUTexture* env_glow_src_ = nullptr;       // reflect2 G: env_night_glow's constant source (released with the device)
    std::uint32_t env_glow_w_ = 0, env_glow_h_ = 0;
    SDL_GPUShader* emissive_fs_ = nullptr;         // nightlights' fixtures (DrawItem::night_emissive), emissive_pipeline on first use
    SDL_GPUGraphicsPipeline* emissive_pipe_ = nullptr;
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
    bool mirror_now_ = false;   // gameopts: the frame being drawn has a mirrored view (FrameParams::mirror_x)
    bool mirror_held_ = false;  // gameopts: the last single-view frame was mirrored (its RENDER SCALE target set is in use)
    SDL_GPUGraphicsPipeline* post_pipe_[4] = {};   // [2]: the SOFT composite (bloom2); [3]: the one-draw composite (mode 5, no blend)
    // photo2 (FrameParams::Dof): dof.frag (made on first use; a failure turns the effect off, nothing else), its pipelines
    // ([0] its passes, [1] the composite), the depth pre-pass's ([0] mesh, [1] culled, [2] cars) and its depth format
    SDL_GPUShader* dof_fs_ = nullptr;
    bool dof_failed_ = false;
    SDL_GPUShader* ao_fs_ = nullptr;   // surface: shaders/ao.frag, made on first use
    bool ao_failed_ = false;
    SDL_GPUGraphicsPipeline* ao_pipe_[3] = {};
    bool dof_debug_ = SDL_getenv("B3GFX_DOF_DEBUG") != nullptr;   // the circles in false colour (diagnostics)
    SDL_GPUGraphicsPipeline* dof_pipe_[2] = {};
    SDL_GPUGraphicsPipeline* dof_depth_pipe_[3] = {};
    // photoblur (FrameParams::Motion): motion.frag (made on first use; a failure turns the effect off), its pipelines
    // [0] the streaks, [1] the accumulation (blended), [2] the composite into the scene pass
    SDL_GPUShader* motion_fs_ = nullptr;
    SDL_GPUShader* drops_fs_ = nullptr;           // weather: the lens drops (made on first use)
    SDL_GPUGraphicsPipeline* drops_pipe_ = nullptr;
    bool drops_failed_ = false;
    bool motion_failed_ = false;
    bool motion_debug_ = SDL_getenv("B3GFX_MOTION_DEBUG") != nullptr;   // the streaks in false colour (diagnostics)
    std::vector<std::uint32_t> tex_gen_;   // photoblur: update_texture's writes per texture (motion_sig)
    SDL_GPUGraphicsPipeline* motion_pipe_[3] = {};
    SDL_GPUTextureFormat dof_depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    // scenedepth: with scene_depth_on_ the scene pass keeps its depth (sd_) instead of the depth pre-pass;
    // depth_src_: the single-sample depth the frame's passes read (the scene's resolved, or the pre-pass's)
    bool sd_failed_ = false;
    bool scene_depth_on_ = false;
    SceneDepthMode scene_depth_mode_ = SceneDepthMode::kOff;   // nightdepth
    SceneDepthState sd_;            // (registered in g_sd_dev by its SDL device while it lives)   // include/b3gfx/scene_depth.h scene_depth_wanted (off by default on every backend)
    SDL_GPUShader* sdfix_fs_ = nullptr;
    SDL_GPUGraphicsPipeline* sdfix_pipe_ = nullptr;
    SDL_GPUTexture* depth_src_ = nullptr;
    // SUN SHADOWS (FrameParams::SunShadow): shadow.frag and mesh.frag's SUN_SHADOW variant (made on first use; a failure
    // turns the feature off), the caster pipeline, the receivers' (by pipeline() slot: blend + 3 x culled), the map and
    // its depth target (sh_side_ texels; released by clear_resources), and sh_live_ while a scene pass takes the map
    SDL_GPUShader *sh_fs_ = nullptr, *sh_recv_fs_ = nullptr;
    bool sh_failed_ = false;
    SDL_GPUGraphicsPipeline* sh_pipe_ = nullptr;
    SDL_GPUShader* sh_prop_fs_ = nullptr;                // propshadow1: shadow_mesh.frag and its caster pipeline (mesh.vert)
    SDL_GPUGraphicsPipeline* sh_prop_pipe_ = nullptr;
    bool sh_prop_failed_ = false;
    SDL_GPUGraphicsPipeline* sh_recv_pipe_[6] = {};
    SDL_GPUTexture *sh_map_ = nullptr, *sh_depth_ = nullptr;
    std::uint32_t sh_side_ = 0;
    bool sh_live_ = false;
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
    struct Batch { bool on = false; SDL_GPUTransferBuffer* tb = nullptr; std::uint8_t* map = nullptr; std::uint32_t used = 0;
                   SDL_GPUCommandBuffer* cmd = nullptr; SDL_GPUCopyPass* cp = nullptr; std::vector<SDL_GPUTexture*> mips;
                   std::uint32_t items = 0, total = 0, submits = 0; Uint64 t0 = 0;
                   // each upload recorded (flush_batch's fallback when the submit fails)
                   struct Op { SDL_GPUTexture* tex; SDL_GPUBuffer* buf; std::uint32_t level, w, h, off, bytes; bool mips; };
                   std::vector<Op> ops;
                   bool broken = false; bool off_logged = false; } bat_;   // perf4: upload_batch (uploadbatch: off_logged)
    bool upload_batch_opt_ = true;   // uploadbatch: Options::upload_batch
    Stats stats_;   // perf1: take_stats()
    unsigned texture_failures_ = 0;   // texture_failed
    unsigned texture_refusals_ = 0, refusals_logged_ = 0;   // texture_refused
    float dither_ = 0.0f;   // perf1: Options::dither
    float render_scale_ = 1.0f;   // effects2: Options::render_scale
    bool warm_sun_shadows_ = false, warm_ao_ = false;   // finalfix: Options::warm_sun_shadows / warm_ambient_occlusion
    std::uint32_t sun_shadow_side_opt_ = 0;             // finalfix: Options::sun_shadow_side
    std::pair<std::uint32_t, std::uint32_t> ss_logged_{};   // perf4: effective_scale
    double ss_budget_mb_ = -1;   // perf4: the supersampled sets' budget (MB; < 0: not read yet)
    bool sharp_upscale_ = false;  // 1.1 UPSCALING: post.frag mode 4 z 2 for the scaled picture
    float line_min_ = 1.0f;       // effects2: the least line width (pixels of the target being drawn)
    Targets scaled_win_, scaled_off_;   // effects2: the 3D picture's targets at the scale (single-view frames)
    std::map<BufferId, Dyn> dyn_;   // dynamic buffers (update_*_buffer): their upload buffer and capacity
    SDL_GPUTexture* white_ = nullptr;
    // reflect-tunnel (render_env): the world env maps, one per split-screen view (MSAA target, its resolve = the sampled
    // atlas, depth); env_face_ while drawing their faces (mesh.frag: light_dir.w)
    EnvTarget env_[2];
    bool env_face_ = false;
    std::vector<SDL_GPUBuffer*> buffers_;
    std::vector<SDL_GPUTexture*> textures_;
    unsigned resource_epoch_ = 0;   // clear_resources calls (Device::resource_epoch)
    std::vector<TextureId> original_;   // hdkey: by texture id, its original (kNone: not an HD replacement)
    HdOriginals originals_;
    bool hd_shown_ = true;
    bool bc_ok_ = false;   // hdgpu: supports_bc
    std::map<TextureId, std::pair<int, int>> dyn_tex_size_;   // update_texture's textures (menu1)
    std::map<int, std::array<SDL_GPUGraphicsPipeline*, 10>> pipelines_;   // (8 / 9: whitesky keep_alpha)
    Targets win_targets_, off_targets_, pic_targets_;   // (pic_targets_: paint3's draw_picture)
    struct PendingPicture { SDL_GPUFence* fence = nullptr; SDL_GPUTransferBuffer* tb = nullptr; std::uint32_t bytes = 0; };
    std::map<int, PendingPicture> pictures_;   // paint3: picture_begin's, until taken
    int picture_id_ = 0;
    std::vector<Targets> win_views_, off_views_;   // gsoff1 (§29): the split screen's per-view targets
    bool shot_seed_ = false;   // screenshot_begin .. screenshot_end
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

// d3d12 (GRAPHICS API, include/b3gfx/driver_order.h): the drivers in order (Windows: Auto = Vulkan, then D3D12; a named
// API then the other; elsewhere the one asked for). SDL 3.4.2 alone would take D3D12 first once DXIL is offered. A
// whole start-up is tried per driver: the device, the window's claim and swapchain, the shaders, the first pipelines
// (an old Vulkan driver that makes a device but fails after it is the case D3D12 is there for); a failed one is
// destroyed (its window claim released) before the next. Each fallback logged.
std::unique_ptr<Device> Device::create(SDL_Window* window, const Options& opt, std::string* err) {
    const std::vector<std::string> tries = driver_attempts(opt.driver, windows_build());
    std::string why;
    for (std::size_t i = 0; i < tries.size(); ++i) {
        Options o = opt;
        o.driver = tries[i];
        auto d = std::make_unique<SdlDevice>();
        std::string e;
        if (d->init(window, o, &e)) return d;
        d.reset();
        why += (why.empty() ? "" : "; ") + (tries[i].empty() ? std::string("default") : tries[i]) + ": " + e;
        if (i + 1 < tries.size())
            SDL_Log("b3gfx: GRAPHICS API: %s did not start (%s): trying %s", tries[i].c_str(), e.c_str(), tries[i + 1].c_str());
    }
    if (err) *err = why;
    return nullptr;
}

}  // namespace b3gfx
