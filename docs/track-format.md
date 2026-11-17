<!-- Imported 2026-11-02 from render2's research (structural prober over all 37 routes; the throwaway prober scripts
     trackfmt/b3track.py / probe.py are not kept: src/b3asset/track.cpp + b3assetdump census reproduce its totals). -->
# Burnout 3 (PS2 retail, SLUS-21050) track render data: byte-level spec

Status 2026-11-02. Scope: `TRACKS/<area>/<route>/{STATIC.DAT, STREAMED.DAT}` -> vertex/index buffers + material and
texture references. Tags: **[C]** seen in code (function address + file:line in `../burnout3-retail-decomp`) or in
data (file offset + bytes, or prober output), **[L]** likely, **[G]** guess. Field names are offsets only (the decomp has
placeholders; nothing here is a Criterion name except the class/function names taken from `research/retail_names.csv`).

Code citations: "matched" = byte-matched unit under `src/b3/**` (exact semantics); "nonmatching" = `src/nonmatching/*.cpp`
behavioural draft (semantics probably right, not proven), so a claim resting only on a nonmatching file is at most [L]
unless the data confirms it.

Prober: `trackfmt/b3track.py` (reader) + `trackfmt/probe.py` (totals) + `trackfmt/coverage.py` (signature-scan vs
structural walk). Run: `python3 -I probe.py <trackfmt dir> <game>/TRACKS [filter]`. Output of the full run:
`trackfmt/probe_all.txt`, `trackfmt/coverage_all.txt`.

All integers little-endian. "off" = 32-bit offset stored in place of a pointer; the base it is relative to is
given per field (it differs per field, which is the main trap).

---------------------------------------------------------------------------------------------------------------

## 0. Summary for a loader

1. Read STATIC.DAT (one image). Materials (+0x08), texture table (+0x18), backdrop objects (+0x24..+0x30), instance
   types (+0x38) and prop types (+0x3C) with their meshes, prop placements (+0x48), unit table (+0x58).
2. For every unit k in the unit table: read the **high** block and the **LOD** block from STREAMED.DAT at the given
   offsets. Each block has a mesh array at +0x50. The mesh -> material mapping is in the high block's render table at
   +0x60 (row with delta 0). The LOD block also carries the per-unit placements of instance types.
3. Every mesh (streamed, backdrop, instance) = one or two DMA "ret" packets; each packet holds 1..n *batches*; each
   batch is a triangle strip of <= 80 vertices with restart flags. Decode by unpack address/format (7 layouts, one
   per material type, sec. 3). World-space for streamed and backdrop meshes; object-space + per-instance 3x4 matrix
   for instance types and props.
4. Texture of a mesh: `material.tex[material.cur]` -> absolute STATIC.DAT offset of a serialized raster (CGtTexture),
   which is also an entry of the texture table.

---------------------------------------------------------------------------------------------------------------

## 1. STATIC.DAT

Loaded whole into one buffer, then `CB3StaticTrackData::FixUp` (0x15E8E0, matched,
`src/b3/_unclassified/B3StaticTrackData_0015E8E0.cpp`) turns offsets into pointers [C]. Load path: `CB3StaticTrack::Prepare`
0x332E00 (`func_00332E00.cpp:185` FixUp, `:192` Prepare of animated materials) [C].

### 1.1 Header (0x60 bytes) [C FixUp unless noted]

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u32 | 39 (0x27) in all 37 files [C prober] -- version [G] |
| 0x04 | u32 | file size [C: equals file length in all 37] |
| 0x08 | off (rel. file) | material array, 0x28 bytes each (`:148`) |
| 0x0C | s16 | material count |
| 0x0E | s16 | count of the "prepare" list |
| 0x10 | off (rel. file) | s16[ +0x0E ] material indices whose `CB3TrackMaterial::Prepare` runs each frame (animated / cycling textures) [C `func_00332E00.cpp:192`] |
| 0x14 | s8 | backdrop range split A, 0x15 s8 split B: `RenderAllBackdrops` 0x21D2E0 draws backdrops [0,A), [A,B), [B,count(+0x1C)) for range 0/1/2 [C `B3StaticTrackPS2_0021D2E0.cpp:113-122`]. 0/0 everywhere except US/C5_V1 (6/7) [C prober] |
| 0x16 | s16 | texture count |
| 0x18 | off (rel. file) | u32[ +0x16 ] texture offsets, each relative to the file start; each target is a CGtTexture fixed up by 0x2514C0 (`:153`) |
| 0x1C/1E/20/22 | s16 x4 | counts of the four backdrop tables at +0x24/+0x28/+0x2C/+0x30 (`:157-160`) |
| 0x24 | off (rel. file) | CB3BackdropObject[] "backdrops", indexed by the unit render table (+0x64 bytes) [C RenderBeforeVehicles, nonmatching `:221`] |
| 0x28 | off | CB3BackdropObject[] drawn by `func_0021C010` with index = high-block table byte +0x6D [C matched `func_0021C010.cpp:174`]; purpose unknown |
| 0x2C | off | CB3BackdropObject[] water surfaces (`RenderWater` 1st object arg; index = table byte +0x6E) [L nonmatching `:339`] |
| 0x30 | off | CB3BackdropObject[] second water object (`RenderWater` optional arg; index = table byte +0x6F) [L] |
| 0x34 | s16 | instance-type count (`:161`) |
| 0x36 | s16 | prop-type count |
| 0x38 | off (rel. file) | instance types, 0x50 each (sec. 1.5) |
| 0x3C | off (rel. file) | prop types, 0x50 each, same layout (sec. 1.5); rendered by `CB3PropManager::Render` (`func_002F99E0.cpp:68`) [C] |
| 0x40 | u16 | prop placement count (+0x42 is 0) [C `B3PropManager_002B7300.cpp` reads `t->field_40`; data: equals the matrix block size] |
| 0x44 | off (rel. file) | u8[ prop-type count ], copied per type by `CB3PropManager::Prepare` 0x2B7300; meaning unknown |
| 0x48 | off (rel. file) | prop placements: [ +0x40 ] x 0x40-byte matrices (sec. 4) [C `B3PropManager_002B7300.cpp:87`] |
| 0x4C | off (rel. file) | u32 off[ unit count ] -> u8 count[ prop-type count ] per unit (`:164-168`; PropManager `:127`) |
| 0x50 | off (rel. file) | u32 off[ unit count ] -> u32 off[ prop-type count ] -> {u16 placement index; u8; u8}[count] (`:170-178`; PropManager `:172`). Prober: 13,359 refs = 13,359 placements, all indices in range [C] |
| 0x54 | s16 | unit count |
| 0x58 | off (rel. file) | unit table, 16 bytes per unit (sec. 2.1) [C `B3StreamedTrack_003029B0.cpp:30`] |
| 0x5C | off (rel. file) | 16 zero bytes in the files looked at; unknown |

0x60: the texture offset table usually starts right after the header (AS/C1_V1: +0x18 = 0x60) [C bytes].

### 1.2 Materials (CB3TrackMaterial, 0x28 bytes)

`CB3TrackMaterial::FixUp` 0x26A170 (matched, `B3TrackMaterialPS2.cpp:28`); use in `CB3TrackRenderer::ReallySetMaterial`
0x26B2F0 (nonmatching `ReallySetMaterial__16CB3TrackRendererFv.cpp`).

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u32 | **type** 0..7 -> VU1 microprogram and vertex layout (sec. 3.4) [C switch `:113`, data] |
| 0x04 | f32 | parameter (types 1/2: `SetW` into renderer vectors; 0.5 for some type-0) [L `:195-210`] |
| 0x08 | f32 | parameter (types 1/2) [L] |
| 0x0C | off (rel. **material start**) | u32[ +0x11 ] texture entries; **each entry is a signed offset relative to the material start** (`&field_0[entry]`, `:28`) and lands on a CGtTexture, i.e. on a texture-table target [C prober: 10,195 refs, all in the texture table; every texture referenced] |
| 0x10 | s8 | current texture index (runtime; Prepare 0x26A160 zeroes it; CB3StaticTrack::Update 0x3329A0 advances it) -- texture used = entries[cur] [C `:145`, asm] |
| 0x11 | s8 | texture count (0 = untextured, 87 materials) |
| 0x12 | s8 | runtime: the next frame's number (atoi of its texture name's suffix) [C Update 0x3329A0; native-renderer.md §15] |
| 0x13 | s8 | direction step for flag 0x100 (back and forth) [C asm; no route uses it] |
| 0x14 | f32 | cycling: seconds per frame-number unit; flag 0x200: scroll speed (per second of world time) [C asm] |
| 0x18 | f32 | cycling: the current frame's period (file: frame 0's); flag 0x200: scroll step (0 = continuous) [C asm] |
| 0x1C | f32 | runtime: the last frame change (world time), or the scroll position; with flag 0x200 the renderer sends 1-frac(t) (UV scroll) [C `:187`, Update asm, anim1 capture] |
| 0x20 | u32 | GS **TEST_1** value sent with A+D reg 0x47 [C `:230`]: 0x50003 (ZTE, GEQUAL, alpha test ALWAYS) or 0x5000D (alpha test GREATER than 0 = cut-out); 0x5100A on line materials |
| 0x24 | u16 | flags: 0x20 selects the alternate VU1 program (`:125/130/141`) [C]; 0x200 UV scroll [C anim1]; 0x100 frames back and forth [C asm]; 0x4/0x8 instance light-alpha 255/128 [L BeginInstanceRender]; 0x18 / 0x400 instance flags [L]; 0x1 co-occurs with TEST 0x5000D, 0x8 with ALPHA 0x44 [C data] |
| 0x26 | u8 | GS **ALPHA_1** A/B/C/D byte, reg 0x42 [C `:231`]: 0x64 = (Cs-Cd)*FIX+Cd i.e. opaque when FIX=0x80; 0x44 = (Cs-Cd)*As+Cd normal blend; 0x42 on lines |
| 0x27 | u8 | ALPHA_1 FIX (0x80 = 1.0) [C `:231`] |

The GS packet ReallySetMaterial builds: GIF tag with PRIM = 0x4C (tri-strip, Gouraud, alpha-blend) | 0x10 (TME) when a
texture is bound, or 0xE9 for type 3 (anti-aliased lines) [C `:166`, `:219`], then A+D CLAMP_1=0, TEST_1, ALPHA_1 [C].
Texture upload goes through `CGtTextureCache::AccessTexture` (filter mode 2 for type 6, else 1) [L].

### 1.3 Textures

Texture table entry = absolute STATIC.DAT offset of a CGtTexture (= serialized RwRaster + sky2 data; handled
elsewhere: TEX0 at +0x3C, upload chain at +[+0x24]). CGtTexture::FixUp 0x2514C0 relocates +0x0, +0x4, +0x8, +0x24,
+0x6C, seven offsets at +0x70..+0x88, and +0x34 of each element of the +0x24 list -- all relative to the texture
start [C matched `func_002514C0.cpp`]. Texture-table total over the 37 files = **9,240 = the census raster count** [C
prober]. ENVIRO.DAT (sky / environment rasters, census) is not referenced by these structures [not analysed].

### 1.4 Backdrop objects (CB3BackdropObject / CB3StaticTrackObject)

FixUp inline helper `:105-126` (matched) [C]:

- **CB3BackdropObject**, 8 bytes: `s16 objCount; pad; off objects` -- off relative to **the CB3BackdropObject itself**.
- **CB3StaticTrackObject**, 0x50 bytes [C `RenderStaticObject` 0x21D770 `:118/128`, `RenderBackdrop` 0x332D00]:
  - 0x00..0x3F: CGtOrthogonalBox (frustum-tested by `GtIntersection::IntersectFrustum(CGtFrustum, CGtOrthogonalBox)`;
    same 4-vector form as the mesh box, sec. 2.4) [C mangled name; L layout]
  - 0x40 s16 meshCount; 0x44 off (rel. **&object+0x40**) -> CB3TrackMesh[meshCount] (0x40 each);
  - 0x48 off (rel. **the owning CB3BackdropObject**, not the object! `:123`) -> s16 materialIndex[meshCount].
- Backdrop meshes are world-space [C data: positions in track range]. Each mesh is drawn with its own material:
  `SetMaterial(materials[mat[i]]); RenderMesh(meshes[i])` [C `:118/:128`].

### 1.5 Instance types (+0x38) and prop types (+0x3C), 0x50 bytes each

FixUp inline `:130-139` (matched) [C]; use in RenderBeforeVehicles / RenderAfterSpecular / PropManager::Render.

| Off | Type | Meaning |
|---|---|---|
| 0x00 | vec4 | local AABB max (CGtAxisAlignedBox mSup) [C data: e.g. (11.5, 26.9, 11.2)] |
| 0x10 | vec4 | local AABB min (mInf) [C data] |
| 0x20 | CB3InstanceMesh (8) | main model: `off chainA; off chainB`, both relative to **the pair's own address** (+0x20) [C `:43-47`] |
| 0x28 | CB3InstanceMesh | present if flags & 1: lower-detail model drawn beyond the switch distance (distance check 2) [C fixup; L meaning: RenderAfterSpecular uses check 1/2 for +0x20/+0x28] |
| 0x30 | CB3InstanceMesh | present if flags & 2: model for render cameras 1..3 (far-range cameras, sec. 2.3) [L nonmatching `:279`] |
| 0x38 | s16 | material of +0x20 model; 0x3A material of +0x28; 0x3C material of +0x30 [C/L same lines] |
| 0x3E | s16 | flags: 1, 2 as above; 4 = rotating (uses +0x48 as speed) [L BeginInstanceRender] |
| 0x40 | f32 | LOD switch distance (e.g. 100) [L BeginInstanceRender squares it] |
| 0x44 | f32 | stop-draw distance (e.g. 200..800) [L] |
| 0x48 | f32 | rotation speed when flags & 4 [L] |

CB3InstanceMesh chains are walked exactly like CB3TrackMesh chains (sec. 3); FlushInstances 0x26A240 calls chainA,
appends the instance matrices (VIF UNPACK V4-32 to qw 0x3A4) and an MSCNT, then calls chainB if non-zero [C matched
`B3TrackRendererRuntimePS2_0026A240.cpp:79,108,153`].

Placements: instance types are placed **per unit** by the LOD block (sec. 2.3, +0x98/+0x9C); prop types by
STATIC +0x48/+0x4C/+0x50 (sec. 1.1).

---------------------------------------------------------------------------------------------------------------

## 2. STREAMED.DAT

No header. The file is the concatenation, per unit k = 0..n-1, of a **high block** then a **LOD block**; both are
images fixed up in place after loading (`UpdateStreamHighFSM` 0x3029B0 / `UpdateStreamLowFSM` 0x302AE0, matched).

### 2.1 Unit table (STATIC +0x58, count STATIC +0x54), 16 bytes per unit [C `B3StreamedTrack_003029B0.cpp:30-36`, `B3StreamedTrack_00302AE0.cpp:30-36`]

| Off | Meaning |
|---|---|
| 0x0 | high block offset in STREAMED.DAT (read with seek(+0), size +8, FixUp = `CB3StreamedTrackHighData::FixUp`) |
| 0x4 | LOD block offset (seek(+4), size +0xC, FixUp = `CB3StreamedTrackLODAndCollisionData::FixUp`) |
| 0x8 | high block size |
| 0xC | LOD block size |

Prober [C]: blocks are contiguous (hi_k, lod_k, hi_k+1, ...) and cover each file exactly; sizes are multiples of
0x8000 (32 KiB .. 160 KiB); the block's own +0x04 = k and +0x08 = its size. Totals: **2,838 units** in 36 files;
US/C5_V1 has an empty STREAMED.DAT and 4 all-zero table rows (static-only scene, 20 backdrop meshes). The stream keeps
17 LOD blocks (unit -8..+8) and 9 high blocks (-4..+4) resident [C `func_00302D20.cpp:213-214` 17 / 9, `:143-144` 10 /
18 slots].

### 2.2 Common block header (both blocks, 0x5C bytes)

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u32 | 1 in all 5,676 blocks [C prober] |
| 0x04 | u32 | unit index k [C] |
| 0x08 | u32 | block size [C] |
| 0x0C | u32 | 0 |
| 0x10..0x4F | 4 x vec4 | four points with one shared y (a quad around the unit), identical in the unit's two blocks [C prober]; 0x1C (w of the first) is a positive float (e.g. 50.47). Use: unit membership / streaming [G] |
| 0x50 | s16 | mesh count (+0x52 pad) [C FixUp `B3StreamedTrackData_0015F0D0.cpp:56`, `_0015F200.cpp:40`] |
| 0x54 | off (rel. **block+0x50**) | CB3TrackMesh[count], 0x40 each [C same] |
| 0x58 | u32 | runtime: FixUp writes 0 (high) / 1 (LOD) [C `:51` / `:67`] |

### 2.3 High block extras: the render table at +0x60 (0xC06 bytes)

The current camera unit's high block is DMA'd to the scratchpad (`RenderBeforeVehicles`, 0xC0 qw from block+0x60) and
drives all streamed-track drawing [C matched RenderAfterVehicles 0x21C590 `:120-128`, RenderAfterSpecular 0x21C120
`:156-170`; L nonmatching RenderBeforeVehicles `:221-252`]. Offsets relative to the block:

| Off | Type | Meaning |
|---|---|---|
| 0x60 | u8 | 0 everywhere [C prober] |
| 0x61 | u8 | nAll: material-slot count, <= 0x53 (83) |
| 0x62 | u8 | nAfterVehicles |
| 0x63 | u8 | nBefore. Slots [0,nBefore) are drawn before vehicles, [nBefore,nAfterVehicles) after vehicles, [nAfterVehicles,nAll) after the specular pass [C matched loops]. Totals: 198,755 / 38 / 16,363 slot uses |
| 0x64 | s8[8] | backdrop indices into STATIC +0x24 drawn with this unit, -1 = none [L nonmatching `:221`] |
| 0x6C | u8 | unknown (0) |
| 0x6D | s8 | index into STATIC +0x28 [C matched `func_0021C010.cpp:174`] |
| 0x6E / 0x6F | s8 | water object indices into STATIC +0x2C / +0x30 [L] |
| 0x70 | f32 | water level [L] |
| 0x74 | s16[83] | slot -> material index (STATIC material array) [C matched `:120` `field_74[s]`] |
| 0x11A + 0xA9*r | row r = 0..16 | `s8 delta; u8 ?; u8 ?; s8 hiMesh[83]; s8 lodMesh[83]` |

Row r describes ring slot r, i.e. unit (k + delta) mod n; deltas are -8..+8 in every table [C prober]. hiMesh[s] /
lodMesh[s] = index into that unit's high / LOD mesh array of the one mesh with material slot s, or -1. **One mesh per
(unit, block, material)**. hiMesh is only meaningful for |delta| <= 4 (the 9 resident high units; beyond that the
bytes are stale: in AS/C1_V1 only ~12 % agree with the target unit, by chance), lodMesh for all 17 [C prober]. Row byte 1 is 0/1 [G visibility].

**Mesh -> material for a loader:** in unit k's own table, take the row with delta 0; for every slot s < nAll:
`high mesh hiMesh[s]` and `LOD mesh lodMesh[s]` use material `mat[s]`. Prober [C]: every one of the 134,137
streamed meshes is mapped exactly once; and every row (delta d) of every table agrees with unit (k+d)'s own row 0 by
material id (1,639,895 checks, 0 failures).

Which block is drawn when [L]: the before-vehicles pass draws high meshes (column 1) with render camera 0 and LOD
meshes (column 2) with cameras 1..3, per slot clip flags `field_58[i] >> 2c & 3`; the after-vehicles / after-specular
passes draw high meshes only [C matched]. Data: a LOD block has 0.1..0.7x the vertices of its high block [C prober],
so they are alternative detail levels of the same unit, not complementary geometry [L]. For a PC loader: draw the
high meshes of every unit (LOD optional for distance).

### 2.4 LOD block extras (CB3StreamedTrackLODAndCollisionData)

| Off | Type | Meaning |
|---|---|---|
| 0x60..0x8F | 3 x f32[4] | SoA (nx[4], nz[4], d[4]) -- unit-normalised 2D normals (e.g. 0.976/0.218): four boundary lines [G] |
| 0x90 | off (rel. block) | CGtCollisionKDTree, FixUp'ed [C `:73`] (collision, not render) |
| 0x94 | off (rel. block) | 0xA0 in all blocks; unknown (irradiance/lighting? `CB3StreamedTrack::GetIrradiance` exists) [G] |
| 0x98 | off (rel. **block+0x98**) | s8 count[instance-type count] [C `:77`; count passed = STATIC +0x34 by UpdateStreamLowFSM] |
| 0x9C | off (rel. block+0x98) | u32 off[instance-type count], each rel. block+0x98 -> count x 0x40-byte CB3InstanceData matrices (sec. 4) [C `:82`; `RenderInstances` 0x26A610 steps 0x40 `:41`] |

Prober [C]: 79,585 static-instance placements, all inside their LOD block, translations inside the track bbox.

### 2.5 CB3TrackMesh (0x40 bytes; streamed and backdrop meshes)

| Off | Type | Meaning |
|---|---|---|
| 0x00 | vec3 + u32 | box half-axis A; w = off (rel. **mesh start**) of DMA chain 1, never 0 [C FixUp `e->field_C + e`; RenderMesh `func_0026AF50.cpp:79`] |
| 0x10 | vec3 + u32 | half-axis B; w = off (rel. mesh start) of chain 2 or 0 [C `:128`] |
| 0x20 | vec3 + u32 | half-axis C; w = 0xFFFFFFFF in all meshes [C prober] |
| 0x30 | vec3 + u32 | box centre; w = 0x0012BFF4 in all meshes [C prober] |

The 4 qwords feed a VU0 frustum test (`vcallms 0xA18`, `:46-54`) [C]. As a box (centre +- axes) it encloses all
vertices of 94 % of meshes; the rest exceed it by up to 49 units (often a near-zero axis), so recompute bounds at load
[C prober]. No material field: the material comes from the render table (streamed) or the object's s16 array
(backdrops). Chains lie inside their own block [C prober].

---------------------------------------------------------------------------------------------------------------

## 3. Mesh packets (DMA + VIF)

### 3.1 DMA

RenderMesh emits `call` (DMAtag id 5, QWC 0, ADDR = chain 1) and, if +0x1C != 0, `call` chain 2 with VIF MSCNT in the
tag's upper half (`0x17000000`) [C `func_0026AF50.cpp:79,128`]. In the files, **every chain is a single `ret` tag**
(id 6, `0x6000_0000 | QWC`, ADDR 0) whose QWC covers the whole packet; tag upper 64 bits = two VIF NOPs [C prober:
189,278 chains, 189,278 ret tags, no other id]. So: chain bytes = 16-byte tag + QWC*16 bytes of VIF stream.

### 3.2 VIF stream per batch [C prober over all files]

```
NOP, STMOD 0                      (0x00000000, 0x05000000)
STCYCL CL=3 WL=1                  (0x01000103)
UNPACK V4-32  num=n addr=0  FLG   (0x6C nn 80 00)   position xyz + flag word, every 3rd qw
NOP, STMOD 0, STCYCL 3/1
UNPACK ...    num=n addr=1  FLG   second attribute (V2-16 / V2-32 / V4-8 signed), if the layout has one
NOP, STMOD 0, STCYCL 3/1
UNPACK ...    num=n addr=2  FLG   third attribute (V4-8 unsigned / V3-8 unsigned / V4-32), if any
[NOP, STMOD 0, STCYCL CL=1 WL=1, UNPACK V4-8 signed num=4*ceil(n/4) addr=204 FLG]   type-1 normals only
ITOP n
[MSCNT]                           every batch of chain 2; never in chain 1
```

- All UNPACKs have FLG=1 (address relative to VU1 TOPS = double buffering), no mask bit, STMOD mode 0 (no STROW
  addition); no STMASK/STROW/STCOL/MPG/DIRECT/BASE/OFFSET/MSCAL inside mesh packets [C: 820,697 unpacks, all
  `mask0_mode0_flg1`; VIF commands seen: STMOD, STCYCL, UNPACK, ITOP, MSCNT only].
- Each UNPACK's data follows the command word, padded to a 4-byte boundary.
- **Batch = the unpacks up to ITOP; vertex count = ITOP = num of the position unpack** [C prober, 0 mismatches].
  Chain 1 always holds exactly 1 batch; chain 2 holds 1..40 batches (1: 21,238 chains, 2: 9,037, ... 40: 2) [C prober]. Max vertices per batch 80 (68 for type 1,
  whose normal block starts at qw 204 = 68*3) [C prober].
- Kicks [L]: chain-2 batches self-kick (MSCNT); chain 1's batch is kicked by the MSCNT in the chain-2 call tag or by
  the renderer's trailing qword (+0x20 of the renderer) when there is no chain 2; the microprogram start (MSCAL) comes
  from the material packet. A loader can ignore kicks: each batch is an independent strip.

### 3.3 Attributes

| Attribute | Unpack | Decode |
|---|---|---|
| position | V4-32 @ 3i+0 | x, y, z float32; w = flag word: only 0 or 0x8000 occur [C prober] |
| ADC flag | w bit 15 | vertex i (i >= 2) closes triangle (i-2, i-1, i) unless its flag is set. Runs of set flags have length 1..4 (1 = a single skipped triangle, 2 = strip restart) [C prober]. Winding follows the parity of i itself: emit (i-2,i-1,i) for even i, (i-1,i-2,i) for odd i; the programs' back-face test uses that parity [C native-renderer.md §11.2] |
| UV (world) | V2-16 signed @ 3i+1 | u, v = s16 / 4096 (4.12 fixed; range -8..+8 observed) [C prober] |
| UV (instances) | V2-32 @ 3i+1 | u, v = float32 [C data] |
| RGBA | V4-8 unsigned @ 3i+2 | r, g, b, a bytes; 0x80 = 1.0 [L]. Opaque materials (ALPHA 0x64) mostly carry a = 0 (some 0x80; alpha evidently unused, even with the cut-out TEST 0x5000D); blended ones (ALPHA 0x44) 0x40..0x80 [C prober, 3 tracks]. RGB values mostly 0x00..0x3F, i.e. the VU1 program scales them (x2 or x4?) [G, open] |
| RGB (type 1) | V3-8 unsigned @ 3i+2 | r, g, b; lane w not written [C] |
| normal + intensity (type 1) | V4-8 **signed** @ 204, CL=1 WL=1, num = 4*ceil(n/4) | transposed groups of 4 vertices: qw 204+4g+c holds component c (x, y, z, intensity) of vertices 4g..4g+3 in its 4 lanes; normal = s8/127 (|n| within 124..132 for all 3,184,521 type-1 vertices) , intensity s8 (specular strength, mostly 96..127) [C prober] |
| type 2 extra | V4-8 signed @ 3i+1 | 4 signed bytes, not unit length (lanes +-64/+-128 clusters); type 2 materials are textured but carry no UV, so the program generates texcoords from this [G, open] |
| type 5 extra | V4-32 @ 3i+2 | 4 floats in [-1, 1], length 0..1.7, 42 % zero -- per-vertex lighting data for the dynamically lit prop program [G, open] |

### 3.4 Layouts: 7 distinct, one per material type [C prober, all 254,623 batches]

| Material type | VU1 program (ReallySetMaterial) | Layout (addr: format) | Batches |
|---|---|---|---|
| 0 (also the default case) | 0x46F0D0 / 0x4745C0 (flag 0x20) | 0: V4-32, 1: V2-16, 2: V4-8u | 172,059 |
| 0, untextured | same | 0: V4-32, 2: V4-8u | 517 |
| 6 | same as 0, texture filter mode 2 | 0: V4-32, 1: V2-16, 2: V4-8u | 846 |
| 1 (specular / env) | 0x470650 / 0x472EB0 | 0: V4-32, 1: V2-16, 2: V3-8u, 204: V4-8s x4 transposed | 68,213 |
| 1, untextured | same | 0: V4-32, 2: V3-8u, 204: V4-8s | 16 |
| 2 | 0x481DD0 / 0x4803F0 | 0: V4-32, 1: V4-8s, 2: V3-8u | 1,060 |
| 3 (lines, PRIM 0xE9; sec. 3.5) | 0x476130 | 0: V4-32, 2: V4-8u | 2,805 |
| 4 (instances, static) | 0x471FA0 (types 4/5) | 0: V4-32, 1: V2-32 | 8,063 |
| 5 (instances, dynamically lit) | 0x471FA0 | 0: V4-32, 1: V2-32, 2: V4-32 | 1,044 |
| 7 | 0x47E300 | not used by any track | 0 |

Program addresses [C nonmatching switch `:113-143`; they are code labels, so [L] until that unit matches]. A UV unpack is
present exactly when the material has a texture, except type 2 (textured, no UV) [C prober, every 4th track (10)].
Untextured: n_tex = 0. Instance meshes use only types 4/5, streamed/backdrop meshes only 0/1/2/3/6 [C].

### 3.5 Line meshes (material type 3): the wires

Type-3 meshes are the overhead wires: tram and trolley-bus wires, power lines and the cables the traffic lights and
street lights hang from. Without them those lights float in the air.

- **Where** [C decoder, all 37 routes]: only in the units' meshes (the high meshes, and on some routes the LOD meshes too,
  e.g. EU/M1_V1: 3,350 LOD segments, US/C2_V2: none); no backdrop, instance or prop uses them. In the render table they
  sit in the **before-vehicles** slot group (high block +0x63, sec. 2.3), never after the vehicles or the specular. One material per route: **TEST_1 0x5100A** (alpha test off, AFAIL FB_ONLY,
  Z test GEQUAL), **ALPHA_1 0x42** = `(0 - Cs) x As + Cd` = **Cd - Cs x As** (a subtractive blend: the wire darkens what
  is behind it), FIX 0x80, flags 0x10, untextured, parameters 0 / 1.
- **Batch layout** (sec. 3.4): V4-32 position (+ ADC in w bit 15) at 3i, V4-8 unsigned RGBA at 3i+2, no UV. Every batch
  has an even vertex count and no ADC flag set [C all 2,805 batches]. Colours are dark greys, 0x15..0x3A (some 0x80,
  i.e. black wires), alpha always 0x80.
- **Program 0x476130** sends every vertex unchanged as a GIF RGBAQ + XYZF2 under PRIM **0xE9** = LINE (independent
  lines: vertices 2k, 2k+1) | IIP | FGE | ABE | AA1; its own ADC is the input's (w bit 15). F is RW's linear fog:
  `clamp(255 (far - w) / (far - fogPlane))` [C program 0x0038-0x0057, b3vucheck: 0 mismatches]. So segment k joins
  vertices 2k and 2k+1, skipped when vertex 2k+1 carries ADC. Segments are 0.5 m to 100+ m; about 7 % of them share an
  end with the next.
- **The GS with AA1** [C GS User's Manual 6.0 §3.6]: with AA1 = ABE = 1 and A = 0x80 the pixel's alpha is replaced by its
  coverage Cov (a line never reaches 0x80), which both drives the blend and is written to the frame alpha; the Z buffer is
  **not written** for AA1 lines. So a wire pixel is `Cd - Cf x Cov` (Cf = the colour fogged by F towards FOGCOL), its frame
  alpha becomes Cov (the full-screen specular then adds `S x Cov` there), and later draws are not hidden by it.

Totals [C decoder, the reader's stat "line segments", high meshes]: 34 of the 37 route folders have line meshes;
AS/C2_V1, AS/C2_V2 and US/C5_V1 have none. Both versions of a route carry the same wires, except where noted:

| Route | Segments | Total length | Route | Segments | Total length |
|---|---|---|---|---|---|
| AS/C1_V1, V2 | 1,004 | 18.5 km | EU/M2_V1, V2 | 707 | 17.1 km |
| AS/C3_V1, V2 | 444 | 10.5 km | EU/P1_V1, V2 | 2,687 | 46.9 km |
| AS/M1_V1, V2 | 1,721 | 41.5 km | EU/P2_V1, V2 | 2,585 | 45.6 km |
| EU/C1_V1, V2 | 1,545 | 10.2 km | US/C1_V1, V2 | 478 | 12.1 km |
| EU/C2_V1, V2 | 2,296 | 27.2 km | US/C2_V1, V2 | 2,071 | 16.4 km |
| EU/C3_V1, V2 | 326 | 12.8 km | US/C3_V1, V2 | 118 | 2.0 km |
| EU/C4_V1, V2 | 383 | 4.4 km | US/M1_V1, V2 | 449 | 9.0 km |
| EU/M1_V1, V2 | 4,091 | 42.6 km | US/P1_V1, V2 | 1,385 | 8.4 km |
| | | | US/P2_V1 / V2 | 455 / 456 | 9.5 km |

The native renderer draws them as screen-space quads (docs/native-renderer.md §11.8).

---------------------------------------------------------------------------------------------------------------

## 4. Coordinates and transforms

- Axis: **Y up** [C data: every unit quad has one y; streamed y range -3,586..2,305 vs x/z extents of 2-30 km;
  instance matrices have the up vector in row 1]. Handedness: not determined [G: RenderWare right-handed].
- Units: metres [L: instance AABBs e.g. 27 x 23 m trees, 0.6 m bollards; tracks 3-30 km across].
- **Streamed meshes and backdrop meshes: world space**, no transform [C data: coordinates in the same km range as the
  instance translations and unit quads; per-track bbox in `probe_all.txt`; L: no per-unit/per-object matrix exists in
  any structure read by RenderMesh/RenderStaticObject].
- **Instance types / prop types: object space** (+0x00/+0x10 AABB of a few metres), placed by **CB3InstanceData** =
  0x40 bytes [C RenderInstances `lqc2 vf15..vf18` from +0x00..+0x30, stride 0x40]:
  - +0x00 row X (xyz) + w, +0x10 row Y + w, +0x20 row Z + w, +0x30 translation xyz (world) + w = 0.
  - Rows carry rotation x uniform scale (e.g. 0.4, 0.65); world = x*rowX + y*rowY + z*rowZ + T (RwMatrix row-vector
    convention) [L; the transpose would mirror the yaw, not checked against the VU0 program at vcallms 0x138].
  - The w of rows X/Y/Z are three floats ~0.17..0.20 (per-instance colour/light tint) [G].
- Static instance placements: LOD block +0x98/+0x9C of each unit (sec. 2.4), drawn when that unit is in the window;
  prop placements: STATIC +0x48 (sec. 1.1), listed per unit via +0x4C/+0x50.

---------------------------------------------------------------------------------------------------------------

## 5. Prober totals (37 track folders; `probe_all.txt`)

Structural walk only (unit table -> blocks -> mesh arrays -> chains; backdrop tables; instance/prop types). ISSUES: none
(checks: unit table contiguous + exact file cover, block index/size, render-table deltas -8..8, every streamed mesh
mapped once, cross-row consistency, chains inside block, ITOP == position count, attribute counts equal, flag words
only 0/0x8000, normal-block size, material/texture/prop indices in range).

| Quantity | Value |
|---|---|
| tracks / with streamed data | 37 / 36 |
| units | 2,838 |
| streamed meshes high / LOD | 77,219 / 56,918 |
| backdrop objects (+0x24/+0x28/+0x2C/+0x30) | 3,088 / 364 / 646 / 314; 4,789 backdrop meshes |
| instance types / prop types | 1,534 / 436 (4,334 instance models incl. +0x28/+0x30) |
| static-instance placements / prop placements | 79,585 / 13,359 |
| batches (all) | 254,623 = 231,547 streamed + 23,076 static |
| vertices / triangles (all) | 12,824,916 / 8,017,133 (10,966,776 if ADC ignored) |
| zero-area triangles among them | 20,452 |
| materials / textures | 9,136 / 9,240 (texture refs 10,195, all valid, no unreferenced texture) |
| VIF unpacks STREAMED | V4-32 231,547; V4-8 231,547; V2-16 227,287; V3-8 67,709 |
| VIF unpacks STATIC | V4-32 24,120; V4-8 13,969; V2-16 13,831; V2-32 9,107; V3-8 1,580 |

Split [C]: streamed 231,547 batches / 11,429,870 vertices / 7,166,168 triangles; backdrops 13,969 / 885,128 / 536,053;
instance + prop models 9,107 / 509,918 / 314,912.

Reconciliation with the signature census (`ps2_asset_census.py`): textures **9,240 = 9,240**. Unpacks: the census
(any STCYCL + UNPACK) finds STREAMED V4-32 231,961 / V4-8 231,908 / V2-16 227,922 / V3-8 68,219 and STATIC 24,244 /
14,069 / 14,119 / V2-32 9,455 / V3-8 1,742, i.e. 0.2-0.6 % (STATIC up to 9 %) more than the structural walk. A strict
scan (`coverage.py`: STCYCL 3/1 or 1/1 followed by UNPACK) finds **every** structurally-reached unpack and only 5
V4-32 / 16 V3-8 / 311 V4-8 / 2 V2-32 unreached hits, all in non-geometry bytes (pixel or collision data, e.g.
AS/M1_V1 STREAMED 0x1E7B0C, EU/M2_V2 0x88D164, US/C2_V1 STATIC 0x669F0) [C `coverage_all.txt`]. So the structures
reach all geometry in both files, and the census excess is signature noise.

ADC check [C]: with restarts honoured, 7,062 batches have an edge > 100 units; ignoring ADC, 58,770 do.

---------------------------------------------------------------------------------------------------------------

## 6. Open questions

render3 (2026-11-02, docs/native-renderer.md §11) answered 1 (no scaling in VU1: the present blit doubles the frame),
2 (type 2 = transposed normal + intensity -> reflection texgen and optional specular alpha; type 1 intensity = Phong
specular strength into the vertex alpha; type 5 @2 = a transposed lighting vector [L]), 3 in part (+0x04 specular
strength, +0x08 exponent; flag 0x20 = variant without back-face culling, 0x200 UV scroll on u, 0x4/0x8 instance alpha
255/128), 4 in part (the instance w column is the instance colour, 128.5 x w), 5 in part (LOD +0x94 = per-collision-vertex
SH irradiance, docs/car-shading.md), 10 (the programs cull by screen winding with the vertex-index parity; flag 0x20
programs and instances do not).

1. Vertex colour scale: RGB bytes are mostly 0x00-0x3F; the VU1 programs presumably multiply (x2/x4, or a per-track
   light colour; ENVIRO.DAT?). Needs the VU1 microcode (`vu_game_0046F0D0` etc.) or a GS capture.
2. Type 2 (44 materials) V4-8 signed attribute and texcoord generation; type 5 V4-32 @2 meaning; type 1 intensity use.
3. Material +0x04/+0x08 and +0x12..+0x1B semantics per type; flag bits 0x1/0x2/0x4/0x8/0x10/0x40/0x400 exactly;
   the alternate program selected by flag 0x20.
4. Instance matrix convention (row vs column) and the meaning of the w column (~0.18 tint?).
5. Unit header +0x10..+0x4F quad and +0x1C float; LOD +0x60..+0x8F lines; LOD +0x94; render-table row bytes 1/2 and
   +0x6C; STATIC +0x5C; STATIC +0x44 per-prop-type byte; prop entry bytes 2/3.
6. STATIC +0x28 backdrop group: drawn by func_0021C010 (index = table +0x6D) under some game condition -- what it is.
   Answered by crash2 (docs/native-renderer.md §12.9): the route's "Barrier" (red X) and "Arrows" (bulb chevron) signs,
   drawn after CB3GameData::Draw unless the track is off, +0x6D < 0, or (IsCrashMode and the logic's slot 0xAC) [C asm].
7. Exact kick/MSCAL sequence (renderer qwords +0x00/+0x10/+0x20 set elsewhere); irrelevant for decoding.
8. Mesh +0x2C = 0xFFFFFFFF and +0x3C = 0x0012BFF4 constants (tool leftovers? runtime fields?).
9. Box at mesh +0x00..+0x3F exceeds vertices for 6 % of meshes -- whether it is meant as a parallelepiped with a
   tolerance, or the tool's OBB fit degenerates.
10. Handedness / winding for back-face culling (GS draws both sides; check whether any VU1 program culls).
