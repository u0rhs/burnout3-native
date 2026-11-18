#include "b3asset/track.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

#include "b3asset/vif.h"
#include "b3host/ps2float.h"

namespace b3asset {

namespace {

bool fail(std::string* why, const std::string& what) { if (why) *why = what; return false; }

std::int32_t s32(Bytes f, std::size_t o) { return std::int32_t(f.u32(o)); }
std::int16_t s16(Bytes f, std::size_t o) { return std::int16_t(f.u16(o)); }
std::int8_t s8(Bytes f, std::size_t o) { return f.has(o, 1) ? std::int8_t(f.p[o]) : 0; }
std::uint8_t u8(Bytes f, std::size_t o) { return f.has(o, 1) ? f.p[o] : 0; }

// PS2 colour byte (0x80 = 1.0) -> 8-bit.
std::uint32_t c8(std::uint32_t v) { return std::min<std::uint32_t>(255, (v & 0xFF) * 2); }
// The w lanes of a CB3InstanceData's rows X/Y/Z: the instance's colour, 128.5 * w = GS bytes
// [C VU0 program 0x479610 0x00CA-0x00D6 (type 4), consumed as RGBAQ by VU1 0x471FA0 0x0073/0x0078].
std::array<float, 4> instance_tint(Bytes f, std::size_t at) {
    return {f.f32(at + 0x0C), f.f32(at + 0x1C), f.f32(at + 0x2C), 0.0f};
}

struct Reader {
    Bytes st, sm;
    const TrackOptions& opt;
    GsMemory& gs;
    Model& out;
    std::vector<TrackMaterialInfo> mats;
    std::vector<int> mat_anim;   // material -> Model::track_anims index (-1 not animated)
    // Decoded (chain A, chain B, material) -> draw range template, so shared instance models decode once.
    std::map<std::tuple<std::size_t, std::size_t, int, bool>, DrawRange> cache;
    int cur_unit = -1;   // for TrackOptions::on_mesh

    Reader(Bytes s, Bytes m, const TrackOptions& o, GsMemory& g, Model& mo) : st(s), sm(m), opt(o), gs(g), out(mo) {}

    bool load_materials(std::string* why) {
        // Texture table: absolute offsets of serialized rasters; decode each once.
        const int ntex = s16(st, 0x16);
        const std::size_t tab = st.u32(0x18);
        std::map<std::size_t, int> tex_index;
        for (int k = 0; k < ntex; ++k) {
            const std::size_t at = st.u32(tab + 4 * std::size_t(k));
            DecodedRaster r;
            std::string err;
            if (!decode_raster(st, at, gs, r, 0, &err)) return fail(why, "track texture " + std::to_string(k) + ": " + err);
            tex_index[at] = int(out.textures.size());
            out.textures.push_back(Texture{r.hdr.name, std::move(r.levels), r.translucent, std::move(r.original)});
        }
        const int nmat = s16(st, 0x0C);
        const std::size_t mat0 = st.u32(0x08);
        for (int k = 0; k < nmat; ++k) {
            const std::size_t b = mat0 + 0x28 * std::size_t(k);
            TrackMaterialInfo m;
            m.type = st.u32(b);
            m.test = st.u32(b + 0x20);
            m.param4 = st.f32(b + 0x04);
            m.param8 = st.f32(b + 0x08);
            m.flags = st.u16(b + 0x24);
            m.alpha = u8(st, b + 0x26);
            m.alpha_fix = u8(st, b + 0x27);
            const std::uint32_t list = st.u32(b + 0x0C);
            if (list && u8(st, b + 0x11)) {
                // Entries are offsets relative to the material start [C CB3TrackMaterial::FixUp]; entry 0 = the first
                // frame of an animated material (the current index at +0x10 is runtime state).
                const std::size_t at = std::size_t(std::int64_t(b) + s32(st, b + list));
                auto it = tex_index.find(at);
                if (it == tex_index.end()) return fail(why, "material texture is not in the texture table");
                m.texture = it->second;
            }
            mats.push_back(m);
        }
        // Animated materials: the header's update list (+0x10, s16[+0x0E]), which CB3StaticTrack::Update 0x3329A0 walks
        // every tick [C asm; on all 37 routes it lists exactly the materials with 2+ textures or flag 0x200].
        mat_anim.assign(mats.size(), -1);
        const int nlist = s16(st, 0x0E);
        const std::size_t lst = st.u32(0x10);
        for (int k = 0; lst && k < nlist; ++k) {
            const int mi = s16(st, lst + 2 * std::size_t(k));
            if (mi < 0 || mi >= nmat) return fail(why, "update list names material " + std::to_string(mi));
            const std::size_t b = mat0 + 0x28 * std::size_t(mi);
            TrackAnim a;
            a.material = mi;
            a.flags = st.u16(b + 0x24);
            a.p12 = s8(st, b + 0x12);
            a.p13 = s8(st, b + 0x13);
            a.f14 = st.f32(b + 0x14);
            a.f18 = st.f32(b + 0x18);
            const std::uint32_t list = st.u32(b + 0x0C);
            for (int q = 0; list && q < s8(st, b + 0x11); ++q) {
                const std::size_t at = std::size_t(std::int64_t(b) + s32(st, b + list + 4 * std::size_t(q)));
                auto it = tex_index.find(at);
                if (it == tex_index.end()) return fail(why, "material texture is not in the texture table");
                a.frames.push_back(it->second);
            }
            // Update reads a frame's number as atoi(name + strlen(frame 0's name)) and keeps it as s8 [C 0x332BB0..]
            for (int f : a.frames) {
                const std::string& base = out.textures[std::size_t(a.frames[0])].name;
                const std::string& name = out.textures[std::size_t(f)].name;
                a.number.push_back(name.size() >= base.size() ? std::int8_t(std::atoi(name.c_str() + base.size())) : 0);
            }
            mat_anim[std::size_t(mi)] = int(out.track_anims.size());
            out.track_anims.push_back(std::move(a));
        }
        out.stats["materials"] = mats.size();
        out.stats["textures"] = out.textures.size();
        return true;
    }

    // One VU batch -> vertices + triangles. `fmts`: start address -> UNPACK format of this batch.
    void emit_batch(const VuMemory& vu, std::uint32_t n, const std::map<std::uint32_t, int>& fmts, DrawRange& dr) {
        auto f = [&](std::uint32_t a) { auto it = fmts.find(a); return it == fmts.end() ? -1 : it->second; };
        if (!n) return;
        if (f(0) != 0xC) { ++out.stats["batches without positions"]; return; }
        if ((dr.material & 0xFFFF) == 3) {   // line mesh: segments as quads (track_line_batch)
            const std::size_t v0 = out.vertices.size(), i0 = out.indices.size();
            out.stats["line segments"] += track_line_batch(vu, n, out.vertices, out.indices);
            if (dr.transform < 0) for (std::size_t i = v0; i < out.vertices.size(); ++i) out.grow_bounds(out.vertices[i].pos);
            dr.index_count += std::uint32_t(out.indices.size() - i0);
            ++out.stats["batches"];
            ++out.stats["line batches"];
            return;
        }
        const int uvf = f(1), colf = f(2), nrmf = f(204);
        const std::uint32_t base = std::uint32_t(out.vertices.size());
        std::vector<bool> adc(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            Vertex v{};
            for (int c = 0; c < 3; ++c) v.pos[c] = vu.f(3 * i, c);
            adc[i] = vu.qw(3 * i)[3] & 0x8000;
            const std::uint32_t* uq = vu.qw(3 * i + 1);
            if (uvf == 0x5) {  // V2-16 signed 4.12
                v.uv[0] = float(std::int32_t(uq[0])) / 4096.0f;
                v.uv[1] = float(std::int32_t(uq[1])) / 4096.0f;
            } else if (uvf == 0x4) {  // V2-32 float
                v.uv[0] = vu.f(3 * i + 1, 0);
                v.uv[1] = vu.f(3 * i + 1, 1);
            }
            // Colour: the raw GS bytes (0x80 = 1.0), alpha included: the GS uses it for the alpha test (At * a >> 7),
            // blending and the frame-buffer alpha the full-screen specular pass reads (docs/native-renderer.md §11).
            const std::uint32_t* cq = vu.qw(3 * i + 2);
            if (colf == 0xE) {
                v.rgba = (cq[0] & 0xFF) | (cq[1] & 0xFF) << 8 | (cq[2] & 0xFF) << 16 | (cq[3] & 0xFF) << 24;
            } else if (colf == 0xA) {
                v.rgba = (cq[0] & 0xFF) | (cq[1] & 0xFF) << 8 | (cq[2] & 0xFF) << 16;  // types 1/2: alpha from the shader
            } else {
                v.rgba = 0x80808080u;  // instances: the colour comes from the instance (Model::transform_tints)
            }
            const std::uint32_t g = i / 4, j = i % 4;
            if (nrmf == 0xE) {  // type 1: qw 204 + 4g + c holds component c of vertices 4g..4g+3 in its lanes
                for (int c = 0; c < 3; ++c) v.normal[c] = float(std::int32_t(vu.qw(204 + 4 * g + c)[j])) / 127.0f;
                v.infl[0] = std::uint16_t(vu.qw(204 + 4 * g + 3)[j] & 0xFF);
            } else if (uvf == 0xE) {  // type 2: the same transposed normal + intensity, in the UV slots 3(4g+c)+1
                for (int c = 0; c < 3; ++c) v.normal[c] = float(std::int32_t(vu.qw(3 * (4 * g + c) + 1)[j])) / 127.0f;
                v.infl[0] = std::uint16_t(vu.qw(3 * (4 * g + 3) + 1)[j] & 0xFF);
            } else {
                v.normal[1] = 1.0f;
            }
            out.vertices.push_back(v);
            if (dr.transform < 0) out.grow_bounds(v.pos);
        }
        const std::size_t before = out.indices.size();
        strip_to_triangles(base, adc, out.indices);
        dr.index_count += std::uint32_t(out.indices.size() - before);
        ++out.stats["batches"];
        ++out.stats["batch layout " + std::to_string(f(0)) + "/" + std::to_string(uvf) + "/" + std::to_string(colf) +
                    (nrmf >= 0 ? "/n" : "")];
    }

    bool run_chain(Bytes f, std::size_t at, VifInterpreter& vif, const VifInterpreter::KickFn& kick, std::string* why) {
        const std::uint32_t tag = f.u32(at);
        if (((tag >> 28) & 7) != 6) return fail(why, "mesh chain is not a DMA RET packet");
        const std::size_t qwc = tag & 0xFFFF;
        if (!f.has(at, 16 + qwc * 16)) return fail(why, "mesh chain outside the file");
        std::string err;
        if (!vif.run(f.p + at + 8, 8 + qwc * 16, kick, &err, 8)) return fail(why, "mesh VIF: " + err);
        vif.flush(kick);
        return true;
    }

    // Decodes a mesh's chains with material `mi`; appends a draw (with `transform`). Returns false on a format error.
    bool add_mesh(Bytes f, std::size_t c1, std::size_t c2, int mi, const std::string& part, int transform, std::string* why) {
        if (mi < 0 || std::size_t(mi) >= mats.size()) { ++out.stats["meshes with a bad material"]; return true; }
        const TrackMaterialInfo& m = mats[mi];
        if (m.type == 3 && !opt.lines) { ++out.stats["line meshes skipped"]; return true; }
        const auto key = std::make_tuple(c1, c2, mi, f.p == st.p);
        auto it = cache.find(key);
        DrawRange dr;
        if (it != cache.end()) {
            dr = it->second;
        } else {
            dr.first_index = std::uint32_t(out.indices.size());
            dr.texture = m.texture;
            dr.material = m.type | std::uint32_t(m.flags) << 16;
            dr.anim = std::int16_t(mat_anim[std::size_t(mi)]);
            dr.colour = m.test;
            dr.transform = transform;
            // GS ALPHA_1 (A,B,C,D) = byte +0x26: 0x64 = (Cs-Cd)*FIX+Cd (opaque at FIX 0x80), 0x44 = (Cs-Cd)*As+Cd.
            const bool fix = ((m.alpha >> 4) & 3) == 2;
            const bool blend = m.alpha == 0x44 || (fix && m.alpha_fix < 0x80) || (m.alpha != 0x64 && m.alpha != 0x44);
            // GS TEST_1: ATE bit 0, ATST bits 1-3 (5 GEQUAL, 6 GREATER), AREF bits 4-11 (0x80 = 1.0).
            const bool ate = m.test & 1;
            const int atst = int(m.test >> 1) & 7;
            const std::uint32_t aref = (m.test >> 4) & 0xFF;
            if (blend) {
                dr.blend = DrawRange::kBlend;
                if (fix) dr.tint = 0x00FFFFFFu | c8(m.alpha_fix) << 24;
            } else if (ate && (atst == 5 || atst == 6)) {
                dr.blend = DrawRange::kAlphaTest;
                dr.alpha_ref = (float(aref * 2) + (atst == 6 ? 1.0f : 0.0f)) / 255.0f;
            } else {
                dr.blend = DrawRange::kOpaque;
            }
            VifInterpreter vif;
            dr.param[0] = m.param4;
            dr.param[1] = m.param8;
            const auto kick = [&](const VuMemory& vu, std::uint32_t itop, VifInterpreter::KickKind, std::uint32_t) {
                if (opt.on_batch) opt.on_batch(vu, itop, vif.batch_unpacks, mi, transform);
                emit_batch(vu, itop, vif.batch_unpacks, dr);
            };
            const std::uint64_t before = out.stats["batches"];
            if (!run_chain(f, c1, vif, kick, why)) return false;
            if (c2 && !run_chain(f, c2, vif, kick, why)) return false;
            for (const auto& kv : vif.stats.unpacks) out.stats["unpack " + kv.first] += kv.second;
            const char* cat = transform >= 0 ? "batches instance models" : f.p == st.p ? "batches backdrop" : "batches streamed";
            out.stats[cat] += out.stats["batches"] - before;
            cache[key] = dr;
        }
        dr.part = part;
        dr.transform = transform;
        if (dr.index_count) out.draws.push_back(dr);
        if (opt.on_mesh) {
            TrackOptions::MeshRef ref;
            ref.in_static = f.p == st.p;
            ref.chain1 = c1;
            ref.chain2 = c2;
            ref.material = mi;
            ref.unit = cur_unit;
            ref.draw = dr.index_count ? int(out.draws.size() - 1) : -1;
            ref.shared = it != cache.end();
            opt.on_mesh(ref);
        }
        return true;
    }

    bool add_track_mesh(Bytes f, std::size_t mesh, int mi, const std::string& part, std::string* why) {
        if (!f.has(mesh, 0x40)) return fail(why, "track mesh outside the file");
        const std::uint32_t a = f.u32(mesh + 0x0C), b = f.u32(mesh + 0x1C);
        if (!a) return true;
        ++out.stats["meshes"];
        return add_mesh(f, mesh + a, b ? mesh + b : 0, mi, part, -1, why);
    }

    int add_transform(std::size_t at) {
        // CB3InstanceData: rows X, Y, Z (w: per-instance values [G]), then the translation; row-vector convention
        // (world = x*X + y*Y + z*Z + T) [L], stored column-major for the GPU.
        std::array<float, 16> m{};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m[r * 4 + c] = st.f32(at + 16 * r + 4 * c);
        for (int c = 0; c < 3; ++c) m[12 + c] = st.f32(at + 0x30 + 4 * c);
        m[15] = 1.0f;
        const float t[3] = {m[12], m[13], m[14]};
        out.grow_bounds(t);
        out.transforms.push_back(m);
        out.transform_tints.push_back(instance_tint(st, at));
        return int(out.transforms.size() - 1);
    }

    // Instance / prop type (0x50 B at `nb` in STATIC): main model pair at +0x20 (offsets relative to the pair), material +0x38.
    bool add_instance(std::size_t nb, int transform, const std::string& part, std::string* why) {
        const std::size_t pair = nb + 0x20;
        const std::uint32_t a = st.u32(pair), b = st.u32(pair + 4);
        if (!a) return true;
        ++out.stats["instance draws"];
        return add_mesh(st, pair + a, b ? pair + b : 0, s16(st, nb + 0x38), part, transform, why);
    }
};

}  // namespace

std::uint32_t track_line_batch(const VuMemory& vu, std::uint32_t n, std::vector<Vertex>& vertices,
                               std::vector<std::uint32_t>& indices) {
    std::uint32_t segs = 0;
    auto end = [&](std::uint32_t i, std::uint32_t other, float side, float which) {
        Vertex v{};
        for (int c = 0; c < 3; ++c) {
            v.pos[c] = vu.f(3 * i, c);
            v.normal[c] = vu.f(3 * other, c);
        }
        v.uv[0] = side;
        v.uv[1] = which;
        const std::uint32_t* cq = vu.qw(3 * i + 2);   // V4-8 unsigned: the GS bytes, 0x80 = 1.0 (RGBAQ as sent)
        v.rgba = (cq[0] & 0xFF) | (cq[1] & 0xFF) << 8 | (cq[2] & 0xFF) << 16 | (cq[3] & 0xFF) << 24;
        vertices.push_back(v);
    };
    for (std::uint32_t k = 0; k + 1 < n; k += 2) {
        if (vu.qw(3 * (k + 1))[3] & 0x8000) continue;   // XYZF3: the second vertex does not kick the line
        const std::uint32_t b = std::uint32_t(vertices.size());
        end(k, k + 1, -1.0f, 0.0f);
        end(k, k + 1, 1.0f, 0.0f);
        end(k + 1, k, -1.0f, 1.0f);
        end(k + 1, k, 1.0f, 1.0f);
        for (std::uint32_t q : {0u, 1u, 2u, 2u, 1u, 3u}) indices.push_back(b + q);
        ++segs;
    }
    return segs;
}

TrackAnimState track_anim_prepare(const TrackAnim& a) {
    TrackAnimState s;
    s.p12 = a.p12;
    s.p13 = a.p13;
    s.period = a.f18;
    return s;
}

// CB3StaticTrack::Update 0x3329A0, the body of its loop over the update list, op for op [C asm 0x332A08..0x332CD8]:
// s8 fields wrap as the byte stores do, % is the C remainder (div / mfhi), add.s / mul.s are the EE FPU's (truncating:
// b3fp::ref; with host rounding the frame changes land a tick off at exact boundaries, e.g. race2 poll 2411).
void track_anim_update(const TrackAnim& a, TrackAnimState& s, float now) {
    const auto add = [](float x, float y) { return b3fp::flt(b3fp::ref::add(b3fp::bits(x), b3fp::bits(y))); };
    const auto mul = [](float x, float y) { return b3fp::flt(b3fp::ref::mul(b3fp::bits(x), b3fp::bits(y))); };
    const int count = int(a.frames.size());   // +0x11
    if (count >= 2) {   // cycling textures: the next frame once `now` passes the last change + the period
        if (now <= add(s.t, s.period)) return;
        s.t = now;
        const auto num = [&](int k) { return k >= 0 && k < count ? a.number[std::size_t(k)] : 0; };
        if (a.flags & 0x100) {   // back and forth: +0x13 is the step (1 / -1)
            s.cur = std::int8_t(s.cur + s.p13);
            if (s.p13 == 1 && !(s.cur < count - 1)) s.p13 = -1;
            else if (s.p13 == -1 && s.cur <= 0) { s.p13 = 1; s.p12 = 1; }
            const int next = std::int8_t((s.cur + 1) % count);
            if (s.p13 != -1) {
                const std::int8_t n = std::int8_t(num(next));
                s.period = mul(a.f14, float(n - s.p12));
                s.p12 = n;
            } else if (next == 0) {
                s.period = a.f14;
            } else {
                const std::int8_t n = std::int8_t(num(s.cur));
                s.period = mul(a.f14, float(s.p12 - n));
                s.p12 = n;
            }
        } else {
            s.cur = std::int8_t((s.cur + 1) % count);
            const int next = std::int8_t((s.cur + 1) % count);
            if (next == 0) {
                s.period = a.f14;
                s.p12 = 1;
            } else {
                const std::int8_t n = std::int8_t(num(next));
                s.period = mul(a.f14, float(n - s.p12));
                s.p12 = n;
            }
        }
    } else if (a.flags & 0x200) {   // UV scroll: t follows now x speed (+0x14), in steps of +0x18 when that is nonzero
        const float target = mul(now, a.f14);
        if (target < add(s.t, s.period)) return;
        if (s.period == 0.0f) s.t = target;
        else s.t = add(s.t, s.period);
    }
}

TrackAnimState track_anim_at(const TrackAnim& a, float now) {
    TrackAnimState s = track_anim_prepare(a);
    const int n = int(std::min(std::max(now, 0.0f), 3600.0f) * 60.0f);
    for (int j = 1; j <= n; ++j) track_anim_update(a, s, float(j) / 60.0f);
    if (now > 0) track_anim_update(a, s, now);
    return s;
}

float track_uv_scroll(float t) { return 1.0f - (t - std::floor(t)); }

bool read_track(Bytes st, Bytes sm, const TrackOptions& opt, GsMemory& gs, Model& out, std::string* why,
                std::vector<TrackMaterialInfo>* materials) {
    out = Model{};
    if (st.u32(0) != 0x27 || st.u32(4) != st.n) return fail(why, "not a version 0x27 STATIC.DAT");
    out.colour_scale = 255.0f / 64.0f;
    Reader r(st, sm, opt, gs, out);
    if (!r.load_materials(why)) return false;

    const int nunits = s16(st, 0x54);
    const std::size_t utab = st.u32(0x58);
    const int u0 = std::max(0, opt.first_unit);
    const int u1 = opt.unit_count < 0 ? nunits : std::min(nunits, u0 + opt.unit_count);
    out.stats["units"] = nunits;
    const int ninst_types = s16(st, 0x34), nprop_types = s16(st, 0x36);
    const std::size_t inst0 = st.u32(0x38), prop0 = st.u32(0x3C);

    if (sm.n) {
        for (int k = u0; k < u1; ++k) {
            const std::size_t hi = st.u32(utab + 16 * std::size_t(k)), lo = st.u32(utab + 16 * std::size_t(k) + 4);
            if (!sm.has(hi, 0x11A + 0xA9 * 17) || !sm.has(lo, 0xA0)) return fail(why, "unit block outside STREAMED.DAT");
            if (sm.u32(hi + 4) != std::uint32_t(k) || sm.u32(lo + 4) != std::uint32_t(k)) return fail(why, "unit block index mismatch");
            const std::string unit = "unit " + std::to_string(k);
            r.cur_unit = k;
            {   // Unit centre: mean of the four points at block +0x10 (one shared height) [C data; G meaning].
                std::array<float, 3> c{};
                for (int q = 0; q < 4; ++q)
                    for (int cc = 0; cc < 3; ++cc) c[cc] += sm.f32(hi + 0x10 + 16 * std::size_t(q) + 4 * std::size_t(cc)) / 4;
                out.waypoints.push_back(c);
            }
            if (opt.streamed) {
                // Render table (high block +0x60): slot -> material at +0x74, rows of {delta, 2 bytes, hi[83], lod[83]}.
                const int nall = u8(sm, hi + 0x61);
                std::size_t row = 0;
                for (int rr = 0; rr < 17; ++rr)
                    if (s8(sm, hi + 0x11A + 0xA9 * std::size_t(rr)) == 0) { row = hi + 0x11A + 0xA9 * std::size_t(rr); break; }
                if (!row) return fail(why, "unit render table has no own row");
                const std::size_t blk = opt.lod ? lo : hi;
                const int nmesh = s16(sm, blk + 0x50);
                const std::size_t meshes = std::size_t(std::int64_t(blk) + 0x50 + s32(sm, blk + 0x54));
                for (int s = 0; s < nall && s < 83; ++s) {
                    const int idx = s8(sm, row + 3 + (opt.lod ? 83 : 0) + std::size_t(s));
                    if (idx < 0) continue;
                    if (idx >= nmesh) return fail(why, "render table mesh index out of range");
                    if (!r.add_track_mesh(sm, meshes + 0x40 * std::size_t(idx), s16(sm, hi + 0x74 + 2 * std::size_t(s)), unit, why))
                        return false;
                }
            }
            if (opt.instances && inst0) {
                // LOD block +0x98: s8 count[types] and u32 offset[types] -> count x 0x40-byte matrices (all rel. +0x98).
                const std::size_t b2 = lo + 0x98;
                const std::size_t counts = b2 + sm.u32(lo + 0x98), tab = b2 + sm.u32(lo + 0x9C);
                for (int t = 0; t < ninst_types; ++t) {
                    const int c = s8(sm, counts + std::size_t(t));
                    const std::uint32_t e = sm.u32(tab + 4 * std::size_t(t));
                    if (c <= 0 || !e) continue;
                    for (int q = 0; q < c; ++q) {
                        const std::size_t at = b2 + e + 0x40 * std::size_t(q);
                        if (!sm.has(at, 0x40)) return fail(why, "instance placement outside the LOD block");
                        // Matrices live in STREAMED.DAT: read them through a STREAMED view.
                        std::array<float, 16> m{};
                        for (int rr = 0; rr < 3; ++rr)
                            for (int cc = 0; cc < 3; ++cc) m[rr * 4 + cc] = sm.f32(at + 16 * rr + 4 * cc);
                        for (int cc = 0; cc < 3; ++cc) m[12 + cc] = sm.f32(at + 0x30 + 4 * cc);
                        m[15] = 1.0f;
                        const float tr[3] = {m[12], m[13], m[14]};
                        out.grow_bounds(tr);
                        out.transforms.push_back(m);
                        out.transform_tints.push_back(instance_tint(sm, at));
                        ++out.stats["instance placements"];
                        if (!r.add_instance(inst0 + 0x50 * std::size_t(t), int(out.transforms.size() - 1), unit + " instance", why))
                            return false;
                    }
                }
            }
        }
    }

    if (opt.backdrops) {
        r.cur_unit = -1;
        for (int w = 0; w < 4; ++w) {
            if (w == 1 && !opt.extra_backdrops) continue;
            const std::size_t base = st.u32(0x24 + 4 * std::size_t(w));
            const int ngroups = s16(st, 0x1C + 2 * std::size_t(w));
            if (!base) continue;
            for (int g = 0; g < ngroups; ++g) {
                const std::size_t go = base + 8 * std::size_t(g);
                const int n = s16(st, go);
                const std::size_t objs = std::size_t(std::int64_t(go) + s32(st, go + 4));
                for (int j = 0; j < n; ++j) {
                    const std::size_t ob = objs + 0x50 * std::size_t(j);
                    const int nm = s16(st, ob + 0x40);
                    if (nm <= 0) continue;
                    const std::size_t meshes = std::size_t(std::int64_t(ob) + 0x40 + s32(st, ob + 0x44));
                    const std::int32_t mo = s32(st, ob + 0x48);  // relative to the owning backdrop entry
                    // STATIC +0x2C "water g", +0x30 "water+30 g" (RenderWater draws the +0x30 object with material type 7, live2 §20)
                    const std::string part = std::string(w == 0 ? "backdrop " : w == 1 ? "backdrop+28 " : w == 2 ? "water " : "water+30 ") + std::to_string(g);
                    for (int k = 0; k < nm; ++k) {
                        const int mi = mo ? s16(st, std::size_t(std::int64_t(go) + mo) + 2 * std::size_t(k)) : -1;
                        if (!r.add_track_mesh(st, meshes + 0x40 * std::size_t(k), mi, part, why)) return false;
                    }
                }
            }
        }
    }

    if (opt.props && prop0 && st.u32(0x4C) && st.u32(0x50)) {
        // Prop placements: STATIC +0x48 matrices; per unit +0x4C (u8 count per prop type) and +0x50 (u32 offset per
        // type -> {u16 placement index, u8, u8}[count]).
        const std::size_t mats = st.u32(0x48);
        const std::uint32_t nplace = st.u16(0x40);
        std::set<std::pair<int, std::uint32_t>> seen;
        for (int u = u0; u < u1; ++u) {
            r.cur_unit = u;
            const std::size_t pu = st.u32(st.u32(0x4C) + 4 * std::size_t(u)), pp = st.u32(st.u32(0x50) + 4 * std::size_t(u));
            if (!pu || !pp) continue;
            for (int t = 0; t < nprop_types; ++t) {
                const int c = u8(st, pu + std::size_t(t));
                const std::size_t ent = st.u32(pp + 4 * std::size_t(t));
                for (int q = 0; q < c; ++q) {
                    const std::uint32_t idx = st.u16(ent + 4 * std::size_t(q));
                    if (idx >= nplace || !seen.insert({t, idx}).second) continue;
                    ++out.stats["prop placements"];
                    const int tr = r.add_transform(mats + 0x40 * std::size_t(idx));
                    out.prop_transforms.emplace_back(tr, idx);
                    if (!r.add_instance(prop0 + 0x50 * std::size_t(t), tr, "prop", why)) return false;
                }
            }
        }
    }
    if (materials) *materials = r.mats;
    out.stats["vertices"] = out.vertices.size();
    out.stats["triangles"] = out.indices.size() / 3;
    out.stats["draws"] = out.draws.size();
    return true;
}

}  // namespace b3asset
