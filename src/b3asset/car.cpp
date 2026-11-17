#include "b3asset/car.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "b3asset/vif.h"

namespace b3asset {

namespace {
bool fail(std::string* why, const std::string& what) { if (why) *why = what; return false; }

const char* kListName[18] = {"L0", "A0", "A1", "A2", "A3", "A4", "A5", "B0", "B1", "B2",
                             "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7"};

struct Mat { float m[16]; };  // CGtMatrix3x4 as stored: rows right, up, at, pos (row-vector convention)

Mat read_mat(Bytes f, std::size_t at) {
    Mat r;
    for (int i = 0; i < 16; ++i) r.m[i] = f.f32(at + 4 * i);
    return r;
}

void xform(const Mat& M, const float in[3], float w, float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = in[0] * M.m[c] + in[1] * M.m[4 + c] + in[2] * M.m[8 + c] + w * M.m[12 + c];
}

// One VU batch of the car program -> vertices + triangles.
void emit_batch(const VuMemory& vu, std::uint32_t n, const Mat& M, Model& out, DrawRange& dr) {
    if (!n) return;
    const std::uint32_t base = std::uint32_t(out.vertices.size());
    std::vector<bool> adc(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        Vertex v{};
        const float p[3] = {vu.f(3 * i, 0), vu.f(3 * i, 1), vu.f(3 * i, 2)};
        adc[i] = vu.qw(3 * i)[3] & 0x8000;
        xform(M, p, 1.0f, v.pos);
        // Normals: V3-8 unsigned added to ROW (65536.0f), so the lane is 65536 + b / 128 and the program uses
        // raw - 65537 = (b - 128) / 128, not renormalised (docs/car-shading.md §4) [C]; kept as is for the car shader.
        float nn[3];
        for (int c = 0; c < 3; ++c) nn[c] = (float((vu.qw(3 * i + 2)[c] - 0x47800000u) & 0xFF) - 128.0f) / 128.0f;
        xform(M, nn, 0.0f, v.normal);
        // UVs and influences, interleaved per group of four vertices g..g+3 (the slots are vertex slots 3k+1):
        // vertex g+j reads UV slot g + (j odd ? 2 : 0), lanes (j < 2 ? 0,1 : 2,3); influences slots g+1 and g+3, lane j.
        const std::uint32_t g = i & ~3u, j = i & 3;
        const std::uint32_t* uvq = vu.qw(3 * (g + ((j & 1) ? 2 : 0)) + 1);
        v.uv[0] = float(uvq[(j & 2) ? 2 : 0] & 0xFFFF) / 4096.0f;
        v.uv[1] = float(uvq[(j & 2) ? 3 : 1] & 0xFFFF) / 4096.0f;
        v.infl[0] = std::uint16_t(vu.qw(3 * (g + 1) + 1)[j]);
        v.infl[1] = std::uint16_t(vu.qw(3 * (g + 3) + 1)[j]);
        v.rgba = 0xFFFFFFFFu;
        out.grow_bounds(v.pos);
        out.vertices.push_back(v);
    }
    const std::size_t before = out.indices.size();
    strip_to_triangles(base, adc, out.indices);
    dr.index_count += std::uint32_t(out.indices.size() - before);
    ++out.stats["batches"];
}

bool decode_mesh(Bytes f, std::size_t at, VifInterpreter& vif, const Mat& M, Model& out, DrawRange& dr,
                 std::string* why) {
    const std::uint32_t tag = f.u32(at);
    if ((tag & 0xFFFF0000u) != 0x60000000u) return fail(why, "car mesh is not a DMA RET packet");
    const std::size_t qwc = tag & 0xFFFF;
    if (!f.has(at, 16 + qwc * 16)) return fail(why, "car mesh outside the file");
    std::string err;
    auto kick = [&](const VuMemory& vu, std::uint32_t itop, VifInterpreter::KickKind, std::uint32_t) {
        emit_batch(vu, itop, M, out, dr);
    };
    if (!vif.run(f.p + at + 8, 8 + qwc * 16, kick, &err, 8)) return fail(why, "car mesh VIF: " + err);
    return true;
}
}  // namespace

bool read_car(Bytes f, const CarOptions& opt, GsMemory& gs, Model& out, std::string* why) {
    out = Model{};
    if (f.u32(0) != 0x17) return fail(why, "not a version 0x17 vehicle file");
    // Texture atlas: one texture per palette variant would be wasteful; the chosen variant only.
    const std::uint32_t tex_at = f.u32(0x60);
    int tex = -1;
    if (tex_at) {
        DecodedRaster r;
        std::string err;
        const int npal = f.p[tex_at + 0xA0] ? f.p[tex_at + 0xA0] : 1;
        if (!decode_raster(f, tex_at, gs, r, opt.palette, &err)) return fail(why, "car texture: " + err);
        out.textures.push_back(Texture{r.hdr.name, std::move(r.levels), r.translucent});
        tex = 0;
        out.stats["palettes"] = npal;
    }
    int lod = opt.lod;
    while (lod >= 0 && !f.u32(0x4C + 4 * lod)) --lod;
    if (lod < 0) return fail(why, "no LOD block");
    const std::size_t block = f.u32(0x4C + 4 * lod);
    out.stats["lod"] = lod;
    const int npanel = f.p[0x0C], nwheel = f.p[0x0D];
    Mat ident{};
    ident.m[0] = ident.m[5] = ident.m[10] = ident.m[15] = 1.0f;

    VifInterpreter vif;
    for (int li = 0; li < 18; ++li) {
        const std::uint32_t rel = f.u32(block + 4 * li);
        if (!rel) continue;
        const std::size_t lst = block + rel;
        const int count = std::int8_t(f.p[lst]);
        const int first_glass = std::int8_t(f.p[lst + 1]);
        const std::size_t entries = lst + f.u32(lst + 4);
        out.stats[std::string("list ") + kListName[li]] = count;
        std::vector<Mat> frames;
        if (li >= 1 && li <= 6) {
            if (!opt.panels) continue;
            frames.push_back(li - 1 < npanel && opt.bake_frames ? read_mat(f, 0xD00 + 64 * (li - 1)) : ident);
        } else if (li == 7 || (!opt.bake_frames && (li == 8 || li == 9))) {
            // B0 / B1 / B2: the wheel at rest / blurred above 25 / 50 (RenderVehicle's blur state by |wheel speed|) [C]
            if (!opt.wheels) continue;
            if (!opt.bake_frames) frames.push_back(ident);
            else for (int w = 0; w < nwheel; ++w) frames.push_back(read_mat(f, 0xB80 + 64 * w));
        } else if (li == 0) {
            frames.push_back(ident);
        } else {
            continue;  // C lists: counted, not drawn (payload objects [L]); B1/B2 only with bake_frames off
        }
        for (std::size_t fi = 0; fi < frames.size(); ++fi)
            for (int i = 0; i < count; ++i) {
                const std::size_t e = entries + 16 * std::size_t(i);
                if (!f.has(e, 16)) return fail(why, "car list entry outside the file");
                DrawRange dr;
                dr.first_index = std::uint32_t(out.indices.size());
                dr.texture = tex;
                dr.material = f.u32(e + 8);
                dr.colour = f.u32(e + 12);
                dr.part = std::string(kListName[li]) + (frames.size() > 1 ? "#" + std::to_string(fi) : "");
                dr.entry = std::int16_t(i);
                dr.glass_entry = std::int16_t(first_glass);
                const bool glass = (dr.material & 0x300) || (first_glass >= 0 && i >= first_glass);
                dr.blend = glass ? DrawRange::kBlend : DrawRange::kOpaque;
                // Colour word: the same 0x80-based byte twice (0x8080 opaque; glass 0x3434) [C data; L meaning].
                const std::uint32_t a = std::min<std::uint32_t>(255, (dr.colour & 0xFF) * 2);
                dr.tint = 0x00FFFFFFu | (glass ? a : 0xFFu) << 24;
                // The first mesh's batch has no MSCNT of its own (the renderer kicks it after the DMA call [L]);
                // the continuation (same VIF state: STROW, cycle) ends each batch with MSCNT.
                const auto kick = [&](const VuMemory& vu, std::uint32_t itop, VifInterpreter::KickKind, std::uint32_t) {
                    emit_batch(vu, itop, frames[fi], out, dr);
                };
                vif.reset();
                if (!decode_mesh(f, e + f.u32(e), vif, frames[fi], out, dr, why)) return false;
                vif.flush(kick);
                if (f.u32(e + 4) && !decode_mesh(f, e + f.u32(e + 4), vif, frames[fi], out, dr, why)) return false;
                vif.flush(kick);
                for (auto& kv : vif.stats.unpacks) out.stats["unpack " + kv.first] += kv.second;
                if (dr.index_count) out.draws.push_back(dr);
            }
    }
    out.stats["vertices"] = out.vertices.size();
    out.stats["triangles"] = out.indices.size() / 3;
    return true;
}

}  // namespace b3asset
