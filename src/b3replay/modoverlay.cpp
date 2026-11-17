// The code mods' 2D over the game (modoverlay.h).
#include "modoverlay.h"

#include "glyphdraw.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace b3replay {

ModOverlay& ModOverlay::instance() {
    static ModOverlay* p = new ModOverlay();   // never destroyed (the render thread may outlive static destruction)
    return *p;
}

void ModOverlay::add_texture(std::uint32_t id, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba) {
    if (!rgba || w == 0 || h == 0 || w > 1024 || h > 1024) return;
    Pending p;
    p.w = w;
    p.h = h;
    p.rgba.assign(rgba, rgba + std::size_t(w) * h * 4);
    std::lock_guard<std::mutex> l(m_);
    pending_[id] = std::move(p);
}

void ModOverlay::box(int w, int h, float& x, float& y, float& bw, float& bh) {
    const float fw = float(w), fh = float(h);
    bh = std::min(fh, fw * 3.0f / 4.0f);
    bw = bh * 4.0f / 3.0f;
    x = (fw - bw) * 0.5f;
    y = (fh - bh) * 0.5f;
}

std::size_t ModOverlay::draw_list(b3gfx::Device& dev, int w, int h, const b3mod::OverlayList& list, std::vector<b3gfx::DrawItem>& out) {
    if (w <= 0 || h <= 0 || list.items.empty()) return 0;
    if (epoch_.changed(dev)) {   // a track load released every id (the game's own took them): upload the mods' pictures again
        textures_.clear();
        white_ = b3gfx::kNone;
        buffers_.clear();
        for (auto& [id, p] : kept_) dev.update_texture(textures_.try_emplace(id, b3gfx::kNone).first->second, int(p.w), int(p.h), p.rgba.data());
    }
    {   // textures given since the last frame
        std::map<std::uint32_t, Pending> fresh;
        {
            std::lock_guard<std::mutex> l(m_);
            fresh.swap(pending_);
        }
        for (auto& [id, p] : fresh) {
            // (try_emplace: operator[] would start the id at 0, a texture of the game's, which update_texture then replaces)
            b3gfx::TextureId& t = textures_.try_emplace(id, b3gfx::kNone).first->second;
            dev.update_texture(t, int(p.w), int(p.h), p.rgba.data());
            kept_[id] = std::move(p);
        }
    }
    if (white_ == b3gfx::kNone) {
        const std::uint8_t px[4] = {255, 255, 255, 255};
        white_ = dev.create_texture({b3gfx::TextureLevel{1, 1, px}});
    }
    float bx = 0, by = 0, bw = 0, bh = 0;
    box(w, h, bx, by, bw, bh);
    const float sx = bw / 640.0f, sy = bh / 448.0f;
    // Colours as the other 2D panels': the 2D stage draws into the half-intensity GS frame (RGB x 64 / 255, alpha x 128 / 255)
    auto colour = [](std::uint32_t c) {
        const auto ch = [](std::uint32_t v) { return (v * 64u + 127u) / 255u; };
        return ch(c & 0xFFu) | ch(c >> 8 & 0xFFu) << 8 | ch(c >> 16 & 0xFFu) << 16 | ((c >> 24) * 128u + 127u) / 255u << 24;
    };
    // the items in order, grouped into runs of one texture (the later run drawn over the earlier)
    struct Run {
        b3gfx::TextureId tex;
        std::vector<b3gfx::Vertex> vs;
        std::vector<std::uint32_t> is;
    };
    std::vector<Run> runs;
    auto run_for = [&](b3gfx::TextureId t) -> Run& {
        if (runs.empty() || runs.back().tex != t) runs.push_back({t, {}, {}});
        return runs.back();
    };
    auto quad = [](Run& r, const float (&x)[4], const float (&y)[4], const float (&u)[4], const float (&v)[4], std::uint32_t c) {
        const std::uint32_t base = std::uint32_t(r.vs.size());
        for (int k = 0; k < 4; ++k) {
            b3gfx::Vertex vx{};
            vx.pos[0] = x[k];
            vx.pos[1] = y[k];
            vx.uv[0] = u[k];
            vx.uv[1] = v[k];
            vx.rgba = c;
            r.vs.push_back(vx);
        }
        for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) r.is.push_back(base + k);
    };
    std::size_t drawn = 0;
    for (const b3mod::OverlayItem& it : list.items) {
        const std::uint32_t c = colour(it.rgba);
        if (it.kind == 1) {
            b3gfx::TextureId t = white_;
            if (it.texture) {
                const auto f = textures_.find(it.texture);
                if (f == textures_.end() || f->second == b3gfx::kNone) continue;   // (not given yet)
                t = f->second;
            }
            const float x0 = bx + it.x * sx, y0 = by + it.y * sy, x1 = x0 + it.w * sx, y1 = y0 + it.h * sy;
            const bool tex = it.texture != 0;
            quad(run_for(t), {x0, x1, x1, x0}, {y0, y0, y1, y1}, {tex ? 0.f : .5f, tex ? 1.f : .5f, tex ? 1.f : .5f, tex ? 0.f : .5f},
                 {tex ? 0.f : .5f, tex ? 0.f : .5f, tex ? 1.f : .5f, tex ? 1.f : .5f}, c);
            ++drawn;
        } else if (it.kind == 2 && !it.text.empty() && it.h > 0) {
            const float cap = it.h * sy, stroke = std::max(1.0f, cap * 0.16f);
            std::vector<glyph::Line> lines;
            glyph::text(it.text, bx + it.x * sx, by + it.y * sy, cap, stroke, lines);
            Run& r = run_for(white_);
            const float half = stroke * 0.5f;
            const float uv[4] = {.5f, .5f, .5f, .5f};
            for (const glyph::Line& l : lines) {
                if (l.size() == 1) {   // a dot
                    const glyph::Pt p = l[0];
                    quad(r, {p.x - half, p.x + half, p.x + half, p.x - half}, {p.y - half, p.y - half, p.y + half, p.y + half}, uv, uv, c);
                }
                for (std::size_t k = 1; k < l.size(); ++k) {   // each segment a quad `stroke` wide, its ends out by half (the joins)
                    const glyph::Pt a = l[k - 1], b = l[k];
                    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
                    if (!(len > 0)) {   // lua6: a zero-length segment is a dot ('.' and ':' are written so): a square
                        quad(r, {a.x - half, a.x + half, a.x + half, a.x - half}, {a.y - half, a.y - half, a.y + half, a.y + half}, uv, uv, c);
                        continue;
                    }
                    const float ux = dx / len * half, uy = dy / len * half, nx = -uy, ny = ux;
                    quad(r, {a.x - ux + nx, b.x + ux + nx, b.x + ux - nx, a.x - ux - nx}, {a.y - uy + ny, b.y + uy + ny, b.y + uy - ny, a.y - uy - ny}, uv, uv, c);
                }
            }
            ++drawn;
        }
    }
    // window pixels -> clip space (top left origin), as the FPS panel's
    const float m[16] = {2.0f / float(w), 0, 0, 0, 0, -2.0f / float(h), 0, 0, 0, 0, 1, 0, -1, 1, 0.5f, 1};
    if (buffers_.size() < runs.size()) buffers_.resize(runs.size(), {b3gfx::kNone, b3gfx::kNone});
    for (std::size_t k = 0; k < runs.size(); ++k) {
        if (runs[k].is.empty()) continue;
        dev.update_vertex_buffer(buffers_[k].first, runs[k].vs.data(), runs[k].vs.size());
        dev.update_index_buffer(buffers_[k].second, runs[k].is.data(), runs[k].is.size());
        b3gfx::DrawItem d;
        d.overlay = true;
        d.shading = b3gfx::Shading::kTrack;
        d.track_type = 0;
        d.textured = true;
        d.texture = runs[k].tex;
        d.gs_alpha = 0x44;
        d.overlay_blend = true;
        d.point_sample = true;   // (textures shown pixel for pixel, scaled without smoothing)
        std::memcpy(d.model, m, sizeof m);
        d.vertices = buffers_[k].first;
        d.indices = buffers_[k].second;
        d.first_index = 0;
        d.index_count = std::uint32_t(runs[k].is.size());
        out.push_back(d);
    }
    return drawn;
}

}  // namespace b3replay
