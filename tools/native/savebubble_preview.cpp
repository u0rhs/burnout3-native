// savebubble_preview -- the save notices' bubble (saveflow, 2026-11-20: src/b3replay/notice.h Style::kBubble, as
// live.cpp's show_save_bubble shows it) over a picture into PNGs, with no window and no game: the same picture and place
// the window draws (Notice::picture, Notice::box), composited here on the CPU.
//   savebubble_preview OUT_PREFIX WxH [--bg shot.png] --status "SAVED" [--status "SAVE FAILED - ..."] ...
// One PNG per status: OUT_PREFIX-<n>.png. The background is stretched to WxH (nearest); without one, mid grey.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "b3asset/png.h"
#include "hud.h"
#include "notice.h"

using namespace b3replay;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: savebubble_preview OUT_PREFIX WxH [--bg shot.png] --status TEXT ...\n");
        return 2;
    }
    const std::string out = argv[1];
    int W = 0, H = 0;
    if (std::sscanf(argv[2], "%dx%d", &W, &H) != 2 || W <= 0 || H <= 0) return 2;
    std::string bg;
    std::vector<std::string> statuses;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--bg")) bg = argv[i + 1];
        else if (!std::strcmp(argv[i], "--status")) statuses.push_back(argv[i + 1]);
        else return 2;
    }
    std::vector<std::uint8_t> base(std::size_t(W) * std::size_t(H) * 4, 96);
    int bw = 0, bh = 0;
    std::vector<std::uint8_t> b;
    if (!bg.empty() && read_png_rgba(bg, bw, bh, b))
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                std::memcpy(&base[4 * (std::size_t(y) * std::size_t(W) + std::size_t(x))],
                            &b[4 * (std::size_t(y * bh / H) * std::size_t(bw) + std::size_t(x * bw / W))], 4);
    int n = 0;
    for (const std::string& s : statuses) {
        std::vector<std::string> lines;
        const Notice::Tone tone = Notice::save_bubble_lines(s, lines);
        Notice placed;
        placed.show_bubble(lines, tone, 2.0f);
        float box[4];
        if (!placed.box(W, H, box)) return 1;
        int pw = 0, ph = 0;
        std::vector<std::uint8_t> pic;
        Notice::picture(lines, H, pw, ph, pic, Notice::Style::kBubble, tone);
        std::vector<std::uint8_t> px = base;
        const int x0 = int(box[0]), y0 = int(box[1]);
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x) {
                const int X = x0 + x, Y = y0 + y;
                if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                const std::uint8_t* sp = &pic[4 * (std::size_t(y) * std::size_t(pw) + std::size_t(x))];
                std::uint8_t* d = &px[4 * (std::size_t(Y) * std::size_t(W) + std::size_t(X))];
                const float a = float(sp[3]) / 255.0f;
                for (int k = 0; k < 3; ++k) d[k] = std::uint8_t(float(sp[k]) * a + float(d[k]) * (1 - a) + 0.5f);
            }
        const std::string path = out + "-" + std::to_string(++n) + ".png";
        if (!b3asset::write_png_rgba(path, W, H, px.data())) {
            std::fprintf(stderr, "savebubble_preview: cannot write %s\n", path.c_str());
            return 1;
        }
        std::printf("%s: %dx%d bubble at %d,%d (%s)\n", path.c_str(), pw, ph, x0, y0, s.c_str());
    }
    return 0;
}
