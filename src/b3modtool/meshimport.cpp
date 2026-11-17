// What the car and track imports share (include/b3modtool/meshimport.h): the strip builder, node matrices, reading a
// .gltf / .glb back.
#include "b3modtool/meshimport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace b3modtool {

namespace {

bool fail(std::string* why, const std::string& m) { if (why) *why = m; return false; }

std::uint32_t rd32(const Buf& b, std::size_t at) { return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24; }

std::string uri_decode(const std::string& u) {
    std::string r;
    for (std::size_t i = 0; i < u.size(); ++i) {
        if (u[i] == '%' && i + 2 < u.size() && std::isxdigit(static_cast<unsigned char>(u[i + 1])) && std::isxdigit(static_cast<unsigned char>(u[i + 2]))) {
            r.push_back(char(std::stoi(u.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            r.push_back(u[i]);
        }
    }
    return r;
}

bool base64(const std::string& s, Buf& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    std::uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        if (c == '=') break;
        const int v = val(c);
        if (v < 0) return false;
        acc = acc << 6 | std::uint32_t(v);
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(std::uint8_t(acc >> bits)); }
    }
    return true;
}

}  // namespace

void build_strips(const std::vector<std::array<std::uint32_t, 3>>& T, int max_batch, std::vector<std::pair<std::uint32_t, bool>>& seq,
                  std::vector<std::uint32_t>& batches) {
    seq.clear();
    batches.clear();
    // directed edge -> the triangles that have it in their winding
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> edges;
    auto ek = [](std::uint32_t a, std::uint32_t b) { return std::uint64_t(a) << 32 | b; };
    for (std::uint32_t t = 0; t < T.size(); ++t)
        for (int k = 0; k < 3; ++k) edges[ek(T[t][std::size_t(k)], T[t][std::size_t((k + 1) % 3)])].push_back(t);
    std::vector<std::uint32_t> used(T.size(), 0);   // 0 free, 1 used, else: the trial's stamp
    std::uint32_t stamp = 1;
    auto third = [&](std::uint32_t t, std::uint32_t a, std::uint32_t b) {
        for (int k = 0; k < 3; ++k)
            if (T[t][std::size_t(k)] != a && T[t][std::size_t(k)] != b) return T[t][std::size_t(k)];
        return T[t][0];
    };
    auto next_tri = [&](std::uint32_t a, std::uint32_t b) -> std::int64_t {
        auto it = edges.find(ek(a, b));
        if (it == edges.end()) return -1;
        for (std::uint32_t t : it->second)
            if (used[t] == 0) return t;
        return -1;
    };
    // A strip s with parity p: its vertex k closes (s[k-2], s[k-1], s[k]) when k + p is even, (s[k-1], s[k-2], s[k])
    // when odd (the decode's rule by batch place: the strip goes on a place of parity p). Grown forward while a free
    // triangle continues it with that winding, then turned round (the parity becomes (length + p) mod 2: the same
    // triangles, the same windings) and grown from its other end.
    struct Strip { std::vector<std::uint32_t> s; int p = 0; };
    auto extend = [&](Strip& st, std::vector<std::uint32_t>& ts) {
        for (;;) {
            const std::size_t k = st.s.size();
            const std::uint32_t a = st.s[k - 2], b = st.s[k - 1];
            const bool even = ((k + std::size_t(st.p)) & 1) == 0;
            const std::int64_t nt = even ? next_tri(a, b) : next_tri(b, a);
            if (nt < 0) break;
            used[std::size_t(nt)] = stamp;
            ts.push_back(std::uint32_t(nt));
            st.s.push_back(even ? third(std::uint32_t(nt), a, b) : third(std::uint32_t(nt), b, a));
        }
    };
    auto turn = [](Strip& st) {
        std::reverse(st.s.begin(), st.s.end());
        st.p = int((st.s.size() + std::size_t(st.p)) & 1);
    };
    auto build = [&](std::uint32_t t, int r, Strip& st, std::vector<std::uint32_t>& ts) {
        st.s = {T[t][std::size_t(r)], T[t][std::size_t((r + 1) % 3)], T[t][std::size_t((r + 2) % 3)]};
        st.p = 0;
        ts = {t};
        ++stamp;
        used[t] = stamp;
        extend(st, ts);
        turn(st);
        extend(st, ts);
        for (std::uint32_t x : ts) used[x] = 0;
    };
    // start where triangles have the fewest neighbours (the strips then eat the mesh from its edges inwards); of a start
    // triangle's three turns, the one that gives the longest strip
    std::vector<std::uint32_t> order(T.size());
    std::vector<int> nb(T.size(), 0);
    for (std::uint32_t t = 0; t < T.size(); ++t) {
        order[t] = t;
        for (int k = 0; k < 3; ++k)
            if (edges.count(ek(T[t][std::size_t((k + 1) % 3)], T[t][std::size_t(k)]))) ++nb[t];
    }
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return nb[a] < nb[b]; });
    std::vector<Strip> strips;
    for (std::uint32_t t : order) {
        if (used[t]) continue;
        Strip best, st;
        std::vector<std::uint32_t> best_ts, ts;
        for (int r = 0; r < 3; ++r) {
            build(t, r, st, ts);
            if (st.s.size() > best.s.size()) { best = st; best_ts = ts; }
        }
        for (std::uint32_t x : best_ts) used[x] = 1;
        strips.push_back(std::move(best));
    }
    // into batches of at most max_batch: a strip starts on a place of its parity (turned round when that saves an
    // extra ADC vertex); a strip that does not fit goes on in the next batch from its last two vertices (ADC), on the
    // parity of its next vertex
    int cnt = 0;
    auto push = [&](std::uint32_t vi, bool adc) {
        seq.push_back({vi, adc});
        ++cnt;
    };
    auto flush = [&] { if (cnt) { batches.push_back(std::uint32_t(cnt)); cnt = 0; } };
    for (Strip& S : strips) {
        if ((cnt & 1) != S.p && (S.s.size() & 1)) turn(S);
        const int pad = (cnt & 1) != S.p ? 1 : 0;
        if (cnt + pad + 3 > max_batch) flush();
        if ((cnt & 1) != S.p) push(S.s[0], true);
        push(S.s[0], true);
        push(S.s[1], true);
        for (std::size_t k = 2; k < S.s.size(); ++k) {
            if (cnt == max_batch) {
                flush();
                if ((k + std::size_t(S.p)) & 1) push(S.s[k - 2], true);
                push(S.s[k - 2], true);
                push(S.s[k - 1], true);
            }
            push(S.s[k], false);
        }
    }
    flush();
}

M4 m4(const std::array<float, 16>& f) { M4 r; for (int i = 0; i < 16; ++i) r[std::size_t(i)] = f[std::size_t(i)]; return r; }

M4 m4_mul(const M4& a, const M4& b) {
    M4 r{};
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a[std::size_t(k * 4 + rr)] * b[std::size_t(c * 4 + k)];
            r[std::size_t(c * 4 + rr)] = s;
        }
    return r;
}

bool m4_inverse(const M4& m, M4& r) {
    const double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(det) || std::fabs(det) < 1e-12) return false;
    const double inv[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                           (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                           (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};   // rows
    r = M4{};
    for (int rr = 0; rr < 3; ++rr)
        for (int cc = 0; cc < 3; ++cc) r[std::size_t(cc * 4 + rr)] = inv[rr * 3 + cc];
    for (int rr = 0; rr < 3; ++rr) r[std::size_t(12 + rr)] = -(r[std::size_t(rr)] * m[12] + r[std::size_t(4 + rr)] * m[13] + r[std::size_t(8 + rr)] * m[14]);
    r[15] = 1;
    return true;
}

bool m4_near_identity(const M4& m) {
    for (int i = 0; i < 16; ++i)
        if (std::fabs(m[std::size_t(i)] - kM4Identity[std::size_t(i)]) > 1e-5) return false;
    return true;
}

std::string path_dir_of(const std::string& p) {
    const std::size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}

bool gltf_load_doc(const FileLoader& load, const std::string& model, Json& doc, Buf& bin, std::string* why) {
    Buf f;
    if (!load(model, f)) return fail(why, "cannot read " + model);
    if (f.size() >= 20 && rd32(f, 0) == 0x46546C67u) {   // GLB: "glTF", version 2, length; JSON chunk, BIN chunk
        if (rd32(f, 4) != 2) return fail(why, model + ": not a glTF 2 binary");
        std::size_t p = 12;
        bool have_json = false;
        while (p + 8 <= f.size()) {
            const std::size_t len = rd32(f, p), type = rd32(f, p + 4);
            if (p + 8 + len > f.size()) return fail(why, model + ": a chunk past the end");
            if (type == 0x4E4F534Au) {
                if (!json_parse(std::string_view(reinterpret_cast<const char*>(f.data() + p + 8), len), doc, why)) return fail(why, model + ": " + *why);
                have_json = true;
            } else if (type == 0x004E4942u) {
                bin.assign(f.begin() + std::ptrdiff_t(p + 8), f.begin() + std::ptrdiff_t(p + 8 + len));
            }
            p += 8 + ((len + 3) & ~std::size_t(3));
        }
        if (!have_json) return fail(why, model + ": no JSON chunk");
    } else if (!json_parse(std::string_view(reinterpret_cast<const char*>(f.data()), f.size()), doc, why)) {
        return fail(why, model + ": " + *why);
    }
    const Json* bufs = doc.get("buffers");
    if (!bufs || bufs->size() != 1) return fail(why, model + ": one buffer expected (the exporter's and Blender's form), found " + std::to_string(bufs ? bufs->size() : 0));
    if (const Json* u = bufs->a[0].get("uri")) {
        if (u->s.rfind("data:", 0) == 0) {
            const std::size_t c = u->s.find(";base64,");
            if (c == std::string::npos || !base64(u->s.substr(c + 8), bin)) return fail(why, model + ": a data URI that is not base64");
        } else if (!load(path_dir_of(model) + uri_decode(u->s), bin)) {
            return fail(why, model + ": cannot read its buffer " + u->s);
        }
    }
    return true;
}

bool gltf_world(const Json& doc, std::vector<M4>& world, std::vector<int>& seen, std::string* why) {
    const Json* nodes = doc.get("nodes");
    const std::size_t n = nodes ? nodes->size() : 0;
    world.assign(n, kM4Identity);
    seen.assign(n, 0);
    std::vector<int> roots;
    if (const Json* sc = doc.get("scenes"); sc && sc->size()) {
        const int s = int(doc.get("scene") ? doc.get("scene")->as_int() : 0);
        if (s >= 0 && std::size_t(s) < sc->size() && sc->a[std::size_t(s)].get("nodes"))
            for (const Json& r : sc->a[std::size_t(s)].get("nodes")->a) roots.push_back(int(r.n));
    }
    if (roots.empty()) {   // no scene: every node no other node names as a child
        std::vector<int> child(n, 0);
        for (std::size_t i = 0; i < n; ++i)
            if (const Json* ch = nodes->a[i].get("children")) for (const Json& c : ch->a) if (c.n >= 0 && std::size_t(c.n) < n) child[std::size_t(c.n)] = 1;
        for (std::size_t i = 0; i < n; ++i) if (!child[i]) roots.push_back(int(i));
    }
    std::vector<std::pair<int, M4>> stack;
    for (int r : roots) stack.push_back({r, kM4Identity});
    while (!stack.empty()) {
        auto [ni, parent] = stack.back();
        stack.pop_back();
        if (ni < 0 || std::size_t(ni) >= n || seen[std::size_t(ni)]) return fail(why, "bad node hierarchy");
        seen[std::size_t(ni)] = 1;
        const Json& nd = nodes->a[std::size_t(ni)];
        world[std::size_t(ni)] = m4_mul(parent, m4(gltf_node_matrix(nd)));
        if (const Json* ch = nd.get("children"))
            for (const Json& c : ch->a) stack.push_back({int(c.n), world[std::size_t(ni)]});
    }
    return true;
}

void gltf_read_field(const Json& doc, const Buf& bin, const Json& attributes, const std::string& name,
                     const std::vector<std::string>& suffix, double norm_scale, std::vector<double>& out, int& ncomp) {
    out.clear();
    ncomp = 0;
    const Json* accessors = doc.get("accessors");
    if (!accessors) return;
    // one accessor's values (scaled when normalized); false when it is not there or cannot be read
    auto read = [&](const std::string& k, std::vector<double>& v, int& n) {
        const Json* a = attributes.get(k);
        if (!a || a->as_int() < 0 || std::size_t(a->as_int()) >= accessors->size()) return false;
        if (!gltf_read_accessor(doc, bin, int(a->as_int()), v, n, nullptr)) return false;
        const Json& acc = accessors->a[std::size_t(a->as_int())];
        if (acc.get("normalized") && acc.get("normalized")->b)
            for (double& x : v) x *= norm_scale;
        return true;
    };
    std::vector<std::vector<double>> cols(suffix.size());
    bool split = !suffix.empty();
    for (std::size_t c = 0; split && c < suffix.size(); ++c) {
        int n = 0;
        split = read(name + "_" + suffix[c], cols[c], n) && n == 1 && cols[c].size() == cols[0].size();
    }
    if (split) {
        const std::size_t nv = cols[0].size(), nc = suffix.size();
        out.resize(nv * nc);
        for (std::size_t k = 0; k < nv; ++k)
            for (std::size_t c = 0; c < nc; ++c) out[k * nc + c] = cols[c][k];
        ncomp = int(nc);
        return;
    }
    if (!read(name, out, ncomp)) { out.clear(); ncomp = 0; }
}

}  // namespace b3modtool
