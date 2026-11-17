#include "b3asset/export.h"

#include <cstdio>

#include "b3asset/discfs.h"
#include "b3asset/png.h"

namespace b3asset {

// a host file or a file in a disc image (b3asset/discfs.h): every disc reader of the renderer comes through here
bool read_file(const std::string& path, std::vector<std::uint8_t>& out) { return disc::read_file(path, out); }

bool write_obj(const Model& m, const std::string& dir, const std::string& stem) {
    for (std::size_t t = 0; t < m.textures.size(); ++t) {
        const Image& im = m.textures[t].levels.at(0);
        if (!write_png_rgba(dir + "/" + stem + "_tex" + std::to_string(t) + ".png", im.w, im.h, im.rgba.data())) return false;
    }
    FILE* mtl = std::fopen((dir + "/" + stem + ".mtl").c_str(), "w");
    if (!mtl) return false;
    std::fprintf(mtl, "newmtl untextured\nKd 1 1 1\n");
    for (std::size_t t = 0; t < m.textures.size(); ++t)
        std::fprintf(mtl, "newmtl tex%zu\nKd 1 1 1\nmap_Kd %s_tex%zu.png\n", t, stem.c_str(), t);
    std::fclose(mtl);
    FILE* o = std::fopen((dir + "/" + stem + ".obj").c_str(), "w");
    if (!o) return false;
    std::fprintf(o, "mtllib %s.mtl\n", stem.c_str());
    for (const Vertex& v : m.vertices) {
        std::fprintf(o, "v %.5f %.5f %.5f %.3f %.3f %.3f\n", v.pos[0], v.pos[1], v.pos[2], (v.rgba & 0xFF) / 255.0,
                     ((v.rgba >> 8) & 0xFF) / 255.0, ((v.rgba >> 16) & 0xFF) / 255.0);
    }
    for (const Vertex& v : m.vertices) std::fprintf(o, "vt %.5f %.5f\n", v.uv[0], 1.0f - v.uv[1]);
    for (const Vertex& v : m.vertices) std::fprintf(o, "vn %.4f %.4f %.4f\n", v.normal[0], v.normal[1], v.normal[2]);
    for (const DrawRange& d : m.draws) {
        std::fprintf(o, "g %s\nusemtl %s\n", d.part.c_str(), d.texture < 0 ? "untextured" : ("tex" + std::to_string(d.texture)).c_str());
        for (std::uint32_t i = 0; i < d.index_count; i += 3) {
            const std::uint32_t* t = &m.indices[d.first_index + i];
            std::fprintf(o, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", t[0] + 1, t[0] + 1, t[0] + 1, t[1] + 1, t[1] + 1, t[1] + 1,
                         t[2] + 1, t[2] + 1, t[2] + 1);
        }
    }
    return std::fclose(o) == 0;
}

}  // namespace b3asset
