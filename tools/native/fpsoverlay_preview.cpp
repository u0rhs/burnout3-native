// fpsoverlay_preview -- renders the FPS counter (src/b3replay/fpspanel.h) over a picture into a PNG, with no window and no
// game: the same layout and quads the window draws (FpsPanel::layout / build), composited here on the CPU, fed by a
// synthetic frame timeline.
//   fpsoverlay_preview OUT.png WxH [--bg shot.png] [--view minimal|standard|detailed] [--corner tl|tr|bl|br] [--hud 0..3]
//                      [--timeline steady|dips|slow|spiky] [--limit FPS] [--notice] [--racehud] [--zoom N]
//   fpsoverlay_preview OUT WxH --bench   the CPU cost of FpsPanel::draw_list per frame (off, minimal, standard, detailed) on
//                                        a device that only counts its calls, with a latency that changes every frame
// The background is stretched to WxH; without one a synthetic bright sky over a dark road is used (the two extremes a plate
// has to stay legible on). --notice draws a notice at the top centre (the counter keeps clear of it).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "b3asset/png.h"
#include "fpspanel.h"
#include "hud.h"
#include "notice.h"

using namespace b3replay;

static void over(std::vector<std::uint8_t>& dst, int W, int H, const std::vector<std::uint8_t>& src, int sw, int sh, int x0, int y0) {
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            const int X = x0 + x, Y = y0 + y;
            if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
            const std::uint8_t* s = &src[4 * (std::size_t(y) * std::size_t(sw) + std::size_t(x))];
            std::uint8_t* d = &dst[4 * (std::size_t(Y) * std::size_t(W) + std::size_t(X))];
            const float a = float(s[3]) / 255.0f;
            for (int k = 0; k < 3; ++k) d[k] = std::uint8_t(std::lround(float(s[k]) * a + float(d[k]) * (1 - a)));
        }
}

// The plate's quads composited over the picture at (ox, oy), the pixel centres inside each triangle (no smoothing, as the window's point-sampled quads)
static void raster(std::vector<std::uint8_t>& dst, int W, int H, const std::vector<b3gfx::Vertex>& vs, const std::vector<std::uint32_t>& is, int ox, int oy) {
    for (std::size_t t = 0; t + 2 < is.size(); t += 3) {
        const b3gfx::Vertex &a = vs[is[t]], &b = vs[is[t + 1]], &c = vs[is[t + 2]];
        const float x0 = std::min({a.pos[0], b.pos[0], c.pos[0]}), x1 = std::max({a.pos[0], b.pos[0], c.pos[0]});
        const float y0 = std::min({a.pos[1], b.pos[1], c.pos[1]}), y1 = std::max({a.pos[1], b.pos[1], c.pos[1]});
        const float area = (b.pos[0] - a.pos[0]) * (c.pos[1] - a.pos[1]) - (b.pos[1] - a.pos[1]) * (c.pos[0] - a.pos[0]);
        if (area == 0) continue;
        const float col[4] = {float(a.rgba & 255) / 64.0f, float(a.rgba >> 8 & 255) / 64.0f, float(a.rgba >> 16 & 255) / 64.0f, float(a.rgba >> 24 & 255) / 128.0f};
        for (int y = int(std::floor(y0)); y <= int(std::ceil(y1)); ++y)
            for (int x = int(std::floor(x0)); x <= int(std::ceil(x1)); ++x) {
                const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                const float w0 = ((b.pos[0] - px) * (c.pos[1] - py) - (b.pos[1] - py) * (c.pos[0] - px)) / area;
                const float w1 = ((c.pos[0] - px) * (a.pos[1] - py) - (c.pos[1] - py) * (a.pos[0] - px)) / area;
                const float w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const int X = ox + x, Y = oy + y;
                if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                std::uint8_t* d = &dst[4 * (std::size_t(Y) * std::size_t(W) + std::size_t(X))];
                for (int k = 0; k < 3; ++k) d[k] = std::uint8_t(std::lround(std::clamp(col[k], 0.0f, 1.0f) * 255.0f * col[3] + float(d[k]) * (1 - col[3])));
            }
    }
}

namespace {
struct CountDev : b3gfx::Device {
    int calls = 0;
    const char* driver_name() const override { return "count"; }
    int msaa() const override { return 1; }
    const char* present_mode_name() const override { return ""; }
    int frames_in_flight() const override { return 0; }
    void wait_swapchain() override {}
    b3gfx::BufferId create_vertex_buffer(const b3gfx::Vertex*, std::size_t) override { ++calls; return 0; }
    b3gfx::BufferId create_index_buffer(const std::uint32_t*, std::size_t) override { ++calls; return 0; }
    b3gfx::TextureId create_texture(const std::vector<b3gfx::TextureLevel>&) override { ++calls; return 1; }
    void update_texture(b3gfx::TextureId&, int, int, const std::uint8_t*) override { ++calls; }
    void clear_resources() override {}
    void update_vertex_buffer(b3gfx::BufferId& id, const b3gfx::Vertex*, std::size_t) override { ++calls; id = 5; }
    void update_index_buffer(b3gfx::BufferId& id, const std::uint32_t*, std::size_t) override { ++calls; id = 6; }
    bool draw_frame(const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, int*, int*) override { return true; }
    bool draw_offscreen(int, int, const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, std::vector<std::uint8_t>&) override { return true; }
    bool draw_views(const std::vector<ViewPass>&, const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, int*, int*) override { return true; }
    bool draw_views_offscreen(int, int, const std::vector<ViewPass>&, const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, std::vector<std::uint8_t>&) override { return true; }
};
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: fpsoverlay_preview OUT.png WxH [--bg shot.png] [--view V] [--corner C] [--hud N] [--timeline T] [--limit FPS] [--notice] [--racehud] [--bench]\n");
        return 2;
    }
    const std::string out = argv[1];
    int W = 0, H = 0;
    if (std::sscanf(argv[2], "%dx%d", &W, &H) != 2 || W < 64 || H < 64) return 2;
    std::string bg, timeline = "steady";
    FpsView view = FpsView::kStandard;
    FpsCorner corner = FpsCorner::kTopLeft;
    int hud = 0;
    float limit = 0;
    bool notice = false;
    int zoom = 0;
    bool race_hud = false, bench = false, two_rows = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--bg") bg = next();
        else if (a == "--view") { const std::string v = next(); view = v == "minimal" ? FpsView::kMinimal : v == "detailed" ? FpsView::kDetailed : FpsView::kStandard; }
        else if (a == "--corner") { const std::string v = next(); corner = v == "tr" ? FpsCorner::kTopRight : v == "bl" ? FpsCorner::kBottomLeft : v == "br" ? FpsCorner::kBottomRight : FpsCorner::kTopLeft; }
        else if (a == "--hud") hud = std::atoi(next().c_str());
        else if (a == "--timeline") timeline = next();
        else if (a == "--limit") limit = float(std::atof(next().c_str()));
        else if (a == "--notice") notice = true;
        else if (a == "--bench") bench = true;
        else if (a == "--timeattack") two_rows = race_hud = true;   // a stand-in for the time attack's two rows of times at the top left
        else if (a == "--racehud") race_hud = true;   // the picture is a race frame: the counter keeps below / above the game's HUD plates
        else if (a == "--zoom") zoom = std::atoi(next().c_str());   // a crop of the plate, scaled up this many times (to read the details)
    }
    std::vector<std::uint8_t> px(std::size_t(W) * std::size_t(H) * 4, 255);
    int bw = 0, bh = 0;
    std::vector<std::uint8_t> b;
    if (!bg.empty() && read_png_rgba(bg, bw, bh, b)) {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) std::memcpy(&px[4 * (std::size_t(y) * std::size_t(W) + std::size_t(x))], &b[4 * (std::size_t(y * bh / H) * std::size_t(bw) + std::size_t(x * bw / W))], 4);
    } else {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                std::uint8_t* p = &px[4 * (std::size_t(y) * std::size_t(W) + std::size_t(x))];
                if (y < H * 6 / 10) { p[0] = 235; p[1] = 240; p[2] = 250; }
                else { const int v = 22 + (x * 40 / W); p[0] = p[1] = p[2] = std::uint8_t(v); }
                p[3] = 255;
            }
    }
    // the frame timeline: 12 s of frames
    const float target = limit > 0 ? limit : 60.0f;
    FpsPanel panel;
    std::uint64_t t = 1000000000ull;
    unsigned rng = 12345;
    auto rnd = [&] { rng = rng * 1664525u + 1013904223u; return float(rng >> 8) / float(1 << 24); };
    const double base_ms = timeline == "slow" ? 1000.0 / 41.0 : 1000.0 / double(target);
    while (double(t - 1000000000ull) * 1e-9 < 12.0) {
        double ms = base_ms * (1.0 + 0.04 * double(rnd() - 0.5));
        const double now_s = double(t - 1000000000ull) * 1e-9;
        if (timeline == "dips") {   // two stretches of slower frames and one hitch
            if (now_s > 6.5 && now_s < 7.4) ms = base_ms * 1.5;
            if (now_s > 9.0 && now_s < 9.05) ms = 85.0;
        } else if (timeline == "slow") {
            if (now_s > 8.0 && now_s < 8.1) ms = 120.0;
        } else if (timeline == "spiky") {
            if (rnd() < 0.012f) ms = base_ms * (3.0 + 5.0 * double(rnd()));
        }
        t += std::uint64_t(ms * 1e6);
        panel.frame(t);
    }
    FpsInfo info;
    info.target_fps = target;
    info.latency_ms = 18.0f;
    info.width = W;
    info.height = H;
    info.msaa = 4;
    info.preset = "HIGH";
    info.gpu = "EXAMPLE GPU 1234";
    info.backend = "VULKAN";
    if (bench) {   // the CPU cost of draw_list per frame, view by view, and the device calls it makes
        const FpsView views[4] = {FpsView::kOff, FpsView::kMinimal, FpsView::kStandard, FpsView::kDetailed};
        const char* names[4] = {"off", "minimal", "standard", "detailed"};
        for (int k = 0; k < 4; ++k) {
            CountDev dev;
            FpsPanel p;
            FpsInfo inf = info;
            std::vector<b3gfx::DrawItem> items;
            std::uint64_t tt = 5000000000ull;
            double lat = 18.0;
            const int N = 20000;
            std::chrono::nanoseconds total{0};
            for (int n = 0; n < N; ++n) {
                tt += 4500000;   // ~222 frames per second
                p.frame(tt);
                lat += 0.02 * (18.0 + 2.0 * double(n % 7) - lat);
                inf.latency_ms = float(lat);
                items.clear();
                const auto t0 = std::chrono::steady_clock::now();
                p.draw_list(dev, W, H, tt, views[k], corner, hud, inf, nullptr, false, items);
                total += std::chrono::steady_clock::now() - t0;
            }
            std::printf("bench %dx%d %-8s: draw_list %.4f ms per frame, %.4f device calls per frame (%d in %d frames, %.0f s)\n", W, H, names[k],
                        double(total.count()) * 1e-6 / N, double(dev.calls) / N, dev.calls, N, double(N) * 0.0045);
        }
        return 0;
    }
    const FpsStats st = panel.stats(t);
    float g[FpsPanel::kColumns];
    panel.graph(t, g);
    FpsPanel::Rect nb, *avoid = nullptr;
    if (notice) {
        Notice n;
        n.show({"SCREENSHOT SAVED", "Burnout3-20261115-120000.png"}, 5.0f);
        int nw = 0, nh = 0;
        std::vector<std::uint8_t> rgba;
        Notice::picture({"SCREENSHOT SAVED", "Burnout3-20261115-120000.png"}, H, nw, nh, rgba);
        float box[4];
        n.box(W, H, box);
        over(px, W, H, rgba, nw, nh, int(box[0]), int(box[1]));
        nb = {box[0], box[1], box[2], box[3]};
        avoid = &nb;
    }
    if (two_rows) {   // two dark rows (best above, current below) of about 8 % of the height each, from 4.9 %, wide as the real plates
        const std::vector<std::uint8_t> row(std::size_t(W / 5) * std::size_t(H * 8 / 100) * 4, 0);
        std::vector<std::uint8_t> navy(row.size());
        for (std::size_t k = 0; k < navy.size(); k += 4) { navy[k] = 14; navy[k + 1] = 22; navy[k + 2] = 48; navy[k + 3] = 235; }
        over(px, W, H, navy, W / 5, H * 8 / 100, 0, H * 49 / 1000);
        over(px, W, H, navy, W / 5, H * 8 / 100, 0, H * 49 / 1000 + H * 9 / 100);
    }
    const FpsPanel::Layout l = FpsPanel::layout(W, H, hud, view, corner, int(FpsPanel::detail_lines(info).size()), avoid, race_hud);
    std::vector<b3gfx::Vertex> vs;
    std::vector<std::uint32_t> is;
    FpsPanel::build(l, view, st, g, info, vs, is);
    raster(px, W, H, vs, is, int(l.plate.x), int(l.plate.y));
    std::printf("layout %dx%d hud %d: scale %.2f margin %.0f plate (%.0f, %.0f, %.0f x %.0f), %zu quads; %d frames, %.1f fps, avg %.1f, 1%% low %.1f, worst %.1f ms\n", W, H, hud,
                double(l.scale), double(l.margin), double(l.plate.x), double(l.plate.y), double(l.plate.w), double(l.plate.h), vs.size() / 4, st.frames, double(st.fps),
                double(st.avg), double(st.low1), double(st.max_ms));
    if (zoom > 1) {
        const int cx0 = std::max(0, int(l.plate.x) - 12), cy0 = std::max(0, int(l.plate.y) - 12);
        const int cx1 = std::min(W, int(l.plate.x + l.plate.w) + 12), cy1 = std::min(H, int(l.plate.y + l.plate.h) + 12);
        const int zw = (cx1 - cx0) * zoom, zh = (cy1 - cy0) * zoom;
        std::vector<std::uint8_t> zp(std::size_t(zw) * std::size_t(zh) * 4);
        for (int y = 0; y < zh; ++y)
            for (int x = 0; x < zw; ++x)
                std::memcpy(&zp[4 * (std::size_t(y) * std::size_t(zw) + std::size_t(x))], &px[4 * (std::size_t(cy0 + y / zoom) * std::size_t(W) + std::size_t(cx0 + x / zoom))], 4);
        return b3asset::write_png_rgba(out, zw, zh, zp.data()) ? 0 : 1;
    }
    if (!b3asset::write_png_rgba(out, W, H, px.data())) {
        std::fprintf(stderr, "could not write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
