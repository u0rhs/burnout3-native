// photohud_preview -- renders photo mode's on-screen UI (src/b3replay/photohud.h) over a picture into a PNG, with no
// window and no game: the same pictures and layout the window draws (PhotoHud::layout / picture_*), composited here on
// the CPU at full opacity. `--before` draws the earlier plain help box (Notice, photomode.h help_lines) instead.
//   photohud_preview OUT.png WxH [--bg shot.png] [--hud 0..3] [--device kb|ps|ps5|xbox|nintendo|generic] [--ui full|readouts]
//                    [--fov DEG] [--roll DEG] [--dof LEVEL] [--stops S] [--centre] [--before]
// The background is stretched to WxH; without one a synthetic bright sky over a dark road is used (the two extremes a
// plate has to stay legible on).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "b3asset/png.h"
#include "hud.h"
#include "notice.h"
#include "photohud.h"
#include "photomode.h"

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

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: photohud_preview OUT.png WxH [--bg shot.png] [--hud N] [--device D] [--ui full|readouts] [--before]\n");
        return 2;
    }
    const std::string out = argv[1];
    int W = 0, H = 0;
    if (std::sscanf(argv[2], "%dx%d", &W, &H) != 2 || W < 64 || H < 64) return 2;
    std::string bg;
    int hud = 0;
    PromptDevice dev = PromptDevice::kKeyboard;
    PhotoUi ui = PhotoUi::kFull;
    PhotoHudData d;
    bool before = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--bg") bg = next();
        else if (a == "--hud") hud = std::atoi(next().c_str());
        else if (a == "--device") { if (!prompt_device_parse(next(), dev)) return 2; }
        else if (a == "--ui") ui = next() == "readouts" ? PhotoUi::kReadouts : PhotoUi::kFull;
        else if (a == "--fov") d.fov_deg = float(std::atof(next().c_str()));
        else if (a == "--roll") d.roll_deg = float(std::atof(next().c_str()));
        else if (a == "--dof") { d.dof_level = std::atoi(next().c_str()); d.dof_on = d.dof_level > 0; if (!d.dof_on) d.dof_level = 4; }
        else if (a == "--stops") d.focus_stops = float(std::atof(next().c_str()));
        else if (a == "--centre") d.focus_car = false;
        else if (a == "--before") before = true;
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
                if (y < H * 6 / 10) { p[0] = 235; p[1] = 240; p[2] = 250; }                 // a bright sky
                else { const int v = 22 + (x * 40 / W); p[0] = p[1] = p[2] = std::uint8_t(v); }   // a dark road
                p[3] = 255;
            }
    }
    const bool pads = dev != PromptDevice::kKeyboard;
    if (before) {
        PhotoDof dof;
        dof.on = d.dof_on;
        dof.level = d.dof_level;
        int tw = 0, th = 0;
        std::vector<std::uint8_t> rgba;
        Notice::picture(PhotoMode::help_lines(pads, int(std::lround(d.fov_deg)), dof, d.focus_car ? 1 : 2), H, tw, th, rgba);
        over(px, W, H, rgba, tw, th, int(std::round(0.5f * float(W - tw))), int(std::round(0.96f * float(H) - float(th))));
    } else {
        const PhotoKeys keys;
        const PhotoHud::Layout l = PhotoHud::layout(W, H, hud, dev, keys, ui);
        int pw = 0, ph = 0;
        std::vector<std::uint8_t> rgba;
        PhotoHud::picture_readouts(l, d, dev, keys, pw, ph, rgba);
        over(px, W, H, rgba, pw, ph, int(l.readouts.x), int(l.readouts.y));
        if (!l.strip.empty()) {
            PhotoHud::picture_strip(l, dev, keys, pw, ph, rgba);
            over(px, W, H, rgba, pw, ph, int(l.strip.x), int(l.strip.y));
        }
        std::printf("layout %dx%d hud %d: scale %.2f margin %.0f readouts (%.0f, %.0f, %.0f x %.0f) strip (%.0f, %.0f, %.0f x %.0f) groups %d rows %d\n", W, H, hud,
                    double(l.scale), double(l.margin), double(l.readouts.x), double(l.readouts.y), double(l.readouts.w), double(l.readouts.h),
                    double(l.strip.x), double(l.strip.y), double(l.strip.w), double(l.strip.h), l.groups, l.rows);
    }
    if (!b3asset::write_png_rgba(out, W, H, px.data())) {
        std::fprintf(stderr, "could not write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
