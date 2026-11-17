// export3d.h -- car and track export to glTF 2.0 / OBJ (mod kit part 3c, docs/modding/DESIGN.md §3; user guide
// docs/modding/b3modtool.md "car" and "track"; formats docs/modding/file-formats/car.md, track.md).
//
// The car import (below, src/b3modtool/carimport.cpp) reads these files back. So that it can rebuild the game's file,
// every export writes beside the .gltf a sidecar (<stem>.b3car.json / <stem>.b3track.json) with the source file's
// size and FNV-1a 64, every header field, and per exported mesh where it came from (offsets in the file), its material
// and colour words, its VU batch sizes, and two hashes of its geometry (in order, and as a set of triangles, which
// survives a modelling tool's re-ordering); the glTF nodes and meshes carry the same keys in their extras, and the raw
// stored vertex fields as custom attributes (_B3_*). Nothing here writes the disc; outputs are a FileSet (name -> bytes).
#ifndef B3MODTOOL_EXPORT3D_H
#define B3MODTOOL_EXPORT3D_H

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "b3asset/model.h"
#include "b3asset/raster.h"
#include "b3asset/track.h"
#include "b3modtool/gltf.h"
#include "b3modtool/modtool.h"

namespace b3modtool {

// ---- cars (PVEH/<class>/CARn.BGV / .BTV) ----

// One vertex as the car's VU1 program receives it (docs/modding/file-formats/car.md "Mesh packets")
struct CarVertex {
    float pos[3] = {0, 0, 0};        // part space (the panel's / wheel's frame for A* / B* lists)
    std::uint32_t w = 0;             // the position's w lane: 0x8000 = ADC (no triangle closes here)
    std::uint16_t uv[2] = {0, 0};    // texture coordinates x 4096
    std::uint16_t infl[2] = {0, 0};  // the two deformation influences (weight << 8 | zone << 2)
    std::uint8_t nrm[3] = {128, 128, 128};   // the V3-8 normal bytes: n = (b - 128) / 128
};
// One mesh entry of a list (slot) of a LOD block
struct CarMesh {
    int lod = 0, slot = 0, entry = 0;    // slot 0 L0, 1..6 A0..A5, 7..9 B0..B2, 10..17 C0..C7
    std::uint32_t list_at = 0, mesh_at = 0, cont_at = 0;   // file offsets (cont_at 0: no continuation packet)
    std::uint32_t material = 0, colour = 0;
    int glass_entry = -1;                // the list's s8 at +1
    std::vector<CarVertex> v;
    std::vector<std::uint32_t> tri;      // triangle list into v (strip order, docs/native-renderer.md §11.3 winding)
    std::vector<std::uint32_t> batches;  // vertex count of each VU batch, in order
};
struct CarLight {
    int group = 0, index = 0;
    std::uint32_t at = 0;
    float raw[12] = {};                  // 0x30 bytes: position (x y z w), facing (x y z w), 4 more floats
};
struct CarFile {
    std::uint32_t version = 0, size = 0, kind = 0;   // +0x00, +0x08, +0x10 (3 BGV, 2 BTV)
    int panels = 0, wheels = 0;                      // +0x0C, +0x0D
    float f14 = 0, scale = 0, wheel_factor[6] = {};  // +0x14, +0x18, +0x1C
    std::uint32_t lod_at[5] = {}, tex_at = 0, list64 = 0, list68 = 0;
    std::vector<std::array<float, 16>> wheel_frames, panel_frames;   // as stored (CGtMatrix3x4)
    std::vector<CarMesh> meshes;
    std::vector<CarLight> lights;
    int palettes = 0;
    std::vector<std::string> notes;
};
const char* car_slot_name(int slot);   // "L0", "A0" .. "C7"
// Every LOD block's lists, each entry's packets decoded with their stored lanes.
bool car_read(const Buf& file, CarFile& out, std::string* why = nullptr);

// The order-independent hash of a mesh's triangles: each triangle with two or more distinct positions is hashed from
// its three vertex keys (position bits, then `extra` bytes per vertex) rotated to start at the smallest key (winding
// kept); the hashes are mixed and summed. Equal for the same triangles in any order and with any vertex sharing.
std::uint64_t triangle_set_hash(const std::vector<std::array<float, 3>>& pos, const std::vector<std::uint8_t>& extra,
                                std::size_t extra_per_vertex, const std::vector<std::uint32_t>& tri);
std::uint64_t car_mesh_tri_hash(const CarMesh& m);   // extra = uv, infl (8 bytes)
std::uint64_t car_mesh_raw_hash(const CarMesh& m);   // FNV-1a 64 of the vertices' stored fields in order, then the batches

struct ExportReport {
    std::size_t files = 0, nodes = 0, meshes = 0, primitives = 0, textures = 0;
    std::uint64_t vertices = 0, triangles = 0;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};   // in the exported scene's space
    std::vector<std::string> notes;                   // what was not exported, and why
};

struct CarExportOptions {
    int lod = -1;                    // -1: every LOD
    bool obj = false;                // OBJ / MTL instead of glTF
    int paint = 0;                   // the paint (palette) the materials show by default
    std::string name;                // the game's name for the car (root node; empty: the stem)
    std::string source;              // its disc path, for the sidecar ("PVEH/COMP/CAR1.BGV")
};
// <stem>.gltf + <stem>.bin + <stem>_paint<k>.png per paint + <stem>.b3car.json (glTF); or <stem>[_lod<k>].obj +
// <stem>.mtl + the PNGs (OBJ, panels and wheels placed at their frames)
bool car_export(const Buf& file, const std::string& stem, const CarExportOptions& opt, FileSet& out,
                ExportReport* rep = nullptr, std::string* why = nullptr);

// ---- car import (part 3c, second half: docs/modding/b3modtool.md "car import") ----
//
// The mesh packet writer is the inverse of car_read's decode: a mesh's vertex stream (CarMesh::v, the ADC flag in w)
// and its VU batch sizes (CarMesh::batches) become the two DMA RET packets the game's files hold, in the files' own
// form (docs/modding/file-formats/car.md "Mesh packets", "How the packets are written"): batch 0 in the first packet
// after STROW 65536.0, batches 1.. in the continuation, each batch STMOD / STCYCL / UNPACK V4-32 positions, V3-8
// normals (offset mode), V4-16 texture coordinates and influences (cycle 3/1; 6/1 and texture coordinates only for the
// wheel lists B0..B2; positions alone for the lists at +0x64 / +0x68), ITOP, and MSCNT in the continuation; every
// UNPACK's data on a quadword (1 to 4 NOPs before its STMOD), the packet padded with NOPs to a quadword. All 67 player
// cars' packets come out byte for byte.
constexpr int kCarMaxBatch = 52;                 // vertices per VU batch: the most any file on the disc has
constexpr std::size_t kCarMaxBgvSize = 0x72000;  // memory block 1, which a player car is loaded into (car.md "Size limit")
// The three packet forms: every field (L0, A0..A5, C0..C7); the wheel lists B0..B2 (6/1: no influences stored); the
// lists at +0x64 / +0x68 (positions only: no STROW, no normals, texture coordinates or influences)
enum CarPacketForm { kCarFull = 0, kCarUvOnly = 1, kCarPositions = 2 };
int car_slot_form(int slot);
void car_form_mask(int form, CarVertex& v);      // zeroes the fields the form does not store (car_read gives them as 0)
struct CarPackets { Buf first, cont; };          // cont empty: one batch, no continuation packet
bool car_encode_mesh(const CarMesh& m, int form, CarPackets& out, std::string* why = nullptr);

// Triangles (indices into `verts`; their winding as car_read gives it) -> a vertex stream and batches for the writer:
// greedy triangle strips, a strip that crosses a batch carried on with its last two vertices (ADC), a strip kept on the
// parity that gives its triangles their winding. Triangles with two equal positions are left out (they draw nothing).
bool car_strip_mesh(const std::vector<CarVertex>& verts, const std::vector<std::uint32_t>& tri, CarMesh& out,
                    std::string* why = nullptr);

// One mesh of the import: what the model says the entry (lod, slot, entry of car_read) should be
struct CarImportMesh {
    int lod = 0, slot = 0, entry = 0;
    std::string name;                    // the glTF mesh / node it came from (messages)
    std::vector<CarVertex> v;            // part space; w (ADC) is not used
    std::vector<std::uint32_t> tri;
    // when its object was moved / turned / scaled (a node transform): the vertices before it, and the transform (glTF
    // column-major, into the part's space) and its normal matrix, so that an object whose mesh data is the file's keeps
    // the file's strips with the transform applied
    std::vector<CarVertex> raw;
    std::array<double, 16> xform{}, nxform{};
};
struct CarImportOptions {
    bool restrip = false;                // every mesh through car_strip_mesh, even an unchanged one (the import check)
    std::size_t max_size = 0;            // 0: kCarMaxBgvSize for a .BGV (kind 3), the original's size for a .BTV
};
struct CarImportReport {
    std::size_t meshes = 0;              // entries in the file
    std::size_t unchanged = 0;           // written back from the file's own stream (the same triangles, in any order)
    std::size_t edited = 0;              // the file's strips with new positions / texture coordinates / normals / influences
    std::size_t restripped = 0;          // new triangles: strips rebuilt
    std::size_t missing = 0;             // not in the model: kept as they are
    std::size_t moved = 0;               // packets placed elsewhere than before (into freed packet space or appended)
    std::size_t paints_changed = 0;      // paint pictures that differ from the file's
    bool new_palette_indices = false;    // the paints were re-quantised together (not a recolour of the CLUTs)
    std::size_t size_before = 0, size_after = 0;
    std::uint64_t vertices = 0, triangles = 0;
    std::vector<std::string> notes;
};
// `original` (the disc's file) with the meshes of `meshes` written over its entries (missing entries kept) and
// `paints` (level 0 of each paint, empty = as stored) written into its texture atlas. Everything else is kept byte
// for byte; when a packet no longer fits its place, packets are moved into freed packet space or appended (the
// entries' offsets and the size word +0x08 follow; of two layouts the smaller). Refused with a message, nothing
// written: an entry the file does not have, a vertex outside the limits (non-finite or beyond 1 km, a texture
// coordinate outside 0..16, an influence zone the file does not use), a file over `max_size`.
bool car_import_meshes(const Buf& original, const std::vector<CarImportMesh>& meshes,
                       const std::vector<b3asset::Image>& paints, const CarImportOptions& opt, Buf& out,
                       CarImportReport* rep = nullptr, std::string* why = nullptr);

// A model exported by car_export (its .gltf / .glb, possibly edited in a modelling tool and saved again) read back
// into CarImportMeshes: meshes matched to entries by their extras or names (LOD3_L0_e00, a tool's ".001" suffix
// ignored), the primitives of one mesh joined, positions brought into the part's space from their node's world
// matrix (the part frames stay the file's), texture coordinates x 4096, normals and influences from _B3_NRM /
// _B3_INFL when they still match (else from NORMAL and the nearest original vertex). `load` reads a file by its path:
// the model, and what it names (the .bin, by its uri, relative to the model's folder; a .glb carries its own).
using FileLoader = std::function<bool(const std::string& path, Buf& out)>;
bool car_model_read(const FileLoader& load, const std::string& model, const CarFile& car,
                    std::vector<CarImportMesh>& out, std::vector<std::string>* notes = nullptr, std::string* why = nullptr);

// The whole import from an export folder: the sidecar (the path `sidecar`) names the source file (its size and
// FNV-1a 64 must be `original`'s) and the paint PNGs (beside it); the model is the path `model`.
bool car_import(const Buf& original, const FileLoader& load, const std::string& model, const std::string& sidecar,
                const CarImportOptions& opt, Buf& out, CarImportReport* rep = nullptr, std::string* why = nullptr);

// ---- tracks (TRACKS/<area>/<route>/) ----

struct TrackExportOptions {
    int section = -1;                // -1: every unit, plus the route-wide backdrops and water; else that unit only
    bool obj = false;
    std::string source;              // "TRACKS/US/C1_V1"
};
// <stem>.gltf + .bin + <NNN>_<texture>.png + <stem>.b3track.json + <stem>_report.txt (what is exported and what not).
// `enviro` may be empty (no sky textures then); `streamed` may be empty (a static-only route).
bool track_export(const Buf& static_dat, const Buf& streamed_dat, const Buf& enviro, const std::string& stem,
                  const TrackExportOptions& opt, FileSet& out, ExportReport* rep = nullptr, std::string* why = nullptr);

// ---- track import (part 3c, second half: docs/modding/b3modtool.md "track import") ----
//
// The visual meshes only: the streamed units' high and LOD meshes (STREAMED.DAT), the backdrops and water, the instance
// and prop models (STATIC.DAT), as the export gives them. The collision (the LOD blocks' trees), GAMEDATA.BGD (AI and
// traffic paths), the placements, the render tables and the materials' GS state stay the file's.
//
// One vertex as the track's VU1 programs receive it (docs/track-format.md §3): every stored field, raw.
struct TrackVertex {
    float pos[3] = {0, 0, 0};
    std::uint32_t w = 0;                 // the position's w lane: 0x8000 = ADC (no triangle closes here)
    std::int32_t uv[2] = {0, 0};         // V2-16: the signed 4.12 words; V2-32 (instances): the floats' bits
    std::uint8_t col[4] = {0, 0, 0, 0};  // V4-8 RGBA (0x80 = 1.0) / V3-8 RGB (col[3] 0)
    std::int8_t nrm[4] = {0, 0, 0, 0};   // types 1 / 2: the normal (s8 / 127) and the specular intensity
    float light[4] = {0, 0, 0, 0};       // type 5: the per-vertex lighting vector (V4-32 at 2)
};
// A mesh's vertex layout: which attributes its batches unpack (one layout per material type, track-format.md §3.4)
enum TrackLayout {
    kTrackUv16 = 1,      // V2-16 texture coordinates at 1 (types 0, 1, 6 textured)
    kTrackUv32 = 2,      // V2-32 at 1 (types 4, 5: instance and prop models)
    kTrackCol4 = 4,      // V4-8 colour at 2 (types 0, 3, 6)
    kTrackCol3 = 8,      // V3-8 colour at 2 (types 1, 2)
    kTrackNrm204 = 16,   // V4-8 signed normal + intensity at 204, CL 1, transposed by four (type 1)
    kTrackNrm1 = 32,     // V4-8 signed normal + intensity at 1, transposed by four (type 2: no texture coordinates)
    kTrackLight = 64,    // V4-32 lighting vector at 2, transposed by four (type 5)
};
int track_material_layout(int type, bool textured);   // the layout a material's meshes have (-1: not importable)
int track_max_batch(int layout);   // vertices per VU batch: the most any route has (80; 68 type 1; 76 instance models)
struct TrackMesh {
    int layout = 0;
    std::vector<TrackVertex> v;
    std::vector<std::uint32_t> batches;  // vertex count of each VU batch (batch 0 in chain 1, the others in chain 2)
    std::vector<std::uint32_t> tri;      // triangle list into v (b3asset::strip_to_triangles: the decode's winding)
};
// A mesh's DMA chains (file offsets; chain2 0: none) decoded with every stored lane. False (and why) for a stream the
// writer does not reproduce: another layout per batch, chain 1 with more than one batch, an unknown unpack.
bool track_mesh_read(const Buf& file, std::size_t chain1, std::size_t chain2, TrackMesh& out, std::string* why = nullptr);
// The writer, the decode's inverse in the disc's own form (docs/modding/file-formats/track.md "How the packets are
// written"): chain 1 = batch 0, chain 2 = the others (each with MSCNT); per batch STMOD 0 / STCYCL / UNPACK with its data
// on a quadword (1 to 4 NOPs), ITOP; the transposed blocks with 4 x ceil(n / 4) elements, the lanes past n zero.
bool track_encode_mesh(const TrackMesh& m, Buf& chain1, Buf& chain2, std::string* why = nullptr);
// Triangles -> strips and batches in `layout` (build_strips; triangles with two equal positions left out)
bool track_strip_mesh(const std::vector<TrackVertex>& v, const std::vector<std::uint32_t>& tri, int layout, TrackMesh& out,
                      std::string* why = nullptr);
// The order-free hash of a mesh's triangles over its stored fields (with or without the normals' direction)
std::uint64_t track_mesh_hash(const std::vector<TrackVertex>& v, const std::vector<std::uint32_t>& tri, int layout, bool normals = true);

// Where a mesh's chains are referenced from (the import's view of the two files)
struct TrackSite {
    enum Kind { kHigh, kLod, kBackdrop, kInstance, kProp } kind = kHigh;
    bool in_static = false;
    std::size_t ref_at = 0;      // the CB3TrackMesh (its +0x0C / +0x1C name the chains, relative to it) or the instance
                                 // model pair (+0x20 of the type: two offsets relative to the pair)
    std::size_t chain1 = 0, chain2 = 0;
    int unit = -1;               // streamed: the unit (its block holds the chains)
    int type = -1;               // instance / prop models: the type
    int table = -1, group = -1;  // backdrops: STATIC +0x24 / +0x28 / +0x2C / +0x30 (0..3), the CB3BackdropObject in it
    int material = -1;           // the STATIC material it is drawn with (streamed: the render table's, -1 unmapped)
    std::size_t mat_at = 0;      // the s16 that names it (backdrops, instance / prop types; 0 streamed)
    std::size_t owner_at = 0;    // the backdrop object / the instance or prop type (their boxes)
};
struct TrackFiles {
    std::vector<TrackSite> sites;
    struct Block { std::size_t at = 0, size = 0, used = 0; };   // STREAMED.DAT: per unit high (2k) and LOD (2k + 1)
    std::vector<Block> blocks;
    std::vector<b3asset::TrackMaterialInfo> materials;
    std::vector<std::size_t> textures;                          // the texture table (STATIC offsets)
};
bool track_files_read(const Buf& static_dat, const Buf& streamed_dat, TrackFiles& out, std::string* why = nullptr);
constexpr std::size_t kTrackHighSlot = 0x28000, kTrackLodSlot = 0x18000;   // the game's slots for a unit's two blocks
constexpr std::size_t kTrackStaticBlock = 0x580000;   // STATIC.DAT's memory block in the smaller layout (track.md)

// One mesh of the import: what the model says the chains (in_static, chain1, chain2) drawn with `material` should be
struct TrackImportMesh {
    bool in_static = false;
    std::size_t chain1 = 0, chain2 = 0;
    int material = -1;                   // as exported (the site's)
    int new_material = -1;               // another STATIC material (backdrops and instance models only), -1 the same
    std::string name;                    // the glTF mesh / node it came from (messages)
    std::vector<TrackVertex> v;          // world space (instance models: object space); w not used
    std::vector<std::uint32_t> tri;
    // moved / turned / scaled as a whole (a world-space mesh's node transform): the vertices before it, the transform
    // (glTF column-major) and its normal matrix, so that a mesh whose data is the file's keeps the file's strips
    std::vector<TrackVertex> raw;
    std::array<double, 16> xform{}, nxform{};
};
struct TrackTextureEdit {
    int material = -1, texture = -1;     // material `material` draws texture `texture` (the STATIC texture table's)
};
struct TrackImportOptions {
    bool restrip = false;                // every mesh through track_strip_mesh, even an unchanged one (the import check)
    std::size_t max_static = 0;          // 0: the larger of kTrackStaticBlock and the original's size
    bool no_limits = false;              // the census: blocks may outgrow their slots, STATIC.DAT its block
};
struct TrackImportReport {
    std::size_t meshes = 0;              // meshes in the model (chains)
    std::size_t missing = 0;             // the export's meshes the model no longer has: kept as they are
    std::size_t unchanged = 0, edited = 0, restripped = 0;
    std::size_t moved = 0;               // packets placed elsewhere (freed packet space, a block's free end, STATIC's end)
    std::size_t blocks_grown = 0;        // STREAMED.DAT blocks that grew (by 32 KB steps, within the game's slot)
    std::size_t materials_changed = 0, textures_swapped = 0, textures_changed = 0, boxes_grown = 0;
    std::size_t static_before = 0, static_after = 0, streamed_before = 0, streamed_after = 0;
    std::uint64_t vertices = 0, triangles = 0;
    std::vector<std::string> notes;
};
// The files with the meshes written over their chains, the material swaps and the texture pictures (`pictures`: the
// texture table index -> level 0, empty = as stored). Everything else byte for byte; refused with a message, nothing
// written: an unknown mesh, a vertex outside the limits, a layout another material cannot draw, a block over its slot.
bool track_import_meshes(const Buf& static_dat, const Buf& streamed_dat, const std::vector<TrackImportMesh>& meshes,
                         const std::vector<TrackTextureEdit>& swaps, const std::map<int, b3asset::Image>& pictures,
                         const TrackImportOptions& opt, Buf& static_out, Buf& streamed_out, TrackImportReport* rep = nullptr,
                         std::string* why = nullptr);
// A model exported by track_export (.gltf / .glb, possibly edited and saved again) read back: primitives matched to
// chains by their extras (else by section and material), world-space meshes brought through their node's matrix, the
// stored fields from the _B3_* attributes while they still agree with COLOR_0 / NORMAL. `sidecar` is the export's.
bool track_model_read(const FileLoader& load, const std::string& model, const Json& sidecar, const Buf& static_dat,
                      const Buf& streamed_dat, std::vector<TrackImportMesh>& out, std::vector<TrackTextureEdit>& swaps,
                      std::vector<std::string>* notes = nullptr, std::string* why = nullptr);
// The whole import: the sidecar (the path `sidecar`) names the route (its files' sizes and FNV-1a 64 must be these),
// the textures beside it (edited pictures go back into STATIC.DAT); the model is the path `model`.
bool track_import(const Buf& static_dat, const Buf& streamed_dat, const FileLoader& load, const std::string& model,
                  const std::string& sidecar, const TrackImportOptions& opt, Buf& static_out, Buf& streamed_out,
                  TrackImportReport* rep = nullptr, std::string* why = nullptr);

// An exported .gltf (and its .bin and PNGs beside it) read back into a b3asset::Model: every mesh placed at its node's
// world matrix, materials as draw ranges with their base colour texture and alpha mode. Nodes whose name contains one of
// `skip` are left out with their children. For `b3modtool preview` and the export check; not an importer.
bool gltf_load_model(const std::string& gltf_path, b3asset::Model& out, const std::vector<std::string>& skip = {},
                     std::string* why = nullptr);

// ---- the disc's cars and routes ----
struct CarListEntry {
    int index = -1;                  // the player car's place in the game's order (-1: traffic)
    std::string key;                 // its disc path, "PVEH/COMP/CAR1.BGV"
    std::string path;                // what read_host_file takes
    std::string name, kind;          // the game's name (DATA/GLOBALUS.BIN, as the launcher's showroom) and class
};
// The 67 player cars in the game's order (src/b3launcher/showroom.h), then with `traffic` every .BTV by path.
// `note`: what was missing (no PVEH, no string table).
std::vector<CarListEntry> car_list(const std::string& game, bool traffic = false, std::string* note = nullptr);
struct RouteEntry {
    std::string key;                 // "TRACKS/US/C1_V1"
    std::string dir, static_path, streamed_path, enviro_path;   // read_host_file paths ("" when missing)
};
std::vector<RouteEntry> route_list(const std::string& game);   // every route folder with a STATIC.DAT, sorted

}  // namespace b3modtool

#endif
