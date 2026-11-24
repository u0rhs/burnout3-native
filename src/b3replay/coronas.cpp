#include "coronas.h"
#include "taillights.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "b3host/ps2float.h"
#include "b3host/vu_random.h"

namespace b3replay {

namespace {
namespace fr = b3fp::ref;
using u32 = std::uint32_t;
constexpr u32 kZero = 0, kOne = 0x3F800000u;

u32 B(float f) { return b3fp::bits(f); }
float F(u32 b) { return b3fp::flt(b); }
float add(float a, float b) { return F(fr::add(B(a), B(b))); }
float sub(float a, float b) { return F(fr::sub(B(a), B(b))); }
float mul(float a, float b) { return F(fr::mul(B(a), B(b))); }
float madd(float acc, float a, float b) { return F(fr::madd(B(acc), B(a), B(b))); }

struct V3 { float x = 0, y = 0, z = 0; };
V3 v3(const float p[3]) { return {p[0], p[1], p[2]}; }
V3 scale(const V3& a, float s) { return {mul(a.x, s), mul(a.y, s), mul(a.z, s)}; }
V3 vadd(const V3& a, const V3& b) { return {add(a.x, b.x), add(a.y, b.y), add(a.z, b.z)}; }
V3 vsub(const V3& a, const V3& b) { return {sub(a.x, b.x), sub(a.y, b.y), sub(a.z, b.z)}; }
// GtMathPs2 TransformPoint: ACC = R0 x; ACC += R1 y; ACC += R2 z; out = ACC + R3 (vmulax, vmadday, vmaddaz, vmaddw) [C
// asm 0x2570FC..0x257108]
V3 xform_point(const Mat34& m, const V3& p) {
    float o[3];
    for (int l = 0; l < 3; ++l) o[l] = madd(madd(madd(mul(m.r[0][l], p.x), m.r[1][l], p.y), m.r[2][l], p.z), m.r[3][l], 1.0f);
    return {o[0], o[1], o[2]};
}
V3 xform_vector(const Mat34& m, const V3& p) {
    float o[3];
    for (int l = 0; l < 3; ++l) o[l] = madd(madd(mul(m.r[0][l], p.x), m.r[1][l], p.y), m.r[2][l], p.z);
    return {o[0], o[1], o[2]};
}
// DotProduct: vmul, then x + y, + 1 x z (vmul.xyz, vadday.x, vmaddz.x) [C asm 0x25712C..0x257134]
float dot(const V3& a, const V3& b) {
    const float x = mul(a.x, b.x), y = mul(a.y, b.y), z = mul(a.z, b.z);
    return madd(add(x, y), 1.0f, z);
}

struct Builder {
    const CoronaInputs& in;
    const CoronaConsts& k;
    CoronaArrays& out;
    float fade = 1.0f;   // traffic-popin: the vehicle's fade while its lights are added (1: the game's colours as they are)
    const std::int64_t* flame = nullptr;   // paint2: per player its boost glow's colour (0xRRGGBB; < 0: the game's)
    void push(int a, const V3& p, float w, const V3& c, float size, CoronaSource src) {
        if (int(out.slot[a].size()) >= k.capacity[a]) { ++out.dropped[a]; return; }
        CoronaSlot s;
        s.pos[0] = p.x; s.pos[1] = p.y; s.pos[2] = p.z; s.pos[3] = w;
        s.col[0] = c.x; s.col[1] = c.y; s.col[2] = c.z; s.col[3] = size;
        out.slot[a].push_back(s);
        out.src[a].push_back(src);
    }
    // the slot write of AddTrafficCorona 0x256FD0 / RenderLightCoronas' inline: colour x dot x 64, w 400
    void light(const V3& p, const V3& colour, float d, float size, CoronaSource src) {
        const V3 c = scale(scale(colour, d), 64.0f);
        push(0, p, 400.0f, fade < 1.0f ? scale(c, fade) : c, size, src);
    }
    // CB3EffectsManager::RenderLightCoronas 0x256690 [L draft, groups / bits / colours / sizes C asm]
    void light_coronas(const Vehicle& v, const float cam_point[3], bool rear_red, CoronaSource src) {
        const CoronaTables* t = v.corona_tables.get();
        if (!t) return;
        const Mat34 inv = inverse_affine(v.main);
        V3 dir = xform_point(inv, v3(cam_point));
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len > 0) dir = {dir.x / len, dir.y / len, dir.z / len};
        const float size = add(1.0f, mul(0.02f, len));
        auto group = [&](int g, const float* colour, float sz) {
            for (const auto& l : t->group[g]) {
                const float d = dot(v3(l.dir), dir);
                if (d > 0.0f) light(xform_point(v.main, v3(l.pos)), v3(colour), d, sz, src);
            }
        };
        // func_00257080: the lights at object z < 0 take the second colour (red) [C asm 0x257114..0x25711C]
        auto group_red = [&](int g, const float* front, const float* rear, float sz) {
            for (const auto& l : t->group[g]) {
                const float d = dot(v3(l.dir), dir);
                if (d > 0.0f) light(xform_point(v.main, v3(l.pos)), v3(l.pos[2] < 0.0f ? rear : front), d, sz, src);
            }
        };
        const std::uint8_t b = v.corona.lights;
        if (b & 0x02) group(0, k.light[0], mul(1.25f, size));
        else if (b & 0x04) group(0, k.light[1], size);
        if (v.corona.tail >= 0.0f) {
            // taillights (night, rain): the brake lights' colour and size at the tail level, rising to full with the brake
            // level; where the tail lamps are the brake lamps (or a car has only one of the groups) one glow carries both
            const bool one = t->group[1].empty() || t->group[2].empty() ||
                             same_lamps<CoronaTables::Light>(t->group[1], t->group[2]);
            const RearCoronas r = rear_coronas(one, v.corona.tail, v.corona.brake);
            auto rear = [&](int g, float level) {
                if (!(level > 0.0f)) return;
                const float c[3] = {mul(k.light[2][0], level), mul(k.light[2][1], level), mul(k.light[2][2], level)};
                group(g, c, mul(0.75f, size));
            };
            if (one) rear(t->group[2].empty() ? 1 : 2, r.brake);
            else { rear(1, r.tail); rear(2, r.brake); }
        } else if (b & 0x10) group(2, k.light[2], mul(0.75f, size));
        else if (b & 0x08) group(1, k.light[3], mul(0.5f, size));
        for (int g : {5, 6}) {
            if (!(b & (g == 5 ? 0x20 : 0x40))) continue;
            if (rear_red) group_red(g, k.light[4], k.light[3], mul(0.5f, size));
            else group(g, k.light[4], mul(0.5f, size));
        }
        if (b & 0x80) group(4, k.light[5], mul(0.5f, size));
    }
    // func_00257230 [C asm; L draft fix2]: the boost glow
    void boost(const Vehicle& v, u32 scales[4], const u32 rnd[3]) {
        const CoronaTables* t = v.corona_tables.get();
        // func_0030AAE0 (matched): intensity t = CB3BoostEffectParams +0x14
        const float bt = v.corona.boost;
        if (!(bt > 0.0f)) return;
        const int red = v.corona.red_boost ? 1 : 0;
        const int a = red ? 2 : 1;
        V3 colour = v3(k.boost[red]);
        if (flame && (v.player == 0 || v.player == 1) && flame[v.player] >= 0) {   // paint2: the player's flame colour, as bright
            const std::int64_t f = flame[v.player];
            const float mx = std::max(colour.x, std::max(colour.y, colour.z));
            colour = V3{mx * float(f >> 16 & 0xFF) / 255.0f, mx * float(f >> 8 & 0xFF) / 255.0f, mx * float(f & 0xFF) / 255.0f};
        }
        float size = k.boost_size[red];
        if (bt >= 1.0f) size = mul(size, bt);
        else colour = scale(colour, bt);
        if (!(size > k.boost_min) || !t) return;
        out.boost_rotations += 3 * int(t->exhaust.size());
        // the three randomisers: A = maximum, B = maximum - minimum [asm 0x2573A8..0x257440]
        const V3 A0 = colour, B0 = vsub(colour, scale(colour, 0.5f));
        const V3 A1 = scale(colour, k.boost_k[0]), B1 = vsub(A1, scale(colour, k.boost_k[1]));
        const V3 A2 = scale(colour, k.boost_k[1]), B2 = vsub(A2, scale(colour, k.boost_k[2]));
        const V3 As[3] = {A0, A1, A2}, Bs[3] = {B0, B1, B2};
        const float off[3] = {k.boost_k[3], k.boost_k[2], k.boost_k[4]};   // D_004E0F50, F4C, F54
        const float fade[3] = {800.0f, 400.0f, 200.0f};
        const float sz[3] = {size, mul(0.5f, size), mul(0.25f, size)};
        for (const auto& e : t->exhaust) {
            const float w = sub(1.0f, add(e.u, e.v));
            Mat34 M;
            static const float I[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
            const Mat34& za = v.corona.zone[e.a & 7];
            const Mat34& zb = v.corona.zone[e.b & 7];
            for (int r = 0; r < 4; ++r)
                for (int l = 0; l < 3; ++l) M.r[r][l] = madd(madd(mul(za.r[r][l], e.u), zb.r[r][l], e.v), I[r][l], w);
            const V3 pos = xform_point(v.main, xform_point(M, v3(e.pos)));
            const V3 dir = xform_vector(v.main, xform_vector(M, v3(e.dir)));
            for (int j = 0; j < 3; ++j) {
                // CV3dRandomiser::RandomInterpolate with R drawn before the loop: t = state.x x r; d = floor(t) - t by
                // the 2^23 trick (truncating VU adds); the state rotates one lane (vmr32)
                const u32 tt = fr::mul(scales[0], rnd[j]);
                const u32 k23 = 0x4B000000u;
                const u32 acc = fr::msub(fr::add(k23, tt), k23, kOne);
                const float d = F(fr::msub(acc, tt, kOne));
                const u32 s0 = scales[0];
                scales[0] = scales[1]; scales[1] = scales[2]; scales[2] = scales[3]; scales[3] = s0;
                const V3 c = {madd(add(As[j].x, 0.0f), Bs[j].x, d), madd(add(As[j].y, 0.0f), Bs[j].y, d), madd(add(As[j].z, 0.0f), Bs[j].z, d)};
                push(a, vadd(pos, scale(dir, off[j])), fade[j], scale(c, 64.0f), sz[j], CoronaSource::kBoost);
            }
        }
    }
};

// CGtRandom::RandomReal on {~n, n} (func_00257230 0x2572D0..0x257380 [C asm]), then VRINIT / VRNEXT (b3host/vu_random.h)
void boost_random(u32 n, u32 out[3]) {
    const u32 hi = ~n;
    const u32 t2 = (hi << 16) + u32(std::int32_t(hi) >> 16);
    const u32 t3 = t2 + n;
    u32 f;
    if (std::int32_t(t3) >= 0) f = fr::cvt_s_w(std::int32_t(t3));
    else { const u32 h = fr::cvt_s_w(std::int32_t((t3 >> 1) | (t3 & 1))); f = fr::add(h, h); }
    f = fr::mul(f, 0x2F800000u);   // 2^-32
    u32 r = b3vurand::r_init(f);
    for (int j = 0; j < 3; ++j) {
        r = b3vurand::r_next(r);
        out[j] = r;
    }
}

}  // namespace

void build_coronas(const CoronaInputs& in, const std::vector<Vehicle>& vehicles, const float cam_point[3], const float cam_pos[3],
                   CoronaArrays& out, const std::vector<float>* fades, const std::int64_t* flame) {
    out = CoronaArrays{};
    if (!in.valid || !in.k) return;
    Builder b{in, *in.k, out};
    b.flame = flame;   // paint2
    // CB3TrafficSystem::Draw: traffic, crashed vehicles (reverse), traffic lights, unassigned coronas
    if (in.traffic_on) {
        auto fade_of = [&](std::size_t n) { return fades && n < fades->size() ? (*fades)[n] : 1.0f; };
        for (std::size_t n = 0; n < vehicles.size(); ++n) {
            const Vehicle& v = vehicles[n];
            if (v.kind == VehicleKind::kTraffic && v.corona.submit && v.corona.lights != 0) {
                b.fade = fade_of(n);
                for (int r = 0; r < std::max<int>(1, v.listings); ++r)   // once per entry in the traffic list
                    b.light_coronas(v, cam_point, false, CoronaSource::kTrafficLight);
            }
        }
        for (std::size_t n = vehicles.size(); n-- > 0;)
            if (vehicles[n].kind == VehicleKind::kCrashed && vehicles[n].corona.submit && vehicles[n].corona.lights != 0) {
                b.fade = fade_of(n);
                b.light_coronas(vehicles[n], cam_point, false, CoronaSource::kCrashedLight);
            }
        b.fade = 1.0f;
        if (in.tl_coronas) {
            const auto& co = *in.tl_coronas;
            const float* tlc = in.k->tl[0];
            const V3 at{0, 1, 0};   // lTLAt
            auto cross = [](const V3& a, const V3& c) { return V3{sub(mul(a.y, c.z), mul(a.z, c.y)), sub(mul(a.z, c.x), mul(a.x, c.z)), sub(mul(a.x, c.y), mul(a.y, c.x))}; };
            auto signal = [&](int idx, V3 offset, const V3& colour, CoronaSource src) {
                const auto& c = co[std::size_t(idx)];
                const V3 dir = scale(v3(c.dir), -1.0f);
                const V3 pos = vadd(v3(c.pos), offset);
                V3 to = vsub(pos, v3(cam_pos));
                const float dist = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
                if (dist > 0) to = {to.x / dist, to.y / dist, to.z / dist};
                const float d = dot(dir, to);
                if (d > 0.0f) b.light(pos, colour, d, add(1.5f, mul(0.004f, dist)), src);
            };
            for (const auto& L : in.tlights) {
                // states 0 / 2 / other: colours D_004C49A0 / D_004C4980 / D_004C4990, offsets D_004C49C0 / D_004C49B0 / 0
                const int s = L.state == 0 ? 0 : L.state == 2 ? 2 : 1;
                const V3 colour = v3(s == 0 ? tlc + 6 : s == 2 ? tlc : tlc + 3);
                V3 offset = s == 0 ? v3(in.k->tl[4]) : s == 2 ? v3(in.k->tl[3]) : V3{};
                for (int q = 0; q < L.count && q < 4; ++q) {
                    const int idx = L.idx[q];
                    if (idx >= int(co.size())) continue;
                    if (!in.tl_flat) {
                        const V3 cr = cross(at, scale(v3(co[std::size_t(idx)].dir), -1.0f));
                        if (s == 2) offset = scale(cr, -0.5f);
                        else if (s == 0) offset = scale(cr, 0.5f);
                    }
                    signal(idx, offset, colour, CoronaSource::kSignal);
                }
            }
            V3 uoff = v3(in.k->tl[4]);
            for (std::size_t i = 0; i < co.size(); ++i) {
                const std::size_t w = i / 31, bit = i % 31;
                const u32 word = w < in.assigned.size() ? in.assigned[w] : ~0u;
                if (word >> bit & 1u) continue;
                if (!in.tl_flat) uoff = scale(cross(at, scale(v3(co[i].dir), -1.0f)), 0.5f);
                signal(int(i), uoff, v3(tlc + 6), CoronaSource::kUnassigned);
            }
        }
    }
    // RenderRaceCars: drawn race cars far to near (func_00199D90: descending key)
    std::vector<const Vehicle*> cars;
    for (const auto& v : vehicles)
        if (v.kind == VehicleKind::kRaceCar && v.corona.valid && v.corona.submit) cars.push_back(&v);
    std::stable_sort(cars.begin(), cars.end(), [](const Vehicle* x, const Vehicle* y) { return x->corona.sort > y->corona.sort; });
    // the boost glow's randomiser: every car's call reseeds R from the same seed; the state's lane rotation continues
    // from car to car. The capture holds it after the frame: rotate back by the frame's rotations.
    int rot = 0;
    for (const Vehicle* v : cars) {
        if (!v->corona_tables) continue;
        const float bt = v->corona.boost;
        if (!(bt > 0.0f)) continue;
        const int red = v->corona.red_boost ? 1 : 0;
        const float size = bt >= 1.0f ? mul(in.k->boost_size[red], bt) : in.k->boost_size[red];
        if (size > in.k->boost_min) rot += 3 * int(v->corona_tables->exhaust.size());
    }
    u32 sc[4];
    for (int l = 0; l < 4; ++l) sc[l] = in.scales[((l - rot) % 4 + 4) % 4];
    u32 rnd[3];
    boost_random(in.seed, rnd);
    for (const Vehicle* v : cars) {
        if (v->corona.lights != 0) b.light_coronas(*v, cam_point, v->corona.rear_red, CoronaSource::kRaceLight);
        b.boost(*v, sc, rnd);
    }
}

bool corona_quad(const Camera& cam, const CoronaSlot& s, CoronaQuad& q, float aspect) {
    // view space: the camera matrix rows right (screen left), up, at, pos [C capture §12.1]
    const float* P = cam.m.r[3];
    const float d[3] = {s.pos[0] - P[0], s.pos[1] - P[1], s.pos[2] - P[2]};
    auto dotr = [&](int r) { return d[0] * cam.m.r[r][0] + d[1] * cam.m.r[r][1] + d[2] * cam.m.r[r][2]; };
    const float w = dotr(2);
    if (!(cam.half_w > 0) || !(cam.half_h > 0)) return false;
    const float hw = std::max(cam.half_w, aspect > 0 ? cam.half_h * aspect : 0.0f);
    const float xs = -dotr(0) / w / hw, ys = dotr(1) / w / cam.half_h;   // -1..1 across the window
    // the program's window: the screen plus 32 pixels each side of 640 x 448 (704 / 640, 512 / 448); near / far
    if (!(w >= cam.znear && w <= cam.zfar) || std::fabs(xs) > 1.1f || std::fabs(ys) > 512.0f / 448.0f) return false;
    const float K = 1.0f / cam.half_w;   // camera record +0x208 [C equal in the captures, §16]
    const float zf = K * s.pos[3];
    if (!(zf > w)) return false;
    const float f = 1.0f - w / zf;
    std::int32_t c[3];
    bool lit = false;
    for (int l = 0; l < 3; ++l) {
        const float v = s.col[l] * f;
        lit = lit || v > 8.0f;
        c[l] = std::int32_t(v);
        q.rgb[l] = std::uint8_t(c[l] & 0xFF);
    }
    if (!lit) return false;
    // a square `size` across, pulled half its size towards the eye for the Z test: the same screen rectangle at depth
    // w - size / 2 (kept in front of the near plane)
    const float h = s.col[3] * 0.5f;
    const float pulled = std::max(w - h, cam.znear * 1.01f), k = pulled / w;
    q.depth = w;
    float ctr[3], R[3], U[3];
    for (int l = 0; l < 3; ++l) {
        ctr[l] = P[l] + d[l] * k;
        const float rl = std::sqrt(cam.m.r[0][0] * cam.m.r[0][0] + cam.m.r[0][1] * cam.m.r[0][1] + cam.m.r[0][2] * cam.m.r[0][2]);
        const float ul = std::sqrt(cam.m.r[1][0] * cam.m.r[1][0] + cam.m.r[1][1] * cam.m.r[1][1] + cam.m.r[1][2] * cam.m.r[1][2]);
        R[l] = -cam.m.r[0][l] / rl * h * k;   // screen right
        U[l] = cam.m.r[1][l] / ul * h * k;    // screen up
    }
    for (int l = 0; l < 3; ++l) {
        q.corner[0][l] = ctr[l] - R[l] + U[l];
        q.corner[1][l] = ctr[l] + R[l] + U[l];
        q.corner[2][l] = ctr[l] + R[l] - U[l];
        q.corner[3][l] = ctr[l] - R[l] - U[l];
    }
    return true;
}

void Coronas::draw(b3gfx::Device& dev, const Camera& cam, const CoronaArrays& arrays, const b3gfx::TextureId tex[3],
                   std::vector<b3gfx::DrawItem>& out, CoronaStats* stats, float aspect) {
    if (dev.resource_epoch() != epoch_) { clear(); epoch_ = dev.resource_epoch(); }   // ids from before a clear_resources
    // CB3EffectsManager::RenderPostFogEffects 0x261E00: arrays 0, 1, 2 (RenderDynamicArray 0x3073A0), one batch each
    for (int a = 0; a < 3; ++a) {
        verts_.clear();
        idx_.clear();
        for (const auto& s : arrays.slot[a]) {
            CoronaQuad q;
            if (!corona_quad(cam, s, q, aspect)) continue;
            static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            const std::uint32_t base = std::uint32_t(verts_.size());
            for (int c = 0; c < 4; ++c) {
                b3gfx::Vertex o;
                std::memset(&o, 0, sizeof o);
                std::memcpy(o.pos, q.corner[c], sizeof o.pos);
                o.normal[1] = 1;
                o.uv[0] = uv[c][0];
                o.uv[1] = uv[c][1];
                o.rgba = std::uint32_t(q.rgb[0]) | std::uint32_t(q.rgb[1]) << 8 | std::uint32_t(q.rgb[2]) << 16 | 0x80000000u;
                verts_.push_back(o);
            }
            for (std::uint32_t t : {0u, 1u, 2u, 0u, 2u, 3u}) idx_.push_back(base + t);
        }
        if (stats) { stats->slots[a] = int(arrays.slot[a].size()); stats->sprites[a] = int(verts_.size() / 4); }
        emit(dev, a, tex[a], out);
    }
}

void Coronas::draw_quads(b3gfx::Device& dev, const std::vector<CoronaQuad>& quads, b3gfx::TextureId tex, std::vector<b3gfx::DrawItem>& out) {
    if (dev.resource_epoch() != epoch_) { clear(); epoch_ = dev.resource_epoch(); }
    verts_.clear();
    idx_.clear();
    static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (const CoronaQuad& q : quads) {
        const std::uint32_t base = std::uint32_t(verts_.size());
        for (int c = 0; c < 4; ++c) {
            b3gfx::Vertex o;
            std::memset(&o, 0, sizeof o);
            std::memcpy(o.pos, q.corner[c], sizeof o.pos);
            o.normal[1] = 1;
            o.uv[0] = uv[c][0];
            o.uv[1] = uv[c][1];
            o.rgba = std::uint32_t(q.rgb[0]) | std::uint32_t(q.rgb[1]) << 8 | std::uint32_t(q.rgb[2]) << 16 | 0x80000000u;
            verts_.push_back(o);
        }
        for (std::uint32_t t : {0u, 1u, 2u, 0u, 2u, 3u}) idx_.push_back(base + t);
    }
    emit(dev, 3, tex, out);
}

void Coronas::emit(b3gfx::Device& dev, int a, b3gfx::TextureId tex, std::vector<b3gfx::DrawItem>& out) {
    {
        if (idx_.empty() || tex == b3gfx::kNone) return;
        dev.update_vertex_buffer(vb_[a], verts_.data(), verts_.size());
        dev.update_index_buffer(ib_[a], idx_.data(), idx_.size());
        b3gfx::DrawItem d;
        d.vertices = vb_[a];
        d.indices = ib_[a];
        d.index_count = std::uint32_t(idx_.size());
        d.texture = tex;
        d.blend = b3gfx::Blend::kAlpha;
        d.shading = b3gfx::Shading::kTrack;   // MODULATE on untransformed GS colours, as the other effects
        d.track_type = 0;
        d.gs_test = 0x53001;                  // ATE, ATST NEVER, AFAIL RGB_ONLY, ZTE, ZTST GEQUAL: RGB written, Z not
        d.lighting = 0;
        d.effect = true;
        d.post_fog = true;                    // after CB3Fog::Render and CB3GameData::Draw, unfogged
        d.gs_alpha = 0x68;                    // (Cs - 0) FIX + Cd
        d.blend_fix = 1.0f;                   // FIX 0x80
        d.clamp_uv = true;
        d.night_glow = true;                  // night mode: lights stay bright
        out.push_back(d);
    }
}

}  // namespace b3replay
