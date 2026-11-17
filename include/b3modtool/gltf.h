// gltf.h -- JSON and glTF 2.0 for the mod kit's model export (part 3c, docs/modding/DESIGN.md §3; the commands
// `car export` and `track export`, docs/modding/b3modtool.md). Standard library only.
//
//   Json         a JSON value: built in code, written as text, and read back (the import of part 3c reads the same)
//   GltfBuilder  a glTF 2.0 document (.gltf JSON + one .bin buffer, images by file name) built from typed arrays
//   gltf_check   a structural check of a .gltf and its buffer: references, buffer / view / accessor bounds and
//                alignment, POSITION min / max, indices in range, no NaN or infinity (tools/modding/gltf_check.py is
//                the same check, independent of this code)
//   coordinates  the game's axes and units against glTF's (gltf_position, gltf_uv, gltf_matrix)
#ifndef B3MODTOOL_GLTF_H
#define B3MODTOOL_GLTF_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b3modtool {

struct Json {
    enum Type : std::uint8_t { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    enum NumKind : std::uint8_t { Double, Float, Int } kind = Double;   // how a number is written (Float: %.9g, exact for f32)
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;   // in insertion order

    Json() = default;
    static Json null() { return Json(); }
    static Json boolean(bool v) { Json j; j.type = Bool; j.b = v; return j; }
    static Json num(double v) { Json j; j.type = Num; j.n = v; return j; }
    static Json f32(float v) { Json j; j.type = Num; j.n = v; j.kind = Float; return j; }
    static Json integer(std::int64_t v) { Json j; j.type = Num; j.n = double(v); j.kind = Int; return j; }
    static Json str(std::string v) { Json j; j.type = Str; j.s = std::move(v); return j; }
    static Json array() { Json j; j.type = Arr; return j; }
    static Json object() { Json j; j.type = Obj; return j; }
    static Json floats(const float* p, std::size_t n);   // an array of f32 numbers

    Json& set(const std::string& key, Json v);           // replaces an existing key
    Json& push(Json v) { a.push_back(std::move(v)); return a.back(); }
    const Json* get(std::string_view key) const;
    Json* get(std::string_view key);
    std::size_t size() const { return type == Arr ? a.size() : type == Obj ? o.size() : 0; }
    std::int64_t as_int(std::int64_t def = 0) const { return type == Num ? std::int64_t(n) : def; }
    double as_num(double def = 0) const { return type == Num ? n : def; }
    const std::string& as_str() const { return s; }

    // `indent` < 0: compact; otherwise pretty with that many spaces (arrays of numbers stay on one line)
    std::string dump(int indent = -1) const;
};
bool json_parse(std::string_view text, Json& out, std::string* why = nullptr);
// A u64 as 16 hex digits (JSON numbers cannot hold one exactly)
std::string hex64(std::uint64_t v);

// glTF constants
namespace gl {
constexpr int kByte = 5120, kUByte = 5121, kShort = 5122, kUShort = 5123, kUInt = 5125, kFloat = 5126;
constexpr int kArrayBuffer = 34962, kElementArrayBuffer = 34963;
constexpr int kTriangles = 4;
}  // namespace gl

class GltfBuilder {
public:
    GltfBuilder();
    Json doc;                        // the glTF JSON without "buffers" (added by text())
    std::vector<std::uint8_t> bin;   // the one buffer

    // A buffer view over `n` bytes copied to the buffer (4-byte aligned). target 0: none.
    int view(const void* p, std::size_t n, int target);
    int accessor(int view, int component, std::size_t count, const char* type, bool normalized = false,
                 const std::vector<double>* min = nullptr, const std::vector<double>* max = nullptr);
    // Typed helpers: one view + one accessor each
    int positions(const std::vector<std::array<float, 3>>& p);   // VEC3 float with min / max
    int vec3(const std::vector<std::array<float, 3>>& p);
    int vec2(const std::vector<std::array<float, 2>>& p);
    int vec4(const std::vector<std::array<float, 4>>& p);
    int ubyte4(const std::vector<std::array<std::uint8_t, 4>>& p, bool normalized);
    int byte4(const std::vector<std::array<std::int8_t, 4>>& p);
    int ushort2(const std::vector<std::array<std::uint16_t, 2>>& p);
    int uint1(const std::vector<std::uint32_t>& p);
    int float1(const std::vector<float>& p);   // SCALAR float
    // A stored field as one float SCALAR attribute per component, `name`_`suffix[c]` (e.g. _B3_NRM_X): Blender 4.2's
    // glTF importer pairs its "_" attributes with their data in a set's order, which only gives every attribute its
    // own values when all of them have one form (docs/modding/b3modtool.md "Blender"). Read back by gltf_read_field.
    template <std::size_t N>
    void scalar_attrs(Json& attributes, const std::string& name, const char* const (&suffix)[N],
                      const std::vector<std::array<float, N>>& v) {
        for (std::size_t c = 0; c < N; ++c) {
            std::vector<float> one;
            one.reserve(v.size());
            for (const auto& x : v) one.push_back(x[c]);
            attributes.set(name + "_" + suffix[c], Json::integer(float1(one)));
        }
    }
    int indices(const std::vector<std::uint32_t>& idx, std::size_t vertex_count);   // u16 when they fit, else u32

    int add(const char* array_name, Json item);   // appends to doc[array_name] (created), returns its index
    void use_extension(const std::string& name, bool required = false);

    // The .gltf text, its buffer named `bin_uri`
    std::string text(const std::string& bin_uri) const;
};

// The structural check. `bin` is the buffer the document's buffer 0 names. Errors: one line each.
struct GltfCheckStats {
    std::size_t nodes = 0, meshes = 0, primitives = 0, accessors = 0, materials = 0, textures = 0, images = 0;
    std::uint64_t vertices = 0, triangles = 0;
    double bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};   // over every POSITION accessor's min / max (object space)
};
bool gltf_check(const Json& doc, const std::vector<std::uint8_t>& bin, std::vector<std::string>& errors,
                GltfCheckStats* stats = nullptr);
// Accessor `index` of `doc` as numbers (normalized integers scaled to 0..1 / -1..1), `ncomp` per element.
bool gltf_read_accessor(const Json& doc, const std::vector<std::uint8_t>& bin, int index, std::vector<double>& out,
                        int& ncomp, std::string* why = nullptr);
// A node's local matrix (column-major): "matrix", or translation x rotation x scale
std::array<float, 16> gltf_node_matrix(const Json& node);
std::array<float, 16> gltf_mul(const std::array<float, 16>& a, const std::array<float, 16>& b);   // a x b

// ---- coordinates ----
// The game's world and model space is right-handed with Y up, in metres (docs/track-format.md §4; the renderer draws it
// unmirrored with a right-handed look-at, docs/native-renderer.md), which is glTF's own convention: a position is
// written as it is. Texture coordinates: the game's (0, 0) is the texture's first stored texel (top left), glTF's too:
// written as they are. OBJ counts v from the bottom: v' = 1 - v.
inline std::array<float, 3> gltf_position(const float p[3]) { return {p[0], p[1], p[2]}; }
inline std::array<float, 2> gltf_uv(float u, float v) { return {u, v}; }
inline std::array<float, 2> obj_uv(float u, float v) { return {u, 1.0f - v}; }
// A CGtMatrix3x4 / RwMatrix as stored (rows right, up, at, position; the row-vector convention world = x right +
// y up + z at + position; the w lanes are not part of the transform) -> a glTF node matrix (column-major,
// world = M [x y z 1]): the columns are those rows, the last row 0 0 0 1.
std::array<float, 16> gltf_matrix(const float stored[16]);
// Applies a glTF matrix to a point (w = 1) or a direction (w = 0)
std::array<float, 3> gltf_apply(const std::array<float, 16>& m, const float p[3], float w);

}  // namespace b3modtool

#endif
