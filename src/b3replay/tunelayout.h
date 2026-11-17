// tunenav3: the F9 tuning console's panel layout, as a pure function of its lines and the window (tests/test_tune_layout.cpp
// holds it to "nothing wraps, nothing is wider than the window"). The console draws one line per row (a label, a value
// column, a default column): fit() picks the text size from the window and the content, then, only if the smallest
// readable size is still too wide, cuts labels with "..." -- the focused row's full text is shown elsewhere in the panel.
#pragma once

#include "glyphdraw.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace b3replay::tunelayout {

struct Item {
    std::string text;            // the label (a wide line: the whole line)
    std::string value, def;      // right-aligned columns
    std::string mark;            // the gutter: "*" changed, "+" a favourite (rows only)
    bool wide = false;           // a line across the panel (heading, key hints); its value, if any, flush right
};

struct Metrics {
    float s = 1, cap = 10, st = 1.5f, pad = 12, gap = 6, col = 18, gutter = 0, margin = 0;
    float pitch() const { return cap + gap; }
};
inline Metrics metrics(float s) {
    Metrics m;
    m.s = s;
    m.cap = 10.0f * s;
    m.st = std::max(1.2f, 1.5f * s);
    m.pad = 12 * s;
    m.gap = 6 * s;
    m.col = 18 * s;
    m.gutter = glyph::text_width("*+", m.cap, m.st) + 0.8f * m.cap;
    m.margin = 0.02f;
    return m;
}

struct Fit {
    Metrics m;
    float panel_w = 0, panel_h = 0;
    float label_w = 0, val_w = 0, def_w = 0;   // the columns' widths (px)
    std::vector<Item> items;                   // after any cutting
    int cut = 0;                               // items with a cut label
    bool floor_scale = false;                  // the smallest size was needed
};

inline std::string ellipsize(const std::string& s, float maxw, float cap, float st) {
    if (glyph::text_width(s, cap, st) <= maxw) return s;
    std::string t = s;
    while (!t.empty() && glyph::text_width(t + "...", cap, st) > maxw) t.pop_back();
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t + "...";
}

// The limits the panel must stay inside at this window size (px)
inline float max_panel_w(int w, int h) { return std::min(0.94f * float(w), 1.45f * float(h)); }
inline float max_panel_h(int h) { return 0.86f * float(h); }

// natural widths at scale s: the wide lines, the rows' label column, the value and default columns
inline void natural(const std::vector<Item>& in, const Metrics& m, float& wide_w, float& label_w, float& val_w, float& def_w) {
    wide_w = label_w = val_w = def_w = 0;
    for (const Item& it : in) {
        const float a = glyph::text_width(it.text, m.cap, m.st);
        const float v = it.value.empty() ? 0.0f : glyph::text_width(it.value, m.cap, m.st);
        if (it.wide) wide_w = std::max(wide_w, a + (v > 0 ? m.col + v : 0.0f));
        else {
            label_w = std::max(label_w, a);
            val_w = std::max(val_w, v);
            def_w = std::max(def_w, it.def.empty() ? 0.0f : glyph::text_width(it.def, m.cap, m.st));
        }
    }
}
inline float body_w(const Metrics& m, float wide_w, float label_w, float val_w, float def_w) {
    return std::max(wide_w, m.gutter + label_w + (val_w > 0 ? m.col + val_w : 0.0f) + (def_w > 0 ? m.col + def_w : 0.0f));
}

inline Fit fit(const std::vector<Item>& in, int w, int h) {
    Fit f;
    const float s_hi = std::max(0.85f, float(h) / 720.0f), s_lo = std::max(0.8f, 0.65f * s_hi);
    const float maxw = max_panel_w(w, h), maxh = max_panel_h(h);
    float s = s_hi;
    for (;; s -= 0.025f) {
        f.m = metrics(s);
        float a, b, c, d;
        natural(in, f.m, a, b, c, d);
        const float bw = body_w(f.m, a, b, c, d);
        const float ph = float(in.size()) * f.m.pitch() - f.m.gap + 2 * f.m.pad;
        if ((bw + 2 * f.m.pad <= maxw && ph <= maxh) || s - 0.025f < s_lo) break;
    }
    if (s < s_lo) s = s_lo;
    f.m = metrics(s);
    f.floor_scale = s <= s_lo + 1e-4f && s < s_hi - 1e-4f;
    float wide_w, label_w, val_w, def_w;
    natural(in, f.m, wide_w, label_w, val_w, def_w);
    const float avail = maxw - 2 * f.m.pad;   // the body's most
    // the value and default columns keep their words (cut only when they alone would take most of the line)
    const float col_cap = 0.45f * avail;
    f.items = in;
    auto cutcol = [&](std::string Item::*field, float& colw) {
        if (colw <= col_cap) return;
        colw = 0;
        for (Item& it : f.items)
            if (!it.wide || field == &Item::value) {
                it.*field = ellipsize(it.*field, col_cap, f.m.cap, f.m.st);
                colw = std::max(colw, glyph::text_width(it.*field, f.m.cap, f.m.st));
            }
    };
    cutcol(&Item::value, val_w);
    cutcol(&Item::def, def_w);
    const float cols = (val_w > 0 ? f.m.col + val_w : 0.0f) + (def_w > 0 ? f.m.col + def_w : 0.0f);
    float body = body_w(f.m, wide_w, label_w, val_w, def_w);
    body = std::min(std::max(body, 0.40f * float(w)), avail);   // (not a sliver on a page of short names: the help needs room)
    f.label_w = std::max(0.0f, body - f.m.gutter - cols);
    f.val_w = val_w;
    f.def_w = def_w;
    for (Item& it : f.items) {
        float room = it.wide ? body - (it.value.empty() ? 0.0f : f.m.col + glyph::text_width(it.value, f.m.cap, f.m.st)) : f.label_w;
        if (glyph::text_width(it.text, f.m.cap, f.m.st) <= room + 0.5f) continue;
        it.text = ellipsize(it.text, room, f.m.cap, f.m.st);
        ++f.cut;
    }
    f.panel_w = std::ceil(body + 2 * f.m.pad);
    f.panel_h = std::ceil(float(f.items.size()) * f.m.pitch() - f.m.gap + 2 * f.m.pad);
    return f;
}

}  // namespace b3replay::tunelayout
