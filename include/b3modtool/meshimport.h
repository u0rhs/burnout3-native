// meshimport.h -- what the car and track imports share (mod kit part 3c, docs/modding/DESIGN.md §3; the commands
// `car import` and `track import`, docs/modding/b3modtool.md):
//
//   VifStream      the packet writer's buffer: words, NOP alignment of an UNPACK's data to a quadword, padding
//   build_strips   triangles -> strips -> VU batches (the strip builder both imports use for new triangles)
//   M4             a glTF node matrix in double precision, and the few operations the imports need
//   gltf_load_doc  a .gltf (and its buffer) or a .glb read into its JSON and its one buffer
//   gltf_world     every scene node's world matrix
//   gltf_read_field  a stored field (_B3_*) as the export writes it (one float SCALAR per component) or as earlier
//                  exports did (one VEC2 / VEC4 accessor)
#ifndef B3MODTOOL_MESHIMPORT_H
#define B3MODTOOL_MESHIMPORT_H

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "b3modtool/export3d.h"

namespace b3modtool {

// A VIF stream as the disc's packets have it: a DMA RET tag (filled in by finish), then the codes
struct VifStream {
    Buf b;
    void put(std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
    // NOPs (one to four, as the disc's files have them) so that the command after `then` more words lands on 12 mod 16:
    // the data of an UNPACK / STROW starts on a quadword
    void align_cmd(int then) {
        int k = 1;
        while ((b.size() + 4 * std::size_t(k) + 4 * std::size_t(then)) % 16 != 12) ++k;
        for (int i = 0; i < k; ++i) put(0);
    }
    void pad_qw() { while (b.size() % 16) put(0); }
    void pad4() { while (b.size() % 4) b.push_back(0); }
    // the packet's quadword padding and its DMA tag (RET, QWC = the quadwords after the tag)
    void finish() {
        pad_qw();
        const std::uint32_t qwc = std::uint32_t(b.size() / 16 - 1);
        for (int i = 0; i < 4; ++i) b[std::size_t(i)] = std::uint8_t((0x60000000u | qwc) >> (8 * i));
        for (int i = 4; i < 8; ++i) b[std::size_t(i)] = 0;
    }
};

// Triangles over vertex ids (their winding as the decode gives it: b3asset::strip_to_triangles) -> a vertex sequence
// (id, ADC flag) cut into VU batches of at most `max_batch` vertices: greedy strips from the triangles with the fewest
// neighbours, each grown both ways while a free triangle continues it with its winding, a strip kept on the batch
// place whose parity gives its triangles their winding (an extra ADC vertex when needed), a strip that does not fit
// carried on in the next batch from its last two vertices. `seq` empty: no triangles.
void build_strips(const std::vector<std::array<std::uint32_t, 3>>& tri, int max_batch,
                  std::vector<std::pair<std::uint32_t, bool>>& seq, std::vector<std::uint32_t>& batches);

using M4 = std::array<double, 16>;   // column-major, as glTF
M4 m4(const std::array<float, 16>& f);
M4 m4_mul(const M4& a, const M4& b);
bool m4_inverse(const M4& m, M4& r);   // an affine matrix's inverse (the last row 0 0 0 1)
bool m4_near_identity(const M4& m);
constexpr M4 kM4Identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

std::string path_dir_of(const std::string& p);   // "a/b/c.gltf" -> "a/b/" ("" without a folder)
// A .gltf (its one buffer: the .bin beside it, by its uri, or a data: URI) or a .glb
bool gltf_load_doc(const FileLoader& load, const std::string& model, Json& doc, Buf& bin, std::string* why);
// The world matrix of every node the scene reaches (from the default scene's roots, or every node that is no node's
// child); `seen[i]` 0 for a node the scene does not reach. False on a bad hierarchy (a node reached twice).
bool gltf_world(const Json& doc, std::vector<M4>& world, std::vector<int>& seen, std::string* why);
// A primitive's stored field `name` (e.g. "_B3_NRM"): the SCALARs `name`_`suffix[c]` the export writes
// (GltfBuilder::scalar_attrs), or else the one accessor `name` exports before them wrote (VEC2 / VEC4). `out` the
// values interleaved, `ncomp` per vertex (suffix.size(), or the old accessor's); a normalized accessor's values x
// `norm_scale`. Empty (ncomp 0) when neither form is complete and readable: the import then takes what it carries from
// elsewhere and says so.
void gltf_read_field(const Json& doc, const Buf& bin, const Json& attributes, const std::string& name,
                     const std::vector<std::string>& suffix, double norm_scale, std::vector<double>& out, int& ncomp);

}  // namespace b3modtool

#endif
