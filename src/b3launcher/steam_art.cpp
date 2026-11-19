// steam_art.cpp -- ADD TO STEAM's own pictures (steam_art.h): PNG in and out, and the backgrounds drawn in code.
#include "steam_art.h"

#include "b3port/steam_logo_data.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace b3launcher::steam_art {

// ---- PNG ----

namespace {

std::uint32_t be32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }

void put32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(std::uint8_t(v >> s));
}

void chunk(std::vector<std::uint8_t>& o, const char* type, const std::uint8_t* data, std::size_t n) {
    put32(o, std::uint32_t(n));
    const std::size_t at = o.size();
    o.insert(o.end(), type, type + 4);
    if (n) o.insert(o.end(), data, data + n);
    put32(o, std::uint32_t(::crc32(0, o.data() + at, uInt(n + 4))));
}

int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

const std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

}  // namespace

std::optional<Image> decode_png(const std::uint8_t* d, std::size_t n, std::string* why) {
    auto fail = [&](const char* m) { if (why) *why = m; return std::optional<Image>(); };
    if (n < 8 || std::memcmp(d, kSig, 8) != 0) return fail("not a PNG file");
    std::size_t p = 8;
    int w = 0, h = 0, ctype = -1;
    std::vector<std::uint8_t> idat;
    bool end = false;
    while (p + 12 <= n && !end) {
        const std::uint32_t len = be32(d + p);
        if (len > n - p - 12) return fail("a chunk runs past the end");
        const std::uint8_t* type = d + p + 4;
        const std::uint8_t* body = d + p + 8;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len < 13) return fail("a short IHDR");
            w = int(be32(body));
            h = int(be32(body + 4));
            ctype = body[9];
            if (body[8] != 8 || (ctype != 2 && ctype != 6) || body[12] != 0) return fail("not an 8-bit RGB / RGBA non-interlaced PNG");
            if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return fail("an image size out of range");
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), body, body + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            end = true;
        }
        p += 12 + len;
    }
    if (ctype < 0 || idat.empty()) return fail("no image data");
    const int bpp = ctype == 6 ? 4 : 3;
    const std::size_t stride = std::size_t(w) * std::size_t(bpp);
    std::vector<std::uint8_t> raw((stride + 1) * std::size_t(h));
    uLongf got = uLongf(raw.size());
    if (uncompress(raw.data(), &got, idat.data(), uLong(idat.size())) != Z_OK || got != raw.size()) return fail("its image data does not inflate");
    Image im;
    im.w = w;
    im.h = h;
    im.rgba.resize(std::size_t(w) * std::size_t(h) * 4);
    std::vector<std::uint8_t> prev(stride, 0), cur(stride);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = raw.data() + std::size_t(y) * (stride + 1);
        const int f = row[0];
        for (std::size_t x = 0; x < stride; ++x) {
            const int a = x >= std::size_t(bpp) ? cur[x - bpp] : 0, b = prev[x], c = x >= std::size_t(bpp) ? prev[x - bpp] : 0;
            int v = row[1 + x];
            switch (f) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: return fail("an unknown row filter");
            }
            cur[x] = std::uint8_t(v);
        }
        for (int x = 0; x < w; ++x) {
            std::uint8_t* o = &im.rgba[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4];
            const std::uint8_t* s = &cur[std::size_t(x) * std::size_t(bpp)];
            o[0] = s[0];
            o[1] = s[1];
            o[2] = s[2];
            o[3] = bpp == 4 ? s[3] : 255;
        }
        std::swap(prev, cur);
    }
    return im;
}

std::vector<std::uint8_t> encode_png(const Image& im, bool alpha) {
    const int bpp = alpha ? 4 : 3;
    const std::size_t stride = std::size_t(im.w) * std::size_t(bpp);
    std::vector<std::uint8_t> raw;
    raw.reserve((stride + 1) * std::size_t(im.h));
    std::vector<std::uint8_t> prev(stride, 0), cur(stride), best, trial(stride);
    for (int y = 0; y < im.h; ++y) {
        for (int x = 0; x < im.w; ++x)
            for (int k = 0; k < bpp; ++k) cur[std::size_t(x) * std::size_t(bpp) + std::size_t(k)] = im.rgba[(std::size_t(y) * std::size_t(im.w) + std::size_t(x)) * 4 + std::size_t(k)];
        // the filter with the smallest sum of its bytes as signed values (the usual heuristic)
        long best_sum = -1;
        int best_f = 0;
        for (int f = 0; f < 5; ++f) {
            long sum = 0;
            for (std::size_t x = 0; x < stride; ++x) {
                const int a = x >= std::size_t(bpp) ? cur[x - bpp] : 0, b = prev[x], c = x >= std::size_t(bpp) ? prev[x - bpp] : 0;
                int pred = 0;
                switch (f) {
                case 1: pred = a; break;
                case 2: pred = b; break;
                case 3: pred = (a + b) / 2; break;
                case 4: pred = paeth(a, b, c); break;
                default: break;
                }
                trial[x] = std::uint8_t(cur[x] - pred);
                sum += std::abs(int(static_cast<std::int8_t>(trial[x])));
            }
            if (best_sum < 0 || sum < best_sum) {
                best_sum = sum;
                best_f = f;
                best = trial;
            }
        }
        raw.push_back(std::uint8_t(best_f));
        raw.insert(raw.end(), best.begin(), best.end());
        std::swap(prev, cur);
    }
    uLongf zn = compressBound(uLong(raw.size()));
    std::vector<std::uint8_t> z(zn);
    if (compress2(z.data(), &zn, raw.data(), uLong(raw.size()), 6) != Z_OK) return {};
    z.resize(zn);
    std::vector<std::uint8_t> o(kSig, kSig + 8);
    std::uint8_t ihdr[13];
    for (int s = 0; s < 4; ++s) {
        ihdr[s] = std::uint8_t(std::uint32_t(im.w) >> (24 - 8 * s));
        ihdr[4 + s] = std::uint8_t(std::uint32_t(im.h) >> (24 - 8 * s));
    }
    ihdr[8] = 8;
    ihdr[9] = alpha ? 6 : 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(o, "IHDR", ihdr, 13);
    chunk(o, "IDAT", z.data(), z.size());
    chunk(o, "IEND", nullptr, 0);
    return o;
}

// ---- the logo ----

const std::uint8_t* logo_png(std::size_t* n) {
    if (n) *n = b3port::kSteamLogoPngSize;
    return b3port::kSteamLogoPng;
}

const Image& logo() {
    static const Image im = [] {
        std::optional<Image> d = decode_png(b3port::kSteamLogoPng, b3port::kSteamLogoPngSize);
        return d ? *d : Image{};
    }();
    return im;
}

// ---- drawing ----

namespace {

struct Rgb { float r, g, b; };

// A picture being drawn: linear floats, 0..1 (more while light adds up; soft-clipped at the end)
struct Canvas {
    int w, h;
    std::vector<float> c;   // r g b per pixel
    Canvas(int w_, int h_) : w(w_), h(h_), c(std::size_t(w_) * std::size_t(h_) * 3, 0.0f) {}
    float* at(int x, int y) { return &c[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 3]; }
};

float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// A small fixed generator (the same picture on every computer)
struct Rng {
    std::uint32_t s;
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float(s & 0xFFFFFF) / float(0x1000000);
    }
    float range(float a, float b) { return a + (b - a) * next(); }
};

// The slant of the logo's letters: its baseline rises about 13 degrees to the right
constexpr float kSlantX = 0.9744f, kSlantY = -0.2250f;

struct Look {
    float gx, gy, grx, gry;   // the glow's centre and radii (pixels)
    int streaks;
    std::uint32_t seed;
    bool road;                // the cover: lane lines running to a point under the logo
    float vx, vy;             // that point
};

void ground(Canvas& cv, const Look& lk) {
    const Rgb top{0.080f, 0.042f, 0.030f}, bottom{0.018f, 0.016f, 0.020f};
    const Rgb glow{1.00f, 0.30f, 0.055f}, core{1.00f, 0.64f, 0.24f};
    for (int y = 0; y < cv.h; ++y) {
        const float t = float(y) / float(cv.h - 1);
        for (int x = 0; x < cv.w; ++x) {
            float* p = cv.at(x, y);
            const float dx = (float(x) - lk.gx) / lk.grx, dy = (float(y) - lk.gy) / lk.gry, d2 = dx * dx + dy * dy;
            const float g = 0.62f * std::exp(-2.3f * d2), k = 0.30f * std::exp(-9.0f * d2);
            p[0] = top.r + (bottom.r - top.r) * t + glow.r * g + core.r * k;
            p[1] = top.g + (bottom.g - top.g) * t + glow.g * g + core.g * k;
            p[2] = top.b + (bottom.b - top.b) * t + glow.b * g + core.b * k;
        }
    }
}

// Thin light lines at the logo's slant, brighter near the glow
void streaks(Canvas& cv, const Look& lk) {
    Rng rng{lk.seed};
    const float scale = float(std::min(cv.w, cv.h)) / 620.0f;
    for (int i = 0; i < lk.streaks; ++i) {
        const float cx = rng.range(-0.1f, 1.1f) * float(cv.w), cy = rng.range(-0.05f, 1.05f) * float(cv.h);
        const float len = rng.range(0.12f, 0.55f) * float(std::max(cv.w, cv.h));
        const float th = rng.range(0.5f, 1.8f) * std::max(1.0f, scale);
        const float dxg = (cx - lk.gx) / lk.grx, dyg = (cy - lk.gy) / lk.gry;
        const float near = std::exp(-0.9f * (dxg * dxg + dyg * dyg));
        const float strength = rng.range(0.10f, 0.45f) * (0.35f + 0.65f * near);
        const float warm = rng.next();
        const Rgb col{1.0f, 0.42f + 0.40f * warm, 0.10f + 0.30f * warm};
        const float hx = std::fabs(kSlantX) * len * 0.5f + th * 4, hy = std::fabs(kSlantY) * len * 0.5f + th * 4;
        const int x0 = std::max(0, int(cx - hx)), x1 = std::min(cv.w - 1, int(cx + hx));
        const int y0 = std::max(0, int(cy - hy)), y1 = std::min(cv.h - 1, int(cy + hy));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = float(x) - cx, py = float(y) - cy;
                const float along = px * kSlantX + py * kSlantY, across = -px * kSlantY + py * kSlantX;
                const float u = std::fabs(along) / (len * 0.5f);
                if (u >= 1.0f) continue;
                const float a = (1.0f - u * u) * std::exp(-(across * across) / (th * th)) * strength;
                if (a < 1e-4f) continue;
                float* p = cv.at(x, y);
                p[0] += col.r * a;
                p[1] += col.g * a;
                p[2] += col.b * a;
            }
    }
}

// The cover's road: lane lines from the bottom edge to a point under the logo, fading out towards it, and a
// faint horizon there
void road(Canvas& cv, const Look& lk) {
    const Rgb col{1.0f, 0.55f, 0.18f};
    const float bottom = float(cv.h - 1);
    for (int y = int(lk.vy); y < cv.h; ++y) {
        const float depth = (float(y) - lk.vy) / (bottom - lk.vy);   // 0 at the point, 1 at the bottom
        const float fade = smoothstep(0.0f, 0.55f, depth) * 0.55f;
        for (int k = -3; k <= 3; ++k) {
            const float bx = float(cv.w) * 0.5f + float(k) * float(cv.w) * 0.36f;
            const float lx = lk.vx + (bx - lk.vx) * depth;
            const float th = 0.6f + 1.6f * depth;
            for (int x = std::max(0, int(lx - th * 4)); x <= std::min(cv.w - 1, int(lx + th * 4)); ++x) {
                const float d = (float(x) - lx) / th;
                const float a = fade * std::exp(-d * d) * (k == 0 ? 0.35f : 0.6f);
                float* p = cv.at(x, y);
                p[0] += col.r * a;
                p[1] += col.g * a;
                p[2] += col.b * a;
            }
        }
    }
    for (int y = std::max(0, int(lk.vy - 30)); y < std::min(cv.h, int(lk.vy + 30)); ++y) {
        const float d = (float(y) - lk.vy) / 7.0f;
        for (int x = 0; x < cv.w; ++x) {
            const float e = (float(x) - lk.vx) / (float(cv.w) * 0.55f);
            const float a = 0.30f * std::exp(-d * d) * std::exp(-e * e * 2.0f);
            float* p = cv.at(x, y);
            p[0] += 1.0f * a;
            p[1] += 0.62f * a;
            p[2] += 0.28f * a;
        }
    }
}

void vignette(Canvas& cv) {
    for (int y = 0; y < cv.h; ++y)
        for (int x = 0; x < cv.w; ++x) {
            const float rx = (float(x) / float(cv.w) - 0.5f) * 2, ry = (float(y) / float(cv.h) - 0.5f) * 2;
            const float m = 1.0f - 0.55f * smoothstep(0.55f, 1.45f, std::sqrt(rx * rx + ry * ry));
            float* p = cv.at(x, y);
            p[0] *= m;
            p[1] *= m;
            p[2] *= m;
        }
}

// A copy of `src` scaled to w x h (an area filter: each output pixel the mean of the source it covers), in
// premultiplied alpha
std::vector<float> resized(const Image& src, int w, int h) {
    auto pass = [](const std::vector<float>& in, int iw, int ih, int ow, bool horizontal) {
        // horizontal: iw x ih to ow x ih; else iw x ih to iw x ow
        const int on = ow, in_n = horizontal ? iw : ih;
        const int out_w = horizontal ? ow : iw, out_h = horizontal ? ih : ow;
        std::vector<float> out(std::size_t(out_w) * std::size_t(out_h) * 4, 0.0f);
        const float s = float(in_n) / float(on);
        for (int o = 0; o < on; ++o) {
            const float a = float(o) * s, b = a + s;
            for (int i = int(a); i < std::min(in_n, int(std::ceil(b))); ++i) {
                const float wgt = (std::min(b, float(i + 1)) - std::max(a, float(i))) / s;
                if (wgt <= 0) continue;
                if (horizontal) {
                    for (int y = 0; y < ih; ++y) {
                        const float* ip = &in[(std::size_t(y) * std::size_t(iw) + std::size_t(i)) * 4];
                        float* op = &out[(std::size_t(y) * std::size_t(ow) + std::size_t(o)) * 4];
                        for (int k = 0; k < 4; ++k) op[k] += ip[k] * wgt;
                    }
                } else {
                    for (int x = 0; x < iw; ++x) {
                        const float* ip = &in[(std::size_t(i) * std::size_t(iw) + std::size_t(x)) * 4];
                        float* op = &out[(std::size_t(o) * std::size_t(iw) + std::size_t(x)) * 4];
                        for (int k = 0; k < 4; ++k) op[k] += ip[k] * wgt;
                    }
                }
            }
        }
        return out;
    };
    std::vector<float> pm(std::size_t(src.w) * std::size_t(src.h) * 4);
    for (std::size_t i = 0; i < std::size_t(src.w) * std::size_t(src.h); ++i) {
        const float a = src.rgba[i * 4 + 3] / 255.0f;
        for (int k = 0; k < 3; ++k) pm[i * 4 + std::size_t(k)] = src.rgba[i * 4 + std::size_t(k)] / 255.0f * a;
        pm[i * 4 + 3] = a;
    }
    const std::vector<float> hx = pass(pm, src.w, src.h, w, true);
    return pass(hx, w, src.h, h, false);
}

// A box blur of one plane, three times (close to a Gaussian)
void blur(std::vector<float>& p, int w, int h, int r) {
    std::vector<float> t(p.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            float sum = 0;
            for (int x = -r; x <= r; ++x) sum += p[std::size_t(y) * std::size_t(w) + std::size_t(std::clamp(x, 0, w - 1))];
            for (int x = 0; x < w; ++x) {
                t[std::size_t(y) * std::size_t(w) + std::size_t(x)] = sum / float(2 * r + 1);
                sum += p[std::size_t(y) * std::size_t(w) + std::size_t(std::clamp(x + r + 1, 0, w - 1))] -
                       p[std::size_t(y) * std::size_t(w) + std::size_t(std::clamp(x - r, 0, w - 1))];
            }
        }
        for (int x = 0; x < w; ++x) {
            float sum = 0;
            for (int y = -r; y <= r; ++y) sum += t[std::size_t(std::clamp(y, 0, h - 1)) * std::size_t(w) + std::size_t(x)];
            for (int y = 0; y < h; ++y) {
                p[std::size_t(y) * std::size_t(w) + std::size_t(x)] = sum / float(2 * r + 1);
                sum += t[std::size_t(std::clamp(y + r + 1, 0, h - 1)) * std::size_t(w) + std::size_t(x)] -
                       t[std::size_t(std::clamp(y - r, 0, h - 1)) * std::size_t(w) + std::size_t(x)];
            }
        }
    }
}

// The logo, `lw` pixels wide, centred on (cx, cy): a soft dark shadow under it, then the logo
void place_logo(Canvas& cv, float lw, float cx, float cy) {
    const Image& lg = logo();
    if (lg.w == 0) return;
    const int w = int(lw), h = int(std::lround(lw * float(lg.h) / float(lg.w)));
    const std::vector<float> pm = resized(lg, w, h);
    const int x0 = int(std::lround(cx - float(w) * 0.5f)), y0 = int(std::lround(cy - float(h) * 0.5f));
    // the shadow: the logo's alpha in a larger plane, blurred, a little lower
    const int pad = std::max(8, w / 20), sw = w + 2 * pad, sh = h + 2 * pad;
    std::vector<float> sh_a(std::size_t(sw) * std::size_t(sh), 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) sh_a[std::size_t(y + pad) * std::size_t(sw) + std::size_t(x + pad)] = pm[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4 + 3];
    blur(sh_a, sw, sh, std::max(2, w / 70));
    const int drop = std::max(2, w / 110);
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            const int X = x0 - pad + x, Y = y0 - pad + y + drop;
            if (X < 0 || Y < 0 || X >= cv.w || Y >= cv.h) continue;
            const float m = 1.0f - 0.85f * std::min(1.0f, sh_a[std::size_t(y) * std::size_t(sw) + std::size_t(x)] * 1.4f);
            float* p = cv.at(X, Y);
            p[0] *= m;
            p[1] *= m;
            p[2] *= m;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int X = x0 + x, Y = y0 + y;
            if (X < 0 || Y < 0 || X >= cv.w || Y >= cv.h) continue;
            const float* s = &pm[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4];
            float* p = cv.at(X, Y);
            for (int k = 0; k < 3; ++k) p[k] = s[k] + p[k] * (1.0f - s[3]);
        }
}

// Soft-clipped to 8 bits, with a little ordered dither against banding in the dark gradients
Image finish(const Canvas& cv) {
    static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    Image im;
    im.w = cv.w;
    im.h = cv.h;
    im.rgba.resize(std::size_t(cv.w) * std::size_t(cv.h) * 4);
    for (int y = 0; y < cv.h; ++y)
        for (int x = 0; x < cv.w; ++x) {
            const float* p = &cv.c[(std::size_t(y) * std::size_t(cv.w) + std::size_t(x)) * 3];
            std::uint8_t* o = &im.rgba[(std::size_t(y) * std::size_t(cv.w) + std::size_t(x)) * 4];
            const float dither = (float(bayer[y & 3][x & 3]) + 0.5f) / 16.0f - 0.5f;
            for (int k = 0; k < 3; ++k) {
                float v = std::max(0.0f, p[k]);
                if (v > 0.8f) v = 0.8f + 0.2f * (1.0f - std::exp(-(v - 0.8f) / 0.2f));
                o[k] = std::uint8_t(std::clamp(int(std::floor(v * 255.0f + dither + 0.5f)), 0, 255));
            }
            o[3] = 255;
        }
    return im;
}

}  // namespace

Image cover() {
    Canvas cv(kCoverW, kCoverH);
    const Look lk{300, 330, 430, 380, 46, 0x5EA7B3u, true, 300, 560};
    ground(cv, lk);
    road(cv, lk);
    streaks(cv, lk);
    vignette(cv);
    place_logo(cv, 548, 300, 330);
    return finish(cv);
}

Image wide() {
    Canvas cv(kWideW, kWideH);
    const Look lk{460, 225, 520, 260, 40, 0xB3A7E5u, false, 0, 0};
    ground(cv, lk);
    streaks(cv, lk);
    vignette(cv);
    place_logo(cv, 690, 460, 215);
    return finish(cv);
}

Image hero() {
    Canvas cv(kHeroW, kHeroH);
    const Look lk{1220, 300, 900, 360, 90, 0x7E50C0DEu, false, 0, 0};
    ground(cv, lk);
    streaks(cv, lk);
    vignette(cv);
    return finish(cv);
}

}  // namespace b3launcher::steam_art
