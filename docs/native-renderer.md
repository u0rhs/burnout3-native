# Native renderer: design and feasibility study (roadmap phase 6, 2026-11-02)

Question: how does the port draw Burnout 3 from the game's own scene data with a modern GPU API (any resolution and
aspect, crisp 2D/HUD/text, better textures/filtering/AA, later an uncapped, interpolated frame rate on the unchanged
fixed 1/60 s simulation ticks, docs/frame-rate.md) instead of emulating the PS2 GS and upscaling its command stream?
Desk study plus offline tools only: no game run. Tags: **[C]** seen in code/data (file:line, address,
tool output), **[L]** likely (inference from code), **[G]** guess/estimate.

Sources: decomp `../burnout3-retail-decomp` HEAD 8e81b8b0f9 (read only); librw `../ext/librw/src` (github.com/aap/librw, shallow
clone `18532c2`, MIT, read only, nothing built); disc files under `$B3_WIN/perf/install/game`
(read, never copied). `$B3_WIN` (`%B3_WIN%` in Windows commands) throughout this file: the Windows-side folder that
`WIN` in `local.env` names (`local.env.example`), where the Windows builds go; its `perf/install/game` holds a copy of
the disc's files. New tool: `tools/render/ps2_asset_census.py` (statistics only).

## 1. Recommendation (short)

- **Intercept at the game's own render classes (option d), not at the GS stream, VU1 packets or RenderWare.** Retail
  uses RenderWare only as a thin platform layer: no RpWorld/RpClump/RpAtomic/RpGeometry/matfx/skin is even linked
  (§2.1). Everything the game draws goes through ~25 Criterion renderer classes whose PS2 back ends are small, named
  `*PS2.cpp` translation units, and every game function already has C (4,487 byte-matched + 843 complete C,
  README). The port replaces those PS2 back ends with a GPU back end; the front halves (culling, LOD, sorting,
  material choice) run unchanged as decomp code.
- **Data decoding is tractable:** the disc's TXD/track/car data hold serialized RW PS2 rasters (palettized PSMT4/PSMT8
  with RGBA8888 CLUTs) and pre-built VIF packets (V4-32 position + V2-16 UV + V4-8 colour, V3-8 normals, V4-16 on
  cars); a signature census finds 9,948 rasters and ~1.0 M STCYCL+UNPACK records (§4).
- **GPU API: SDL3 + SDL_GPU** (Vulkan / D3D12 / Metal, Switch through Vulkan), shaders in HLSL through SDL_shadercross;
  bgfx is the fallback if SDL_GPU's feature set blocks something (§5).
- **3D passes are developed offline against scene captures**, one renderer class at a time (§6).
- **Milestone 1: confirmed, with one change:** native 2D/HUD/text at window resolution, captured at the
  `Gt2dRenderer` API (22 functions, ~190 call sites) rather than reconstructed from GS sprites. About **4-5 weeks**
  [G] (§7).

## 2. What the game draws and how (inventory)

### 2.1 RenderWare's part is small

| Fact | Evidence |
|---|---|
| Game code calls 50 RW functions at 182 sites. Top: `_rwDMAOpenVIFPkt` 73, `RwCameraSet{Near,Far}ClipPlane` 9+9, `RpSkyTexCache*` / `RpSkyRenderState*` ~25, then engine/camera/raster/frame calls (create, begin/end update, show raster) | [C] `research/port/game_library_calls.tsv` (direct `jal` scan; misses calls through function pointers) |
| **No** `RpWorld*`, `RpClump*`, `RpAtomic*`, `RpGeometry*`, `RpMatFX*`, `RpSkin*`, `RwIm2D*` symbol exists in the linked RW at all. The linked RW is core (Engine, Camera, Frame, Matrix, Raster, Texture, Image, Stream, Chunk, Plugin) + `RwIm3D*` + the sky2 driver (`RpSky*`, `_rwSky*`, `_rwDMA*`) | [C] the 679 labels of `assembly/asm/rw/renderware.s` |
| `RwIm3D*` is linked but the game calls none of it; it has its own `B3Im3d` with its own VU1 program | [C] tsv; `BeginRendering__6B3Im3dFv` 0x365640 uploads `vu_game_0047A060` |
| A game texture (`CGtTexture`) is an `RwRaster` with the sky2 raster extension (TEX0 at ext+8, TEX1 parts at ext+0x14/0x16, MIPTBP1/2 at ext+0x18..0x24, cache entry at ext+0x58), cached in GS memory by the game's own `CGtTextureCache` over `RpSkyTexCache` | [C] `AccessTexture__15CGtTextureCache` 0x221A70 (`src/nonmatching/AccessTexture__...cpp`, field reads); [L] layout per RW 3.5 sky2 |

So option (c), "RenderWare API level", reduces to camera + raster + texture cache: useful for texture creation and
camera parameters, but no geometry passes through RW.

### 2.2 The game's render classes (Criterion code, all with C)

The E-build link map (`research/E_dnat_symbols.csv`, June 2004 prototype, [L] for retail) has one `*PS2.cpp` file
per renderer. Retail addresses from `research/retail_names.csv` [C names].

| Group | Classes / files (E map) | VU1 microprogram(s) (retail address, size, uploader) [C] | Notes |
|---|---|---|---|
| Frame orchestration | `CB3World::Render(int)` 0x276300, `RenderOnePlayer` 0x2768F0, `RenderTwoPlayer` 0x276870 (split screen = Render(0) + Render(1)), `RenderFrontEnd` 0x275EF0; `CB3GraphicsManager` (PS2: 26 fns) | -- | pass order in §2.3 |
| Track / world | `CB3TrackRenderer` (13 named; `RenderMesh` 0x26AF50, `BeginInstanceRender`, `ReallySetMaterial` 0x26B2F0, `FlushInstances`, SPR cache), `CB3StreamedTrack` (`RenderBeforeVehicles/AfterVehicles/AfterSpecular`, unit frustum tests), `CB3StaticTrack` (`RenderAllBackdrops` 0x21D2E0, `RenderWater`, `RenderStaticObject`), `CB3TrafficSystem`, `CB3PropManager` | **9** material programs from `ReallySetMaterial` (0x46F0D0 5504 B, 0x470650 6480, 0x471FA0 3856, 0x472EB0 5904, 0x4745C0 4944, 0x476130 4224, 0x47E300 5072, 0x4803F0 6624, 0x481DD0 7184); instances 0x479610 2640 (`BeginInstanceRender`) | meshes are pre-built DMA chains from the disc: `RenderMesh` emits a DMA `call` (tag id 5) to the mesh's own packet plus `MSCNT` (VIF 0x17) [C `src/b3/_unclassified/func_0026AF50.cpp`] |
| Cars | `CB3VehicleRenderer` (14: `RenderObject`, `RenderDeformedObject`, `RenderMesh`, `ComputePerObjectData`, `SetupCameraData`, frustum tests), `CB3VehicleDeform` (43 fns, 60 KB: crash deformation), `CB3VehicleReflectivityData`, `CB3CameraData::ConstructEnvMap`, `CB3GraphicsManager::DistortEnvmapBuffersIntoSphereMap` | 0x46BB80 5392 + 0x47F6D0 3360 (`BeginVehicleRendering` 0x220540) | env map rendered per frame and warped into a sphere map [L names] |
| Sky / sun | `CB3SkyDomeRenderer` (12), `CB3EnvironmentManager` (`RenderSky`, `RenderClouds`, `RenderBackdropTexture`, `RenderSunBlob`, `SunOcclusionTest`, `RenderSunCorona`, `...ToEnvironmentMap`), `CB3SunRenderer` (13) | 0x46A590 5616 (sky dome) | sky is rendered into a backdrop target first (`SetBackdropAsRenderTarget`) |
| Particles and effects | `CB3EffectsManager` (47 fns, 60 KB), `CB3ParticleRenderer`, `CB3SparkRenderer`, `CB3DebrisRenderer`, `CB3TrailRenderer` / `CB3TrailSystem` (skid marks, trails), `CB3ScrapeRenderer`, `CB3CoronaRenderer`, `CB3PopcornArray` | particles 0x46D750 6528; sparks 0x46D090 1728; debris 0x475910 2080; trails 0x4771B0 4272; coronas 0x478260 1392; popcorn 0x47C6F0 7184 | |
| Shadows | `CB3ShadowRenderer` (`Begin/EndShadowPlaneRendering`, `RenderAmbientVehicleShadow`), `CB3ShadowMgr` | -- (render-state packets) | |
| Immediate 3D | `B3Im3d` (13: tri list/strip, line list, blend, alpha test, z write, line AA) | 0x47A060 9872 | debug lines, wrong-way walls, misc |
| Post / framebuffer | `CB3Fog` (`ZGreen2FBAlpha`, `GenerateClut`, `RenderIndexedFog`), `CB3Bloom` (down-sample, blur, blend), `CB3RadialBlur`, `CB3DirectionalBlur` (motion blur), `RenderFullScreenSpecular`, `RenderPalOrHdtvContrastFilter`, `DownsampleFramebufferAndGenerateMipMaps`, `Copy{4,8}BitTextureAsPSMT{4,8}H`, `CopyTextureAs32Bit` | -- (GS-direct: sprites over the framebuffer, GS memory reinterpretation) | the PS2 tricks (Z buffer read as a paletted texture for fog, PSMT8H/4H aliasing) become ordinary shaders [L] |
| 2D / HUD / text / menus | `Gt2dRenderer` (22, `Gt2dRendererPS2.cpp`), `CGt2dScene`, `CGt2dFrameHierarchy`, `CGt2dObject*` (Box, Image, Text, ItalicText, Paragraph, Table, Vignette, VideoImage, BezierCurve), `CGt2dFont`, ~40 `CB3HUD*` components, menu components, `CB3MenuPageManager::Render`, `CB3Globe2dObject`, `CGtVideoImage` (movies) | -- (GIF packets through PATH2: `_Dispatch` 0x21A4F0 copies its 512-qw buffer into the RW DMA stream with `_rwDMAOpenVIFPkt(0x80000000, n)` [C]) | positions arrive as floats in screen pixels and are converted to GS 12.4 only at the end (`RenderSpritesTex` 0x219830: `vftoi4` after adding `_gGsOffsetXYXY`) [C] |
| VU0 (not drawing) | `GtIntersection::IntersectFrustum` etc. | 0x4787D0 3648 (`vcallms 0xA18`), 0x4839E0 1312 (`func_001618C0`) | culling math on VU0: stays CPU/host SIMD |

Count [C, from the textbin references]: **20 game VU1 microprograms** across 11 renderers + 2 VU0 programs + 5 small RW
ones (in `renderware.rodata`/`.data`). GS-direct packet builders: **60 functions** open raw VIF/GIF packets with
`_rwDMAOpenVIFPkt` (73 sites) [C scan of `jal` in `assembly/asm/{b3,game}`]; besides the renderers above they include
`CB3MenuPageManager::Render` (2), `CGtVideoImage::SetTileTexture`, `CB3CameraData::SendRegistersToGS`,
`CB3SunRenderer::OcclusionTest`.

### 2.3 Frame order (one player)

From the calls in `CB3World::Render` (`src/b3/_unclassified/B3WorldPS2_00276300.cpp`, matched), in source order [C
calls; L order = execution order]: `OpenViewport` -> environment `BeginRender` -> `BuildCustomCameraData` ->
`ClearOffscreenBuffer` -> backdrop texture, sky, sun blob, clouds -> env-map sun corona data -> sky and sun to the
environment map -> `SendRegistersToGS` -> texture cache swap -> **`RenderBeforeVehicles`** (streamed track) ->
traffic -> **`RenderRaceCars`** -> coronas (`EndCoronaSubmit`) -> **`RenderAfterVehicles`** -> **full-screen
specular** -> `RenderAfterSpecular` -> **shadows** -> pre-fog effects -> **fog** -> game data -> post-fog effects ->
race position, wrong-way walls -> sun occlusion test -> **bloom** -> sun corona -> the **directional blur** or the
radial blur -> `CloseViewport`. (An earlier version of this summary had the bloom and `CloseViewport` before the race
cars; §11.1 and the matched source have this order.) The HUD (`CB3HUDManager::Render`) and menus draw after this
through `Gt2dRenderer` [L].

## 3. Interception level: options

| Level | What it enables | What breaks / is hard | Cost |
|---|---|---|---|
| **(a) GS stream** (a GS emulator) | exact PS2 picture; supersampling; FSR/SMAA on the result | aspect: geometry is already projected to 640-wide raster space; HUD/text: sprites are anonymous after the fact, so a UI layer has to guess them; no MSAA/aniso (no geometry), no new texture filtering beyond GS modes, one picture per guest frame (no interpolation) | a whole GS emulator |
| **(b) VU1/VIF packet level** | real meshes (pre-transform), views from VU1 constants, native resolution and AA | one decoder per microprogram (20) and per input layout; state still arrives as GS registers in the packet stream; programs that clip/cull change what a strip contains; framebuffer tricks stay GS | high, per program |
| **(c) RenderWare API** | raster/texture creation, camera parameters | no geometry passes through RW (§2.1) | small, but covers only textures/camera |
| **(d) Game render functions** | everything: meshes, materials, camera matrices, lights, fog values, effect parameters and the 2D API with float coordinates, named by the decomp; a retained draw list per sim tick, which is what interpolation needs; post effects as shaders | each renderer's PS2 back end is rewritten (~25 classes, the `*PS2.cpp` files: 3-26 functions each); typed layouts needed for the structs they read (today `field_XX` placeholders); pre-built disc packets still have to be decoded once into vertex buffers (the (b) knowledge, but offline, per input layout, not per program) | medium per class, finite list |

**Recommendation: (d), with (c) for textures/camera and (b)'s decoding done once at load time.** The VIF packets in the
track and car files are data, not code: decoding them when the mesh is loaded (or lazily at first draw) gives vertex
buffers; the 20 microprograms are then read only for their *math* (transform, lighting, fog, env-map coordinates),
which becomes vertex/fragment shaders.

## 4. Data decoding

### 4.1 Census of the disc files

`python3 -I tools/render/ps2_asset_census.py <game>/DATA <game>/PVEH <game>/TRACKS` (23 s) [C output]:

| Files | Rasters (signature) | Formats | VIF unpacks after STCYCL (top) |
|---|---|---|---|
| `DATA/GLOBAL.TXD`, `FRONTEND.TXD` (2) | 412 | T8/PAL8 231, T4/PAL4 159, CT32 6, a few with 1-4 mips; 8-512 px | -- (hits are pixel noise) |
| `TRACKS/*/*/STATIC.DAT` (37) | 9,240 | T4/PAL4 8,049, T8/PAL8 1,060, almost all mipmapped (2-5 levels); 128 px 5,734, 256 px 2,707 | V4-32 24,244; V2-16 14,119; V4-8 14,069; V2-32 9,455; V3-8 1,742 |
| `TRACKS/*/*/STREAMED.DAT` (37) | 0 | -- (textures live in STATIC) | **V4-32 231,961; V4-8 231,908; V2-16 227,922; V3-8 68,219** |
| `TRACKS/*/*/ENVIRO.DAT` (37) | 148 | T8 111, CT32 37 (one 1024 px CT32 each) | -- |
| `PVEH/*/CAR*.BGV` (67), `CAR*.BTV` (40) | 67 + 40 | T8/PAL8, 256 px (two 512 px) | V4-32 / V4-16 / V3-8, ~19,000 each in BGV |
| `TRACKS/*/GAMEDATA.BGD` (36) | 0 | -- | -- (game data; but it places the crash ramps and pickups the game draws: §12.9) |

Signatures (in the tool's docstring): a raster is `cType` 4 at +0x20, a valid `cFormat` at +0x23, power-of-two width/height
at +0x0C/+0x10, and a GS TEX0 at +0x3C whose PSM/TW/TH agree; a geometry record is a VIF `STCYCL` followed by an
`UNPACK`. Both are [L] heuristics. Check against the TXD directories (true counts): 193 of GLOBAL.TXD's 194 entries
and 219 of FRONTEND.TXD's 221 are found [C], so the raster column slightly undercounts (non-power-of-two or other
raster types are skipped). The 9,948 total in §1 also includes 41 rasters in `DATA/*.BIN` (LOADSCRN etc.).

### 4.2 Textures

- **Container** [C, GLOBAL.TXD bytes]: header `{u32 0x543C0000, u32 id 0xBCDEED81, u32 count 194, u32 dir 0x10}`,
  then `{u32 index, 0, u32 offset, 0}` per entry. Entry 1 at 0xC40 is a serialized `RwRaster` with offsets for pointers:
  cpPixels 0x250, palette 0x3D0, 16x16, depth 4, `cType` 4, `cFormat` 0x45 (PAL4 | 8888), and at +0x3C TEX0
  `0x2000000511408000` = PSMT4, TBW 2, TW=TH=4, TCC 1, CLD 1; the name "16_curve" at +0xA8. Track and car files embed
  the same raster structs (census).
- **Pixels** [C bytes; L meaning]: cpPixels points at a GIF image-transfer packet (BITBLTBUF/TRXPOS/TRXREG/TRXDIR as
  A+D, registers 0x50-0x53, then IMAGE data). 4/8-bit rasters are uploaded in a 32-bit-swizzled layout (rows with
  gaps in the dump), i.e. sky2's "new style" rasters; **librw `src/ps2/ps2raster.cpp`** has the matching code:
  `unswizzleRaster` (:1371), `convertCSM1_16/32` CLUT reorder (:1509/:1524), TEX0/TEX1 decode (`printTEX0`/`calcTEX1`,
  :1905-:2001), `readNativeTexture` for the RW stream form (:2004) [C file].
- **Alpha**: CLUT alpha bytes run 0x00-0x80 with 0x80 = opaque (palette dump of entry 1: 0x3F, 0x45 ... 0x7F) [C bytes];
  convert `a8 = min(255, a * 255 / 128)` at load, except where a blend uses alpha above 1 (GS alpha may exceed 0x80) [L].
- **Plan**: decode every raster at load into RGBA8 (or keep palette + index and decode in the shader for palette
  animation, if any material swaps CLUTs; `Copy*AsPSMT*H` suggests CLUT tricks [G]), build GPU mips from the stored
  levels, and optionally substitute modded/upscaled textures keyed by content hash.

### 4.3 Geometry

- **Track** (STREAMED/STATIC): per vertex V4-32 (float x, y, z + w = ADC/flag word), V2-16 UV (signed 4.12 fixed), V4-8
  RGBA prelight (alpha 0x80 = 1), plus V3-8 normals (s8 / 127) for the specular world programs [C census,
  docs/track-format.md]. Strips with ADC bits as restarts [C docs/track-format.md]. The packets are reached through the
  mesh struct (`RenderMesh` reads its DMA address at +0x0C and an extra chain at +0x1C [C func_0026AF50.cpp]).
- **Cars** (BGV/BTV): V4-32 + V4-16 + V3-8 per batch [C census]; the meanings (position, UV + damage influences,
  normals) are [G] until the car loader's C is read; `CB3VehicleDeform` (43 functions, C) computes the deformation
  on the CPU.
- **Approach**: walk the packets once per mesh at load (librw `src/ps2/ps2.cpp` has a general VIF unpack/instance
  reader for RW's own PS2 layouts, 1,586 lines [C]; our layouts differ, so it is a reference, not a reader we call), emit
  indexed triangle lists, cache by content hash. Disc packets are immutable, so this can also be an offline
  conversion step for mods.

## 5. GPU API / platform layer

| Candidate | Win | macOS (Apple Silicon) | Linux | Android | iOS | Switch | Notes |
|---|---|---|---|---|---|---|---|
| **SDL3 + SDL_GPU** | D3D12 / Vulkan | Metal | Vulkan | Vulkan (some devices) | Metal | Vulkan | backends per SDL's docs [C web: SDL_GPU docs, "Vulkan: Windows, Linux, Nintendo Switch, and certain Android devices", D3D12 incl. Xbox GDK, Metal macOS 10.14+/iOS 13+]; zlib licence; one dependency for window, input, audio, GPU; shaders via SDL_shadercross (HLSL -> SPIR-V/MSL/DXIL) [L]. Limits: modern-subset API (no geometry shaders, no occlusion queries [L]) -- none needed here |
| bgfx | D3D11/12, Vulkan | Metal | Vulkan/GL | Vulkan/GLES | Metal | NDA backend | mature, BSD-2; own shader language and build tool; heavier integration [L] |
| sokol_gfx | D3D11 | Metal | GL | GLES | Metal | none | tiny, no Vulkan/D3D12 backend [L]; fine for tools |
| Vulkan + MoltenVK | Vulkan | MoltenVK | Vulkan | Vulkan | MoltenVK | Vulkan | most control, most code; Apple via translation [L] |
| wgpu / Dawn | yes | yes | yes | yes | yes | no | WebGPU model, large dependency, no console path [L] |

**Recommendation: SDL3 + SDL_GPU** (already the README's leading candidate). The renderer's needs are modest by
modern standards: a few thousand draws per frame at most, ~10 k textures, simple shaders, a few full-screen passes.
Our renderer sits behind a thin `b3gfx` layer of our own (buffers, textures, pipelines, passes) so a bgfx or raw Vulkan
backend stays possible.

## 6. Interception and verification plan

### 6.2 What is intercepted

| Layer | Functions (retail) | How |
|---|---|---|
| 2D | the 22 `Gt2dRenderer` functions (0x217850-0x21A4F0): Begin/End/Suspend/Resume, SetTexture x2, SetBlendMode, SetTextureFilterMode/AddressMode, Set/ResetClipRegion, EnableZTest/SetZ, Render{TriStrip,TriList,Sprites}{Flat,Gouraud}{Tex,Untex}, _Dispatch; plus the direct 2D packet builders `CB3MenuPageManager::Render`, `CB3Globe2dObject::_Render`, `CGtVideoImage::SetTileTexture` | wrapper overrides that record the call (state, float positions, UVs, colour, texture = guest `RwRaster` address) and then run the original/native body, so guest memory and the GS stream are unchanged |
| 3D | per class: the `*PS2.cpp` entry points (`CB3TrackRenderer::RenderMesh/ReallySetMaterial/BeginInstanceRender/...`, `CB3VehicleRenderer::RenderObject/RenderDeformedObject/SetupCameraData`, sky dome, particles, ...) | the same record-then-run wrappers; the recorded stream (draw list + the referenced meshes/textures/materials) is the scene capture |
| Camera, lights, fog | `CB3CameraData::Construct(RwCamera*)`, `BuildCustomCameraData`, `ReallySetupCameraData`, `CB3Fog::Render(CGtV3d,float,float)`, `CB3ParticleArray` / directional light setters | read the structs at entry through `gp<T>` with typed layouts |

Recording wrappers never change guest state, so every feature in its default state passes the regression by
construction [L]. The native records are host-only.

### 6.3 Scene state from guest memory

The structs the renderers read are mostly `UnkStruct_XXXX` / `field_XX` placeholders in the decomp today (e.g.
`CB3Viewport` +0x58 = `RwCamera*`, +0x84/+0x88 floats [C `B3GraphicsManager_0019EE40.cpp`]). Each renderer moved to the
native side needs its structs consolidated into typed headers (`include/b3/classes/`, the decomp's header rounds) with
`static_assert` offsets. RW structs come from the RW 3.5 headers [L for 3.6]. This is the main
per-class cost besides the shaders.

### 6.4 Verification

1. **Offline, exact**: per capture, a reference rasterizer at 640x448 [L: the retail frame size; to confirm from
   `SetVideoModeParams`' `RwSkyVideoMode` or the stream's DISPLAY registers] with GS rules (point sampling, the game's
   blend equations) compared with the GS frame of the same capture: per-pixel diff with a tolerance, per pass.
2. **Offline, quality**: the same capture at 4K / 21:9 / MSAA: a visual review image set.
3. **Interpolation** (later): the simulation runs fixed 1/60 s ticks, N ticks per displayed frame (2 per frame only in
   crashes and split screen; 1/30 s ticks only in the front end) [C docs/frame-rate.md "The main loop"]. The native
   renderer keeps the draw lists recorded at the last two **60 Hz ticks**, matches objects by identity (mesh key +
   owner address) and draws at display rate with transforms and camera interpolated between those ticks; the
   simulation (and the guest's own rendering) is unchanged [L]. Particles
   and 2D hold or extrapolate per class. (Corrected 2026-11-02: an earlier version said "30 Hz".)

## 7. First milestone and the order of the rest

### 7.1 Milestone 1: native 2D/HUD/text (confirmed, adjusted)

Why it is the right first step: the 2D API is small and fully known (22 functions, float coordinates, explicit state),
it fixes the most visible problems (text breaking with widescreen/high-res UI, blurry upscaled UI), it needs no
geometry decoding, and it builds the pieces every later milestone needs (presenter, texture decode, record stream,
capture + diff tooling).

| Step | Content | Estimate [G] |
|---|---|---|
| 1a | Record wrappers for the 22 `Gt2dRenderer` functions + the 3 direct 2D builders; host-only record stream; capture to file in a headless run | 3-4 days |
| 1b | Texture decode library (`RwRaster` + sky2 ext from guest memory: PSMT4/8 unswizzle, CLUT CSM1 reorder, alpha 0x80) with GLOBAL/FRONTEND.TXD as offline test vectors; librw's `ps2raster.cpp` as reference | 3-4 days |
| 1c | `b3gfx` on SDL_GPU + an offline 2D capture viewer at any resolution; a small software reference for Gt2d's primitive subset at 640x448 and a diff against the capture's GS frame | 4-5 days |
| 1d | Layout v1: uniform scale with a 4:3 safe area (pillarbox), then edge anchoring per HUD component (the `CB3HUD*` classes and their `CGt2dFrame` roots); then a check in a window | 4-5 days |

**Total: ~4-5 weeks of work** [G]. Gate: offline diff at 640x448 within tolerance for menu, race HUD and crash
HUD captures; the regression unchanged (records are host-only); a session in a window.

Honest limits of milestone 1: glyphs are bitmap rasters (font TXD), so text at 4K is sharp-edged and correctly placed
but not more detailed than its texture; truly crisp text needs higher-resolution glyph art or a distance-field
rebuild of the font atlas (follow-up, ~1 week [G]).

### 7.2 After milestone 1 [G sizes]

| # | Milestone | Content | Size |
|---|---|---|---|
| 2 | Capture + texture/material core | full scene capture format (draw list + meshes + textures + camera), material/blend translation of the game's GS state (ALPHA/TEST/TEX1/CLAMP as set by the renderers), texture streaming from `CGtTextureCache` | 2-3 weeks |
| 3 | Sky, backdrops, water | `CB3SkyDomeRenderer`, `CB3EnvironmentManager`, `RenderAllBackdrops`/`RenderWater` (1 VU1 program); first native frame layer in captures | 2 weeks |
| 4 | World | `CB3TrackRenderer` + streamed/static track: packet decode, 9 material programs as shaders, instances, traffic, props, lights/prelight, specular programs | 5-6 weeks |
| 5 | Cars | `CB3VehicleRenderer` (2 programs), deformation (CPU, decomp code), env map + sphere-map warp (upgrade path: cube map), glass, damage | 4-6 weeks |
| 6 | Effects | particles, sparks, debris, trails/skids, scrapes, coronas, popcorn, sun/flare + occlusion, shadows, `B3Im3d` (8 programs) | 4-6 weeks |
| 7 | Post | fog (depth), bloom, radial and directional blur, full-screen specular, contrast filter, split screen | 2-3 weeks |
| 8 | Interpolated frame rate | two-tick draw lists, identity matching, camera interpolation, per-class policies | 3-4 weeks |
| 9 | Front end + remaining frame types | menus with 3D (showroom), loading, movies (IPU frames as textures) | 3-4 weeks |

Rough total to a fully native renderer: **7-9 months of work** [G], parallelisable by renderer class after
milestone 2.

## 8. Risks and unknowns

| Risk / unknown | Level | Settles it |
|---|---|---|
| **GS-semantics effects**: fog reads Z as a paletted texture (`ZGreen2FBAlpha`, `GenerateClut`), PSMT8H/4H aliasing copies, downsample chains, sun occlusion by reading back pixels -- each needs its *intent* recovered from the C before it becomes a shader | High (effort) | reading the C per effect in milestone 7; captures as reference |
| Typed struct layouts for the render classes (placeholders today) | High (effort) | header rounds in the decomp per class, `static_assert` |
| VU1 math fidelity (lighting, fog factor, env-map UVs, specular) when moved to shaders: 20 programs to read | Medium | per-program reference: run the program in a VU1 interpreter on captured input and compare vertices |
| Car vertex layout (V4-16 meaning, damage influences) | Medium | read the car loader / `CB3VehicleRenderer::RenderMesh` C |
| Frame size and display mode (640x448 assumed; interlace/field rendering) | Low | `SetVideoModeParams` C or the stream's DISPLAY/SMODE registers |
| 2D drawn into offscreen targets or before 3D (front end, radial-blur 2d object) | Low-medium | count in captures; stays GS in milestone 1 |
| Timing: recording wrappers in hot render paths (Gt2d ~190 sites, track meshes thousands per frame) | Low | host-side buffers; measure in a headless run |
| HUD anchoring needs a per-component anchor table (no anchors in the data) | Low | derive from each component's frame position (left/right/centre third), user review |

## 9. Artefacts

- `tools/render/ps2_asset_census.py`: texture/VIF census of the disc files (statistics only; run with `python3 -I`).
- VU1 microprogram -> uploader table: §2.2, from `assembly/assets/vu/*.textbin.bin` and the `%hi/%lo(vu_game_*)`
  references in `assembly/asm` (decomp, read only).
- Prior art read: librw `18532c2` `src/ps2/` (ps2raster.cpp, ps2.cpp).
- Foundation built on this study (decoders, b3gfx, b3view): §10.

## 10. Foundation built (render2, 2026-11-02)

Offline only: nothing here runs the game. Code: `include/b3asset`, `src/b3asset` (decoders), `include/b3gfx`,
`src/b3gfx` (GPU layer), `src/b3view` (viewer), `tools/render/b3assetdump.cpp`, `tests/test_b3asset.cpp`; build and
dependency notes in `tools/render/README.md`.

### 10.1 What decodes (whole install, `b3assetdump census`, Linux and the Windows build give identical totals) [C]

| Data | Result |
|---|---|
| Textures | **9,843 of 9,843** rasters: GLOBAL.TXD 194 + FRONTEND.TXD 221, STATIC.DAT 9,240 (= the track texture tables), ENVIRO.DAT 148, DATA/*.BIN 40; 8,941 track textures carry 2-6 stored mip levels, each level's mean colour within 19/255 of level 0 |
| Cars | **107 of 107** files (67 BGV + 40 BTV) at LOD 0-3, each with its N paint palettes; LOD 3: 12,915 batches, 591,463 vertices, 345,779 triangles |
| Tracks | **37 of 37** routes: 82,008 meshes (77,219 streamed high + 4,789 backdrop/water), 56,918 LOD meshes, 79,585 instance + 13,359 prop placements, 9,136 materials; 10.19 M vertices / 6.11 M triangles (high detail, all parts, lines included). Batches reconcile exactly with the structural spec: streamed high 161,770 + LOD 69,777 = 231,547, backdrops 13,969 |

### 10.2 Textures

Every texture in the game's files is a serialized `RwRaster` + sky2 extension (table in `include/b3asset/raster.h`)
whose `originalPixels` (+0x24) points at the raster's own **upload DMA chain** (DMAcnt + GIF BITBLTBUF, DMAref of GIF
TRXPOS/TRXREG/TRXDIR + IMAGE packets, relative addresses) [C bytes]. The decoder replays that chain into a model of GS
local memory (`gs_memory.h`: page/block/column addressing for CT32/24/16/16S, T8, T4, T8H/T4HL/T4HH) and samples the
result with the stored TEX0, MIPTBP1/2 and the CLUT at `palOffset` (+0x44; CSM1 = index bits 3/4 swapped). This
reproduces exactly what the GS sees, so RW's "swizzled" forms (PSMT8 uploaded as CT32 and PSMT4 as CT16 at half size,
palettes folded into the last level's transfer) need no special cases. Checks [C `tests/test_b3asset.cpp`]: GS
T8-from-CT32 and T4-from-CT16 reads equal librw's independent `swizzle()` for 128-256 px textures; a synthetic raster
with a CSM1 palette round-trips; real files decode with visual checks (logos, maps, fonts, track textures).

- Counts: the transfer count is the first word of the upload block; when the palette rides in the last level's transfer
  the second word is one higher and its extra chain entry holds a stale pointer [C GLOBAL.TXD "ramp"].
- Alpha: 0x80 = opaque, mapped to 0xFF; values above 0x80 clamp (the `max_alpha` field reports them) [C bytes].
- Raster tail: +0xA0 u8 = palette count (car paint variants; `CB3RaceCar::Prepare` clamps the colour index with it),
  +0xA8 = name [C].
- Car atlases keep a **mask in alpha** (about half the texels below 0.25, not coverage): car draws ignore texture
  alpha; glass is blended with the entry colour's alpha byte (0x34 of 0x80) [C data; L meaning].

### 10.3 Geometry

Both readers run the mesh packets through `VifInterpreter` (`vif.h`), a VIF1 model writing VU1 data memory
(STCYCL skipping and filling writes, STMOD offset/difference, STROW/STCOL, STMASK, sign/zero extension, ITOP,
TOPS-relative addresses taken from 0); each MSCAL/MSCNT (or end of packet) hands the memory and ITOP to the reader,
which reads the attributes the VU1 program would. Strips: vertex i >= 2 closes (i-2, i-1, i) unless its position w has
the ADC bit 0x8000; winding alternates from each restart.

**Cars** (`car.h`) [C `CB3VehicleRenderer::FixUpVehicleData` 0x220B70 + data]: LOD blocks at +0x4C, atlas raster at +0x60, panel frames +0xD00 (count +0x0C), wheel frames +0xB80
(count +0x0D); per LOD 18 lists (L0 body, A0-A5 panels drawn at their frames, B0-B2 wheels at the wheel frames, C0-C7
unused); entry = mesh, continuation mesh, material word, colour word. **Car V4-16 (unsigned, @1) = texture coordinates
and deformation weights interleaved per group of four vertices**: vertex g+j reads its UV (1/4096 of the atlas) from
slot g + (j odd ? 2 : 0), lanes (j < 2 ? 0,1 : 2,3), and its two influences (u16 = weight << 8 | zone << 2) from slots
g+1 and g+3, lane j; wheels use STCYCL 6/1, so only the UV slots are written [C decode = correct pictures; L meaning of
the influence fields]. V3-8 @2 (STMOD offset, ROW = 65536.0f): normal = (b - 128) / 127. B0/B1/B2 are the wheel models
by blur state (wheel speed > 25 / > 50 picks B1 / B2) [L decomp `RenderVehicle` draft, src/nonmatching].

**Tracks** (`track.h`; byte-level spec from `CB3StaticTrackData::FixUp` 0x15E8E0, the streamed-data FixUps
0x15F0D0/0x15F200, `CB3TrackMaterial::FixUp` 0x26A170, the render functions, and a structural prober over all routes;
full spec with citations in `docs/track-format.md`, highlights here):

| Part | Layout |
|---|---|
| STATIC.DAT header | version 0x27, size; materials +0x08 (0x28 B, count +0x0C); texture table +0x18 (count +0x16, file offsets); backdrop tables +0x24/+0x28/+0x2C/+0x30 (counts +0x1C..); instance types +0x38 / prop types +0x3C (0x50 B, counts +0x34/+0x36); prop placements +0x48 (count u16 +0x40) listed per unit by +0x4C/+0x50; unit table +0x58 (count +0x54) [C FixUp] |
| Material | +0x00 type 0-7 (VU1 program + vertex layout), +0x0C texture list (entries are offsets relative to the material), +0x11 texture count, +0x20 GS TEST_1, +0x24 flags (0x20 = alternate program), +0x26/+0x27 GS ALPHA_1 and FIX [C]. b3asset maps ALPHA 0x44 or FIX < 0x80 to blending, TEST ATE + GEQUAL/GREATER to alpha test with AREF |
| STREAMED.DAT | no header; per unit a high block then a LOD block (offsets/sizes from the unit table); mesh array at +0x50 (count) / +0x54 (offset rel. +0x50); high block render table at +0x60: slot -> material at +0x74, rows of {delta, 2 bytes, hiMesh[83], lodMesh[83]} at +0x11A + 0xA9 r; the delta-0 row maps the unit's own meshes [C prober: every mesh mapped once]; LOD block +0x98/+0x9C: instance placements per type |
| CB3TrackMesh (0x40) | bounding box (4 qw, frustum-tested on VU0); DMA chain 1 at +0x0C and optional chain 2 at +0x1C (relative to the mesh), each one DMA RET packet [C] |
| Batch | STCYCL 3/1; V4-32 @0 (xyz + ADC); @1 UV V2-16 signed / 4096 (world) or V2-32 float (instances); @2 RGBA V4-8 (types 0/6), RGB V3-8 (types 1/2) or V4-32 (type 5, unknown); type 1 adds signed V4-8 normals + intensity, transposed per 4 vertices at qw 204; ITOP = vertex count (<= 80) [C] |
| Coordinates | Y up, metres [L]; streamed and backdrop meshes in world space; instance/prop models in object space placed by 0x40-byte matrices (rows X, Y, Z, translation; row-vector convention [L, confirmed visually: trees and poles stand upright]) |

Seven vertex layouts occur, one per material type (0/6, 1, 2, 3 lines, 4, 5 and the untextured variants); counts in
the census (`batch layout <pos>/<@1>/<@2>[/n]`).

### 10.4 b3gfx and b3view

- **b3gfx** (`include/b3gfx/b3gfx.h`): backend-neutral interface (opaque ids), one SDL3 SDL_GPU backend. Static meshes
  (40-byte vertex = `b3asset::Vertex`), RGBA8 textures with stored or GPU-generated mips, anisotropic trilinear
  sampling, opaque / alpha-test / blended pipelines, **reverse-Z** infinite projection with D32F (km-scale tracks),
  MSAA resolved into the swapchain, **uncapped presentation** (IMMEDIATE, else MAILBOX, else VSYNC), and an offscreen
  target with read-back.
- **Shaders**: GLSL 450 -> SPIR-V (glslangValidator 16.2) -> MSL (spirv-cross), embedded in
  `src/b3gfx/shaders_gen.h` by `tools/render/build_shaders.sh` (committed; builds need no shader tools; it records each source's SHA-256 and `make test` checks them: `embed_shaders.py --check`). SDL_GPU
  backends: Vulkan (Windows, Linux) with SPIR-V, Metal (macOS) with MSL. D3D12 needs DXIL, which is not generated (no
  DXC here); Windows uses SDL_GPU's Vulkan backend. Adding DXIL later = SDL_shadercross or DXC in `build_shaders.sh`.
  Simplest robust route chosen over SDL_shadercross: no runtime shader compiler, two small tools at build time only.
- **SDL3 3.4.2** from package managers: Ubuntu 26.04 debs (Linux) and the MSYS2 mingw64 package (Windows), unpacked
  into `../ext/` with recorded hashes (`tools/render/README.md`). macOS: Homebrew `sdl3` (not built here).
- **b3view**: own window, free camera (right mouse look, WASD/QE, Shift, wheel), any window size/fullscreen,
  title shows fps / frame time / size / MSAA / present mode. Tracks start at stream unit 0 (N/B jump between units,
  O overview, T LOD meshes); cars: P paint, 0-3 LOD. `--offscreen out.png` renders without a window (SDL's offscreen
  video driver), which is how it was checked here: cars and tracks on WSL (Mesa Vulkan) and on Windows (the real GPU,
  Vulkan, 1920x1080 and 2560x1080, 4x MSAA).

User command (Windows): `%B3_WIN%\b3view\b3view.exe <path>`, e.g.
`b3view.exe %B3_WIN%\perf\install\game\TRACKS\AS\C1_V1` or `...\PVEH\COMP\CAR1.BGV`
(build: `tools/render/build_win.sh`; Linux/WSLg: `make render && tools/render/b3view.sh <path>`).

### 10.5 Viewer approximations (render2; superseded for tracks by §11)

render2's first cut: no sky, sun, fog, env maps, specular, shadows, effects or post; one directional N.L light; track
prelight scaled x2 on top of 0x80 = 1.0 [G]; type-2 materials untextured; type-3 lines skipped; only the main
instance model; no culling. Since render3 (§11) tracks draw with the game's own shading model; cars still use this
simple path in b3view (their model exists as C++, §11.6).

### 10.6 What the decoders cannot explain yet (input for the capture/materials milestone)

Status after render3 (2026-11-02; evidence in §11): 1 answered, 2 answered (type 5's per-vertex lighting read, not
run-checked), 3 partly (+0x04/+0x08, flags 0x20/0x200/0x4/0x8/0x1 answered), 4 partly (instance w column = colour;
LOD +0x94 = SH irradiance), 5 answered by the car model (§11.6), 6 answered, 7 open.

1. **Vertex colour scale**: track RGB bytes sit mostly in 0x00-0x3F; the track VU1 programs (0x46F0D0, 0x470650, ...)
   must scale them (x2? x4? a per-track light colour, ENVIRO.DAT?). Read the microcode or compare with a GS capture.
2. **Type 2** materials (44): textured but no UV; a signed V4-8 attribute @1 presumably drives texture-coordinate
   generation (env/projection mapping) in its program. **Type 5** V4-32 @2 (instances "dynamically lit"): unknown.
   **Type 1** normals + intensity: specular/env-map strength; the program's math (and the env map from ENVIRO.DAT's
   rasters, not yet linked to anything) decides the look.
3. Material fields +0x04/+0x08 (per-type floats), +0x12..+0x1B (animation: frame count/time [G]), flag bits other
   than 0x20; which program flag 0x20 selects and why.
4. Instance matrix w column (~0.18 per row: tint/light? [G]); the per-unit quad (+0x10..+0x4F) and the LOD block's
   boundary lines (+0x60..+0x8F), +0x94 (irradiance?); render-table row bytes 1-2; STATIC +0x28 backdrop group (drawn
   by `func_0021C010` under some condition), +0x44 per-prop-type byte, +0x5C.
5. Cars: material bits 2-7 are lights (brake, head, indicators: toggled by `lx16LitBits` in `RenderVehicle`), 0x100/
   0x200 glass, 0x400000 vs 0x600000 in wheel lists; glass state changes the entry's colour bytes (cracked 0x48,
   broken 0x80 [L decomp draft]); the env-map/reflectivity use of the atlas alpha; deformation uses the influences
   with zone matrices that `CB3VehicleDeform` computes on the CPU.
6. Handedness/winding for back-face culling: no program has been checked for culling; b3view draws both sides.
7. Mesh bounding boxes enclose their vertices for only 94 % of meshes [C prober]; b3asset recomputes bounds.

## 11. Game shading recovered from the VU1 microprograms (render3, 2026-11-02)

Offline only: nothing here ran the game. The game's own VU1 microprograms (decomp `assembly/assets/vu/*.textbin.bin`,
read only) were disassembled (`tools/render/vudis.py`; listings are game-derived and stay in `build/vu/`), read
together with the EE code that feeds them, and **run in a VU1 interpreter on real data**; the recovered math is a C++
model (`include/b3asset/trackshade.h`, `carshade.h`, `enviro.h`) that the GPU shaders implement. Car and sky details
live in `docs/car-shading.md` and `docs/sky-enviro.md` (written by render3's sub-investigations); this section is the
summary with the track material model. Tags as in the rest of this document.

### 11.1 The frame model (why track colours sit at 0x00-0x3F) [C]

- **Nothing scales the vertex colour.** The type-0 program 0x46F0D0 stores the unpacked RGBA qword unchanged as the
  GIF RGBAQ (`lq.xyzw vf11, 2(vi01)` ... `sqi.xyzw vf11`, 0x0059/0x0076) [C listing + interpreter: 1,434,453 RGB lanes
  equal]. Track rasters carry TEX0 TFX = MODULATE, TCC = 1 [C all track textures], so the GS writes
  `Ct * Cf >> 7` (0x80 = 1.0) into the frame: track scenery is drawn at about half intensity.
- **The present blit doubles the frame.** `CB3GraphicsManager::PresentDrawBuffer` 0x2270B0 -> `CBlit::BuildPacket`
  0x22B540 draws the render buffer (PSMCT24) into the display buffer as a textured sprite, TFX = MODULATE, vertex RGB =
  `luBrightness = min(255, int(0.5 + 256 * brightness))` with brightness 1.0 by default (`B3GraphicsManager_0019EBF0.cpp:144`)
  -> every channel x255/128, clamped [C matched + nonmatching draft; TEX0 decoded: PSM 1, TFX 0]. Its alpha
  `128 - 96 * field_264` blends with the previous display (motion blur, ALPHA_2 0x44).
- **Frame order** (`CB3World::Render` 0x276300, `B3WorldPS2_00276300.cpp:117-200` [C]): sky components, sun blob,
  clouds -> track before vehicles -> traffic, race cars -> track after vehicles -> **full-screen specular** -> track
  after specular -> shadows, pre-fog effects -> **fog** (`CB3Fog::Render`, a post pass) -> game data, post-fog
  effects -> sun occlusion -> bloom -> sun corona -> directional blur -> ... -> present.
- **Full-screen specular** `RenderFullScreenSpecular` 0x2274B0: a sprite of colour `min(env specular colour * 127.5 *
  env +0xDC, 255)` with ALPHA 0x58 = `Cs * Ad + Cd`: every pixel gains that colour times its *frame alpha* / 128 [C].
  The frame alpha is what the track (and car) programs write as vertex alpha (x texel alpha): type 1 writes Phong
  specular there, opaque materials carry 0 (no sheen) or 0x80 (flag 0x4: full sheen), cut-outs a small value [C data,
  `b3vucheck stats`].
- Fog, sky and the ENVIRO.DAT parameters: §11.5.

GPU equivalent (b3gfx): an RGBA16F "GS frame" at half intensity whose alpha holds As/128; tracks shade into it with the
GS rules, the specular pass is a full-screen triangle blended `DST_ALPHA, ONE` (alpha untouched), and the present pass
multiplies by 255/128 and clamps (`src/b3gfx/shaders/present.frag`).

### 11.2 Track materials: what each program computes [C, checked §11.4]

Constants (VU1 qwords) [C ReallySetMaterial 0x26B2F0, RenderMesh 0x26AF50, ReallySetupCameraData 0x26BAA0,
CB3CameraData::Construct 0x225B30, PrepareRenderStatesForRenderingTrack 0x26B8E0 (asm `unit_0026B8E0.s`)]:

| qw | Content |
|---|---|
| 1005 | x: UV-scroll on (flag 0x200), y: `1 - frac(t)` added to **u** of every vertex (program 0x0038-0x0054 rewrites the UVs in place) |
| 1006 | GIF tag: PRIM 0x4C (tri-strip, Gouraud, ABE) \| TME, regs ST, RGBAQ, XYZF2 |
| 1007 | renderer +0x30 = -(env +0xB0[light]) = minus the **to-sun** vector, w = 10 (types 1/2: material +0x08) |
| 1008 | renderer +0x40 = camera position + (0, 10, 0), w 1 (type 1: material +0x04); type 2: renderer +0x50 = camera position |
| 1009 | type 1: x = 4.0 (alpha floor); type 2: camera right .x/.z, z = (material +0x04 > 0) |
| 1013 | x: clip mode from RenderMesh (0 fast, 2 guard-band test, 10 full clipping), y: program's path cache, z: RenderMesh's last argument (specular off), w: "camera constants changed" |
| 1014-1017 | RW camera view matrix rows widened (x, y, z, z): view x/z, y/z in [0, 1] over the view window |
| 1018 / 1019 | vu1DataXYZScale (w, h, zScale; w lane = -255/(far - fogPlane)) / XYZShift + Offset3D (w lane = far) [C + RW 3.1 skyinst.c:2314] |

Per material type (program; flag 0x20 picks the second program, which is the **same code without the back-face test**):

| Type | Program | Per-vertex output |
|---|---|---|
| 0, 6 | 0x46F0D0 / 0x4745C0 | RGBA = input bytes; STQ = (u, v, 1) / w; screen = shift + scale (v/w - 0.5); depth Z = zShift + zScale / w; F unused (PRIM without FGE) |
| 1 | 0x470650 / 0x472EB0 | as type 0, plus a pre-pass writing **alpha = max(4, intensity x 2 strength x max(R.V, 0.01 exponent)^exponent)** with L = -to-sun, R = 2(n.L)n - L, V = unit(vertex - (camera + 10 up)), strength = material +0x04, exponent = +0x08, intensity = the s8 4th lane of the normal block; the power is the float-bits log/exp trick `bits(p) = e bits(s) + bits(2 strength) - e bits(1.0)` (0x0067-0x00CD). 1013.z set -> alpha 0 |
| 2 | 0x481DD0 / 0x4803F0 | the V4-8 @1 attribute is a **transposed normal + intensity** (like type 1's block, in the UV slots). Reflection texgen: D = vertex - camera, R = D - 2(n.D)n, **u = 0.5 + (R.xz . right.xz)/4 / \|R.xz\|** (fast rsqrt by float bits), **v = 0.5 - 0.5 R.y / \|D\|** (lit path) or with the fast rsqrt of \|R\|^2 (unlit). Lit path (+0x04 > 0 and 1013.z = 0): alpha = intensity x the type-1 power term with the eye at the camera, no floor; unlit: alpha 0 |
| 3 | 0x476130 | AA lines (PRIM 0xE9, FGE): RGBA = input, **F = clamp(255 (far - w)/(far - fogPlane))** (RW linear fog from 1018.w/1019.w) |
| 4 | 0x471FA0 + VU0 0x479610 | per-instance RGBA built on VU0: **RGB = ftoi0(128.5 x the w lanes of the instance matrix rows X/Y/Z), A = 255 / 128 / 4 for material flags 0x4 / 0x8 / neither** (VU0 0x00CA-0x00D6; VU1 sends it as RGBAQ, 0x0073/0x0078) [C code read, not run-checked]. Data: w lanes 0.11-0.38 -> 14-49, the same range as the track prelight |
| 5 | 0x471FA0 + VU0 0x479610 | per-vertex lighting: the V4-32 @2 attribute is a transposed vector, dotted with the light direction moved into instance space (VU0 0x00D7-0x0115: rows normalised, `0.5 x mean(w)` as ambient/diffuse scalars in the z lanes of the matrix qwords) [L: read, not run; b3view draws types 4/5 alike] |
| 7 | 0x47E300 | not used by any track |

Back-face culling (programs without flag 0x20, types 0/1/2/6) [C 0x46F0D0 0x0076-0x0086]: the screen-space cross
product of the last two edges; its z sign (MAC flag 0x20 read by FMAND, added to 0x7FE0) sets ADC; the edge order
alternates with the **parity of the vertex index** (not with strip restarts). Emitting triangle i as (i-2, i-1, i) for
even i and (i-1, i-2, i) for odd i, the programs keep exactly the triangles whose normal (b-a)x(c-a) faces the eye
(full-install run: 834,617 kept facing / 2 culled facing; 221,685 culled away-facing / 4 kept) [C]; floors face up in
that order 14:1 [C data] -> on the GPU: right-handed view, front = CCW, cull back. Lines and instances never cull.
`b3asset::strip_to_triangles` now follows this rule and also keeps the triangle after a single ADC flag (the GS draws
it; render2's reader dropped it: +4.4 % triangles, AS/C1_V1 107,726 -> 112,868).

GS state per material (ReallySetMaterial): TEST_1 = material +0x20, ALPHA_1 = +0x26/+0x27, TEX0 MODULATE + TCC. The
vertex alpha is part of the material: cut-outs (TEST 0x5000D = alpha GREATER 0) carry small non-zero alphas, so
`At * a >> 7 > 0` sets the cut threshold per material; blended ones (ALPHA 0x44) carry the blend amount; FIX blends
(ALPHA 0x64, FIX < 0x80) blend by FIX [C data, `b3vucheck stats`]. The shader evaluates exactly these rules.

### 11.3 GPU implementation (b3gfx / b3view)

`DrawItem::shading = kTrack` (tracks) or `kSky`; `src/b3gfx/shaders/mesh.vert` holds the per-vertex math of §11.2
(types 0/1/2/6 and instance colours, with the same float-bit tricks via floatBitsToInt/intBitsToFloat, so the
approximations of the PS2 are reproduced, not "improved"), `mesh.frag` the GS (MODULATE with clamp, alpha byte
`floor(At x a / 128)`, TEST_1 compare, As/128 as blend factor and frame alpha, fog), `specular.frag` and
`present.frag` the two full-screen passes. Per frame: camera position/right, to-sun, specular colour, fog model and
the 255/128 present scale (`b3view::frame_params`). Uncapped presentation and MSAA are unchanged.

### 11.4 VU1 reference check ("shader accuracy") [C]

`include/b3asset/vu1.h`: an independent VU1 micro-mode interpreter (render3; semantics from the VU manuals): b3fp::ref float model, 4-cycle FMAC/flag latency with stalls, Q/P delays
read without interlock, the microVU branch-VI rule, E-bit delay slot, XGKICK -> GIF PATH1 packet reader. Two hardware
details the game depends on: **MAX/MINI compare raw bit patterns** (the alternate programs pass the ADC word, and type
1 its integer alpha, through MAX as "denormal floats"; flushing them breaks both), and the old-Q reads of
software-pipelined loops (1.55 M per full run, by design).

`tools/render/b3vucheck check <TRACKS dir> [--units a n] [--samples n]` runs every track batch's program (VIF ->
VU1 -> GIF) with constants built like the EE side and compares with the C++ model; also the 1013.z and guard-band
variants. Full install (37 routes, all units, 300 batches per material type and route): **61,587 batches, 3,283,204
vertices: RGB, alpha, u, v, screen x/y (<= 0.12 px) and line fog F: 0 mismatches**; culling 19 of 1,045,200 + 231,697
vertex decisions and q at 44 vertices differ, all within about a metre of the camera (float cancellation; PS2
truncation vs IEEE). `make test` runs a 23,046-vertex subset (`real_track_vu1_reference`) when the game and the
microprogram images are present.
Car programs: `build/b3vucheck_car <game>/PVEH/*/CAR*.B[GT]V` -> ALL WITHIN TOLERANCE over 107 cars, 942,220 vertices
+ 523,920 env-pass vertices (docs/car-shading.md §6). Sky dome: `build/b3envdump <route> --vu <vu dir>` -> all three
components OK (docs/sky-enviro.md).

### 11.5 Sky, sun, fog: ENVIRO.DAT (summary of docs/sky-enviro.md)

ENVIRO.DAT = one serialized CB3EnvironmentData per route (0xB0 header + four rasters: sky gradient + glow mask, cloud
panorama, car env-map sky, sun corona), loaded by CB3EnvironmentManager::Prepare 0x2BCA20 [C]: fog colour, near-plane
scale, fog max / sky fog, full-screen specular intensity, specular colour, sun colour and the sun direction (as light
travels; Prepare negates it into env +0xB0, the to-sun vector the track programs use). The sky dome is generated by
VU1 0x46A590 itself (h = 2e - e^2, r = 1 - e^2 rings, camera-centred) and textured with a 64x32 backdrop rebuilt per
frame from the gradient around the sun's bearing; clouds scroll with the environment clock. **Fog** is a post pass on
the Z buffer: Z >> 8 indexes a CLUT from GenerateClut (index 0: sky fog, 1-11: max, 12+: max x a smooth curve), so
fog = f(w) through Z = zShift + zScale / w; b3view evaluates the same quantised curve per fragment and, for opaque
draws, scales the frame alpha by (1 - f) so that "specular, then fog" is reproduced exactly.

### 11.6 Cars (summary of docs/car-shading.md)

One VU1 program (0x46BB80) plus VU0 0x47F6D0 (not VU1). Lighting is 9-coefficient spherical harmonics from
`CB3StreamedTrack::GetIrradiance` 0x301340, which blends SH stored per collision vertex (**LOD block +0x94 = SH
irradiance**); RGB = base + E (full - base), Fresnel-like alpha; a second pass blends a 256x256 sphere map by the
destination alpha (the atlas alpha is per-texel reflectivity); damage blends two zone matrices per vertex; no culling,
no per-vertex fog. Model: `include/b3asset/carshade.h`. Wired into b3gfx / b3view / b3replay by render5 (§12.6): the
`car.vert` / `car.frag` shaders, checked per vertex against the programs (`b3vucheck_car --shader`), RenderVehicle's
draw order, the renderer's texture slots (slot 1 = the underside texture), the game's LOD choice.

### 11.7 Still guessed or not done

- Type 5 per-vertex lighting and the instance VU0 stage are code-read only (no interpreter run with BeginInstanceRender's
  VU0 constants); b3view draws type 5 with the type-4 colour [L].
- `RenderWater` (STATIC +0x2C/+0x30 water objects) and the +0x28 backdrop group draw as ordinary meshes; bloom
  (CB3Bloom), the sun corona, shadows, effects, motion blur and the PAL/HDTV contrast filter are not modelled.
- Lines (type 3): drawn since lines1 (§11.8).
- Animated materials: done by anim1 (§15): b3replay from the capture's game clock, b3view on a wall clock; the
  environment clock is fixed.
- Fog's Z-bits-to-index step and the backdrop column blend input are inferred (docs/sky-enviro.md S.5).
- The order "after-specular" slots vs. the specular pass is approximated: b3view adds the specular after all opaque
  draws and before blended ones.
- Camera handedness of RW's view matrix is inferred from the culling statistics (floors face up), not from RW's code [L].

Commands: `make render && tools/render/b3view.sh <game>/TRACKS/AS/C1_V1` (F fog, H specular); Windows:
`%B3_WIN%\b3view\b3view.exe %B3_WIN%\perf\install\game\TRACKS\EU\M2_V2`.

### 11.8 Line meshes: the wires (lines1, 2026-11-14)

Before this the renderer skipped the type-3 meshes, so the overhead wires were missing and the traffic lights and lamps
hung on them floated in the air (a player report). What a line mesh is: docs/track-format.md §3.5 (in short: GS LINE
segments, untextured, ALPHA_1 0x42 = Cd - Cs x As with AA1, Z tested and not written, drawn among the units'
before-vehicles slots; 34 of the 37 route folders have them).

- **Reader** (`b3asset::track_line_batch`, with `TrackOptions::lines`, which the race view, b3view, the photo mode and the
  showroom's track set): each drawn segment becomes a quad of four vertices that carry this end (`pos`), the other end
  (`normal`), (side -1 / +1, end 0 / 1) in `uv` and the end's colour; indices (0 1 2) (2 1 3). The quads join the route's
  vertex and index buffers like any other draw (one draw per mesh, culled and streamed with its unit), so the whole
  track view, the streamed units and the LOD units all draw them.
- **Width**: the GS draws a line 1 pixel wide in a frame of 448 rows. On the GPU (a line primitive is 1 output pixel and
  its width is not portable) each quad is widened in screen space in `mesh.vert`: both ends to clip space (an end behind
  the eye moved along the segment onto w = 0.001 first), then the vertex moves across the segment's screen direction by
  `width / 2 + 1` pixels with **width = max(1, output rows / 448)** (1080p: 2.4 px, 448 or less: 1 px; a split-screen
  view uses the whole output's rows). The same rule in every quality preset, so PS2 ORIGINAL draws them like the PS2.
- **Coverage**: `mesh.frag` takes the coverage of a line `width` wide over the pixel (box filter: `clamp(width / 2 + 0.5 -
  |d|, 0, 1)`, d = the screen-linear distance from the centre line): the AA1 coverage, smooth at any size and with MSAA.
- **GS state**: Cs = the vertex colour (raw GS bytes) x F, the program's fog (RW linear fog with RwCamera's default
  fogPlane 5 [L: the game never calls RwCameraSetFogDistance] and the far plane of the fog model, 10,000 m; FOGCOL taken
  as 0 [G]); the pipeline blends RGB as `Cd - Cs x Cov` (REVERSE_SUBTRACT, ONE, ONE) and keeps the frame alpha
  (the GS would write the coverage there: see below); Z tested GEQUAL, not written; no culling. The fog pass comes later
  on the PS2: the darkening and the alpha are scaled by (1 - f) at the wire's depth, as the opaque draws do (§11.5).
- **Order**: with the opaque draws, in list order (the route's units, then the cars), before the full-screen specular:
  the game draws them in RenderBeforeVehicles among the units' slots, before the vehicles and the specular. As they
  write no Z, a car drawn later covers a wire even when it is behind it, on the PS2 and here alike.
- **Frame alpha kept (a choice, not the GS):** the GS writes the coverage as the pixel's alpha, and the full-screen
  specular, drawn after the wires, adds `S x alpha`. With that rule, on routes with a strong specular colour (US/C1_V1:
  S = 102 GS bytes, 0.8 of the display) the specular outweighs the darkening, and the wires read lighter than the sky.
  The port keeps the frame alpha of whatever is behind the wire instead (alpha blend ZERO, ONE), so the specular is the
  same as without the wire and the wire stays a dark line on every route.

Check: `make test` (`track_line_batch_synthetic_packet`: a VIF packet through the reader; `track_line_width_and_expansion`:
the width rule, the coverage, the expansion twin `b3asset::track_line_expand` of `mesh.vert`).

## 12. Race view, first light: scene capture and b3replay (render4, 2026-11-02)

Milestone "race view native, first light": a recorded race session replays in our own renderer with the track, the
cars and the camera where the game has them, uncapped and interpolated between the game's 60 Hz ticks: the game runs unchanged (headless replay of a user
recording), a host hook reads the scene state from guest memory, and the native renderer draws it offline from the
game's own files.
Tags as in the rest of this document.

### 12.1 The capture

- **When.** Once per retail controller poll, so only while a pad log is active (every replay or recording). The poll is the input update inside the game's fixed
  1/60 s tick loop (docs/frame-rate.md), so **one record = one simulation tick** [C frame-rate.md main loop]. The state
  read at poll k is what the game rendered in the frame before it (ticks, then `UpdatePostSimulation` with the camera,
  then render, then the next frame's ticks) [L order from the loop]. A rendered frame of N ticks shows as N records whose
  camera changes only at the first; "per rendered frame" is therefore derived (camera keys, `CB3Game+0x2DA84` = N per
  record), with no extra hook.
- **Format.** A delta stream of a sparse guest-memory image: per poll a `TICK` header (poll, vblank) and runs of 16-byte
  blocks that changed since they were last written. A reader that applies the runs up to poll k holds exactly the bytes
  the hook read at poll k and decodes them with the same pointers and layouts (`src/b3replay/capture.cpp`). Size:
  race2 (5,181 polls, 134 s) = 10.0 MB.
- **Read only.** Only addresses in main RAM (0x100000..0x2000000) are read: no MMIO, no scratchpad, no write, no host-clock dependence (replays are paced by guest EE cycles).

What is captured and where it comes from (retail addresses; offsets from the decomp's drafts and matched units):

| Value | Guest source | Tag |
|---|---|---|
| Ticks per frame, half-rate state | `CB3Game` 0x51BA80..0x51BAD0 (`+0x2DA84` = N) | [C] frame-rate.md |
| World base | `gWorld` = 0x522660 (its `CGtTimer` at +0) | [C] literal `0x522660` in `Draw__16CB3TrafficSystem` (draft written from the asm); E-map name [L] |
| Viewports | graphics manager 0x665EC0: selection 1 (player 1) at +0x90, 2 at +0x130, 0 (full screen) at +0x310, each 0xA0; params +0x1D0 / +0x270; open viewport +0x3B0 | [C] `OpenViewport__18CB3GraphicsManager` 0x227360 (matched) |
| Camera matrix | viewport +0x58 `RwCamera*` -> +0x4 `RwFrame*` -> modelling matrix at frame +0x10 (right, up, at, pos), written from the camera's quaternion by `CGtViewport::Update` | [C] Update 0x211ED0 (draft from the asm); [L] RW 3.x RwFrame layout |
| Projection | viewport +0x48 / +0x4C = half view window at distance 1 (+0x48 = +0x70 x tan(fov / 2), +0x4C = +0x48 / aspect), +0x84 / +0x88 near / far, +0x00 the game's world -> screen matrix; `CGtCamera` (+0x68): fov (degrees) +0x4, quat +0x10, position +0x20 | [C] Update, OpenViewport, `DrawPositionMarker` |
| Race cars | count `gWorld+0x12AD1C`, `CB3RaceCar*[6]` at `+0x12AD28`; drawn only if +0x18E8; matrix +0x00, or +0x1920 when +0x199B: Draw swaps it into +0x00 before RenderVehicle, so the body, panels and wheels all take it (the slammed car's fishtail, written by `CB3RaceCar::UpdateAggressiveReaction` 0x151DC0: a decaying yaw about the up axis for krFishTailTime; wobble1, 2026-11-18: b3replay swapped it in after placing the parts, so only the body wobbled and the panels and wheels stayed put) | [C] `RenderRaceCars` 0x199A20 (draft), `CB3RaceCar::Draw` 0x151A40 (matched); [L] UpdateAggressiveReaction draft |
| Car render inputs | `CB3VehicleEssentialParams` (= the car, +0x00): matrix +0x00, `CB3VehicleData*` +0x40, tilt sin/cos +0x44..+0x50, light bits +0x58, paint +0x59, frustum result +0x5B; `CB3VehicleParams` (car +0x80): wheel matrices +0x000 (relative unless state +0x4AC = 3), panel matrices +0x180 (relative to the tilted body unless state +0x4B2 >= 3; 4 = not drawn), wheel speeds +0x4E8, glass +0x1014, deformed +0x101B, deform matrices +0x500 (captured while deformed) | [C] `RenderVehicle` 0x21E6F0 (draft, every op from the asm) |
| Car model identity | vehicle data header [0, 0x70) (version, size, counts, floats before the relocated pointers at +0x4C) and its atlas raster's name (raster +0xA8) | [C] header bytes equal the file's for every car seen (below) |
| Track route | static track 0x64C990, its data (+0x4) = the loaded STATIC.DAT image: file size +0x04, unit count +0x54 | [C] track-format.md §1.1; match below |
| Track units | streamed track 0 at `gWorld+0x12A348` (0x1CC; split screen: player 2's track 1 at +0x12A514, §29.5): +0x44 on, +0x58 u16[17] visibility (2 bits per camera, camera 0 = main), +0x7C block per slot drawn by the main camera, +0xC4 LOD blocks (environment cameras), +0x1C4 the camera unit's high block, +0x1C8 the block whose +0x64 lists the backdrops; each block's +0x04 = unit index, +0x58 = 0 high / 1 LOD | [C] `IsUnitVisibleToMainCamera` 0x21CFF0 (matched), `DoUnitFrustumTesting` (draft), `RenderBeforeVehicles` 0x21C780 (draft); track-format.md §2.2 |
| Traffic | `CB3TrafficSystem` at `gWorld+0x4DE0` (`CB3World::Render` passes it): 254 `CB3TrafficVehicle` of 0x180 at +0x127D0, active if +0x174 bit 0; EssentialParams at +0x70 ([0, 0x80) since render6: the 9 SH coefficients at +0xCC), speed +0xC4, spin / steer +0x128 / +0x130, flags +0x174 / +0x17A; the per-race-car lists Draw walks +0x35C24 (0xFE each, counts +0x36350), the player's race-car index `gWorld+0x12AD50+0x19AC` | [C] Render draft call, asm offsets of Draw 0x3078F0; [L] Draw draft roles (§12.8) |
| Crashed vehicles (traffic hit in a crash, as physics objects) | count 0x64517C, pointers `gWorld+0x122A10`; +0x204 the matrix Draw measures distance from, EssentialParams +0x1360 ([0, 0x80) since render6), VehicleParams +0x13E0, +0x2402 | [L] `Draw__16CB3TrafficSystem` draft |
| Tilt axes | `kRightAxis` 0x4B9D80, `kAtAxis` 0x4B9D90 | [C] retail_data_names |
| Traffic wheel axes (render6) | `kXAxisVector` 0x4C4950, `kYAxisVector` 0x4C4960 (zero in the ELF, set at run time; captured (1,0,0) / (0,1,0)) | [C] E map names, Draw 0x3083A0 / 0x3083FC |
| Props (capture version 2, props1: §12.6) | prop manager 0x64A854 [0, 0x9C), its per-unit CB3Prop lists, the dynamic props' placements; CB3PhysicsManager 0x55EF70 prop-body mask and bodies; the stage logic's props-off virtual | [C] §12.6 |
| Effects (capture version 3, fx1: §14) | shadow inputs per vehicle (ground normal / distance, darkness, the vehicle data's box and wheel base), the view's traffic list, IsPlayerVisible's inputs; the trail system's emitter lists, emitters and point data, the per-type VU rows D_01E865E0 and mgaDefaultParams | [C] §14.1 |

### 12.2 Proof that the capture changes nothing

A headless replay of a session with the capture on (`$P/scene/<name>.b3scene`) checks every poll against the
recording (exact: same vblank, sample and EE cycles); the same replay runs without the capture, and when both runs
exist their GS streams are compared frame by frame (`streamcmp.py`). Build sha256 51059b3e... [C]:

| Session | Capture on | Capture off (same exe) | GS stream on vs off |
|---|---|---|---|
| race2 (US/C3_V1, Crash-mode junction) | 5,181 of 5,181 polls exact, 0 with other EE cycle counts; 5,181 records, 10.0 MB | 5,181 of 5,181 exact, 0 other cycles | **IDENTICAL over 8,030 frames (every column)** |
| roadrage1 (EU/C4_V1, Road Rage, 4 race cars) | 13,474 of 13,474 exact, 0 other cycles; 13,474 records, 44.2 MB | 13,474 of 13,474 exact, 0 other cycles | GS path payloads (p0/p1/p2) **identical in all 17,645 frames**; every column identical from frame 4684 (the race starts at vblank 7040); 80 front-end frames (1787..4683, the `_FEMain.m2v` menu movie loop) differ in `hdr`/`full`/`bytes` only, and so do two capture-off runs (82 frames, same window, same columns), so that is run-to-run variance of host records (`hdr` = everything but GS transfers), not the capture [C `scene-stream.txt`, streamcmp] |

Finding (not render4's): roadrage1's headless replays are poll-exact and GS-transfer-exact, but their front-end host
records vary between runs; race2's do not. Stream comparisons of such sessions should use the p0/p1/p2 columns or
start after the front end.

### 12.3 b3replay (`src/b3replay`, `build/b3replay`, Windows `b3replay.exe`)

Loads the capture, picks a race segment (consecutive ticks with the track on, a camera and race cars on one route; the
longest unless `--segment` / `--from-poll`), finds the route under `<game>/TRACKS` by STATIC.DAT size and unit count,
matches every vehicle data header to a `PVEH/*/*.BGV|BTV` file (bytes 0..0x4C), and draws per frame with render3's
game model (§11, the same `b3view::build_scene` / `add_enviro` / `frame_params` path as b3view): track materials
types 0-6 with their VU1 math, culling and GS alpha test, the route's ENVIRO.DAT sky and clouds, the sun, fog (G) and the
full-screen specular (H), at the game camera's position and direction:

- **Track:** the units in the 17 main-camera slots, high or LOD meshes as the slot's block says (b3asset's track reader
  with and without `lod`), their instances, the backdrops the current block lists, props and water. By default every
  resident unit (so a free camera or interpolation never reveals a culled hole); V switches to the game's own
  main-camera visibility bits.
- **Cars:** body at the tilted matrix, panels at their VehicleParams matrices and wheels at theirs, as `RenderVehicle`
  composes them; b3asset bakes panels/wheels at their file frames, so b3replay applies `inverse(file frame) x world`.
  Traffic: the whole model at its EssentialParams matrix. Paint from +0x59 (the atlas palette).
- **Camera:** attached = the game's: eye = frame pos, forward = at, up = up (the frame's right vector points to screen
  left, so a right-handed look-at gives the game's picture unmirrored: signs read correctly) [C pictures]; vertical
  field 2 atan(+0x4C), horizontal from the output aspect (Hor+; the game's 4:3 field at 640x448 in `--offscreen` without
  `--size`). C toggles a free camera starting at the game's pose.
- **Interpolation (§6.4):** time runs at 60 ticks per second; objects are matched across ticks by guest address and
  model, matrices blend by slerp (rotation), lerp (scale, position). The camera blends between the ticks where its value
  changed (every tick at 60 FPS, every second tick in 30 FPS crash frames), also the view window (crash-camera zooms);
  a cut (> 25 m or > 60 degrees between keys) is not blended. I toggles it off (the game's own 60 Hz steps) for
  comparison. Presentation is uncapped (b3gfx IMMEDIATE / MAILBOX).

Checks (`b3replay <capture> --info`, race2) [C output]:
- Route **US/C3_V1** matched by size 4,718,592 + 36 units; the player's car **PVEH/CUPE/CAR3.BGV** (raster `coupe3`),
  traffic and crashed vehicles to `PVEH/COMP|HEVY/*.BTV` (raster names `Compact19`, `Heavy34`, ... agree with the files);
  no unmatched or ambiguous model.
- **Camera / projection against the game's own culling:** every race car's frustum result (EssentialParams +0x5B,
  written by the game's `FrustumTestDamagedVehicle` with its own camera) against our camera model with the vehicle
  data's radius (+0x14 [L]). race2: all 264 cars the game found outside are outside ours, none of the 1,965 it drew is.
  roadrage1 (4 race cars, 159 s): 12,085 of 12,090 game-outside outside ours; of 26,118 game-visible, 10 outside ours;
  15 disagreements in 38,208 (0.04 %, edge cases). The counts also fix the encoding: game 1 only ever meets our
  "crossing" and our "inside" only game 2, so +0x5B is 0 outside / 1 crossing / 2 inside, as in `DoUnitFrustumTesting`
  (r == 2 -> fully visible) [L]. Our radius is the more conservative (more crossings). Position, orientation, field of
  view and near plane agree.
- **Interpolation at 144 Hz** over the 37-s segment, counting only displayed frames where the game state moves: the
  camera repeats in 67.8 % of 2,835 frames with the game's steps and 5.4 % interpolated; the player car 59.0 % -> 4.6 %
  of 2,350 (the residue is where motion starts or stops inside the window); median camera step 0.376 m -> 0.082 m.
  roadrage1 (159 s, mostly 60 FPS): camera 62.0 % -> 0.2 % of 20,488, player car 58.3 % -> 0.0 % of 22,867.
- Offscreen renders (game camera, 640x448 and 1280x540; the Windows exe on the RTX 4080 at 2560x1080): the chase view on
  C3_V1 with the car on its lane, the crash junction with the tumbling car and the RV pile-up, and a free view over the
  junction. Not compared against GS frames; the frustum agreement above is the camera check instead.

### 12.4 Running it on Windows

```
%B3_WIN%\b3replay\b3replay.exe %B3_WIN%\perf\scene\race2.b3scene
%B3_WIN%\b3replay\b3replay.exe %B3_WIN%\perf\scene\roadrage1.b3scene
```
(`--game` defaults to `%B3_WIN%/perf/install/game`.) Space pause, Left/Right -/+5 s, PgUp/PgDn
-/+30 s, Home start, `,` / `.` one tick, `+` / `-` speed, Tab next race segment, C game / free camera (free: right mouse
look, WASD/QE, Shift x4, wheel speed), I interpolation, V game culling, F11 fullscreen, Esc quit. The title shows the
poll, the blend factor, frame rate and present mode. Build: `OUT=$B3_WIN/b3replay
tools/render/build_win.sh` (a worktree build must not replace the main `b3view\b3view.exe`).

macOS / Linux / any machine (from the repo root): copy the disc's contents (the folders `DATA`, `TRACKS`, `PVEH`, ...
from the ISO) to `data/game/` (git-ignored); the recordings are kept locally in the git-ignored `data/scene/`. b3replay's `--game` defaults
to `data/game` when it exists. macOS: `brew install sdl3`, then
`make render SDL3_CFLAGS="$(pkg-config --cflags sdl3)" SDL3_LIBS="$(pkg-config --libs sdl3)"` and
`build/b3replay data/scene/roadrage1.b3scene`, `build/b3view data/game/TRACKS/AS/C1_V1` (run on an M1 Mac since
2026-11-02; scripted build, tests and report: docs/mac.md, `tools/mac/`).

Region mode (b3view, 2026-11-03): `b3view data/game/TRACKS/EU` (an area folder) loads every route's V1 into one
world -- each region's routes share coordinates (checked 2026-11-02) -- and drops every triangle an
earlier route already brought (its three world corners to 5 cm, any order; per draw, z-fighting showed where
routes cut the same road into different draws). EU: 8 routes, 1,478,755 triangles kept, 1,946,461 duplicates (the
M/P routes' pieces of the circuits: 76-92 % of their triangles), 723 textures, 0.90M vertices. Not handled: route-only
barriers and each route's own backdrops (they overlap); surfaces tessellated differently between routes (z-fighting
where that happens). The first look in a window (EU city, Metal 159 fps at 3200x1800 with the whole region) showed the other
case: one place modelled differently by two routes (a road one route races on, another closes off as snowy pavement
with a barrier), so both versions overlap. X / `--exclusive`: one version of each place. Single routes also model a
junction they pass twice once per pass, in units far apart in the route order (EU C4 units 3 / 12: 170 shared 4 m
ground cells; M2 45 / 106: 295; P2 76 / 85: 170; the game streams one at a time). The first (route, unit) whose ground
(|normal.y| > 0.7) claims a 4 m cell owns it; a triangle in a cell owned by another route, or by a unit of its route
more than one step away, is dropped (neighbours share seams; props, backdrops and water are kept). EU 1,392,641
triangles kept. The title shows the camera position, so a spot can be reported and probed. `--first <route>` (e.g. P1_V1) loads that route first, so its version wins.
A reported spot at (2200, -330) in the EU city: no circuit there; M1 units 108-109 and P1 units 91-94 both model it, and
**both close the road north of the roundabout with a fake building facade** (same wall in either version; each route
bakes its own vertex lighting, so the versions differ in colour). Open regions therefore need a blocker pass: find the
walls and barriers whose footprint crosses a road some route drives on, review, remove (docs/remaster-features.md §7). Controls added for trackpads: left-drag looks, M mouse look without a button, arrow keys look
(b3replay's free camera: left-drag too).

### 12.5 Missing (next steps)

- **Look**: tracks, sky, fog and specular are render3's game model (§11); cars the game's car model since render5
  (§12.6: SH lighting, Fresnel alpha, env pass, slot textures, glass state, light bits, wheel blur lists, the game's LOD),
  with damage deformation, traffic wheels and the game's traffic choice since render6 (§12.8); car shadows and skid
  marks since fx1 (§14); no other effects, no post.
- Water surfaces are all drawn (live2 §20.3: now only the camera unit's, reflections mirrored). Props: §12.7 (version-1 captures still draw them all the time). Traffic payload
  objects (VehicleParams +0x1024, the C lists) are not drawn.
- Not captured: environment / time of day / fog (`CB3EnvironmentManager`, `CB3Fog`), particles (skid marks and car
  shadows since capture version 3, §14), the HUD (captured and drawn since hud1, §17),
  split screen (player 2's viewport and streamed track 1: both captured and drawn since gsoff1 / split1, §29), the front end.
- Each race car's simulation pose is used; the game's own per-frame camera smoothing is in the captured camera, but
  Impact Time (slow motion) plays at tick rate, i.e. as slow as in the game.

### 12.6 Cars the game's way (render5, 2026-11-03)

Code: `src/b3gfx/shaders/car.vert|frag` (b3gfx `Shading::kCar`, `CarParams`), `src/b3view/carscene.{h,cpp}` (shared by
b3view and b3replay), `b3asset::CarOptions::bake_frames = false` (lists in part space, B1/B2 decoded, raw normals),
capture fields in `src/b3replay/capture.{h,cpp}`. Tags as above.

**Draw order and masks** [C RenderObject 0x21FBB0 draft "every op from the asm", RenderVehicle 0x21E6F0 draft]:
RenderObject draws a mesh entry only if `entry mask & argument`; RenderVehicle calls it for the body with 0xFF (all but
glass), the panels with 0x3FF sorted far to near by squared distance from the CGtCamera (+0x68 -> +0x20; state 4
skipped), the body again with 0x300 (its glass), then each wheel with 0x3FF from list B0, or B1 / B2 when |VehicleParams
+0x4E8[w]| > 25 / > 50 and the LOD has that list. Panels pass `false` for the lighting bool and so are lit with the
body's SH matrix [C car-shading.md §2]. `CB3RaceCar::Draw` 0x151A40 sets the tilt sin/cos to 0/1 before RenderVehicle
[C matched], so race cars never tilt (b3replay used the captured tilt before). Cars draw among the opaque draws in list
order, glass included, before the full-screen specular (CB3World::Render order, §11.1).

**Texture slots: the underside livery** [C]. Material byte 3 selects renderer TEX0 slot `field_20[b]`
(RenderObject); slot 0 = the car's atlas with its paint CLUT (SetTexture 0x21FF60), slots 1..4 are copied by
BeginVehicleRendering 0x220540 from gGraphicsManager +0x3D0..+0x400 (D_00666290..D_006662C0), which
`CB3GraphicsManager::Prepare` 0x228070 [C matched `func_00228070.cpp`] fills from the textures
`CB3GraphicsManagerBase::Prepare` 0x19EBF0 [C matched] looked up by name in the global dictionary: **+0x3D0 =
"VehicleUnderside"** (8-bit, own CLUT), +0x3E0 "UnbrokenGlass", +0x3F0 "CrackedGlass", +0x400 "SmashedGlass" (strings
0x4B7328..0x4B7360), all in DATA/GLOBAL.TXD. The underside is the body mesh 0x01640002 (slot 1, mask 2: no env pass):
CUPE/CAR3 LOD 3, 97 triangles, 96 % of its area facing down, UVs over the whole 0..1 square. b3asset drew every mesh
with the atlas, so the underside showed the atlas (wheels, logos, livery), as a Mac screenshot showed. Glass meshes are
slot 2, and `CB3VehicleObject::SetGlassState` (VehicleParams +0x1014) retextures the object's glass entry (list byte +1):
1 -> slot 3 / FIX 0x48, 2 -> slot 4 / FIX 0x80, else slot 2 / the default FIX [C name, L body].

**Back-face culling: none** [C]. VU1 0x46BB80 sets ADC only from the data bit and the guard-band test (car-shading.md
§4.4); the GS has no culling, so the GPU pipeline draws both sides. The underside problem was the texture, not culling.

**LOD** [C race cars, L traffic]: race cars, `CB3RaceCar::Draw` 0x151A40 (matched): d = |EssentialParams +0x30 -
CGtCamera position|, r = open viewport +0x90; LOD 0 if d > 60r, 1 if > 30r, 2 if > 15r, else 3, distances from
gGraphicsManager +0x70 = karDefaultLodSwitchDistances {15, 30, 60, 20, 80} (rodata 0x4B72A0, copied by
SetDefaultLODSwitchDistances 0x19E9A0 [C]). Traffic and crashed vehicles, `CB3TrafficSystem::Draw` 0x3078F0 (draft
from the asm): 2 if d <= 20r, 1 if <= 80r, else 0. RenderVehicle uses the nearest block the file has (up to 4, then
down). b3replay reads +0x90 and +0x70 from the capture (the graphics manager [0, 0x3C0) is captured); `--car-lod N`
forces a level. The scale +0x90 is not constant: race2 0.700..1.732, roadrage1 0.525..3.234 [C capture].

| Vehicle-ticks per LOD (render4 drew all at 3) | 3 | 2 | 1 | 0 |
|---|---|---|---|---|
| race2 seg 0 (37 s): race cars | 1,966 | 49 | 8 | 206 |
| race2: traffic / crashed | - | 1,554 / 3,147 | 20,387 / 14,629 | 26,357 / 932 |
| roadrage1 (159 s): race cars | 14,697 | 5,604 | 5,003 | 12,904 |
| roadrage1: traffic / crashed | - | 664 / 204 | 2,610 / 295 | 28,915 / 146 |

**Shading** (car.vert / car.frag): per vertex exactly §11.6 / car-shading.md §4-5 in world space: E = (n,1) M (n,1)^T with
M from the car's 9 SH coefficients (EssentialParams +0x5C, which BeginRenderingSingleVehicle 0x220040 copies as three
rows of three to scratch +0x310 [L Ghidra draft]) **plus k (|n|^2 - 1)**: the VU0 builds its part-space matrix from the
rotated coefficients, which on the unit sphere equals R M R^T but differs from it by k diag(1,1,1,-1), and the program's
normals ((b - 128) / 128) are not unit length; k = 0.5 (a^T M3 a - M3[z][z]), a = the lighting part's at axis
(`b3asset::car_irradiance_k`, derived; checked below). Colour = base + E (full - base), Fresnel alpha, FTOI0 and the low 8
bits (no clamp: values below 0 wrap to ~252). Per mesh the VU0 constants come from `car_mesh_constants` (lights from
+0x58, shininess / lighting scales from damage +0x370 and explosion +0x1015/+0x1000: 1 - 0.6 damage; 1 - 2.25 t /
1 - 1.875 t up to 0.4 s, then 0.1 / 0.25 [C draft, rodata 0x4E0E94 / 0x4E0C50 / 0x4E0E24]).
The GS's two passes are folded into one draw: pass 1 MODULATE + ALPHA_1 (FIX or As) and the env pass's ALPHA_2 (0x54
lerp by Ad = pass-1 As, 0x58 add) are linear in the colour behind the car, so the shader outputs X with alpha 1 - keep
and the pipeline blends ONE, ONE_MINUS_SRC_ALPHA; Z written (TEST_1 0x50003), no culling.

**Car frame alpha / full-screen specular** [L]: after an env pass the frame alpha is the env map's alpha (context 2,
FB_ONLY: As written); the env map is drawn by RenderSkyToEnvironmentMap 0x2BC140 (sky components 4 and 5 [C matched]),
whose component-4 dome writes alpha 0 like component 1, and the sun blob writes no alpha; the pass-1 alpha of the meshes
without an env pass is 0 in every case (lights on: base/full alpha 0; mask bit 1 with C = FIX: 0) [C car-shading.md §3].
So cars leave frame alpha 0 and the full-screen specular adds nothing on them; the pipeline writes alpha 0.

**Env map content** [G parts]: the sky the game draws into it, sampled by the reflected eye ray directly instead of
through the 256x256 sphere map (same directions; b3vucheck_car --shader confirms the ray equals the program's sphere-map
input): component 4's dome mapping over the route's backdrop (§11.5) above the horizon, component 3's below [G: the game
does not draw 3 there; what the buffer holds below the horizon is not known], plus a sun blob at to-sun with the main
view's size and the sun colour [G size / colour]; component 5 (uv mode 1, blended) and DistortEnvmapBuffersIntoSphereMap
are not modelled.

**Checks** [C]:
- `b3vucheck_car --shader <game>/PVEH/*/CAR*.B[GT]V` (107 cars, 942,220 vertices, 524,741 env vertices; inputs the
  interpreter-checked model's per-vertex position and normal): E vs the VU0's own matrix, yawed parts max |err|
  6.4e-7, pitched parts 0.0145 (the VU0's band-2 SH rotation is approximate there, car-shading.md §2); RGB vs the VU1
  output max 1 integer (yawed) / 2 (pitched); alpha max 1; env-pass sphere-map s / t from the shader's world ray through
  the game's basis max 1.3e-4 / 5.5e-5. ALL WITHIN TOLERANCE. Without the k term yawed parts were off by up to 0.016.
- RGB wrap with real lighting (`b3replay --info`, race-car body vertices at LOD 3, every 30th tick): race2 1,905 of
  497,700 lanes (0.38 %, the underside: E(down) slightly negative with the captured SH), roadrage1 849 of 8,129,715
  (0.01 %); never above 255. Faithful to the program; a remaster may clamp.
- Before/after offscreen renders were compared (render5; game-derived, not committed).

**Headless on Metal** (`b3gfx::create_headless`, used by `--offscreen` of b3view and b3replay): SDL 3.4.2's Metal
backend is selectable only when the video driver implements Metal_CreateView (`METAL_PrepareDriver`); the "offscreen"
video driver does not (it provides only Vulkan surfaces and EGL), so every backend's PrepareDriver failed: "No supported
SDL_GPU backend found!" [C SDL source read; not run on a Mac]. Now: (1) "offscreen" video driver, no window (Vulkan:
Linux, WSL, Windows); (2) the platform video driver (Cocoa provides Metal_CreateView; METAL_CreateDevice needs no window)
without a window; (3) the platform driver with a hidden window claimed by the device. On WSL all three give
byte-identical PNGs (`B3GFX_HEADLESS_STEP=2|3` forces the later steps); the first line of output names the path taken.

**Not done**: payload objects (C lists); the env map's component 5 and sphere-map warp. Done by render6 (§12.8):
damage deformation, traffic wheel spin / steer / blur lists, the exact traffic / crashed SH (captures before render6
still borrow the nearest race car's coefficients scaled to their L00 [G]).

### 12.7 Props: the game's breakable objects in the capture and b3replay (props1, 2026-11-03)

In b3replay the car passed through tables, chairs and signs. Those objects are the route's **props**
(STATIC.DAT +0x3C types, +0x48 placements; track-format.md §1.1), which b3replay drew at rest all the time. The game's
own prop system, from the decomp [tags as above; names from the decomp's tables, placeholders otherwise]:

| What | Where / layout | Evidence |
|---|---|---|
| Prop manager (`CB3PropManager`) | **0x64A854** (= gWorld + 0x1281F4): +0x4 its own copy of the route's placements (0x40 each: rows X, Y, Z, T as CB3InstanceData, w lanes = instance RGB), +0x8 u8[type], +0xC dynamic count, +0x10 u16 placement count, +0x12 u8 prop-type count, +0x14 u8 count[unit][type], +0x18 CB3Prop list[unit][type], +0x1C CB3Prop[32] dynamic list (memset 0x80) | [C] `Prepare` 0x2B7300 (matched), `DynamisePropBody` passes `&D_0064A854`, `CB3StreamedTrack::RenderAfterSpecular` 0x21C120 passes it to `Render` (call at 0x21C3CC); `UpdateActiveBodyList` addresses the dynamic list as `D_00522660 + 0x128210` |
| `CB3Prop` | 4 bytes {u16 placement, u8 type, u8 state}; state 0 at rest, 2 = not drawn (the original of a dynamised prop), 1 = the copy in the dynamic list | [C] `DynamiseProp` 0x2B7230 (copy -> state 1, original -> 2), `Render` |
| Drawing | `CB3PropManager::Render` 0x2F99E0 [L name, DNAT]: per type, the per-unit lists of the units it is given, skipping state 2, then the dynamic list (also skipping 2), each at **manager +0x4 + placement x 0x40** (`RenderInstances`); it returns at once when the stage logic's slot 0xB8 is nonzero | [C] `func_002F99E0.cpp` (matched); only caller `RenderAfterSpecular` (so props draw after the full-screen specular, §2.3) |
| Prop bodies | `CB3PhysicsManager` **0x55EF70** (`D_0055EF70`): active bodies +0x70 (0x30 each, count +0x1CB70): type 5 = a prop at rest, 6 = dynamised; +0x4 matrix = manager +0x4 + placement x 0x40, +0x8 = the prop type (box), +0xC = its CB3Prop. 32 `CB3PropPhysics` at +0xC3900 (0x780 each; slots 0..15 used), mask +0xE9220; each moves the placement **in place** (+0x204 = that matrix, +0x220 its CB3Prop in the dynamic list, +0x224 = world time + 10 s) | [C] `Prepare` 0x1B9850 at 0x1BA020..0x1BA0A0 (asm), `DynamisePropBody` 0x1B4530, `func_001BE3C0`, `CB3PropPhysics::Update` 0x1BE250 (draft); the base also matches the crashed-vehicle count 0x64517C = +0xE620C |
| Life cycle | a hit (the body-collision response at 0x1B751C: `CB3Score::HandlePropHit`, then `DynamisePropBody`) copies the prop to the dynamic list and hides its original; when no body is free the oldest-expiring one is taken back (`KillProp(.., true)`: the list slot is reused, the old prop stays hidden); a released body (`UpdateActiveBodyList`, state 6) swap-removes its copy (`KillProp(.., false)`). **A prop once hit is never drawn at rest again in that race** | [C] the three functions above |
| Props off by mode | `D_0051BA88` +0x1B4 (the stage logic) slot 0xB8: `CB3PhysicsManager::Prepare` builds no prop bodies and `Render` draws nothing when it returns nonzero. It is `jr ra; li v0, 1` (0x38C050) for `CB3OfflineRoadRageLogic` and `CB3OnlineSingleRaceLogic` (0x38EB90 for `CB3OnlineCoOpCrashLogic`), `move v0, zero` (0x1343F0) for the other 13 logic vtables | [C] asm of the three functions, the vtables' slot words in the ELF; [L] vtable names (retail_data_names, medium) |
| Car panels / wheels knocked off | not props: `CB3BodyPartPhysics` (64 at physics manager +0xD2900, mask +0xE9218), drawn by the vehicle renderer from VehicleParams (state >= 3 = world matrices), already in the capture since version 1 | [C] `RenderVehicle` draft `GetBodyPartPhysics` |

**Capture version 2** (`src/gameview/scene_capture.cpp`): the manager [0, 0x9C), every per-unit count and CB3Prop array (all units,
from the static track's unit count), the 0x40-byte placement of each dynamic-list prop, the physics manager's two
masks and each prop body in use [0x200, 0x230), and the stage logic -> vtable slot 0xB8 -> that function's first 8 bytes.
Same records as version 1; b3replay reads both (a version-1 capture draws every prop as before). Cost: +16 KB on race2.

**b3replay**: per tick the set of placements whose CB3Prop is in state 2 (not drawn at rest) and the dynamic list (drawn
at the live matrices, matched by placement across ticks and blended like the vehicles); with the mode's props off, no
prop is drawn. b3asset's track reader now records each prop draw's placement (`Model::prop_transforms`). `--info`
prints per segment the mode (stage logic vtable, props on/off), the manager's counts against STATIC.DAT, every dynamise
event (poll, placement, type, position, nearest race car), the race cars' closest approach to each placement, and the
tick with the most placements in the game camera's view.

Results [C, `--info` and replays with build sha256 73bbea3e...]:

| Session | Mode per segment (stage logic) | Props | Replay exact (capture on / off) | GS streams on vs off |
|---|---|---|---|---|
| race2 | US/C3_V1: 0x4DF360 `CB3OfflineSingleCrashLogic` | on; 123 placements = STATIC +0x40, 123 CB3Props listed; cars never within 18 m; no hit | 5,181 / 5,181 exact, 0 other cycles (both) | **IDENTICAL over 8,030 frames (every column)** |
| roadrage1 | EU/C4_V1: 0x4DEE00 `CB3OfflineRoadRageLogic` | **off**: the 498 placements (cafe tables, chairs, parasols, planters) are never drawn by the game; cars pass within 0.2 m of them | 13,474 / 13,474 exact, 0 other cycles (both) | p0/p1/p2 identical in all 17,645 frames; 79 front-end frames (1787..4679) differ in `hdr`/`full`/`bytes` only, the known run-to-run variance (§12.2; capture-off vs the earlier capture-off run: 82, same window) |
| races | EU/C3_V1: Road Rage (off); AS/C2_V2 x3: Single Crash (on, cars >= 568 m from any prop) | no hit | 21,011 / 21,011 exact, 0 other cycles (20 / 23 with other step counts only; earlier plain checks: 16-18) | p0/p1/p2 identical in all 27,887 frames; 102 front-end frames (3135..5539) in `hdr`/`full`/`bytes` only |
| crash1 | US/C3_V1, C1_V1, EU/C2_V2: Single Crash (on); the closing US/C1_V1 Road Rage (off) | **2 hits** (US/C1_V1 Crash, polls 7744 / 7920): placements 208 and 215 (type 3, a chevron sign) dynamised with bodies 0 and 1, originals hidden; 3,716 prop-body ticks, every body's +0x204 = the manager's copy of its placement | 35,635 / 35,635 exact, 0 other cycles (23,880 with other step counts only, both runs alike) | p0/p1/p2 identical in all 41,990 frames; 79 front-end frames (15848..17998) in `hdr`/`full`/`bytes` only |

So what showed in roadrage1 was b3replay drawing objects the game does not have in Road Rage; with a
version-2 capture they are gone. Pictures (640x448, game camera): `roadrage1-p12681-before.png` (version-1 capture: 29
placements in view, cafe furniture on both pavements) and `-after.png` (version 2: none, as in the game), and
`race2-p3070-after.png` (Crash mode: the cones still drawn). crash1 at the first hit: `crash1-p7760-before.png` (the same capture read as version 1: the sign stays at rest behind
the camera, where the car drove through it) and `-after.png` (the sign tumbling away right of the car, at its body's
matrix); 432 / 6,754 bytes differ at polls 7748 / 7760, none at 7740 (before the hit) [C pixel diff]. Image paths:
the props1 lane's scratch directory (game-derived, not committed). Captures: `$P/scene/{race2,roadrage1,races,crash1}-props1.b3scene`.

Open: the local `data/scene` captures are version 1 (they still show the Road Rage props); the per-unit unit window
`Render` is given (`a1` units from `a2`: the streamed track's) is not applied (b3replay draws every route prop when the
mode has them); debris (`CB3DebrisArray`), popcorn and the other effects are milestone 2 (docs/effects-survey.md).

### 12.8 Cars finished for milestone 1: traffic and crashed vehicles, damage deformation (render6, 2026-11-03)

Code: `src/gameview/scene_capture.cpp` (added ranges, same capture version 2), `src/b3replay/capture.{h,cpp}`
(`game_draws`, traffic / zone fields), `src/b3view/carscene.{h,cpp}` (`traffic_parts`, zones per part),
`src/b3gfx/shaders/car.vert` (zone blend), `src/b3replay/view.cpp`, `tools/render/b3vucheck_car.cpp` (`--zones`). Tags
as above; decomp drafts read: `src/nonmatching/Draw__16CB3TrafficSystemFi.cpp` (written from the asm), RenderVehicle
and `func_0021F790.cpp` (RenderDeformedObject), matched `func_00301220` (ForEveryUnitInSafeArea), 0x21CFF0
(IsUnitVisibleToMainCamera), the asm of 0x3078F0, 0x309300 (CDrawnTrafficCache::TestPosition), 0x309070, 0x3090A0.

**1. Traffic and crashed vehicles' lighting** [C]. Draw computes each drawn traffic vehicle's irradiance itself
(`CB3StreamedTrack::GetIrradiance` into +0xCC = EssentialParams +0x5C, the 9 coefficients BeginRenderingSingleVehicle
copies); the capture now takes EssentialParams [0, 0x80) for traffic and crashed vehicles too. The reader already took
as many coefficients as the image holds, so the borrowing (nearest race car scaled to L00 [G]) now only runs for older
captures. race2-render6 / roadrage1-render6: 69,235 / 71,042 vehicle-ticks, all with 9 coefficients.

**2. Traffic wheels** [C asm offsets and operands, L draft roles]. Draw memsets a VehicleParams block per vehicle
(traffic system +0x363C0) and fills it: panels = the file frames (+0xD00), wheel w relative to the main matrix =
`(Spin x Steer) x frame` for w = 0, `(RevSpin x Steer) x frame` for w = 1, `Spin x frame` for other even w, `RevSpin x
frame` for odd, with Spin = R(kXAxisVector, +0x128), RevSpin = R(kXAxisVector, -(+0x128)), Steer = R(kYAxisVector,
-(+0x130)) (`neg.s` at 0x3083E0 / 0x308418, `jal 0x3090A0` x3), R = CreateRotationMatrixOnPrincipalAxis [L DNAT name]
whose rows are RenderVehicle's axis rotation with sin / cos of the angle [C asm 0x3090A0, lane by lane]; wheel speeds
+0x4E8[w] = +0xC4 x 1 / (type data +0x18, the wheel radius) (`div.s` at 0x3082DC, `lwc1 0xC4` at 0x3084F8), which picks B0 / B1 / B2
in RenderVehicle at 25 / 50. `b3view::traffic_parts`. Checks (`--info`, render6 captures): spin is an unwrapped angle
(roadrage1 0.02..628.3), steer -0.28..0.35, speed 0..31.2; the steer sign against the turn (heading change along the
car's right row, |steer| > 0.05, speed > 2): **into the turn 1,088, against 0** (roadrage1; race2 183 / 0). Drawn
traffic vehicle-ticks by list: roadrage1 B0 102 / B1 1,811 / B2 6,257 (render5 drew all B0); race2 10,884 / 1,438 / 392.
Interpolation blends the angles (spin the shorter way round).

**3. Which traffic and crashed vehicles Draw draws** (`b3replay::game_draws`, per tick) [C constants and tests from the
asm, L the draft's structure]:
- traffic: only those in the player race car's list (`lb 0x19AC` at 0x3079E8, list base +0x35C24, `lbu` at
  0x307ABC); active (+0x174 bit 0); |EssentialParams +0x30 - CGtCamera +0x20|^2 <= 75625 (= 0x12769 converted,
  0x307A94, `c.le.s` 0x307B14: **275 m**) unless crashing (+0x174 bit 2); not crashing: TestPosition's safe-area slot (+0xCA) must be one the
  main camera sees: ForEveryUnitInSafeArea's 9 ring slots (+0x4C - 4 + i) mod +0xC [C matched 0x301220], each tested by
  IsUnitVisibleToMainCamera [C matched 0x21CFF0: its LOD data +0x17C[k] among the 17 slots' +0xC4 blocks -> visibility
  bits & 3]; TestPosition returns a ring slot or -1 [C asm 0x309300]; frustum result +0xCB (FrustumTestUndamagedVehicle in
  the same Draw) nonzero; not camera-inside (+0x17A bit 1); then the 8 nearest crashing and the 32 - those nearest others
  (`slti 9`, `0x20`, `slti 0x21` at 0x3080CC..0x3080F8 after qsort with the ascending comparator 0x309070).
- crashed vehicles: |*(+0x204) +0x30 - camera|^2 <= 75625, unit +0x13BA seen, frustum +0x13BB nonzero or +0x2402, the 32
  nearest. +0x204 points at the EssentialParams matrix in every captured tick (race2 18,708 of 18,708).
- The fields Draw writes (+0xCA, +0xCB, +0x17A) are read at the next poll, i.e. as that render left them; the 275 m test
  is applied always, the camera-dependent tests with the game culling (V; `--offscreen` uses it). Not reproduced: the
  64-entry fill limit before the sort (list order); never reached in the captures (at most 23 drawn per tick).
- **Proof against the game's own trace**: GetIrradiance rewrites a traffic vehicle's SH only when Draw draws it, so
  between two one-tick frames a changed SH means "drawn". race2: 3,656 changed-and-drawn, **0 changed but not drawn**;
  roadrage1: 6,684 / **0**. The other direction is weaker: of the vehicles drawn and moved, the SH stayed equal for 18 of
  1,889 (race2) / 1,013 of 7,697 (roadrage1): equal irradiance across the hit triangles, or a vehicle drawn by us and not
  the game; the capture cannot tell them apart [L].
- Counts (vehicle-ticks, race2 segment 0 / roadrage1): traffic drawn 12,714 / 8,170; beyond 275 m 13,506 / 10,252; not in
  the player's list 16,445 / 331; unit not seen 858 / 7,235; frustum 0 4,775 / 6,201; crashed drawn 17,750 / 278,
  frustum 0 958 / 367. render5 drew every active vehicle.
- **Coming and going (traffic-popin, 2026-11-11; `view.h` traffic_fade, `src/b3replay/trafficfade.cpp`)**. Without the
  game culling (the default) the renderer draws every active traffic car within the 275 m radius, so what decides when a
  car appears is the radius and the traffic system's activation / removal. Measured (`b3replay --traffic-scan` on the
  captures, `B3_TRAFFIC_PROBE=1` on a live STRICT race on US/C2_V2; "on screen" = in front of the game camera within a
  32:9 picture, occlusion not tested): race2 20 activations (11 on screen, at 100..250 m), 20 entries from beyond 275 m,
  5 removals on screen at 188..206 m; the US/C2_V2 race 2 activations on screen at 143 / 178 m, 38 entries from beyond
  275 m on screen, 10 removals on screen at 111..265 m (not crashes: no crashed vehicle within 5 m at the next tick, which
  is how a traffic car that crashes goes on); cars at 268..275 m toggle in and out tick by tick as the camera bobs. On the
  PS2 most of this hid behind the streamed track and the fog; with the whole track drawn it is in plain view. The fix
  (renderer only, the game's traffic untouched): (a) a traffic car fades in over 0.6 s of game time (Tick::now) from the
  first tick of its unbroken run within the radius (found by looking back over the timeline's ticks: the same guest object
  and model, not kFar, no jump over 20 m between ticks; the run reaching the timeline's start, a reset or stopped clock
  counts as old); (b) a car removed inside the radius stays for 0.6 s, moving on at its last speed and fading out
  (`traffic_vanishing`, appended to the frame's vehicles by frame_at); (c) the distance factor is a smoothstep from 1 at
  200 m to 0 at 275 m, only for traffic that is not crashing and for crashed vehicles (race cars are drawn at any
  distance by the game; mac5's 25 m fade hid them beyond 275 m); (d) the product is the car's opacity: car.frag scales
  its premultiplied output (`CarParams::fade` < 0; Z still written, cars below 1/256 not drawn), the shadow strip's alpha
  and the car's corona colours by it. mac5's 4 x 4 screen door is kept for the Time Attack ghost only: on a car a few
  pixels across it read as a speckled block. The probe prints one line per car coming in (spawn / range / jump, distance,
  screen position), per tick of its fade-in and per car leaving (removed / out of range / slot reused, the nearest wreck,
  whether it fades out). Test: `test_traffic_fade`.

**4. Damage deformation** [C]. RenderVehicle draws the body through RenderDeformedObject 0x21F790 with the 8 zone matrices
at VehicleParams +0x500 while +0x101B is set, and panel p with the 2 at +0x900 + 0x80 p while +0x101C[p] [C draft 0x21E6F0,
"every op from the asm"]; RenderDeformedObject appends them to the VU1 header after its 13 quads (974 + 4 k) and puts
the count in 1013.z [C draft]; VU1 0x46BB80 0x02C..0x099 blends per vertex: share u / 65535 of the zone at (974 + u) mod
1024 for both influences (u = weight << 8 | zone << 2), the rest on the identity (3x3 only), n not renormalised
(car-shading.md §4.1). Wheels go through RenderObject (no zones). car.vert does the same (`zone_share`); the capture takes
the matrices while +0x101B or any +0x101C is set (before render6: +0x101B only).
- Per-vertex check against the VU1 interpreter (`b3vucheck_car --shader --zones race2.zones`: 2,662 distinct zone sets
  captured in race2, 1,313 body / 1,349 panel, from `b3replay --info --dump-zones`; 8-zone sets on the body list, 2-zone
  sets on the panel lists, none on the wheels): 107 cars, 23,317 batches; the shader's deformation from the batch's raw
  position / normal / influences against the model, 378,341 deformed vertices: position max |err| 7.6e-6 m, normal
  1.2e-7; the model against the interpreter as before (screen x / y 0.068 / 0.074 px, RGB 1, alpha 1). ALL WITHIN
  TOLERANCE. The data never addresses a zone outside the uploaded ones (body 951,262 / panel 502,776 nonzero influences:
  0 beyond 8 / 2 zones, 0 not a multiple of 4).
- Captured deformation: race2 17 crashed vehicles (largest zone translation 3.23 m, a flatbed trailer at poll 3789);
  roadrage1 the four race cars (1.59 m); crash1-props1 the player's car from poll 11338.
- Pictures (`--no-deform` for the before; scratch, not committed): race2 poll 3789 free view of the trailer (bent stakes),
  crash1 polls 11420 / 11600 game camera (the player's car and the upturned car on the left), roadrage1 6296 (the
  airborne race car).

**5. Capture exactness** (build sha256 c1a133d9...) [C]:

| Session | Capture on | Capture off (same exe) | GS stream on vs off |
|---|---|---|---|
| race2 | 5,181 of 5,181 polls exact, 0 other EE cycle counts, 0 other step counts; 10.3 MB (was 10.0) | 5,181 of 5,181, 0 other cycles | **IDENTICAL over 8,030 frames (every column)** |
| roadrage1 | 13,474 of 13,474 exact, 0 other cycles, 0 other step counts; 44.3 MB (was 44.2) | 13,474 of 13,474, 0 other cycles | p0/p1/p2 identical in all 17,645 frames; 79 front-end frames (1787..4683) differ in `bytes`/`full`/`hdr`/`masked` only, the known host-record variance (§12.2, §12.7) |

Captures: `$P/scene/race2-render6.b3scene`, `$P/scene/roadrage1-render6.b3scene` ($P = %B3_WIN%\perf).

### 12.9 Crash mode: the crash ramps and pickups (crash2, 2026-11-03)

In b3replay race2's car (US/C3_V1, Crash mode, stage logic `CB3OfflineSingleCrashLogic` 0x4DF360) went up into the air at
the crash junction with no ramp under it. The ramp is not track geometry: the game draws it every frame from the event's
game data. Code: `include/b3asset/crash.h` + `src/b3asset/crash.cpp` (GAMEDATA.BGD, the geometry),
`src/b3view/crashscene.{h,cpp}` (the draws, shared by b3replay and b3view), the capture block in `src/gameview/scene_capture.cpp`,
`src/b3replay/capture.*` (`CrashState`), `view.cpp` (`crash_draws`), b3gfx `DrawItem::post_fog`. Tags as above.

**What draws it** [C unless noted; decomp addresses]:

| Step | Evidence |
|---|---|
| `CB3World::Render` 0x276300 calls `CB3GameData::Draw(idx)` on gWorld+0x125B40 right after `CB3Fog::Render`, then `func_0021C010` (the STATIC +0x28 backdrop group, track-format.md §6 q6; not crash data) | matched `B3WorldPS2_00276300.cpp:167-170` |
| `CB3GameData::Draw` 0x14E8C0: with a stage logic (D_0051BA88 +0x1B4 != 0) it calls `CB3CrashPickup::Draw` for each of the track data's +0x3F8 pickups (CB3GameData + 0x10 j) and `CB3CrashRamp::Draw(CGtTexture*)` for each of its +0x3FC ramps (CB3GameData + 0x140 + 0x70 j, texture CB3GameData +0x454) | matched `B3GameData_0014E8C0.cpp` |
| `CB3GameData::Prepare` 0x14F050, state 0x17, only when `func_00131E40` returns true (IsCrashMode, [L] E-map note): +0x454 = the texture named "ramp" (D_004B60F0), ramp j from track data +0x3E4 + 0x50 j; pickups from +0x3E0 + 0x20 j filtered by their mode mask +0x11 (bit 0 normally, bits 2 / 1 when the logic's virtuals 0xA8 / 0xA4 return true), textures D_00488080[9] = "pickup_boost", "pickup_score01..03", "pickup_multiplier_skull", "pickup_multiplierx2/x4", "pickup_gamebreaker", "pickup_stealer" | nonmatching draft "every state accounted for" [L]; strings read from the ELF [C]; all ten textures in DATA/GLOBAL.TXD [C] |
| `CB3CrashRamp::Prepare` 0x3A1EB0 on a `CB3CrashModeRampStruct` (CGtMatrix3x4 rows X, Y, Z, P, then s32 type +0x40): six points P +- Z b +- X a, the +Z pair raised by Y c (-X) / Y d (+X); type 0: b 4, a 3, c = d = 1.2; 1: 6, 4, 3, 3; 2: 7, 2.55, 3.2, 1; 3: 7, 2.55, 1, 3.2 (2 / 3: sideways-tilted kickers) | nonmatching draft (VU register allocation only) [L]; checked against the game's own corners below |
| `CB3CrashRamp::Draw` 0x3A1800: returns if D_00665EA1 != 0; else B3Im3d with alpha-test threshold 0.5 and the ramp texture, four tri lists: +X side (p5 p3 p1), -X side (p4 p0 p2), back (p5 p0 p1, p0 p4 p5), slope (type 2: p5 p2 p3, p5 p4 p2; else p5 p4 p3, p4 p2 p3); UVs as stored at vertex +0x10 / +0x14 (slope: the texture's upper half, sides and back: its lower half); no bottom face; colour lWhite = (0.5, 0.5, 0.5, 1) x 128 = GS (64, 64, 64, 128) (static init 0x4DA3A0) | asm, every store read |
| `CB3CrashPickup::Draw` 0x3A0AA0: returns if taken (+0x8) or not prepared; a diamond at the struct's position facing player 1's camera manager camera (gGame +0x29010 CGtQuatPos, the ConvertToMatrix right row), top / bottom pos +- kPickupUp (0, 1.4, 0), sides pos -+ right x 1.4, UVs kTopUV (0, 0), kLeftUV (1, 0), kBottomUV (1, 1), kRightUV (0, 1), colour (0.6, 0.6, 0.6, 1) x 128, alpha-test 0.078125 | asm (constants from static init 0x4DA310); the body: nonmatching draft "every block mapped" [L] |
| B3Im3d state: textured vertex colours sent as is (`RenderTrilist` 0x364B70 doubles only untextured ones), ALPHA_1 0x44 (blend by As), TEST_1 0x5000B with AREF = 128 x threshold (`SetAlphaTestThreshold` 0x365230), Z written (the camera record's ZBUF; only `SetZWrite(false)` 0x365460 masks it), no back-face cull (`_gbBFC` in .sbss = 0, set only by the wrong-way wall, to false) | matched units; `BeginRendering` 0x365640 nonmatching [L] |

**The data: GAMEDATA.BGD** (§4.1 had it as "game data, not render data"): a 0x800-byte directory of up to 50 stages (u64
id +0x08, header offset +0x198, count +0x260); per stage a 0x800-byte header whose +0x3C0 / +0x3BC locate its
`CB3TrackDataStruct`: 6 start positions (0x50 each, position +0x30 [L]), start / split lines, +0x3E0 / +0x3F8 pickups,
+0x3E4 / +0x3FC ramps. All 36 routes parse: **377 stages, 81 crash ramps, 1,333 crash pickups** (`make test`,
real_gamedata_all_routes). US/C3_V1: 14 stages; the crash events are 6-10 (13-15 pickups each, 1 / 1 / 3 / 3 / 0 ramps).
Which stage the game loads is its run-time choice (CB3GameData +0x558 = the directory id), so the ramps are run-time
state and the capture has to name the stage.

**race2** [C, `b3replay --info`]: the player's car starts on stage 6's start point 0 (5075.53, 148.75, 1862.80; the car
at y 148.98), the only stage that fits; stage 6 has one type-0 ramp at (4918.38, 148.72, 1679.79) with its Z axis
along the car's heading. The car is on the ramp's 8 x 6 m footprint at polls 3190..3197, leaves the high edge at 3197 and
peaks 2.41 m above the ramp's base at 3216 (fx1's count of 220 race-car ticks without a shadow strip, the "> 1.25 m up"
case, falls in this flight). Stage 6's four mode-bit-0 pickups: boost (5016.8, 150.7, 1772.5) before the junction, then
score01, score03 and score02 after it.

**Capture** (no version change; readers detect the block by its presence, so version 1-3 captures keep loading):
CB3GameData [0, 0x590), the track data's counts, each pickup and ramp data struct, D_00665EA0 [0, 0x10), and the camera
manager's CGtQuatPos. b3replay draws the stage the capture names: the ramps from the file's matrices (checked against the
captured corners, below), the pickups at the captured positions unless taken, facing the captured quaternion's right row.
Older captures (including the local `data/scene` ones) fall back to the stage whose start positions hold every race
car at the segment's first tick, if exactly one does [L] (US/C3_V1 stages 7-9 share their start points, so those would
draw nothing), with the mode-bit-0 pickups.

**Drawing** (b3gfx `DrawItem::post_fog`): the game draws these after the fog, the shadows and the pre-fog effects, so they
go in a stage after fx1's effects stage, in list order, unfogged, Z tested and written. They use track-model type 0
(MODULATE, the B3Im3d TEST_1, ALPHA 0x44). The "ramp" texture's alpha is only ever 0 or 0x7F, so the texels that pass the
test blend at 127/128. b3view: `--crash-stage N` / K shows a stage's ramps and pickups on the static route (pickups facing
the viewer).

**The same stage also draws the route's barriers** (STATIC +0x28, track-format.md §6 question 6, until now never drawn by
b3asset's default reader): `func_0021C010`, called right after CB3GameData::Draw, draws entry [current backdrop block
+0x6D] of the STATIC +0x28 table through `CB3StaticTrack::RenderBackdrop` when the streamed track is on, +0x6D >= 0,
D_00665F1E != 0, the view's gWorld+0x12AD50 block +0x18EA == 0, D_00665E53 == 0, and not (IsCrashMode and the stage
logic's slot 0xAC nonzero) [C asm]. Every route has one or two such entries, textured "Barrier" and "Arrows" (US/C3_V1:
102 triangles, EU/C4_V1 102, AS/C1_V1 502) [C b3asset with `extra_backdrops`]: "Barrier" is a red X, "Arrows" a
light-bulb chevron (track material type 0, flags 0x208: its UV scroll runs the chevron; ALPHA 0x44, TEST 0x5000D) [C the
decoded textures]; in the pictures the Xs close side streets and the arrow boards line the edges [L: route boundary
signs]. IsCrashMode (`func_00131E40`) is the logic's slot 0x98 == 6 or slot 0x9C in 3..5; slot 0xAC is a
constant 0 except in the crash logics (0x1342F0: logic +0x40 == 1 or 4) [C asm, the 16 CB3*Logic vtables in the ELF].
The capture adds those inputs (the slots, their first 0x24 bytes of code, logic +0x40, D_00665E53, the view block's
+0x18E0); b3replay draws the entry as track draws in the post-fog stage when the decision is "drawn" (older captures:
not drawn, as before).

**Checks** [C, `b3replay --info` on `$P/scene/race2-crash2.b3scene`]:
- The capture names stage id 1a0f229ff68040d4 = directory entry 6, the stage the start-position fallback picks; game
  data in 2,229 of 2,229 segment-0 ticks, stage logic in all.
- Captured ramp corners against Prepare's formula on the file's matrix: 2,229 ramp-ticks, max |diff| 0.000977 m (2 ulp at
  x ~ 4900: VU float rounding); the captured matrix equals the file's exactly; type and count agree in every tick.
- The 4 prepared pickups are exactly stage 6's mode-bit-0 ones; taken at polls 3050 (boost), 3210 (score03), 3245
  (score01).
- D_00665EA1 is set at polls 4316..4613 (the end of the crash segment): the game draws no ramp there, and neither does
  b3replay with this capture (older captures draw it).
- The pickups' camera (camera manager quaternion's right row) equals the viewport frame's right row except at the cuts
  4316 and 4615..4622.
- Barriers (func_0021C010's decision per tick): race2 IsCrashMode in every tick, entry 0 drawn at polls 2494..3198 (the
  run-up, until the launch), then hidden by the crash logic's state (+0x40 = 1 or 4); roadrage1 (`$P/scene/roadrage1-crash2.b3scene`,
  Road Rage, stage 3, no ramps or pickups) not crash mode, entry 0 drawn in 7,048 of 9,552 ticks (3557..5206, 5808..6149,
  6661..7286, 7726..10405, 10692..12031, 12375..12784; elsewhere the current block's +0x6D is -1 or a test fails).

**Capture exactness** (build at 9082787, the branch merged with main, sha256 99bbd968...) [C]:

| Session | Capture on | Capture off (same exe) | GS stream on vs off |
|---|---|---|---|
| race2 | 5,181 of 5,181 polls exact, 0 other EE cycle counts, 0 other step counts; 10,984,944 bytes (main's version-3 race2: 10,981,320) | 5,181 of 5,181, 0 other cycles | **IDENTICAL over 8,030 frames (every column)** |
| roadrage1 | 13,474 of 13,474 exact, 0 other cycles, 0 other step counts; 47,181,616 bytes (version 3: 47,179,080) | 13,474 of 13,474, 0 other cycles | p0/p1/p2 identical in all 17,645 frames; 77 front-end frames (1787..4679) differ in `bytes`/`full`/`hdr`/`masked` only, the known host-record variance (§12.2, §12.7) |

An intermediate exe without the barrier inputs (2603c61e...) gave the same results (race2 identical over 8,030 frames;
roadrage1 75 front-end frames, `full`/`hdr`/`masked`). Captures: `$P/scene/race2-crash2.b3scene`,
`$P/scene/roadrage1-crash2.b3scene`.

Pictures (640x448 offscreen, `--no-crash` = before; game-derived, not committed: `%B3_WIN%\crash2\pictures\`):
race2 `race2-p2600-*` (the junction in the run-up: red X barriers on the side roads), `-p3180-*` (approach: the chevron
wedge on the lane ahead, a score pickup further on), `-p3196-*` (the car on the slope), `-p3192-side-*` / `-p3204-side-*`
(free view from the kerb, `--free-eye 4910.5,153.5,1692 --look-at 4917.5,149.5,1680.5`: the car on the ramp, then airborne
past its high edge), `-p4400-*` (ramp hidden by D_00665EA1); roadrage1 `roadrage1-p4000-*` (an arrow board along the river
edge), `-p6000-*` (a red X closing a side street); `b3view-c3-{6,8,none}.png` (b3view's static route at the junction with
stages 6 and 8).

**Not done.** CB3GameData::Draw's start / finish lines (`func_0015E720`) and split-time checkpoints, drawn under the stage
logic's vfunc 0x44 flags (not checked in crash mode). The pickups' option sets (mode bits 1 / 2) are exact only with a
crash2 capture. Other Crash-mode visuals (Crashbreaker explosion, debris, popcorn, crash-camera blur) are effects, not
game data (milestone 2, docs/effects-survey.md).

## 13. The native renderer live in a play session (live1, 2026-11-03)

Milestone 1 of "running the game with the native renderer": while the game runs, the native renderer draws the race
live in its own window, from the scene hook's per-poll state (in memory, no file), with the game camera, uncapped,
interpolated between the 60 Hz ticks, through the same draw code as b3replay. Tags as in the rest of this document.

### 13.1 Design

**A separate renderer process, fed through shared memory** (`b3replay --live <name>`): in its own process the viewer
cannot touch the game's input, and a crash or a stall of the viewer cannot stop the game.

**The ring** (`include/b3live/ring.h`, header-only, Win32 named mapping `Local\<name>` / POSIX shm): a single-producer,
single-consumer byte ring (32 MB; 128 MB since font1, §22.7) of records. Each record is exactly one scene-capture record (§12.1's format), stamped
with the writer's steady-clock time.
- Writer (the scene hook on the game thread, once per retail poll): appends
  the poll's delta and publishes it (`reserved`, bytes, then `head`). **It never waits and never reads the reader's
  position.** Its only extra work per poll is copying the record it already builds for the file.
- Keyframes: when the ring has none yet, or the reader asks (`want_key`), the poll's record is sent as every valid block
  of the writer's shadow image instead of the delta. A reader that starts at a keyframe and applies every later delta
  holds, at each poll, the same image as a reader of the capture file (the shadow is the union of everything written
  so far). So the live feed decodes the same ticks [C below].
- Reader: copies a record, then checks that `reserved` has not passed its start plus the capacity (nothing was being
  overwritten); if it was lapped, it sets `want_key` and restarts at the next keyframe, as a new segment. The writer's
  process id lets it notice a game that ended without closing the feed (a killed session).

**Threading.** The game never waits for the renderer: the hook copies the poll's sparse blocks (2-10 KB in a
race) into the ring and returns. The renderer's one thread drains the ring at the start of every frame (decode = the
offline reader's code), keeps the last 20 s of ticks, and draws. Track loads (~0.3 s) block only the viewer; the ring
holds minutes of polls meanwhile.

**Shared draw path.** `src/b3replay/view.{h,cpp}` (moved from b3replay's main.cpp in fc43c6b, behaviour-identical) holds
route and car matching, the frame interpolation (`frame_at` over a `Timeline`: the offline segment or the live feed's
ticks) and the draw list (`View`). The offline replay and the live viewer call the same functions.

**Following the session** (`live.cpp`). Segments are cut as the offline reader cuts them: track on, camera, route and a
race car; gaps up to 60 ticks hold the last race tick; a route change, a longer gap or a resync starts a new one. 30 race
ticks into a segment, the route is matched by STATIC.DAT size and unit count plus the camera score (render4's
`find_route`), and matched again with 10 s of samples (V1/V2 pairs share size and units). A different route is loaded,
the same one kept. Cars are matched by their header bytes on first sight. Between races (front end, loading) the window
is empty and its title says so.

**Display clock.** The viewer shows tick position x, which advances at 60 ticks per second of real time and is pulled
gently (gain 2/s) towards `newest tick + time since it arrived - delay`; jumps over 30 ticks snap. The delay defaults to
3 ticks (50 ms): 2-tick crash frames move the camera every second tick, and the camera interpolation needs the next
change. `[` / `]` change it in the window. Interpolation is §12.3's (slerp / lerp per object by guest address and
model, camera between its change ticks, cuts not blended).

Two interpolation details changed with the live path (e256689; offline too, since both use the same code):
- The camera keys are looked up within 8 ticks either way. A camera held longer is shown as is, instead of drifting
  slowly towards a change far ahead: a live feed has no far-ahead ticks, and the drift was an artefact. race2's
  `--info` at 144 Hz: camera repeats 5.4 % -> 5.5 %, median step 0.082 -> 0.086 m; roadrage1 unchanged.
- `--offscreen --alpha 0` is the tick itself; it was 1e-6 past it, which sent every matrix through the quaternion
  round trip.

**Viewer options.** `--max-fps N` caps the viewer's frame rate, `--vsync`, `--delay`, `--size`, `--msaa`. Without a
window (headless proofs): `--shots` writes frames at given polls, `--bench WxH` renders every display frame offscreen
and reports frame times, and `--verify <capture>` compares every tick read from the ring with the file reader's tick of
the same poll. The writer side needs only `include/b3live/ring.h` (no SDL, no b3gfx). The viewer is `b3replay.exe` from
`tools/render/build_win.sh`.

### 13.2 Proofs (headless; viewer sha256 97340bae...)

Each session was replayed with the capture file and the live feed, the offscreen viewer attached (shots, bench,
verify) [C]:

| Session | Ring | Ticks from the ring vs render4's capture file | Live frames vs `b3replay --offscreen` (640x448) |
|---|---|---|---|
| race2 | 5,181 records, 1 keyframe, 0 dropped, 0 resyncs | 5,181 identical, 0 differ | polls 2500, 3000, 3600+0.5, 4300+0.5, 4800 (second segment): 0 pixels differ in each |
| roadrage1 | 13,474 records, 1 keyframe, 0 dropped, 0 resyncs | 13,474 identical, 0 differ | polls 4000, 6000+0.5, 9000, 12000+0.5, 13000: 0 pixels differ in each |

The capture files written in these runs are byte-identical to render4's (`cmp`), so the hook with the ring
writes the same records as before. The offline reference frames come from the same Windows exe on the RTX 4080
(Vulkan) with render4's captures. The runs used the viewer before `--max-fps` was added (window path only); the
delivered exe (97340bae...) reproduces all 10 reference frames identically.

**Render frame times** (the viewer offscreen at 1280x720, MSAA 4x, game camera, interpolated, every display frame drawn
and read back while the replay ran; the window path draws the same list without the read-back) [C `live.txt`]:

| Session | Frames | Rate while racing | Frame p50 / p95 / p99 | CPU: drain + frame + draw list p50 / p95 / p99 | GPU draw + read-back p50 / p95 / p99 |
|---|---|---|---|---|---|
| race2 | 35,891 | 635 fps | 1.45 / 2.26 / 3.08 ms | 0.05 / 0.17 / 0.27 ms | 1.38 / 2.15 / 2.98 ms |
| roadrage1 | 122,721 | 570 fps | 1.63 / 2.57 / 3.33 ms | 0.05 / 0.13 / 0.17 ms | 1.57 / 2.48 / 3.24 ms |

The maxima (565 / 485 ms) are the frames that loaded the track (310 / 341 ms) and the first car models. Two other lanes'
replays ran on the same machine during these runs.

Linux tests (`b3livefeed`: a capture played into a ring under the writer's rules; Vulkan offscreen in WSL):
- Whole session at 10x real time: race2 5,181 and roadrage1 13,473 ticks identical, shots 0 pixels differ.
- Reader joining late: starts at the ring's keyframe.
- Lapped reader (256 KB ring at 2,000 polls/s with the bench drawing): 2 resyncs; every tick received after them is
  identical (2,454 of 2,454).
- Writer killed mid-race: the reader ends ("ended without closing the feed").

### 13.4 Not yet (milestone 1 gaps)

- The free camera, pause and seek of offline b3replay are not in live mode. Split screen and the front end are not drawn
  (not captured, §12.5); the look is b3replay's (cars on the simple path until render5 lands).
- The hook's per-poll cost on the game thread was not measured separately [L: it reads ~10-60 KB of guest memory
  through a per-byte callback; render4's exactness runs already include it].
- Tick timing comes from host arrival, so if the game itself stalls (a hitch) the viewer shows that
  stall too, smoothed by the pull towards the newest tick.

## 14. Effects: car shadows and skid marks (fx1, 2026-11-03)

First step of milestone 2 (the whole race through the native renderer): items 0-2 of docs/effects-survey.md §5, i.e. a
shared effects base (capture version 3, an effects draw stage in b3gfx / `View`), the car shadows and the skid marks.
Both are drawn the game's way from captured inputs, in the offline replay and the live viewer alike (`View`). Code:
`src/b3replay/effects.{h,cpp}`, `include/b3asset/trailshade.h` + `src/b3asset/trailshade.cpp` (the skid-mark VU program
as C++), `tools/render/b3vucheck_trail.cpp`, capture fields in `src/b3replay/capture.*`, the hook's `grab_effects` in
`src/gameview/scene_capture.cpp`, the stage in `src/b3gfx/b3gfx_sdl.cpp`. Decomp citations: HEAD fd4f39a072 (read
only). Tags as above.

### 14.1 Capture version 3

Same records as version 1/2; version 1 and 2 captures still load and draw (no effects). Everything is plain main RAM read
through the guest's own pointers, as §12.1 (no writes, no host clock).

| Value | Guest source | Tag |
|---|---|---|
| Shadow pass / effects switches | gGraphicsManager +0x67 (`D_00665F27`), +0x6C (`D_00665F2C`): already in [0, 0x3C0) | [C] RenderShadows 0x3033F0, RenderPreFogEffects 0x2620C0 return when 0 |
| Race-car shadow input | car +0x510 (ground normal xyz, distance w), +0x60 darkness (EssentialParams), +0x1914, +0x18E8 | [C] `RenderShadows__12CB3ShadowMgrFi.cpp` (draft, calls and offsets from the asm, fix2 at 0x3034F4 / 0x303500) |
| The player car hidden | `CB3PlayerCameraManager::IsPlayerVisible` 0x2E7340 of view 0 (0x4EE040 + 0x28E00): flags +0xB80, +0x9E8 / +0x9F0 / +0x9F4, its CB3BehaviourInterpolate +0x8C0 (`GetTimeElapsed` 0x373940: +0x120 object +0xB88 / +0xB8C, +0x124, +0x12C, +0x144) | [C] both matched; the decoder evaluates the same function |
| Traffic shadow input | the view's list: s = gWorld + 0x12AD50 + 0x19AC, count gWorld+0x4DE0+0x36350+s, indices +0x35C24 + 0xFE s; per vehicle the normal = EssentialParams +0x10 (its up row, vehicle +0x80), distance = (gWorld+0x4DE0+0x343F0)[vehicle +0x176] +0x18, darkness EssentialParams +0x60 (vehicle +0xD0); skipped when +0x174 bit 2, +0x17A bit 1 or frustum +0xCB = 0 | [C] RenderShadows draft |
| Crashed-vehicle shadow input | +0x1870 (normal, distance), +0x13C0 darkness, +0x13BB drawn | [C] RenderShadows draft |
| Shadow geometry | vehicle data +0x18 wheel radius, +0xE80 / +0xE90 box, wheel positions +0xBB0 + 0x40 k (z of wheels 1 and n-2), krShadowDarkness `D_004E0FC0` (3.3333333) | [C] `RenderAmbientShadow` 0x303170 draft (every op from the asm); captured values equal the car files' bytes at those offsets: race2 8, roadrage1 9 vehicle datas, 0 differ |
| Skid marks | trail system = gEffectsManager 0x1D9D200 + 0x2600: per type 0..4 the emitter list +0x13C0C + 0x208 t (count +0x200); each listed emitter [0, 0x14) (+0x0 last time, +0x10 s8 count, +0x11 type, +0x12 flags); its point data (16 x V4-32 xyz / time, then 16 x 3 s8 side vectors); the half index +0x14634; `D_01E865E0` (5 x 0x30, the per-type VU rows); `CB3TrailSystem::mgaDefaultParams` 0x49FFF0 (5 x 0x50: texture name, ALPHA / FIX word +0x20) | [C] `func_002BD560`, `CB3TrailRenderer::Render` 0x2F3600, `AddTrailSegment` 0x2BD320 |
| VU check only | camera record 0 (gGraphicsManager +0x470) +0x120, 10 qw (VU 1014..1023): already captured | [C] BeginRender 0x2F38B0 |

**The emitters' pointer swap.** `func_002BD560` swaps every emitter's two data pointers (+0x8 / +0xC) after each rendered
frame and copies the drawn half of the 2 x 0x9800 buffer into the other. Captured raw, every listed emitter changed
every tick: roadrage1 grew from 44.2 MB (version 2) to 72.0 MB, 25.7 MB of it emitter structs [C byte tally]. +0x8 is
always half (+0x14634) x 0x9800 + the emitter's slot (its index in the 128-emitter array) x 0x130 [C: 177,412 of
177,412 listed emitters of race2], so the hook captures an emitter only when a field other than the two pointers changed,
and the reader derives the data address. Size now:

| Session | Version 2 (props1; render6's recapture in data/scene) | Version 3 (fx1 before the merge; after it, with render6's traffic [0, 0x80)) | Growth |
|---|---|---|---|
| race2 (5,181 polls) | 10.05 MB; 10.29 MB | 10.91 MB; 10.98 MB | +0.86 MB (8.5 %); +0.69 MB (6.7 %) |
| roadrage1 (13,474 polls) | 44.19 MB; 44.26 MB | 47.28 MB; 47.18 MB | +3.09 MB (7.0 %); +2.92 MB (6.6 %) |

**Exactness** (build sha256 8d41e405... before
the merge with main (render6), 957b45f9... after it; the table is the post-merge pair of runs, the pre-merge pair gave the
same verdicts with 78 instead of 76 front-end frames) [C]:

| Session | Capture on | Capture off (same exe) | GS stream on vs off |
|---|---|---|---|
| race2 | 5,181 of 5,181 polls exact, 0 with other EE cycle counts | 5,181 of 5,181, 0 other | **IDENTICAL over 8,030 frames (every column)** |
| roadrage1 | 13,474 of 13,474 exact, 0 other | 13,474 of 13,474, 0 other | GS path payloads (p0/p1/p2) identical in all 17,645 frames; 76 front-end frames (1787..4681, the menu movie loop) differ in `full`/`hdr`/`masked` (1 in `bytes`), every column identical from frame 4682: the run-to-run host-record variance of §12.2 (props1: 79, live1: 75 such frames) |
| races (check capture only) | 21,011 of 21,011 exact, 0 other EE cycle counts (23 polls with other EE step counts only) | 21,011 of 21,011, 0 other cycles (14 polls with other step counts only: the session's own run-to-run variance, without any capture) | p0/p1/p2 identical in all 27,887 frames; 100 front-end frames (3135..5553) differ in `full`/`hdr`/`masked` (6 in `bytes`) only | 78,300,224 bytes |

The live feed decodes the same ticks: `b3livefeed` race2-fx1 into a ring, `b3replay --live --verify`: 5,180 ticks
identical (effects included in `same_tick`), 0 differ [C].

### 14.2 The effects stage (b3gfx, View)

`DrawItem::effect` items are drawn after every other draw (b3gfx phase 4: after the sky, the opaque draws, the
full-screen specular and the blended draws), in list order, as `CB3World::Render` 0x276300 draws `RenderShadows` and
`RenderPreFogEffects` after the track, the cars, `RenderFullScreenSpecular` and `RenderAfterSpecular` (props) [C matched
`B3WorldPS2_00276300.cpp`]. They use the track model's GS stage (mesh.frag kTrack: MODULATE, TEST_1, colours in GS units)
and a blend from the GS ALPHA register `gs_alpha`: (A - B) C + D with C = As (or FIX / 128 through `blend_fix`) as the
shader's alpha, so only the destination factor varies: B = D = Cd -> ONE_MINUS_SRC_ALPHA (0x44), B = 0, D = Cd -> ONE
(0x48, additive), D = 0 -> ZERO. RGB only (both effects set FRAME FBMSK 0xFF000000 or leave the frame alpha alone),
Z tested GEQUAL (reverse Z) and not written (ZBUF ZMSK), no culling, `clamp_uv` = CLAMP_1 clamp (a bilinear clamp
sampler). Per-frame geometry goes through new dynamic buffers (`Device::update_vertex_buffer / update_index_buffer`:
grown in place, uploaded with cycling so a frame in flight keeps its data).

Fog: both effects are drawn before `CB3Fog::Render`, which fogs the frame by the Z buffer; they write no Z, so each
pixel is fogged by the ground behind it. The shader fogs Cs by the pixel's own depth (the same surface within 6 cm):
fog is affine, so fog(lerp(Cd, Cs, a)) = lerp(fog(Cd), fog(Cs), a) and the blend over the already-fogged ground gives the
same result [L].

`View::show_effects` (`--no-effects`, key F in b3replay and the live viewer) turns the stage off.

### 14.3 Car shadows

From the game's own code, not captured vertices: the inputs are 9 floats per vehicle plus the vehicle data's box (static),
so the native renderer rebuilds the strip and it interpolates with the vehicle (the shadow frame from the blended
EssentialParams matrix, normal and distance blended linearly). `b3replay::shadow_strip`:
- `RenderShadows` 0x3033F0 [C draft]: race cars with +0x18E8, and `IsPlayerVisible` when +0x1914 = 0; traffic in the
  view's list with the flags above; crashed vehicles with +0x13BB; height 0.06; nothing when `D_00665F27` = 0.
- `RenderAmbientShadow` 0x303170 [L draft, every op from the asm]: darkness = min(1, 3.3333 x EssentialParams +0x60);
  offset = distance - wheel radius; opacity = 1 - min(offset / 1.25, 1), nothing when <= 0; frame: up = the ground
  normal, right = normalise(up x forward (EssentialParams +0x20)), at = right x up (`CrossProduct` = a x b [C
  GtMathPs2.h]), position = EssentialParams +0x30 lowered by the distance; box x / z from +0xE80 (sup) / +0xE90 (inf), the
  wheel base's z from wheels 1 and n-2, all scaled by 1 + 0.4 offset.
- `RenderAmbientVehicleShadow` 0x303A40 [L draft, 114 of 114 instructions]: the 8-vertex strip (inf.x / sup.x) x (sup.z,
  front wheel z, rear wheel z, inf.z) at y = 0.06, V = 0 / 0.1875 / 0.8125 / 1 (`BeginShadowPlaneRendering` 0x303C10),
  RGB 0x80, alpha = int(90 darkness x opacity).
- GS [C 0x303C10]: GLOBAL.TXD "blobbyshadow" (`D_004C4550`), ALPHA_1 0x44, TEST_1 0x50003, ZBUF ZMSK, FRAME FBMSK
  0xFF000000, CLAMP_1 5; B3Im3d's VU1 program 0x47A060 only transforms (not read here [L]).

| Strips per tick (`b3replay --info`) | race cars | traffic | crashed | all: mean / max |
|---|---|---|---|---|
| race2 segment 0 (2,229 ticks) | 0.90 (220 vehicle-ticks with none: > 1.25 m above the ground) | 5.70 | 7.71 | 14.32 / 24 (poll 3903) |
| roadrage1 (9,552 ticks) | 3.74 (2,517 none) | 0.93 | 0.03 | 4.70 / 10 |

These are the game's `RenderAmbientVehicleShadow` calls by construction (its conditions on the captured state); the GS
stream was not decoded to count them independently.

### 14.4 Skid marks (VU1 0x4771B0, read in fx1)

`CB3EffectsManager::RenderPreFogEffects` 0x2620C0 -> `func_002BD560` -> `CB3TrailRenderer::Render` 0x2F3600 per type,
each emitter's points to VU1 0x4771B0 (E `B3TrailRenderer.dsm` [L]). `b3asset::trail_ribbon` is the program's ribbon as
C++ in the program's float order (b3fp::ref), view-independent; the GPU projects and clips it [C program addresses in
`include/b3asset/trailshade.h`]:
- Rows (`CB3TrailRenderer::Construct` 0x2F3A60 from mgaDefaultParams; captured, not recomputed): 1008 = (w0 / 2,
  (w1 - w0) / life / 2, 1 / life), 1009 = start colour, 1010 = end - start colour (GS units).
- Per point k: a = min((now - t_k) / life, 1); half width = w0 / 2 / 127 + rate / 127 x a times the s8 side vector
  (unit side x 127.5); edges p +- side x hw; colour = start + delta x a (FTOI0, low 8 bits); s = k & 1, t = 0 / 1 on
  the two edges (sent as s q, t q, q: perspective-correct). One TRISTRIP +e0, -e0, +e1, -e1, ... (PRIM 0x5C, no fog
  bit, no ADC). Point 0 always has zero width (the program converts its side vector with `ftoi0` where the loop uses
  `itof0`: every ribbon starts as a point); trailing points not newer than point 0 are dropped; dead points draw at
  a = 1 (the end colour). Type 0's width turns negative after 30 % of its life (0.25 -> 0.0 over 0.3 s, then the edges
  swap); it has no caller.
- Types (retail data): 1 width 0.25, (0,0,0,64) -> alpha 0 over 15 s; 2 width 0.15, (10,10,10,64) -> alpha 0; 3 width
  0.25, (89,89,89,32) -> alpha 0; ALPHA_1 0x44 for 1-3, 0x48 (additive) for 0; texture "fxskid"; ZBUF ZMSK
  (BeginRender); TEST_1 is never set by the trail code (whatever the previous draw left [L]; drawn without alpha test);
  CLAMP at that point not known (s, t stay in [0, 1]) [G].
- The display time: `now` blended between the ticks, so the fades are evaluated at the displayed time.

**Check: the real program on the captured skid marks** (`b3vucheck_trail --capture <file>`: every tick's listed
emitters, the captured rows, `now` and the camera block through the VIF and VU1 interpreters, against `trail_ribbon`
projected the program's way) [C]:

| Capture | Emitters (ticks) | Strip vertices compared | Result |
|---|---|---|---|
| race2-fx1 | 177,412 (1,966) | 3,331,564: x / y (1/16 px), z, s, t, q, r, g, b, a, prim, adc: max error 0 | strip exact; 4 of 20,972 vertices re-emitted by the program's clipper have alpha off by 1 (its barycentric re-weighting; the GPU clips on its own) |
| roadrage1-fx1 | 1,088,512 (9,251) | 977,412, max error 0; clipper 70,065 inside vertices exact | ALL WITHIN TOLERANCE |
| synthetic (seed 20261103, 2,000 frames) | 35,028 | 368,638, max error 0 | ALL WITHIN TOLERANCE |

| Skid marks (`--info`) | listed emitters per tick | drawn (>= 2 points) | points per tick | max |
|---|---|---|---|---|
| race2 segment 0 | 72.5 | 67.8 (all type 1) | 858 | 122 emitters, 1,576 points (poll 4190) |
| roadrage1 | 113.9 | 97.3 (type 1: 88.9, type 2: 8.5) | 489 | 125 emitters, 797 points (poll 9370) |

### 14.5 Pictures (game camera, 640x448 offscreen, `--no-effects` = before; post-merge build; fx1's scratch directory, game-derived, not committed)

- race2 poll 3070 (chase): the player car's shadow and its own fresh tyre marks behind it; 9,700 pixels differ.
- race2 poll 3903 (crash junction, 24 strips): shadows under the RVs and the crashed car; 3,046.
- race2 poll 3300 free view over the junction (`--free-eye 4852,155,1618 --look-at 4867,149,1636`): the vans' shadows
  and two fresh tyre marks leading to the van; 3,270.
- roadrage1 poll 4410: tyre marks behind the car ahead and the three cars' shadows; 7,935. Poll 9350: marks on the
  gravel beside the wall; 9,282.
- The Windows exe (`%B3_WIN%\fx1\b3replay.exe`, RTX 4080 Vulkan) draws roadrage1 4410 the same way
  (mean difference to the WSL frame < 1 level per channel: different GPU and filtering).

### 14.6 Running it on Windows

```
%B3_WIN%\fx1\b3replay.exe %B3_WIN%\perf\scene\roadrage1-fx1.b3scene
%B3_WIN%\fx1\b3replay.exe %B3_WIN%\perf\scene\race2-fx1.b3scene
```
Key F toggles the effects (shadows, skid marks); the other keys as §12.4. the local `data/scene` recordings are
version 2 (no effects) until they are recaptured with a version-3 exe.

### 14.7 Not done

- The other effects (sparks, coronas, particles, debris, scrapes, water, sun, post): docs/effects-survey.md §5 order.
- The shadow strip's B3Im3d program (0x47A060) is assumed to be a plain transform; the front-end shadow
  (`RenderFrontEndShadow` 0x303750) is not drawn (front end not captured).
- Shadow and skid counts are not compared against the GS stream (no GS decoding of PATH1 here).
- The PS2 clips trails beyond view depth 1000 (the program's far limit); the GPU does not.
- Split screen: player 2's traffic list and track are captured since split1 (§29.5), player 2's IsPlayerVisible and
  view 1's own traffic results since split2 (§29.6); the camera record of view 2 is not.

## 15. Animated track materials: UV scroll and cycling textures (anim1, 2026-11-03)

Seen in b3replay: the light-bulb arrow boards move forward in the game and stand still in the native
renderer. They and every other animated track material now animate in b3replay (from the game's own clock and state)
and in b3view (on a wall clock).

### 15.1 How the game animates them [C asm unless tagged]

- **Which materials.** The STATIC.DAT header's update list: +0x10 = s16 material indices, count s16 +0x0E
  (`func_00332E00` state 23 calls `CB3TrackMaterial::Prepare` on exactly these). On all 37 routes the list holds exactly
  the materials with 2+ textures or flag 0x200: none outside it, none with both, none with flag 0x100 [C census of the
  files]. Seen kinds: UV scroll in steps (`Arrows`, speed 1.2 or 1.25, step 0.125: 36 materials), continuous UV scroll
  (`ATB_River`, `ATB_Waterfall`, `bk_skytrainads` +-0.1, `bk_billboards2`, `spray`, three waters: 52), cycling textures
  (`water` x 17 at 1/15, 1/20, 1/10 or 1/2 s, `Chgo_Flag`, signs `bk_warnsign*`, `Chgo_AccidentSign_Words`,
  `ATB_Sign_Info_*`, `Freeway_info_slow`, `Surf`: 120).
- **Who advances them.** `CB3StaticTrack::Update` 0x3329A0 (on gWorld+0x12A330), called by `CB3World::Update` 0x19A550
  at 0x19A5D8, which the main loop calls at 0x132E90 **inside the per-tick loop** (0x132DA8..0x132F28), after
  `CGtTimer::Update` of the world timer. So: once per 1/60 s tick, with **world time** `now` = gWorld+0x20 (`D_00522680`).
  Per listed material (fields of `CB3TrackMaterial`, docs/track-format.md §1.2):
  - 2+ textures: when `now > t + period` (+0x1C, +0x18): t = now; cur (+0x10) = (cur + 1) % count; next = (cur + 1) %
    count; period = +0x14 x (number(next) - +0x12) and +0x12 = number(next), or period = +0x14 and +0x12 = 1 when next
    is 0. number(k) = atoi(texture k's name + strlen(texture 0's name)) ("bk_warnsignb4" -> 4: the frame's start in +0x14
    units). Flag 0x100 runs the frames back and forth with +0x13 as the step (not used by any route).
  - Flag 0x200 (one texture): when `now x speed >= t + step` (+0x14, +0x18): t = now x speed if step is 0, else t + step
    (at most one step per tick).
  - `CB3TrackRenderer::ReallySetMaterial` sends 1 - (t - floor t) for flag 0x200 (VU1 qw 1005 y, added to every u; §11).
- **The clock** [C capture, anim1 race2 / roadrage1]: world time in seconds, dt 1/60 per tick; reset to 0 at each race
  load and restart (race2 polls 2061, 2485, 4615, 4967); held during the countdown, pauses and the crash replay (race2
  2494..2594, 3362..3379, 3581..3762, 4317..4614); **Impact Time slow motion runs it at 1/5** (dt 1/300: roadrage1
  4399..4567 and 14 more stretches), so the arrows and water slow down with it. `Prepare` runs a few ticks after each
  reset (race2 2493, 4623): +0x10 and +0x1C go to 0, +0x12 / +0x18 keep their last values, so the first periods after
  a restart come out odd (0, then -0.87 s for race2's water) -- the port reproduces that too.
- **Direction.** u += 1 - frac(t) with t growing: the Arrows texture moves along -u; on the boards that is the way the
  chevrons point (pictures, §15.4). The steps are the game's own (0.125 of the texture = one bulb column, every 6.25
  ticks at speed 1.2): the arrows jump about 10 times a second; they are not smoothed.

### 15.2 Capture (no version change; detected by presence)

`src/gameview/scene_capture.cpp` appends, after crash2's block: the update list (static track data +0x10, 2 x count bytes) and each
listed material's [0x10, 0x20) (+0x10 cur, +0x11 count, +0x12/+0x13, +0x14, +0x18, +0x1C). The static track data header
[0, 0x60) was already captured. Reader: `Tick::anim` / `anim_valid` (`decode_anim`); `same_tick` compares it (live
feed). `Tick::now` (gWorld+0x20) is now decoded for every version, and `Capture::load` uses `decode()` like the live feed.
Growth: race2 10,984,944 -> 11,018,216 bytes (+0.3 %), roadrage1 47,181,616 -> 47,264,688 (+0.2 %).

**Exactness** (build at e6b4896 + this change, sha256 1f288b1b...) [C]:

| Session | Capture on | Capture off (same exe) | GS stream on vs off |
|---|---|---|---|
| race2 | 5,181 of 5,181 polls exact, 0 other EE cycle counts, 0 other step counts; 11,018,216 bytes | 5,181 of 5,181, 0 other cycles | **IDENTICAL over 8,030 frames (every column)** |
| roadrage1 | 13,474 of 13,474 exact, 0 other cycles, 0 other step counts; 47,264,688 bytes | 13,474 of 13,474, 0 other cycles | p0/p1/p2 identical in all 17,645 frames; 78 front-end frames (1787..4679) differ in `full`/`hdr`/`masked` (3 in `bytes`) only, the known host-record variance (§12.2; crash2 77, fx1 76) |

Live feed: `b3livefeed` race2-anim1 into a ring, `b3replay --live --verify`: 5,181 ticks identical (animated materials
included), 0 differ [C].

**The port against the game's values** (`b3replay <capture> --info`, anim part) [C]: one step of
`b3asset::track_anim_update` from the previous tick's captured state reproduces every field bit for bit **when given the
previous tick's clock**: race2 2,227 of 2,228 ticks for each of its 4 materials (the miss is poll 2494, the Prepare tick)
and 218 of 218 in its second segment; roadrage1 9,551 of 9,551 for each of its 3 materials (slow motion included). So the
state captured at poll k is Update(now of tick k - 1): the poll (input update, 0x132DF8) runs before `CB3World::Update`
(0x132E90) in the same tick [C asm order]. The arithmetic must be the EE FPU's: with host round-to-nearest add/mul the
frame changes land a tick off at exact boundaries (race2 water: 2,154 instead of 2,227 exact; first at poll 2411,
0.2333 + 0.0667 vs 0.3); with `b3fp::ref` (truncating) none do.

### 15.3 Renderer

- b3asset: `Model::track_anims` (the list, with each frame's texture and number), `DrawRange::anim`;
  `track_anim_prepare` / `track_anim_update` (the port, op for op, EE FPU arithmetic) / `track_anim_at` (Update replayed
  from a fresh race at clock 0) / `track_uv_scroll`.
- b3view::Scene keeps its GPU textures; `b3view::animate(scene, states)` sets each animated item's texture (frames[cur])
  and `uv_scroll` (flag 0x200 only; the shader's existing `flags.y`, §11). Nothing else changes: other items are untouched.
- b3replay (`View::anim_states`, called by `draw_list`): the captured state of the displayed tick; a continuous scroll
  (period 0) is blended to the displayed time with the interpolation alpha (t is linear in the clock), so it is smooth at
  any frame rate; steps and frames change at their ticks as in the game. Captures without the block (older ones) fall
  back to `track_anim_at` on the previous tick's clock: right for continuous scroll; steps and frames as after a fresh
  race start [L], so they differ after restarts and wherever slow motion shifted the tick grid (roadrage1: arrows drawn
  as the game does at 9,551 of 9,552 ticks; water 745) [C --info]. `--no-anim` draws as before (frame 0, no scroll).
- b3view: an animation clock from window start (`AnimClock`: Update on 1/60 s ticks of the wall clock, continuous scroll
  exact at the frame time); offscreen `--time S` (default 0 = the prepared state). Region mode (several routes merged)
  keeps frame 0 and no scroll.

### 15.4 Proofs and pictures (640x448 offscreen; game-derived, scratch only, not committed)

- Pixel identity of everything else [C pngdiff]: main's b3replay (e6b4896) vs this one with `--no-anim`: 0 of 286,720
  pixels differ (race2 poll 3180 game camera, poll 3006 free camera at the arrow board; roadrage1 poll 4000). This one
  with vs without animation: race2 3180: 52 pixels (the far board); 3006 at the board: 39,074, all on the board and the
  distant animated water (diff mask). b3view main vs this one at `--time 0`: 0 differ (US/C3_V1 at the board and at the
  water); `--time 0.5` at the water: the water changes.
- race2 free camera `--free-eye 5732,162,2413 --look-at 5747,157,2413` (US/C3_V1's one board, STATIC +0x28 entry 0, drawn
  at polls 2494..3198), polls 3000..3015 every 3 ticks: the chevrons step one bulb column towards where they point every
  ~6 ticks (`r2-arrows-sequence.png`); roadrage1 poll 4000 (the river-edge board) with and without animation, 4004, 4008.
- Tests: `real_track_as_c1_v1` checks the 7-material list of AS/C1_V1, the arrows' steps and a sign's frame periods.

### 15.5 Not done

- Captures without the block (none of the committed ones after this change) animate from a fresh-race model (above).
- `RenderWater`'s own water pass and the environment clock (sky) are still not modelled (§11.7).
- b3replay's live shots (`--offscreen-live --shots`) frame the camera differently from `--offscreen` at the same poll
  (race2 3006: 245,002 pixels differ, the same with main's binaries): not anim1's, noted for the live-renderer owner.
  **Retracted (main d9de3d1, 2026-11-03 afternoon):** b3livefeed race2 at 120 ticks/s into `b3replay --live
  --offscreen-live --shots 3006,3180` vs `b3replay --offscreen --poll` (game camera, 640x448): 0 of 286,720 pixels
  differ at both polls [C pngdiff]. The 245,002 came from comparing against the free-camera board shot; note that
  `b3livefeed --rate 0` outruns the reader's 1,200-tick window (shots then report "not in the current race segment").

## 16. Coronas: the boost glow and the car and traffic lights (corona1, 2026-11-03)

Seen in b3replay: the boost glow from the exhaust or the back of the car was missing. The boost glow is a
corona: the same system draws every car's head / tail / brake / indicator lights and the traffic signals' lamps. Item 6
of docs/effects-survey.md §5 (survey §3.7). Drawn the game's way from captured inputs in the offline replay and the live
viewer alike (`View`); recomputed per displayed frame, so the coronas move with the interpolated cars. Code:
`src/b3replay/coronas.{h,cpp}` (the EE side and the drawing), `include/b3asset/coronashade.h` + `src/b3asset/coronashade.cpp`
(the corona VU program as C++), `tools/render/b3vucheck_corona.cpp` (the checks), the hook's `grab_coronas` in
`src/gameview/scene_capture.cpp`, `CoronaInputs` / `Vehicle::corona` in `src/b3replay/capture.*`. Decomp citations: HEAD
fd4f39a072 (read only). Tags as above.

### 16.1 What the boost glow is [C unless noted]

| Step | Evidence |
|---|---|
| `CB3RaceCar::Draw` 0x151A40 renders the car, then `ActuallyRenderRaceCarImmediateEffects` 0x2621D0 when +0x18EA is clear (while +0x199B, with the +0x1920 matrix); the cars come from `CB3World::RenderRaceCars` 0x199A20's drawn list (+0x1914, or not the view's own car (+0x27C0), or `IsPlayerVisible`; unit +0x5A visible; frustum +0x5B or +0x10A2), sorted **far to near** (comparator `func_00199D90`: -1 when a's key is larger) | matched `B3RaceCar_00151A40.cpp`, `B3EffectsManager_002621D0.cpp`; nonmatching RenderRaceCars draft (calls / offsets from the asm); asm 0x199D90 |
| 0x2621D0: `RenderLightCoronas(car, +0x18F0)` when the light byte +0x58 is nonzero, then **`func_00257230`** (unnamed) unless +0x18EA | matched |
| `func_00257230` = **the boost glow**: `func_0030AAE0(car +0x118C, out, +0x18F1)` (matched, unit `B3BoostEffectParams.cpp`: car +0x118C is the `CB3BoostEffectParams` [L by the unit]); with intensity t = its +0x14 > 0: array 2 ("coronaboostred") with colour D_004C4A20 = (0.8, 0.8, 0.8) and size 0.72 when +0x18F1, else array 1 ("coronaboost") with D_004C4A10 = (0.70, 0.72, 0.75) and size 0.6 (static init `func_004D6DF0`); t >= 1 scales the size by t, t < 1 the colour; nothing unless the size is above D_004E0C6C = 0.01 | asm, constants from the ELF; this retracts survey §3.7's "[G]" |
| Where: the vehicle data's **exhaust items**, pointer +0x1684, count u8 +0x16B4, 0x30 each: position, direction (vehicle space), weights u (+0x24) / v (+0x28) of two body zones a (+0x2C) / b (+0x2D); each item's frame = zone a x u + zone b x v + identity x (1 - u - v) of the car's 8 body deformation zones (+0x580, VehicleParams +0x500), then the car matrix (+0x0) | asm `lw 0x1684` / `lbu 0x16B4` (0x257334); the nonmatching draft's inline array is wrong (a pointer) |
| Per item three slots along the item's direction: +0.08 m (fade distance 800, size x 1), +0.14 m (400, x 0.5), +0.18 m (200, x 0.25); colours randomised in [c/2, c], [0.28 c, 0.56 c], [0.14 c, 0.28 c] (x 64, GS units) | constants D_004E0F44..F54 |
| The flicker: every call seeds VU0's R with `CGtRandom::RandomReal` on {~n, n}, n = effects manager +0x1F820 (`EndUpdate` 0x2623F0 reseeds it every update), draws three R values (VRNEXT) before the item loop, and per slot takes floor(s x r) - s x r with the 2^23 trick, s = lane x of `gEffectsRandomiserScales` (D_01D9D190), which then rotates one lane (vmr32); so every car in a frame shares the three R values and the rotation runs on from car to car | asm; checked bit-exact below |
| race2's car (US/C3_V1): 4 exhaust items, two pipes at x = +-0.39 / +-0.49, z = -2.22, pointing back, zones 1 and 2 | capture |
| "Red" boost: +0x18F1 = the car id +0x1960 is one of the 5 ids at 0x4B6180 (`CB3RaceCar::Prepare` draft, asm 0x152ED0); +0x18F0 (`RenderLightCoronas`' lbRearIndicatorsAreRed, Revenge STABS name) = one of the 2 ids at 0x4B6170. retail_data_names puts `kaRedIndicatorCarIDs` at 0x4B6180 (medium), but the E map gives it 0x10 bytes = 2 ids, which fits 0x4B6170 [L: the label is one symbol off; for the decomp lane]. No red-boost car in race2 / roadrage1 | asm |

The boost's other part, the radial blur (`CB3RadialBlur::Render` 0x2FA9A0, parameters `CB3BoostEffectParams::GetRadialBlurParams`
0x30ABA0), is a post pass: survey item 10, not done here.

### 16.2 The lights and the traffic signals [C asm; L drafts where marked]

`RenderLightCoronas` 0x256690 [L draft, every group / bit / colour / size C asm]: the camera point (effects manager
+0x1F810 = the camera position - 2 x its at axis, `func_002622A0`) in vehicle space gives the direction and distance d;
size = 1 + 0.02 d; per group each light (0x30: position, facing) facing the camera (facing . direction > 0) writes position
(fade distance 400) and colour x facing x 64:

| +0x58 bit | Group (vehicle data +0x1664[k], count +0x16AC[k]) | Colour | Size | Meaning [G by colour] |
|---|---|---|---|---|
| 0x02, else 0x04 | 0 | (1.5, 1.5, 1.5) D_004BBFE0, else (1, 1, 1) BFF0 | x 1.25, else x 1 | head lights (two levels) |
| 0x10, else 0x08 | 2, else 1 | (1.4, 0, 0) BC000, else (1.1, 0, 0) BC010 | x 0.75, else x 0.5 | brake, else tail lights |
| 0x20 / 0x40 | 5 / 6 | (1, 0.9, 0) BC020; with +0x18F0 the lights at z < 0 take (1.1, 0, 0) (`func_00257080`) | x 0.5 | indicators |
| 0x80 | 4 | (0.9, 0.9, 0.9) BC030 | x 0.5 | reverse lights |

Traffic and crashed vehicles: `CB3TrafficSystem::Draw` 0x3078F0 [L draft] calls `RenderTrafficImmediateEffects` 0x2621A0
(the same light code, rear indicators never red) for each drawn traffic vehicle, then each drawn crashed vehicle in reverse;
then the traffic signals: per light (+0x117CC, 0x18 each) its state picks the colour (0: (0.8, 2, 0.8), 2: (1.8, 0, 0),
other: (2, 1.4, 0.2); D_004C49A0 / 80 / 90) and its coronas (indices +0x10, count +0x14) are offset sideways by +-0.5 x
(up x -facing) unless +0x363BA (then D_004C49C0 / B0 vertically); the coronas no light owns (bit clear in +0x36320, 31 per
word) draw green; `AddTrafficCorona` 0x256FD0 writes those with size 1.5 + 0.004 x distance from the CGtCamera. All into
array 0 ("coronaglow", 126 slots). In race2 / roadrage1 the race cars' lights are off except the brake lights (bit 0x10,
roadrage1 polls 3558-3559, 4669-4676, 4758-4762); traffic shows its head / tail lights in daylight.

### 16.3 The corona program VU1 0x478260 (read here; `b3asset::corona_sprites`)

Uploads [C matched `UploadCodeAndStaticData` 0x306E80, `EndRender` 0x3070B0 draft, `ConstructDMAData` 0x3076D0 Ghidra
draft]: VU 1015..1018 = camera record +0xE0 rows (w = z), 1019..1022 the record's window / z / clip constants (+0x168 ..
+0x21C), 1023 the GIF tag (PRIM 0x56 SPRITE, TME, ABE; NREG 5: ST RGBAQ XYZF2 ST XYZF2); each array's slots in batches
of 72 (UNPACK V4-32 to TOPS, ITOP = n, MSCAL / MSCNT); the array buffer always holds every slot, unused ones with w = 0
(`EndUpdate` 0x307440). Per slot [C program]:
- cull: kept when the point is inside the view window plus 32 of 640 pixels each side (704 x 512 of 640 x 448), at view
  depth 0.3..10000, and nearer than K x fade distance (K = camera record +0x208 = 1 / the view window's half width: 0.886
  for the race camera, so lights fade out at 354 m, boost glows at 709 / 354 / 177 m);
- fade: colour x (1 - depth / (K x fade distance)); dropped unless a channel is above 8;
- sprite: a GS SPRITE `size` metres across at the slot (half extent (640 / 4 / half_w) x size / depth pixels), Z taken at
  depth - size / 2 (nearer by half the size), ST (0, 0) -> (1, 1), RGB = FTOI0 of the faded colour, A 0x80.
GS state [C 0x306E00 matched]: ALPHA_1 0x68 with FIX 0x80 (Cs + Cd: additive), TEST_1 0x53001 (alpha test NEVER with
AFAIL RGB_ONLY: RGB written, neither alpha nor Z; ZTST GEQUAL), the texture's own TEX0 (TFX MODULATE, TCC 1: captured
CGtTexture +0x3C; "coronaglow" 128 x 128 with 4 mip levels, "coronaboost" / "coronaboostred" 32 x 32) and a TEX1 from its
mip count. Drawn by `RenderPostFogEffects` 0x261E00 (matched) after the fog and `CB3GameData::Draw`, unless D_00665F2C
(the switch the skid marks share): after popcorn and particles, before sparks.

### 16.4 Capture (recompute, not the buffers)

The slots are rebuilt from the inputs, not captured: the inputs are small and mostly static, and rebuilt slots follow the
interpolated cars at any frame rate. Same version 3; readers detect the block by its presence (older captures: no coronas).
`grab_coronas` adds: the effects manager [+0x1F810, +0x1F840) (camera point, seed, textures), D_01D9D190, the constant
tables (D_004BBFE0.., D_004C4980.., D_004C4A10.., the .sdata floats, `_gaCoronaParams` and the names), the three textures'
CGtTexture [0, 0x50); per race car [+0x118C, +0x11AC), the zones [+0x580, +0x780), +0x27C0; every vehicle data's light
and exhaust tables; the traffic system's track data counts and corona list, its lights and their index lists, +0x36320,
+0x363BA. A check capture (proof runs only) also captures the game's
own arrays (headers, player 0's used slots of the half the last `EndCoronaSubmit` closed).

Size: race2 11,212,312 bytes (crash2's capture 10,984,944: +2.1 %), roadrage1 49,141,008 (47,181,616: +4.2 %; the
randomiser state, seed and camera point change every tick).

### 16.5 Checks (`b3vucheck_corona --capture <a check capture>`) [C]

| Check | race2 (US/C3_V1, crash mode, 2,447 race ticks) | roadrage1 (9,552 race ticks) | races (EU/C3_V1, AS/C2_V2; 14,280 race ticks) |
|---|---|---|---|
| EE: rebuilt slots vs the game's, in the ticks after a render (records 2..N of an N-tick frame hold cars moved on since the render: skipped) | 1,725 ticks: every count equal (46,087 glow, 1,776 boost slots); boost slots **bit-exact** (position, fade distance, colour, size); lights: positions exact, colours within 0.011 GS units, sizes within 4e-5 m; except one tick, the camera cut at poll 4615 (14 signal slots: the CGtCamera moved after the render [L]) | 8,298 ticks: every count equal (17,515 glow, 91,629 boost); **all 91,629 boost slots bit-exact**; lights: positions exact, colours within 0.009, sizes within 4e-5 m | 12,088 ticks: every count equal (69,054 glow, 116,442 boost); all boost slots bit-exact; lights: positions exact, colours within 0.016, sizes within 2e-5 m |
| VU: the real program 0x478260 in the VU1 interpreter on the game's slots vs `corona_sprites` | 41,304 sprites, every lane of both vertices (x, y, z, s, t, q, r, g, b, a, prim): max error 0 | 84,946 sprites, max error 0 | 158,932 sprites, max error 0 |
| GPU path (`corona_quad`, host float) vs the program: drawn set, colour, corners | same slots drawn; colours equal; corners within 0.18 px | same slots drawn; 5 of 254,838 colour values 1 apart; corners within 0.30 px | same slots drawn; 5 of 476,796 colour values 1 apart; corners within 0.28 px |
| camera point +0x1F810 vs viewport pos - 2 x at; K vs 1 / half_w | 0.5 mm; 1.6e-7 | 0.5 mm; 1.2e-7 | 0.25 mm; 1.2e-7 |

**A traffic vehicle can be drawn twice** [C]: the player's traffic list (traffic system +0x35C24, u8 indices) can hold an
index twice (races poll 5476: 1, 3, 5, 8, 9, 10, 11, 12, 11), and the game then writes that vehicle's light slots twice
(3,146 ticks of races had more glow slots in the game's array than a once-per-vehicle rebuild). The reader counts the
entries (`Vehicle::listings`) and the rebuild repeats the lights; with that every count is equal. By the same walk the
game renders the vehicle body and its shadow strip twice [L: Draw's list walk; the shadow, alpha-blended, would be darker
there]: b3replay draws them once (render6 / fx1 scope; noted, not changed).

`make test`: corona_program_model_synthetic (the program model's drops and one sprite on a synthetic camera).
Live feed: `b3livefeed` race2's check capture at 10x into a ring, `b3replay --live --verify`: 5,180 ticks identical (the
corona inputs are in `same_tick`), 0 differ; its shots at polls 3100 and 3156 equal the offline `--offscreen` frames (0
pixels differ).

**Capture exactness** (build sha256 38d099ec...):

| Session | Capture on | Capture off (same exe) | GS stream on vs off | Check capture (same exe) |
|---|---|---|---|---|
| race2 | 5,181 of 5,181 polls exact, 0 other EE cycle counts | 5,181 of 5,181, 0 other | **IDENTICAL over 8,030 frames (every column)** | 5,181 of 5,181, 0 other (12,335,048 bytes) |
| roadrage1 | 13,474 of 13,474 exact, 0 other | 13,474 of 13,474, 0 other | GS path payloads (p0/p1/p2) identical in all 17,645 frames; 80 front-end frames (1787..4683) differ in `full`/`hdr`/`masked` (3 in `bytes`), every column identical from frame 4684: the host-record variance of §12.2 (crash2: 77, fx1: 76 such frames) | 13,474 of 13,474, 0 other (53,531,848 bytes) |

### 16.6 Drawing (b3gfx, View)

`View::draw_list` rebuilds the arrays for the displayed frame (`build_coronas`: the displayed vehicles and camera; light
bits, boost intensity, the seed and the randomiser from the tick, so the flicker changes at the 60 Hz ticks) and draws each
array's sprites as camera-facing quads (`corona_quad`: the program's cull, fade and threshold in float, a square `size`
across placed at depth - size / 2 so the Z test is the program's) with its texture, as post-fog effect items
(`DrawItem::effect` + `post_fog`: phase 5 after the crash objects, unfogged; ALPHA 0x68 -> ONE / ONE with FIX 1, RGB only,
Z GEQUAL without write). Hor+ windows widen the program's window test to the wider screen (`View::render_aspect`); a free
camera gets the coronas computed for itself (`View::corona_view`), as the game would compute them for that camera.
Toggles: `--no-effects` / F (all effects), `--no-coronas` / L (coronas alone), in b3replay and the live viewer.

### 16.7 Pictures (game camera 640x448 offscreen unless noted; game-derived, not committed)

`%B3_WIN%\corona1\pictures\` (also corona1's scratch directory), each as `-coronas`, `-nocoronas`
(fx1's effects only) and `-noeffects`:
- race2 `race2-p3100-*`, `-p3156-*`: the chase camera behind the boosting car, the glow at the two exhaust pipes (6,905 /
  6,578 pixels differ); `-p3300-free-*` (free view over the junction, `--free-eye 4852,155,1618 --look-at 4867,149,1636`):
  the traffic signals' green lamps.
- roadrage1 `roadrage1-p4410-*` (the rival ahead boosting), `-p11052-*` (the busiest tick: 32 sprites), `-p4672-*` (the
  player braking while boosting: brake-light coronas, the boost glow, fx1's tyre marks).
- `win-race2-p3100-*`: the Windows exe (RTX 4080, Vulkan; its first build 6ce14402..., before the traffic-list fix): the coronas change 6,932 pixels against 6,905 in WSL; the
  frame differs from WSL's by the same amount with or without coronas (mean 1.2 levels: GPU and filtering).
No night route in the recorded sessions; the lights were checked in daylight (their colours and fades do not depend on it).

### 16.8 Running it on Windows

```
%B3_WIN%\corona1\b3replay.exe %B3_WIN%\perf\scene\race2-corona1.b3scene
%B3_WIN%\corona1\b3replay.exe %B3_WIN%\perf\scene\roadrage1-corona1.b3scene
```
(`b3replay.exe` sha256 35735b89...) L toggles the coronas, F all effects; the other keys as §12.4. race2 boosts at polls 3051..3227 (after the
crash junction's boost pickup); roadrage1 boosts through most of the race. the local `data/scene` recordings are the
corona1 captures. The Mac build needs nothing new (one more source file in the Makefile).

### 16.9 Not done

- The boost's radial blur (post pass, survey item 10) and the sun corona / lens flare (item 9).
- The order of the traffic vehicles' slots is the vehicle table's, not the traffic cache's sorted order: it only matters
  when the 126-slot glow array fills (the game drops the rest); none filled in race2 / roadrage1 / races (most in a tick:
  glow 60 of 126, boost 42 of 72; `--info`). Duplicated list entries also count towards the traffic cache's 32-vehicle
  cap in the game; `game_draws` counts vehicles [L].
- A camera cut between a render and the poll (race2 4615) leaves the traffic signals' sizes one frame off [L].
- Split screen: the game's own corona arrays and camera record are player 1's / the last view's; the slots are rebuilt
  per view since split2 (§29.6: each view's submits, camera point and sort); b3view (static route, no capture) draws
  no coronas; no red-boost car or indicator was seen in the recorded sessions (code paths [C], not exercised).

## 17. HUD / 2D: the Gt2d layer in the capture and b3replay (hud1, 2026-11-03)

Item 1 of milestone 2 (docs/effects-survey.md §5): the game's 2D layer (race HUD, crash HUD, prompts, results, event
intro) captured per rendered frame and drawn by b3gfx over the native 3D at window resolution, offline (`b3replay`) and
live (`b3replay --live`). Code: the HUD hooks and the record (today `src/gameview/hud_capture.cpp`), the scene
capture (its host-only region and the raster bytes in the delta stream), `src/b3replay/hud.{h,cpp}` (reader: a GIF interpreter for
Gt2d's packets, guest rasters decoded with b3asset), `View::hud_list` (`view.cpp`), b3gfx `DrawItem::overlay` (the 2D
stage), `tests/test_hud.cpp`. Decomp citations: HEAD fd4f39a072 (read only). Tags as above.

### 17.1 What draws the 2D, and why the capture records calls

- Everything 2D goes through the 22 `Gt2dRenderer` functions (0x217850-0x21A4F0, `Gt2dRendererPS2.cpp`): they append
  GS register writes to a 512-quadword buffer at `*_gau128Buffer` (0x4E1950; **in the scratchpad**, 0x70002000, in
  every capture so far [C]) and `_Dispatch` 0x21A4F0 copies it into RenderWare's DMA stream (`_rwDMAOpenVIFPkt`). Only
  these functions reference the buffer pointer `_gpu64Current` [C asm scan of the decomp]. State goes in as PACKED A+D
  (ALPHA_1, FRAME_1, TEX1_1, CLAMP_1, SCISSOR_1, TEX0_1), primitives as REGLIST groups (ST / RGBAQ / XYZ2) under one
  of ten PRIM values (`Construct` 0x21A290 [C matched]). Positions are float pixels plus `_gGsOffsetXYXY`, converted to
  GS 12.4 at the end (`vftoi4`) [C RenderSpritesTex 0x219830].
- `Begin` 0x21A0F0 emits nothing: it takes RenderWare's sky state (ALPHA, TEX1, CLAMP, FRAME, the current raster) and
  the current camera raster's rectangle as the 2D space (SCISSOR = the raster, offsets = XYOFFSET + raster offset) [C
  asm]. `Suspend` / `End` restore RenderWare's state and flush; `Resume` re-emits Gt2d's copies [C matched].
- **SetTexture 0x219A20** [C asm]: a raster with +0x8C set gets a TEX0_1 write in the buffer (CBP + the u16 table at
  +0x90 for palette > 0); a texture-cache raster (+0x8C == 0) flushes what is pending and calls
  `RpSkyTexCacheAccessRaster`, which sets TEX0 in RenderWare's own packet, so the buffer gets a TEX0 only for palette >
  0. +0x8C changes at run time (GlobalFont is a cache raster at race2 poll 3000, a +0x8C one at 4500 [C]).
- Frame order [C `CB3OnePlayerStage::Render` 0x134A20, matched]: world render, `OpenViewport(0)` (full screen), then
  `CB3HUDManager::Render` and the stage's own Gt2d pass, all before `PresentDrawBuffer`'s blit: the 2D goes into the
  render buffer (FRAME FBP 70, FBW 10, 640 wide [C capture]) that the present blit doubles, so b3gfx draws it into the
  same half-intensity GS frame before its present pass (§11.1).
- Gt2d also draws into small offscreen targets (FRAME FBP 362, FBW 1: a 64-pixel-wide buffer, in 1,979 of race2's
  4,471 HUD frames [C]; blend modes 0x2A, 0x6A alpha-only, 0x54: a mask-and-fill pass, probably a texture the boost
  effects sample [G]). b3replay draws only the passes whose FRAME is 640 wide [L].

The HUD is immediate mode, so the capture records the call stream, not memory state at the poll.

### 17.2 The capture (no version change; readers detect the region by its presence)

**Hooks.** Capture-only entries: each records and the original code runs, so dispatch is unchanged. Entries: Begin,
SetTexture(…, int) (the one-argument form calls it), Resume, Suspend, End, `_Dispatch`. Every batch passes through the
guest `_Dispatch`, and the recorder checks it: a write pointer that goes back without a batch counts as a missed flush
(**0 in every run**). On whenever the scene capture is.

**Record.** Per rendered frame, published at the next retail poll (the same point as everything else: the frame the
game drew before poll k is in record k) as a host-only region of the delta stream, outside guest RAM:
- 0x40000000: {'HUD1', frames published, index bytes, missed flushes}, then the index: events {kind, bytes, payload}:
  BEGIN; STATE (Gt2d's state copies 0x67CF80 x 5 qw, `_gGsOffsetXYXY` 0x67CD90, the gp block 0x4E28D8..0x4E2910,
  skyTextureRaster 0x4E2860, read at the first call after Begin); TEXTURE {raster, palette, +0x8C, a flush follows};
  RESUME {skyTextureRaster}; SUSPEND; END; BATCH-REF {quadwords, slot address}.
- 0x40100000 + 8 KB x k: the frame's k-th batch, as `_Dispatch` found it (scratchpad bytes). One slot per batch keeps a batch that grows or shrinks from
  moving the bytes after it, so the delta stream carries only the changed 16-byte blocks.
- The guest rasters the frame used: header [0, 0xC8), the upload block at +0x24 and every DMAref'd packet (absolute
  addresses in RAM: RW 3.1 sky2 `texcache.c` sends the ref tags as they are [C source]), re-read only when the header's
  stable fields change (size, format, upload pointer, palette table, name; the cache rewrites TEX0 and its counters).
  So every texture decodes, the fonts included (GlobalFont 256x256, HeadFont 128x256: on no disc TXD [C]).

**Reader** (`hud.cpp`): applies STATE, pairs TEXTURE events with the buffer's TEX0 writes in order (or switches at
once / after the pending flush for cache rasters with palette <= 0), interprets PACKED A+D and REGLIST, assembles
sprites, triangles, strips and fans with PRIM IIP (flat: the last vertex's colour), TME, ABE, FST, and groups triangles
by state (texture, ALPHA, FIX, ABE, FRAME target and FBMSK, TEX1 filter, TFX, TCC, SCISSOR) into draws. Positions back
to the 2D space: X / 16 - `_gGsOffsetXYXY`; scissor minus Begin's raster offset; ST / Q, scaled to the decoded image
(2^TW / w). Guest rasters decode with b3asset's GS model after one fix: in RAM the cache has made TEX0's TBP0 / CBP
absolute (race2 hud_element01 TBP 14848, CBP 14876 = TBP + palOffset 28) while the upload chain stays slot-relative, and
the files hold TBP0 = CBP = 0 (all 194 GLOBAL.TXD rasters) [C]: the reader zeroes them as the files have them.

### 17.3 The 2D space and how it maps to the window

- **2D space [C]: 640 x 448**, the camera raster at offset (0, 0) (Begin's scissor in all 4,471 race2 and 12,219
  roadrage1 HUD frames); `_gGsOffsetXYXY` (1728, 1824) = XYOFFSET / 16. The game itself lays text out for 640 x 480 and
  scales by the graphics manager's size (e.g. `CB3OnePlayerStage` 0x134A20's debug text: x 320 sx, y 50 sy with
  sx = width / 640, sy = height / 480 [C matched]), so components arrive already in 640 x 448 pixels.
- The PS2 shows that frame as a 4:3 picture, so b3replay maps the 2D space to a 4:3 box scaled to the window height and
  centred (pillarboxed at 16:9 / 21:9; letterboxed below 4:3); `--offscreen` without `--size` (the game's 640 x 448
  frame) fills the image. The 3D stays Hor+ at the window aspect. A GS pixel is sampled at its integer corner, a GPU
  pixel at its centre: half a 2D pixel is added [L].
- **Anchoring: not in this slice.** The Gt2d stream carries no component identity (immediate mode; consecutive draws
  with the same state merge across components), so an edge table cannot be keyed cheaply. Plan (below): tag each draw
  with the `CGt2dScene` / `CB3HUD*` component that issued it.

### 17.4 Blend modes and the 2D stage (b3gfx)

`DrawItem::overlay` items draw last (phase 6 of the scene pass, after the post-fog stage), in list order, no depth, no
fog, with the item's scissor; shaded as the track model's type 0 (MODULATE in GS units: Cv = Ct Cf >> 7, Av = At Af >>
7; untextured Cv = Cf), so the present pass doubles them like the game's blit. The GS ALPHA (A - B) C + D becomes GPU
factors with the shader's alpha = As / 128 and the frame alpha holding Ad / 128 (both may exceed 1):

| Gt2d mode | ALPHA | GS | GPU | Seen (race2 triangles) |
|---|---|---|---|---|
| 0 | 0x44 | (Cs - Cd) As + Cd | SRC_ALPHA, ONE_MINUS_SRC_ALPHA | 5,890,182 |
| 1 | 0x48 | Cs As + Cd | SRC_ALPHA, ONE | 19,332 |
| 2 | 0x42 | Cd - Cs As | SRC_ALPHA, ONE, reverse subtract | 57,550 |
| 3 | 0x2A | Cs | ONE, ZERO | 3,958 |
| 4 | 0x54 | (Cs - Cd) Ad + Cd | DST_ALPHA, ONE_MINUS_DST_ALPHA | 3,958 |
| 5 | 0x58 | Cs Ad + Cd | DST_ALPHA, ONE | 0 |
| 6 | 0x52 | Cd - Cs Ad | DST_ALPHA, ONE, reverse subtract | 0 |
| 7 | 0x6A + FBMSK 0 | Cd, frame alpha := As | colour masked, alpha ONE / ZERO | 51,454 |

SetBlendMode 0x217E70 sets FRAME FBMSK 0xFF000000 (frame alpha kept) except mode 7 [C matched]; PRIM ABE clear draws
unblended. TEX1 MMAG picks bilinear or point sampling; textures are clamped. Not modelled: GS colour clamping between
blends (the frame is float), TCC 0 (4,840 race2 draws: drawn with the texture's alpha; their effect not checked), the inherited TEST_1 (Gt2d never writes it; RenderWare's state
[G]).

### 17.5 Interpolation and switches

The HUD is per rendered frame on the PS2; b3replay holds the HUD of the displayed frame's base tick (`Frame::hud`, the
tick the 3D interpolates from) at every display rate; nothing is interpolated (positions move with the game's own
frame steps, as on the PS2) [design choice]. `--no-hud` and the key **U** (H is the full-screen specular) toggle it in
the offline and live viewers.

### 17.6 Exactness (build sha256 5ed75ae6...) [C]

| Session | Capture on (HUD hooks) | Capture off (same exe) | GS stream on vs off |
|---|---|---|---|
| race2 | 5,181 of 5,181 polls exact, 0 other EE cycle counts, 0 other step counts; 4,472 HUD frames, 168,118 batches, 0 missed flushes; 24,808,072 bytes (crash2's 10,984,944) | 5,181 of 5,181, 0 other | **IDENTICAL over 8,030 frames (every column)** |
| roadrage1 | 13,474 of 13,474 exact, 0 other cycles / steps; 12,219 HUD frames, 382,023 batches, 0 missed flushes; 114,481,480 bytes (47,181,616) | 13,474 of 13,474, 0 other | p0 / p1 identical in all 17,645 frames; p2 identical except frames 16594-16595 (the results screen, poll ~13,100), where one 256-byte PATH2 transfer is counted one vblank earlier (p2 2,536,896 / 667,792 bytes on vs 2,536,640 / 668,048 off: same total); 76 front-end frames (1787..4679) and 16596 differ in `full` / `hdr` / `masked` only, the known host-record variance (§12.2) |

Earlier exes of this lane (72a22069 / 676cf19c / 51d02251: the same hooks with the batches inline and without the
TEXTURE flags) gave the same verdicts: race2 identical over 8,030 frames in every pair; roadrage1 the same p2 shift at
16594-16595. Inline batches cost roadrage1 135.7 MB; the slots 114.5 MB.

**Size.** The HUD is mostly vertex data that moves every frame (boost-bar flames, rival markers, counters): race2's
slots 12.0 MB (2.7 KB per HUD frame), the index 0.6 MB; roadrage1's race part 80 MB. data/scene therefore holds the
captures **gzip-compressed** (`gzip -9`: race2 8.1 MB, roadrage1 39.6 MB, less than the plain version-3 files they
replace); `Capture::load` and b3livefeed read them through zlib's `gzread`, which reads plain captures unchanged.

**Live feed.** b3livefeed of the gzipped race2 into a ring, `b3replay --live --verify`: 5,181 ticks identical (HUD frame
included in `same_tick`), 0 differ; live shots at polls 3000 and 4500 are byte-identical to `--offscreen` of the file.

### 17.8 Pictures (game-derived, not committed: `%B3_WIN%\hud1\pictures\`)

`<session>-p<poll>-169.png` (1920 x 1080) and `-43.png` (1440 x 1080), from the local data/scene captures, Linux
Vulkan offscreen: race2 2450 (event intro: curved frame, "TRAILER TRASH" sliding in), 3000 (run-up: "76 mph" on its
blue panel), 4200 ("CRASHBREAKER" prompt), 4500 (results: "TOTAL SCORE $125,678", "PLAYER1 $125,678", "Continue (X)");
roadrage1 6000 ("TAKEDOWNS 2", boost bar x2 with flames, "152 mph"), 9000 ("AIR" award, "DRIFT", full x4 boost bar,
rival markers over the cars ahead), 13105 (results: title sliding in, "CONTINUE" trail). At 16:9 the HUD is the
central 4:3 box; the rival markers stay on their cars (the 3D is Hor+, so the centre 4:3 matches the PS2's projection).

### 17.9 Running it on Windows

```
%B3_WIN%\hud1\b3replay.exe C:\path\to\burnout3-native\data\scene\roadrage1.b3scene --game %B3_WIN%/perf/install/game
```
U toggles the HUD, `--no-hud` starts without it; the other keys as §12.4. Exe f097e75c... (reads gzipped and plain
captures; roadrage1 poll 9000 at 1920 x 1080 matches the WSL frame to 0.77 levels mean). Debugging aids:
`B3HUD_DEBUG=1` lists each frame's draws, `B3HUD_TEXDIR=<dir>` writes the decoded rasters as PNG.

### 17.10 Not done, and the next HUD / menu / movie slices

- **Edge anchoring**: done in hud2 (§17.11), keyed by the captured EHUDAlign of each CB3HUD component. Plan as written: a capture entry at the components' render calls (`CGt2dScene::Render` 0x1D75C0 per
  HUD scene, or each `CB3HUD*` `_Render`) that tags the events after it with the component's object address, so the
  reader can group draws by component; then a table (left / right / centre by the component's frame position,
  reviewed in a window) that moves edge components to the window edges at wide aspects.
- **Front end / menus** (3-4 d): captured already (the race2 and roadrage1 front ends are in the stream), not drawn by
  the live viewer between races and not by b3replay outside race segments. Needs a 2D-only frame path (no 3D, or the
  menu's own 3D backdrop), `CB3MenuPageManager::Render` 0x30D6E0's direct packets (not Gt2d) and `CB3Globe2dObject`
  0x3B2EE0 (direct packets + B3Im3d).
- **Movies** (4-5 d, docs/effects-survey.md §6): `CGtVideoImage::SetTileTexture` uploads; host MPEG decode vs guest
  image.
- Not modelled: TCC 0, the inherited TEST_1, GS colour clamping between blends; the offscreen FBP-362 passes (not
  drawn); split screen (player 2's HUD pass is in the same stream, not separated); crisp text (higher-resolution glyph
  art or a distance-field font, §7.1).

### 17.11 hud2 (2026-11-03): the boost bar's tread, component identity, the wide layout, sharp fonts

Branch `hud2`. The report (b3replay on roadrage1, macOS, 3456 x 2104 and an ultrawide 3456 x 890 window): the
boost bar's tyre tread only on its left part, the crash banners and letterbox stuck in the 4:3 box, blurry speedo and
takedown digits, "CRA H !" letters, orange blobs near a lamp post.

**Boost bar tread [C].** `func_001A8180` (the bar background, draft src/nonmatching/func_001A8180.cpp) draws BoostBits
with U from 0 to 6 x the bar width: one 64-texel tread tile repeated six times per bar. Its draw has CLAMP_1 = 0 (WMS =
WMT = REPEAT) in the capture (roadrage1 4942, every BoostBits background draw: 7,006 frames); hud1 clamped every HUD
texture, so past the first tile the edge column smeared into the horizontal stripes of the report's 14 / 17.webp. Fix:
`HudDraw::wms / wmt` from CLAMP_1, b3gfx `DrawItem::repeat_u / repeat_v` with repeat samplers (2D stage only; the
all-clamp draws keep the hud1 samplers). REPEAT occurs on BoostBits, `grid` (a tiled backdrop) and some GlobalFont
draws (UVs inside the glyph: no visible change), nothing else. The 64-wide FBP-362 passes (§17.1) are not the bar's:
they are drawn outside every Gt2d object callback (race2: all 59,370 of their triangles untagged) [C], so they stay
undrawn.

**Component identity (capture; readers detect it by the new events).** Two more event kinds:
- **SCENE** (9), at `CB3HUDManager::Render` 0x1A0790 (it renders one `CGt2dScene` per player HUD, CB3HUD i at gHUDManager
  0x66A3D0 + 0x70 i, scene +0x14, count +0x170 [C matched]): per HUD the scene, the HUD rect (+0x48 / +0x50), the root
  frame (+0x28), the align table `D_004B7650` (filled at run time; zero in the ELF [C]) and the component list (+0x68,
  next +0xC): {component, vtable, frame +0x8, mode flags +0x14, EHUDAlign +0x18, player +0x1C} (`CB3HUDComponent::Prepare`
  0x19F6A0 [C matched]: the frame is a child of the HUD root frame at `GetAlignPos(align)` = rect min + (max - min) x
  D_004B7650[align], align 8 = absolute).
- **OBJECT** (10), at each object render callback (`CGt2dScene::Render` calls object +0x44 with (object space, object +
  0x28) through jalr; object +0x40 = its frame [L Ghidra draft 0x1D75C0]): {callback, object, the byte offset of the next
  write in Gt2d's buffer, the object space, the frame and 7 ancestors (+0x48) [C Gt2dFrameHierarchy.cpp matched]}.
  Hooked: every symbol with the callback signature `(const CGt2dObject::CGt2dObjectSpace&, const void*)` in the decomp's
  tables (less the two `_BuildBatchData` helpers and the two `CGt2dObjectParagraph` callbacks 0x2F8C50 / 0x2F8ED0, which
  no HUD scene used), plus six unnamed ones the hook's own completeness check found
  as HUD objects' callbacks (func_00381030, func_00381AB0, func_003D62A0, func_003D6C30, func_003D76C0, func_003CD920).
  The check: at each CB3HUDManager::Render, every object of the HUD scenes' visible layers must be seen; the report line
  counts objects with unhooked callbacks and hooked objects not seen.
The reader switches the current object at the event's byte offset in the next batch (a callback's first write lands
there), splits draws by object and resolves each object to the first component whose frame is in its chain. Captures
without the events read as before (`HudFrame::has_objects` false).

**The components** (roadrage1 / race2 captures; names: decomp config/symbol_addrs_b3names.txt data symbols) and the
align table as captured [C]: 0 top centre (0.5, 0), 1 top left (0, 0), 2 middle left, 3 bottom left (0, 1), 4 bottom
centre, 5 bottom right (1, 1), 6 middle right, 7 top right (1, 0), 8 absolute; HUD rect (30, 22) - (610, 458).
| Component | Address | Align | Wide layout |
|---|---|---|---|
| `_gTakedownsComponentP1` (TAKEDOWNS counter) | 0x66AF40 | 1 top left | to the left edge |
| `_gBoostBarComponentP1` | 0x66A610 | 3 bottom left | to the left edge |
| `_gSpeedoComponentP1` | 0x66AE10 | 5 bottom right | to the right edge |
| `_gScoreComponentP1` | 0x66ACC0 | 0 top centre | stays |
| `_gCrashStyleComponentP1` | 0x66AFE0 | 3 bottom left | to the left edge |
| `_gCrashTimerComponentP1` | 0x66B040 | 0 top centre | stays |
| `_gPrimaryMessageComponentP1` (awards, prompts, "CRASHED!") | 0x66B350 | 8 absolute | stays |
| `_gCrashComboComponent` (the crash combo band) | 0x66C470 | 8 absolute | stays (edge pieces: below) |
| D_0066C430 (R1 IMPACT TIME) | 0x66C430 | 7 top right | to the right edge |
| D_0066C3F0 | 0x66C3F0 | 0 top centre | stays |
Draws outside every listed component (the crash letterbox, the stage's own Gt2d passes, front end) get no anchor shift.

**Wide layout (`View::hud_list`, `--hud-layout wide` default | `43`, key Y).** At aspects above 4:3 the HUD rect is
widened to the output, as if the PS2 frame were wider: ext = the extra half width in 2D units; each draw moves by
(2 fx - 1) x ext of its component (fx from the captured table). Edge pieces: a textured triangle exactly at one
edge of the 4:3 box (x 0 or 640, within half a pixel) moves to that edge of the output whole (the crash band's end
curve); at both, it stretches (the band); an untextured draw (one object's run of one state) touching an edge stretches,
its vertices left of the middle going to the left edge and the others to the right (the letterbox; the band's top
line, two gouraud quads, reaching its moved end curve); glyphs that merely cross an edge (the scrolling combo text,
which the PS2's scissor hides past 640) move with their component and show in the wider frame. The full-width scissor
widens with the output, a clip region moves with its component. The 3D is Hor+ (the centre 4:3 is the PS2's
projection), so absolute items (rival markers) stay on their cars. `--hud-layout 43` is hud1's layout: pixel-identical
to main 82cebe1 at 1920 x 1080 and 640 x 448 for race2 2450 / 3000 / 4200 / 4500 and roadrage1 5454 / 13105; roadrage1
6000 / 9000 differ only inside the BoostBits background sprite (2D 30..210 / 30..390 x 403..426: the tread fix).

**Sharp fonts (`--hud-filter sharp` default | `game`, key J; a display choice, not the GS).** The HUD text uses TEX1
bilinear in the game too (GlobalFont MMAG linear in every capture), so hud1 matched the filter; the blur is a 256 x 256
coverage atlas (white RGB, glyph shape in alpha; outlines and drop shadows are separate passes of the same glyphs)
magnified 4.7 x at 2104 lines. For rasters named `*Font` (GlobalFont, HeadFont, SmallFont) drawn bilinear when the 2D
space is magnified 2 x or more, mesh.frag re-thresholds the bilinear coverage at 0.5 with a one-pixel ramp
(`(a - 0.5) / fwidth(a) + 0.5`, a distance-field style edge). SmallFont, the menus' thin light font (128 x 128, 1-texel strokes, diagonals split
over two texels), cuts lower (mesh.frag's gs.w = `DrawItem::sharp_level`): a full-coverage stroke stays 2 (1 - cut)
texels wide. mac5 took it to 0.3 (the name keyboard: 0.5 broke its thin diagonals); menu-font (2026-11-11, the TROPHIES
checklist "hard to read" at ~2000 x 1110) to 0.2 (1.6 texels: the weight the PS2's bilinear fringe gives it at 448
lines), and lower near 2 x so no stroke is under 3.5 output pixels (`View::kSmallFontMinStrokePx`, the cut at least
0.1); `B3_SMALLFONT_LEVEL` overrides it. GlobalFont and HeadFont keep 0.5. Other textures (flames, panels, icons) keep the game's
filter; the full-resolution level 0 is what is sampled (no mips). The planned HD texture pack (hdtex1) would cover the
font atlases (GlobalFont / HeadFont are built in RAM, on no disc TXD, §17.2) only if it replaces them by hash; the
panels (hud_element01), BoostBits, the BoostFire* sequences and the hud_sign* icons are ordinary GLOBAL.TXD rasters [L].

**Exactness** (build sha256 744618a4...): [C]
| Session | Capture on (hud2: 46 entries = hud1's six, CB3HUDManager::Render, 39 object callbacks) | Capture off (same exe) | GS stream on vs off |
|---|---|---|---|
| race2 | 5,181 of 5,181 polls exact, 0 other EE cycle / step counts; 4,472 HUD frames, 168,118 batches, 0 missed flushes; 1,075 HUD scenes, 61,039 object calls, 0 objects with unhooked callbacks, 5 hooked objects not seen; 25,377,640 bytes (gzip -9: 8.2 MB) | 5,181 of 5,181, 0 other | 5 of 8,030 frames differ, all in p0-p2 timing: frames 4385-4386 and 7426-7428 (byte totals over each run of frames within 6 / 30 bytes) |
| roadrage1 | 13,474 of 13,474 exact, 0 other cycle / step counts; 12,219 HUD frames, 382,023 batches, 0 missed flushes; 8,001 HUD scenes, 145,984 object calls, 0 unhooked, 0 not seen; 117,522,928 bytes (gzip: 40.7 MB) | 13,474 of 13,474, 0 other | 74 front-end frames in `full` / `hdr` / `masked` only (§12.2 host-record variance) and 1 more with `bytes` too; p0 at 6697-6699 (new) and p2 at 16594-16597 (hud1's results-screen shift, §17.6) |

**15.webp (orange blobs, TAKEDOWNS 13).** Not located: roadrage1's counter is 8 at poll 9470 and its late award
episodes (polls 8129-13377) are takedown cams; the HUD style in 15.webp (large TAKEDOWNS panel without SCORE) and 13
takedowns suggest another session. That session's tick is needed to check the blobs against the GS frame.

**Pictures** (game-derived, not committed): `%B3_WIN%\hud2\pictures\` (35 files): roadrage1 4942 / 5454 / 6879
/ 9000 / 10522 and race2 3000 / 4200 at 3456 x 2104, 3456 x 890 (ultrawide; also `-layout43-gamefilter`: hud1's look) and
1920 x 1080; `speedo-cmp`, `td-cmp` (game filter / sharp at 3456 x 2104).

**Not done.** The FBP-362 offscreen passes (not HUD objects; whose they are is open), TCC 0, the inherited TEST_1, GS
colour clamping between blends; split screen (player 2's components are listed; not laid out separately); menus / front
end in the live viewer; the 15.webp blobs; the two paragraph-text callbacks are not hooked (no HUD scene used them;
the completeness check would name them).

### 17.12 hud3 (2026-11-03): the split intro band, the letterbox gap, award values, the full boost bar

Branch `hud3`. The report (Mac, roadrage1, 3200 x 1800): the event intro's top band split in the wide layout,
a strip of 3D above the crash letterbox, no value with the DRIFT / TRADIN' PAINT awards, the full boost bar not full of
flames. Pictures (game-derived, not committed): `%B3_WIN%\hud3\pictures\` (29 files).

**Split band (wide layout) [C].** The intro's top band is one object (callback `0x31B130`, roadrage1 3654): a translucent
body and two gouraud lines from x 161 (top) / 76 (bottom) to 640, the lines' middle vertices at x 320, and big_curve as
four quads over x 0..272 (vertex dump, `B3HUD_DEBUG_V`). hud2's per-piece rules moved the big_curve triangles touching
x 0 to the window edge but left the one at 136..272, sent the band's vertices left of x 320 to the left edge and the
rest (x 320 included) to the right: three shades over the band's left part. The results screen's title band (13105)
split the same way. Fix: each object gets an **anchor**, the side its one-sided textured edge pieces touch (a triangle
exactly at one edge; one at both edges stretches and does not vote; disagreement or none: no anchor). The object's
other textured pieces move with the anchor, and an untextured draw touching an edge sends its edge vertices to the
window edge and the others with the anchor, or (no anchor) in proportion ((2x / 640 - 1) ext, the letterbox). The
crash combo band and the intro's bottom band (end curve at 608..640: anchor right) lay out as in hud2. Both bands are
whole now at 1600 x 900, 3200 x 1800 and 3456 x 890 (`roadrage1_3654_*`, `roadrage1_13105_*`, `-main` / `-hud3`).

**3D above the letterbox [C].** The crash letterbox bars are 2D y 0..56 and 392..448, x 0..640 (roadrage1 5730): they
reach the frame's edges, so no PS2 overscan margin is involved (margin 0 from the data; the DISPLAY registers were not
read). The gap was ours: hud1's GS-to-GPU correction added half a **2D** pixel (2 output pixels at 1800 lines), so the
whole HUD sat 2 px down and right. At 3200 x 1800 main's rows 0-1 average 127-142 levels (3D) and its columns 0-1 82-149;
hud3's are 0.0, in both layouts. Fix: the correction is half an **output** pixel (`hud_list` takes the output width;
the same as before at 640 x 448), and an untextured vertex exactly on the raster's edge goes one 2D pixel past it (the
window or the 4:3 scissor clips it): a GPU pixel centre on the edge is a fill-rule tie that main lost even at 640 x 448
(row 0 under the letterbox), where the GS covers the edge pixel.

**Award values [C].** The value is a `CB3Score2dObject` (`_Render` 0x3D5D00, hooked since hud2) that
`CB3HUDPrimaryMessageHandler::CreateScoreObject` builds when the message's score (+0x38) is non-zero, after half the
spin-in [L decomp drafts src/nonmatching UpdateSpinAnimationIn / CreateScoreObject]. In roadrage1 (`b3replay --info`,
new per-callback lines; `B3HUD_INFO_POLLS=3d5d00`) it draws in 12 episodes, all takedowns ("WALL TAKEDOWN! 150", 4480),
and never with a star award (callback 0x3D6050: DRIFT 4844-4985, AIR 8965-9106, 11500-11624) or a sign award without a
score (0x3D62A0: DUELING! 4700, CONCRETE KISS! 6720, NUDGE! 7180, SLAM! 10200, ...). The capture's completeness check (0 objects with unhooked callbacks) rules out
another draw path. Not a renderer bug in this session; a value under these awards would need the reported session.

**Full boost bar [C for roadrage1].** Whenever the bar is full (e.g. 8583-8676, 9956-10092) the game's fire reaches
x 393-396 (bar end 390): fire cores, edges and the additive BoostFireOver layer (6 gouraud triangles x 27..394.8, alpha
0x66 with 12-px fades at both ends) cover the whole bar (`B3HUD_INFO_EXTENT`). 8590 at 3200 x 1800 is full with and without the HD
pack (`bar8590-3200x1800-nopack-vs-hdpack.png`; the pack replaces BoostFireCore / Edge, not BoostFireOver). 20.png's
dimmer right third is not reproduced; its tick (or session) is needed.

**`--hud-layout 43` vs main:** at 640 x 448 pixel-identical for race2 3000 / 4200 and roadrage1 4942 / 6000 / 9000; in
roadrage1 3654 / 5454 / 5730 / 13105 only edge pixels of untextured draws differ (5730: 639 pixels of row 0 and 119 of
column 0 under the letterbox, now covered as on the GS). At larger sizes the whole HUD moves by 1.5 output pixels (the
half-pixel fix), so every HUD edge differs slightly (3200 x 1800: 0.6-8.7 % of pixels).

**Not done.** The ticks for 20.png and for awards with values; the PS2 DISPLAY
registers (not needed: the bars cover 0..448); capture unchanged (no recapture).

### 17.13 hud4 (2026-11-04): rival markers projected per displayed frame

Branch `hud4`. The report: in b3replay the little blue arrows over the other racers (roadrage1) drift off the
roofs between ticks. Decomp citations: HEAD 2ad95ae82e (read only). Pictures (game-derived, not committed):
`%B3_WIN%\hud4\pictures\`.

**What draws them [C unless tagged].** `CB3RacePosition::Draw` 0x154350 (names: decomp `config/symbol_addrs_b3names.txt`)
ranks the race cars by distance from the camera and, inside its own Gt2d pass (Begin, SetBlendMode 0, SetTextureFilterMode 0,
EnableZTest(1)), calls `CB3RacePosition::DrawPositionMarker` 0x1531C0 once per other car with the world point by address
in `$a1`: the car's physics matrix position (gWorld+0x3C98C+0x30 i -> +0x204 -> +0x30) plus (0, vehicle data +0xE80 y,
0), the rank slot in `$a3`, the CB3RaceCar in `$t0`, the viewer's car in `$t1` [L drafts src/nonmatching
`Draw__15CB3RacePositionFi.cpp`, `DrawPositionMarker...cpp`; the call's registers C from the asm, e.g. DrawSingleVehicleValue's
call 0x1541E0..0x154200]. The marker projects the point through the active viewport `*D_00666270` (CGtViewport [L]):
p = point x viewport +0x00, culled unless +0x84 < p.z < +0x88; screen (p.x / p.z x +0x78, p.y / p.z x +0x7C); size
0.5 x sw x +0x50 x 1.5 (1 + (p.z - 1) / 74) / p.z, at most 320 [C asm 0x153244..0x153490]. It then draws, `height` =
(sh / sw) +0x54 +0x48 size / 3 above that point, a downward flat triangle (`RenderTriStripFlatUntex`, the near arrow) or
the rank / label text through stack CGt2dObjects whose render callbacks are hooked since hud2 [L draft]. In Road Rage
(CB3RacePosition +0x20 = 3) they are the blue / red arrows (kColourPosMarkerBlue / Red by the car's +0x16B4 [L]).

**Cause [C].** The capture holds the marker as 2D vertices placed with the tick's camera and car; b3replay draws the 3D with
the camera and cars interpolated to the displayed time, so between ticks the arrow stays where the tick put it while
its car moves on: at 640 x 448 over roadrage1's race (11,292 drawn markers, polls 3557..13108) the held arrow is off its
point by median 0.44 / 0.88 / 1.32 px at alpha 0.25 / 0.5 / 0.75, 95 % 2.5 / 5.0 / 7.5 px, max 26 / 75 / 201 px (fast
approaching cars) (`b3replay --info`). **A second, larger error at wide outputs [C]:** roadrage1's game camera has a 16:9
view window (viewport +0x48 / +0x4C = 1.778 in all 17,539 markers; race2's is 4:3, 0.678 x 0.508: the session's own
setting), so the game's 640 2D pixels span a 16:9 picture, while b3replay draws the HUD in a 4:3 box over a Hor+ 3D. A
held marker therefore sits 1.333 x too far from the screen centre horizontally at any output wider than the capture's
frame: at 2560 x 1440 37 2D px (110 output px) at poll 4563, over the wrong car (picture below). hud2's "absolute items
stay on their cars" holds only for 4:3-window sessions.

**Capture (host-only, detected by presence; no version change).** `src/gameview/hud_capture.cpp`: one more capture-only entry at
DrawPositionMarker 0x1531C0 (with the object hooks; `B3PORT_SCENE_HUD_MARKERS=0` leaves it out) records a MARKER event
(kind 11): {pc, byte offset of the next write in Gt2d's buffer, `$a3`, `$t0`, `$t1`, `$t3` (points mode), the world point,
the viewport address, viewport +0x00 x 16, +0x48..+0x54, +0x78, +0x7C, +0x84, +0x88}. The observation key now includes
`$a3` / `$t0`: Draw passes every car's point at the same stack address, so a culled marker (no write) and the next car's
call agreed in pc, write pointer, `$a0`, `$a1` and the second was dropped as a repeat (first capture: 13,577 calls seen,
the fixed one 17,539; its arrows were attributed to the culled marker before).

**Reader** (`hud.cpp`): MARKER events switch the current marker at their byte offset like OBJECT events (Begin ends it);
each draw gets `HudDraw::marker`; `hud_marker_project` repeats the game's projection; `Capture::decode` anchors each
marker to its CB3RaceCar's Vehicle (`HudAnchorRef`: the tick's vehicles; a points-mode value with no car: the vehicle
under the point within 0.5 m) and keeps offset = point - the vehicle's main position.

**Per-frame projection** (`View::hud_list`, `HudProjection`: the 3D pass's view-projection and the frame's interpolated
vehicles; offline, offscreen and live viewers): the point becomes the anchor vehicle's interpolated position + offset,
projected with the frame's view-projection into the HUD's 2D space (`hud_project`: hud_list's own mapping inverted, so
it follows Hor+ and the 4:3 box at any aspect, and a free camera); the marker's draws keep their vertices and get a
per-frame 2D -> clip matrix: moved by the difference to the captured place and scaled about the point by the game's size
rule at the new depth (cap 320); hidden when the point is behind the near plane. `--hud-markers frame` (default) | `tick`
(as captured, hud1-hud3), key **K**. Not changed: which markers exist, their colours and texts (the tick's).

**Checks** (`b3replay --info` on roadrage1-hud4: build sha256 4be82c2f...) [C]:
| Check | Result |
|---|---|
| Anchor offset (point - car main position) | horizontal at most 0.0001 m, height 0.8715 .. 0.9219 m (the roof): the physics matrix is the main matrix |
| The game's projection vs each drawn arrow's centre (attribution and formula) | 2,254 of 2,254 arrows within 0.5 px (max 0.251) |
| Tick frames: b3replay's camera (640 x 448) vs the game's projection, drawn markers | **11,285 of 11,285 within 0.5 px**: mean 0.0048, median 0.0021, 99 % 0.037, max 0.156 px; depth (clip w) vs the game's z within 0.001 |
| The same with the previous / next tick's camera | 1,467 / 1,421 of ~11,280 within 0.5 px: the HUD frame of poll k was drawn with tick k's camera and cars |
| Rendered tick frames, per-frame vs held (alpha 0, polls 4559..4565) | 4 of 7 pixel-identical; the others differ in 1 / 3 / 42 arrow-edge pixels (moves of at most 0.16 px) |
| race2 (Crash event; capture 5,181 of 5,181 polls exact) | 68 points-mode values over vehicles (`DrawSingleVehicleValue`, car 0), each anchored to the vehicle under its point (horizontal 0.0000 m, height 0.97 .. 3.25 m); 43 of 43 drawn within 0.013 px at the tick (2-tick frames: the next tick's camera agrees too); held vs point at alpha 0.5 median 1.48, max 5.6 px |
| Live viewer (`b3livefeed` of the capture, `--offscreen-live` shots 4563+0.75, 4563+0, 4564+0.5) | byte-identical to the offline per-frame frames |

**Before / after over a run of frames** (polls 4559..4565, alpha 0 / 0.25 / 0.5 / 0.75, 640 x 448 offscreen; the arrow's
pixel centroid from HUD-on minus `--no-hud` frames, against its roof point projected in that frame; 4558 left out:
the takedown banner's translucent band covers the window). The arrow's centroid is 1.167 x `height` = 0.469 x its
drawn size above the point (the triangle's geometry):
| Arrow (slot, size) | Held (main): offset / drawn size, y | frame-to-frame change of the offset | Per frame (hud4): offset / drawn size, y | change |
|---|---|---|---|---|
| slot 0, size 18-20 (far car) | -0.446 .. -1.019 | mean 4.87 px, max 10.82 | **-0.446 .. -0.480** | mean 0.35 px, max 0.94 |
| slot 2, size 30-38 | -0.469 .. -0.840 | mean 5.39 px, max 11.23 | **-0.462 .. -0.478** | mean 0.48 px, max 1.15 |
| slot 3, size 45-60 (closing fast) | -0.469 .. -0.943 | mean 8.20 px, max 12.45 | **-0.469 .. -0.481** | mean 3.31 px (the arrow grows 1.34 x in the tick), max 6.77 |
The held offset saw-tooths each tick (back to -0.469 at every tick); the per-frame one stays at the geometric value
within pixel quantisation. Pictures: `roadrage1-4563-075-held-vs-perframe-x3.png` (640 x 448 crop x 3, held left),
`roadrage1-4563-075-2560x1440-{tick,frame}.png` and `-held-vs-perframe-crop.png` (the held arrow over the white car,
the per-frame one over its blue car).

**Exactness** (roadrage1, capture on and off) [C]: capture on 13,474 of
13,474 polls exact, 0 other EE cycle / step counts, 0 missed flushes, 0 unhooked objects, 17,539 markers; capture off
13,474 of 13,474 exact. GS stream on vs off: 83 of 17,645 frames differ, the classes hud2 accepted (§17.11): 75
front-end frames in full / hdr / masked only, frame 1911 with bytes too, p0 at 6697-6699, p2 at 16594-16597; nothing
new. Capture size 119.2 MB plain (hud2's 117.5).

**Other 3D-anchored 2D** (decomp asm: every function that reads `D_00666270` and draws 2D) [C list; L roles]:
`DrawSingleVehicleValue` 0x1540A0 (via `func_003CC2D0`, which sorts vehicles by distance; and `DrawTrafficValues`
0x154230) calls the same DrawPositionMarker in points mode with a vehicle's point (car 0): covered by the same events
(anchored to the vehicle under the point; 0 in roadrage1); `CB3Globe2dObject::DrawGlobeHalo` / `DrawInvisiblePolys`
0x3B17F0 (the front end's globe, not drawn by b3replay yet); `func_0019DD00` (the loading screen: reads the viewport
only to end its camera update, nothing anchored). Other code could project through another viewport pointer; none is
known.

**Not done.** The markers' Z test (Draw sets EnableZTest(1) and SetZ from the point's depth, so on the PS2 a marker behind
a building is hidden; b3replay's 2D stage has no depth); the 2D layout of 16:9-window sessions (the PS2 stretches the
whole HUD to 16:9; b3replay keeps the 4:3 box by design); data/scene not recaptured here.

## 18. HD texture packs (hdtex1, 2026-11-03)

A PCSX2 HD texture pack for SLUS-21050 (a third-party mod: read in place from its own folder, never copied into
the repo; 7,859 DDS, 2.3 GB) loads in
b3replay, b3view and the live viewer in place of the game's textures. Code: `include/b3asset/hdpack.h`,
`src/b3asset/hdpack.cpp`; tool `tools/render/b3hdkeys.cpp`; tests `tests/test_hdpack.cpp` (synthetic data only).

### 18.1 The key PCSX2 names a replacement by [C]

Read from PCSX2's source (github.com/PCSX2/pcsx2 master, 2026-11-03; GPL, read only, nothing copied): `GSTextureReplacements.cpp`
(`TextureName`, `ParseReplacementName`, `CreateTextureName`), `GSTextureCache.cpp` (`HashCacheKey::Create`,
`HashTextureLevel`, `PaletteKeyHash`), `GSClut.cpp` (`Read32`, `ReadCLUT_T32_I4/I8`), `GSLocalMemory.cpp` (block sizes,
`fmsk`), `GSState.cpp` (`GetTex0Layer`).

- File name `<TEX0 hash>-<CLUT hash>-<bits>.<ext>` (`%llx-%llx-%08x`; no CLUT part for unpaletted formats; region
  forms `-r<w>x<h>-` exist, none in this pack). The folder is scanned recursively (`FILESYSTEM_FIND_RECURSIVE`).
- **bits** = PSM | TW << 6 | TH << 10 | bit 14 (an old TCC copy, cleared on load: `RemoveUnusedBits`) | TEXA TA0 << 15,
  AEM << 23, TA1 << 24 (only for CT24 / CT16; 0 for paletted). The earlier reading [G] holds: 0x5994 = PSMT4 64x64
  (bit 14 set), 0x5993 PSMT8 64x64, 0x5dd4 PSMT4 128x128.
- **TEX0 hash** = XXH3-64 (seed 0, default secret) over the texture as the GS holds it: the 256-byte GS memory blocks of
  the TW x TH rectangle in block-row order (left to right, top to bottom); for textures smaller than one block
  (PSMT8 16x16, PSMT4 32x16, 32-bit 8x8) and for formats using part of each word (`fmsk` not all ones: T8H, T4HL/HH,
  CT24, CT16) over the texels expanded row by row (paletted: one index byte per texel; else RGBA8). The result does
  not depend on TBP0 (the blocks move with it), so the raster's own upload replayed into `GsMemory` at its stored
  relative TBP gives the key the game's texture-cache slot gets.
- **CLUT hash** = XXH3-64 over the palette PCSX2 holds after `Read32`: 16 (T4) or 256 (T8) u32 colours in index order
  (CSM1 unswizzled, CSA applied; T8 groups clamp at 240), raw GS alpha (0x80 = 1).
- **Mip levels**: with hardware mipmapping PCSX2 hashes the draw's LOD range together; a mip level drawn on its own uses
  `GetTex0Layer` (TBP/TBW from MIPTBP1/2, TW/TH minus the level). The pack holds many such keys, and their names carry
  another level's size bits than the hashed range [C pack data: GL_rockblend1 (US/C3_V1) levels 2..4 hashed, named
  64x64 and 128x128]. `hd_candidate_keys` lists every (named level j, hashed range f..f+c-1, j <= f) combination,
  most detailed first (base key first; each range hashed once); the first one in the pack wins.
- Evidence [C]: own XXH3 equals python xxhash 4.0.2 on 36 lengths (0..20000, every size class and block edge) and on the
  test's 15 vectors; the very first scan hit pack files (GLOBAL.TXD alpine-st1 ->
  `14d96bc39c0238bc-73dceb5dfd53ae7a-00006213.dds`, car paint palettes per CLUT), and over the whole install the keys
  claim 98.9 % of the pack (below).

### 18.2 Coverage [C `b3hdkeys scan` / `--keys`, pack and game read in place]

| Set | Rasters | With a replacement | Notes |
|---|---|---|---|
| race2 route US/C3_V1 STATIC.DAT | 177 | 175 (174 base key, 1 mip-path key) | misses: animated sign frames / small props (e.g. Freeway_info_slow2-4 on other routes) |
| race2 ENVIRO.DAT | 4 | 0 | sky textures: runtime CLUT (below) |
| race2 cars (CUPE/CAR3 + 7 traffic BTV) | 8 | 8 | per paint: e.g. coupe3 6 of 8 palettes |
| roadrage1 route EU/C4_V1 STATIC.DAT | 133 | 130 | |
| roadrage1 ENVIRO.DAT | 4 | 0 | |
| roadrage1 cars (HSPC/CAR20, SPRT/CAR5/8/9, 5 BTV) | 9 | 9 | HSPC20 2 of 8 palettes; Sport5/8/9 6 of 8 |
| DATA/GLOBAL.TXD | 194 | 128 | misses: effects (fx*, BoostFireOver01-20, coronas, glass), some HUD icons |
| DATA/FRONTEND.TXD | 221 | 182 | |
| Whole install (header scan of every file) | 9,955 | 8,961 | |

In the replays (`--hd-pack`, every raster the renderer decodes incl. HUD and effects): race2 poll 3500 534 looked up,
493 replaced; roadrage1 poll 9000 477 / 411.

**The pack** [C]: 7,859 files = 7,505 in the top folder, 182 in `UI/`, 172 in `UI/Cars/`; all DDS DXT5 (BC3), one
level, exactly 4x the TW x TH size; formats PSMT4 / PSMT8 / PSMT8H only (no CT32 / CT16 replacements). Alpha in PS2
units (most common maximum 127; some 0x80..0x8D). Of the 7,859, **7,776 (98.9 %) are claimed** by a key of a game
raster: 3,670 by base keys, 4,106 by mip-path keys (mostly level 1 and 2 drawn on their own). The **83 unclaimed**:
- 46 known textures with a CLUT no file holds: the ENVIRO.DAT sky / cloud textures under CLUT `94e30d971b37f631`, which
  is [C, XXH3 of the candidate] **black with alpha = index** (`i << 24`, 256 entries): the game draws the sky as an
  alpha mask through a runtime palette, so those replacements are alpha-only images (not used: our sky decodes the
  stored palette); and car atlases in paints not stored in the car files.
- 37 with no texture in any file: T8H 1024x256 / 1024x128 / 128x128 / 64x64 and one T4HL / T4HH 128x64: textures
  read from 32-bit buffers' alpha bits, built at run time [L].

### 18.3 Loader

- `b3asset::HdPack` indexes the folder by name (strict hex parse; 89 ms for 7,859 names over /mnt/c); nothing is read
  until a hit. `HdTextures` holds the pack, the files read and (§18.6) the BC chains built from them.
- **One hook** in `decode_raster_clut` (`set_hd_textures`; off by default): after the normal decode, the raster's key
  is looked up with the same GsMemory state and CLUT add (car paints, HUD palettes), and on a hit `levels` is replaced
  by the pack image **cropped to the raster's extent** (a replacement covers TEX0's 2^TW x 2^TH, the decode covers
  width x height: crop = width / 2^TW of the DDS), as one level (the GPU builds the chain). Normalised UVs stay valid
  for every consumer (tracks, cars, effects, HUD); `translucent` / `max_alpha` keep the original's values.
- HUD: `HudTexture.w/h` stay the raster's extent (its UV normalisation), new `img_w/img_h` carry the pixel size for the
  upload (3 lines in hud.h / hud.cpp / view.cpp).
- **Alpha** [C]: DDS alpha is PS2 units (PCSX2's loaders write 0x80 for opaque), mapped like GS alpha (>= 0x80 -> 255,
  a -> 2a); TCC 0 forces 1. Then it is **held to the original's**: 0 over the original's transparent texels (alpha 0)
  where its bilinear alpha is below 1/4, never above the original's maximum. Low but nonzero alpha is never cut: the
  blended overlays (the sun haze over US/C2_V2's buildings, the light pattern in EU/P1_V2's tunnel, road and sign
  masks) keep the pack's alpha. Up to 1.2.0 every texel whose bilinear alpha was below 1/4 was cut, which made those
  overlays vanish or turn blotchy with any pack; with an identity pack (each texture's own decode at 1x) 9-61 % of a
  frame's pixels differed from no pack, now the frames are byte-identical to `B3_HD_RAW_ALPHA=1`. Why the cut: race2's
  flag frames `Chgo_Flag1..10` (US/C3_V1, alpha-tested GREATER AREF 0, original maximum 0x7F) have a triangle mapped onto
  one transparent texel beside opaque ones, which the game never draws; the pack's outline (slightly wider, opaque
  0x80..0x8D) drew it as a solid cream wedge under the gateway sign. Bisected to that one replacement (`B3_HD_LIMIT`-style
  experiment, not kept); the coverage cut alone or the clamp alone leaves it (28.4 / 29.0 levels mean difference to the
  original in that region), both match (2.2). A cut only where the bilinear alpha is 0 leaves it too (20.1 levels, as
  raw alpha; this rule 1.9, the same as the old cut). A nearest-texel coverage mask also fixes it but squares foliage
  outlines; the bilinear contour keeps the pack's outlines (zoom compared), and on textures with only transparent and
  opaque texels the rule equals the old cut (race2's trees: 0 pixels differ).
  `B3_HD_RAW_ALPHA=1` uses the pack's alpha unchanged (as PCSX2 does). PCSX2 with this pack most likely shows the wedge
  too [L: the same values meet the same test].
- **Speed**: `hd_prefetch` reads the route's hits on 8 threads before the track decode (reads over WSL's /mnt/c are
  ~7x faster in parallel: 54 MB in 0.29 s vs 43 MB in 1.6 s serially); the cache is dropped after the track's scenes are
  built. The route's LOD model now reuses the main model's GPU textures (`build_scene(..., share)`, same table checked
  by name and size; halves track texture uploads with or without a pack, frames unchanged). race2 poll 3500 offscreen
  at 1920 x 1080: 4.0 s / 1.27 GB without the pack, 8.0 s / 2.5 GB with it (DDS ~1.5 s, the rest GPU upload of ~300 MB
  RGBA8 plus mip generation through per-texture transfers in b3gfx).
- Switches: `--hd-pack <dir>` (b3replay all modes incl. `--live`, b3view), else `$B3_HD_PACK`; `--no-hd` wins; one stderr
  line reports lookups / replacements / time. Window key **P** in b3replay toggles the pack (reloads track, cars,
  effects; the HUD keeps its startup textures) [not run: no windows here].
- Formats decoded: BC1 / BC2 / BC3 (also via DX10 headers) and 32-bit RGBA / BGRA; BC7 and others are refused with a
  reason (the pack has none). Truncated or malformed headers are refused (bounds-checked; tests).

### 18.4 Checks

- `make test`: all pass, new test_hdpack 7 tests / 67 checks (XXH3 vectors; names; T8 raw-block key and TBP
  independence; T4 expanded key; mip candidates; DDS RGBA / BC3 and truncation; hook crop and alpha rules).
- No pack and `--no-hd` (with `B3_HD_PACK` set): **pixel-identical to main c4f09f5** (pngdiff 0 of 2,073,600) for
  race2 polls 2450 / 3500, roadrage1 6000 / 9000 at 1920 x 1080, b3view EU/C4_V1 and SPRT/CAR8.BGV.
- Live: b3livefeed race2 at 120 ticks/s, `b3replay --live ... --hd-pack`: shot at poll 3500 identical to the offline
  HD shot (0 pixels); all 5,181 ticks, 0 resyncs. At 600 ticks/s the HD route load outran the reader's window (shot
  lost: the slower load, not the pack); route load 683 ms without the pack, 4.7 s with it (5.5 s before the LOD texture
  sharing): the live view joins a race about 4 s later with the pack.
- `tools/render/build_win.sh` (OUT = `%B3_WIN%\hdtex1`) builds (b3view / b3replay / b3assetdump.exe).
- Pictures (game- and pack-derived, not committed): `%B3_WIN%\hdtex1\pictures\` (race2 2450 / 3500,
  roadrage1 6000 / 9000, original and HD at 1920 x 1080 and 3456 x 2104, side by sides, the flag zoom, the foliage
  alpha comparison).

### 18.5 Not done

- Lazy pop-in (a texture drawn before its replacement is ready); the HUD toggle. (Compressed upload: §18.6.)
- The sky / cloud replacements (they need the runtime alpha-ramp CLUT path in our sky model); runtime-built T8H
  textures; region-named replacements (none in this pack); other packs.
- macOS: not built here; the code is portable (no SIMD; `unsigned __int128`, std::thread, std::filesystem).

### 18.6 Kept compressed on the GPU (hdgpu, 2026-11-19)

Bigger packs (4x-8x) did not fit as RGBA8: every replacement was decoded on the CPU, cached, uploaded as RGBA8 and its
chain built by the GPU. Now a BC1 / BC2 / BC3 replacement stays BC from the file to the sampler (code:
`include/b3asset/bc.h` + `src/b3asset/bc.cpp`, `HdTextures` in hdpack.cpp, `TextureLevel::bc` and
`Device::supports_bc` in b3gfx):

- **When**: the device samples BC1, BC2 and BC3 (`SDL_GPUTextureSupportsFormat`; desktop D3D12 / Vulkan / Metal do;
  `set_hd_compressed` after the device is made), the decode is a track's or a car's (`HdCompressScope` in read_track's
  texture table and read_car's atlas: their textures are only uploaded), and the crop (§18.3) is whole 4x4 blocks.
  Everything else keeps the RGBA8 path as before: RGBA32 files, crops that are not whole blocks, the HUD, effects,
  sky and sun (they read texels on the CPU), and `$B3_HD_RGBA=1` (all of it RGBA8, for A/B).
- **Level 0** is the file's blocks with the PS2 alpha mapped as decode_dds maps it: BC1 as is (opaque black for TCC 0
  where a block holds a transparent texel); BC2's 4-bit alpha a -> (a >= 8 ? 15 : 2a), exact; BC3's alpha endpoints
  doubled when both are <= 0x80 (indices kept: within one level of the CPU decode), else that block's alpha encoded
  again; TCC 0: opaque alpha blocks. A BC1 with transparent texels (TCC 1) becomes BC3 (its mips need partial alpha;
  3-colour blocks get their colour fitted again, BC3's colour half having no 3-colour mode).
- **Mips**: the file's stored levels while the crop is the whole file; the rest built down to 1 x 1 as
  SDL_GenerateMipmapsForGPUTexture does (bilinear at the texel centres: an exact 2x2 average for even sizes, `mip_down`)
  from the decoded level above, then encoded (principal-axis colour fit plus a least-squares pass; BC3 alpha in the
  8- or 6-value mode, whichever is closer).
- **The alpha rule** (§18.3) needs RGBA8. It can change nothing when the original has no alpha below 1/4 and a
  maximum of 0x80 or more (for any rule that only cuts where the original's bilinear alpha is below 64 and clamps to
  the maximum): those rasters take the file's blocks. The others run the rule on RGBA8 as before; if it changed
  texels, the chain keeps the file's colour halves (the rule leaves colour alone) with their alpha encoded again as
  BC3, the mips' alpha built from the rule's level 0; if it changed nothing, the file's blocks. The rule's loop runs in
  row strips on up to 8 threads (its body unchanged).
- **Where**: `hd_prefetch` builds the chains on its threads (the file chains, and the rule's chains for the rasters
  whose rule may change something), so the track decode only looks them up; the route's LOD model shares them. Once
  `build_scene` has uploaded a track's or car's textures, their BC levels drop the CPU copy (sizes kept); `hd_trim`
  drops the files and chains after the route. The HD TEXTURES key (F4) needs no CPU copy (both pictures are on the
  GPU).
- **Threads**: `HdTextures` takes a lock around the hook, prefetch's merge and trim, and prefetch suppresses the hook
  on its own thread only (`hd_textures()` is per thread there), so the live view's route worker (loadlag1) now also
  loads an HD route: the window keeps drawing while it decodes.
- One stderr line after each live route load: replacements kept compressed / as RGBA8, BC MB, rule re-encodes, hook
  time; another the replacements resident on the GPU and their MB (`Device::hd_originals`).
- **Measured** (race_check's race, US/C2_V2, offscreen lavapipe, route load before the race; medians of 3, other jobs on
  the machine): a 4x BC3 pack (7,859 files): load 6.3 s -> 2.2 s, peak RSS 2.96 -> 2.43 GB, the route's replacements
  on the GPU 387 -> 111 MB (306 of 348 hits kept BC, the rest HUD / effects); a 4x BC1 / BC3 pack: 5.5 s -> 2.6 s,
  2.99 -> 2.37 GB, 407 -> 123 MB. Without a pack: 0.53 / 0.57 s, the same RSS, shots identical. Pictures: no pack and
  `$B3_HD_RGBA=1` identical to the RGBA8 loader (pngdiff 0 of 2,073,600 at 1920 x 1080, polls 10700 / 11500); BC
  against RGBA8 mean 0.25-0.6 levels per channel, the mips' encoding (max 109-236 on single alpha-tested edge pixels).
  With the async worker (offscreen bench) the window's thread spends 0.65 s on the route instead of 2.0 s (upload).

## 19. Contact effects: scrapes and sparks (fx2, 2026-11-04)

Item 3 of docs/effects-survey.md §5 (sparks; popcorn below) and the scrapes of item 6, drawn the game's way from captured
state in the offline replay and the live viewer alike (`View`). Code: `src/b3replay/sparks.{h,cpp}` and the scrape
strip in `src/b3replay/effects.cpp`; `include/b3asset/sparkshade.h` + `src/b3asset/sparkshade.cpp` (the spark VU
program as C++, read in fx2), `tools/render/b3vucheck_spark.cpp`, `tests/test_sparkshade.cpp`; the hook's
`grab_contact_effects` in `src/gameview/scene_capture.cpp`; `ContactFx` in `src/b3replay/capture.h`. Decomp
citations: HEAD 2ad95ae82e (read only). Tags as above. Toggles: `--no-particles` / O (scrapes, sparks, later popcorn and
particles), `--no-effects` / F (all effects), in b3replay and the live viewer; `--info` prints a contact report.

### 19.1 Scrapes (CB3ScrapeSystem, gEffectsManager + 0x16C80) [C asm unless tagged]

| Step | Evidence |
|---|---|
| Spawners: `CB3EffectsManager::UpdateRaceCarWorldCollisionEffects` 0x2594C0, `UpdateTrafficWorldCollisionEffects` 0x258490, `UpdateBodyPartWorldCollisionEffects` (in 0x257E10's unit) call `func_00375840` (DNAT `CB3ScrapeSystem::AddScrapeSegment` [L]; matched) | jal scan of `assembly/asm/game/unit_00257E10.s` |
| `func_00375840`: continues the caller's emitter (its +0x10, while now <= its +0x14 + 1.5 x the frame step and the buffer holds < 16), else takes a free emitter (+0x8A00 list, count +0x8A04) into the type list +0x8A8C (32 pointers, count +0x80 after it), +0x0 = an LCG value (`CB3ScrapeRenderer::mRandom` 0x4E2E50) & 3, carrying the last segment over when the old one was full | matched source |
| `CB3ScrapeEmitter::AddScrapeSegment` 0x3755E0: position y += D_004E0FA0 (0.0254), optionally x the matrix; ignored if within 2^-32 m of the last; side = normalise(position - last) x normal; the first segment takes the second's side; a segment nearly parallel to the last one (dot > D_004E136C 0.99999) replaces it, else appended (max 16); +0x4 = now | asm, every op |
| Emitter (0x448): +0x0 UV index, +0x4 last time, +0x8 type, +0xC params, +0x10 flags, +0x12 buffer index, two buffers +0x20 / +0x230 (+0x0 count, segments 0x20 from +0x10: position, side), +0x440 / +0x444 pointers (= &buf[+0x12] / the other) | asm 0x3755E0, 0x375AA0, 0x375BF0 |
| `CB3ScrapeSystem::Update` 0x375BF0 (E map order [L name]): an emitter untouched (flags & 3 = 0) for more than params +0x3C (15 s) is freed; a touched one has +0x440's count, first and last segment copied into +0x444, flags cleared | asm |
| `RenderPreFogEffects` 0x2620C0 calls `CB3ScrapeSystem::Render` 0x375AA0 only while the view's race car (gWorld + 0x12AD50 + 0x27D0 p) +0x18EA is set (Crash mode's crash: race2 polls 3253..4622); Render finds the texture named params +0x20, `CB3ScrapeRenderer::Render` 0x375F90 draws buffer **+0x440** of each type-0 emitter, then swaps the buffers (so at a poll the drawn buffer is +0x444) | matched 0x2620C0 / 0x375AA0, asm 0x375F90 |
| The strip (0x375F90): per segment k colour = lerp(params +0x0, +0x10, **0**) x 128 -> FTOI0, min (255, 255, 255, 128) = (99, 99, 99, 110) (params from `CB3ScrapeSystem::Construct` 0x375E90: (0.78, 0.78, 0.78, 0.86), D_004E114C / 50); half width = (+0x34 + 0 x (+0x38 - +0x34)) x 0.5 = 0.875; offset = side x half x karScrapeUVs[+0x0] (0, 0.25, 0.5, 0.75), offset.y += 0.0254; vertices position -+ offset, ST (k & 1, 0) / (k & 1, uv); one `B3Im3d::RenderTristrip` 0x365040 per emitter. No fade: a scrape stays until Update frees it | asm, every op; constants from the ELF |
| GS state: `CB3ScrapeRenderer::BeginRender` 0x376260 = `B3Im3d::BeginRendering` 0x365640 (ALPHA_1 0x44, TEST_1 0x5000B with AREF 0 (_grATestThreshold = 0), CLAMP_1 0 = REPEAT) + `SetZWrite(false)`; texture "fxscrape" (GLOBAL.TXD) | asm |

Port: `b3replay::scrape_strip` (the strip, op for op in host float), drawn in the pre-fog effects stage after the skid
marks (as RenderPreFogEffects orders them), one draw for all strips (list order). Scrapes are static geometry: they change
at the ticks (no interpolation needed). Capture: the list always; while the gate is set, each listed emitter's header,
pointers, the drawn buffer (+0x444) and the other's count. In race2 the gate is on in 1,424 ticks, 9.2 strips / 32
segments per gated tick (max 10, poll 3337); they look like pale scratch marks on the road at the crash junction.

### 19.2 Sparks (CB3SparkArray x 6, gEffectsManager + 0x21F0; VU1 0x46D090) [C unless tagged]

Read in fx2; every input and step is in `include/b3asset/sparkshade.h`. In short:
- **Spawn** (`SpawnBouncySpark` 0x263C40; spawners func_0025DF30 (E map `SpawnSparksFromPoint` [L]) from the race car -
  traffic / race car - race car / object collision handlers, `func_0025D930` from `DoCrazySparksWithWorld` 0x255EE0,
  `HandleBattleSparks` 0x2612A0): each array has a normal and a crash bank of 4 rings (round robin), records in blocks of
  0x90 (4 spawn times, -9999 = unused; 4 x (position, velocity) whose w lanes carry the post-bounce parabola: the one
  ground bounce is solved on the CPU at spawn).
- **Draw**: a spark is a motion-blurred streak: `BeginRender` 0x2638C0 keeps an 18-frame camera history (D_01E31A30,
  `CSparkFrameData` 0x60: a clip matrix, screen constants, the frame's time; shifted when now - f[1].t >= 1/96 s), and for
  each pair of consecutive frames k, k+1 (k < i, i = 17 once the history is full) the program draws one camera-facing
  quad (half width params +0x58: 0.017 m for array 0, 0.125 m for 1-5) from the spark at t_k seen by frame k's camera to
  the spark at t_k+1 seen by frame k+1's, s = age / 0.049 s (clamped), t 0 / 0.875, one flat colour per ring. A spark is
  used in an interval iff born before its older end and alive at its newer end (ring lifetime quantised per batch).
- **Which**: `RenderSparkArray` 0x2637F0: an array is drawn when its normal bank spawned within the max lifetime, its
  crash bank too when the player's crash flag (gWorld + 0x12C63A, mbIsCrash) is set and the crash bank did (+0xA4). The
  bank's DMA chain is static (`CSparkBank::Construct` 0x264170); every block is sent; the program drops dead sparks.
- **GS** (`SetRenderstates` 0x2633A0, matched): TEST_1 0x71001 (alpha test NEVER, AFAIL FB_ONLY: colour written, no Z;
  ZTST GREATER), ALPHA_1 0x48 (Cs As + Cd, additive), CLAMP_1 5, texture "fxspark", PRIM 0x54 (flat TRISTRIP, ST/Q).

**Capture** (`grab_contact_effects`, no version change, detected by presence): the six array heads and params, each
bank's static chain head and every ring's blocks (written only at spawns, so the delta stream carries the spawns),
CB3SparkRenderer's statics 0x4E2A00, D_01E31A20 (the GIF tag), D_00667050, and the camera history: the whole 0x6C0 bytes
on ticks where an array is renderable by RenderSparkArray's test (evaluated against the last render's time, f[0].t), its
newest record otherwise. `B3PORT_SCENE_SPARK_CHECK=1` (proof runs) takes the whole history every tick.

**Drawing** (`b3replay::build_sparks`): per renderable array and bank, the batches parsed from the captured chain, per
batch and interval `spark_segments` + `spark_project` (the program's own cull and quads), and each quad's view-space
corners (A +- n in frame k's view, B +- n in frame k+1's, before the divide) mapped back to the world through the inverse
of frame 0's clip rows (the render's own camera). Drawn by the native camera these land where the game drew them at the
tick, at any resolution, depth-tested against the scene; post-fog items after the coronas (`RenderPostFogEffects`
0x261E00 order), ALPHA 0x48 = Cs x As + Cd (additive, weighted by the shader's alpha), Z tested, not written.
Between ticks the quads stay at the tick's values (the game's own sparks change at its 60 Hz renders; the streaks
already carry the motion).

**Checks** [C]: the real program in the VU1 interpreter against the model (`b3vucheck_spark`):

| Input | Frames | Quads / vertices compared | Result |
|---|---|---|---|
| synthetic (helper, seed 20261104, 2,000 frames, retail params) | 2,000 (7,041 arrays) | 22,215,749 quads / 88,862,996 vertices | x, y, z, s, t, q, adc, prim, fog, quad and kick counts exact |
| race2-fx2 capture, every 5th tick with the whole history | 314 (623 banks, 301 crash) | 2,947,932 quads / 11,791,728 vertices | every field max error 0; chains rebuilt from the params = captured 623 / 623; +0xA4 recomputed = captured |

### 19.3 Popcorn (CB3PopcornArray x 4, gEffectsManager + 0x20F0; VU1 0x47C6F0, first 1728 B) [C unless tagged]

Read in fx2 (`include/b3asset/popcornshade.h`, `tools/render/b3vucheck_popcorn.cpp`,
`tests/test_popcornshade.cpp`). Spawned by `func_0025D430` (E map `SpawnPopcornFromPoint` [L]) from the collision handlers
and `DoVehicleGlass` 0x25FE70; `SpawnBouncyPopcorn` 0x365E80 writes the same 0x90 record blocks as the sparks (one ring per
array). Drawn first in `RenderPostFogEffects` 0x261E00 and only while the player's crash flag (gWorld + 0x12C63A, the view
race car +0x18EA) is set.
- **Not the spark program** (all 210 code pairs differ but the tail): one GS SPRITE per live particle at "now", no history.
  Alive iff ts < now and ts >= now - L (params +0xC); centre = p + v age + (0, g/2, 0, g/2) age^2, y = max(y, w); clipped by
  the full clip box with far = min(camera far, params +0x44 = 150 m); half extent size x sqrt(depth) in view space (size =
  params +0x10 x (0.2 + view window): on screen 1 / sqrt(depth), not perspective); colour (c0 + (c1 - c0) age^2 / L^2) x
  (1 + (params +0x34 - 1) depth / far) (+0x34 = (1, 1, 1, 0.01): only alpha fades, to 1 % at the far clip).
- GS: ALPHA_1 = array +0x0 = `_gu64PopcornBlendModes[params +0x8]`: 0x48 (additive) for array 0 ("fxpopcornspark"), 0x44
  for 1-3 ("fxpopcorndebris"); TEST_1 0x53001 (RGB only, no Z write, ZTST GEQUAL); CLAMP_1 5; PRIM 0x56.
- Program quirks [C, settled by running it]: pair 0x0046 loads block 0's particle-1 position into vf06 where vf05 belongs,
  so particle 1 of each batch's first block is drawn from the qword the previous batch prefetched (race2: 31,744 of
  59,988 such particles moved) — on the PS2 that is VIF-timing dependent [L], so the port draws the particle at its own
  record position; the pipelined sprite loop flips ST once more than it draws, so sprites alternate (0,0)-(1,1) /
  (1,1)-(0,0) and the first sprite of each later batch has one ST on both corners (kept: the port takes each sprite's ST
  from `popcorn_project` for the tick's camera). `CB3PopcornArray::Prepare` 0x366050 clears `field_0[i & 4]` per block
  (time lane 0 or position x of particle 0, never lanes 1-3), so old times can survive into a new race [C matched source].

**Capture**: the four array heads [0, 0x40) and their record blocks ((capacity + 3) / 4 x 0x90), maStandardParams 0x4A4DB0.
The RwCamera inputs (`ourGlobals` 0x670BD0 -> view matrix, view window, reciprocals, near, far) and vu1DataXYZScale /
Shift come from the already captured game camera (CGtViewport +0x00 screen matrix, +0x48 / +0x4C, +0x84 / +0x88) and the
camera record's +0x120 block: the interpreter check is exact with them [C], so no RwCamera range was added.

**Drawing** (`b3replay::build_popcorn`, `Effects::draw_popcorn`): per array the program's set-up from the tick's game
camera, `popcorn_particles` at the **displayed** time (the particles are closed-form in time, so they move smoothly between
ticks), each one a quad facing the displayed camera, half size `popcorn_view_half_extent(depth)`, colour x `popcorn_fade`
by its depth, ST as the program sends them; the program's near / far clip (the GPU clips the sides). One draw per array,
before the coronas.

**Check** (`b3vucheck_popcorn`): synthetic 2,000 frames (5,631 arrays, 1,703,509 live particles, 653,912 sprites /
1,307,824 vertices) and race2-fx2 every tick with a live particle (1,918 ticks, 76,720 batches, 4,335,062 particles,
2,359,487 sprites / 4,718,974 vertices): x, y, z, s, t, q, r, g, b, a, prim, adc, fog, sprite and kick counts all max
error 0 (the model handed the interpreter's vf05 for the 0x0046 particle).

### 19.4 Capture size and exactness (build sha256 9e15525b...)

| Session | Size (main's hud4 capture of the same session) | Largest additions |
|---|---|---|
| race2 (5,181 polls) | 29,361,544 bytes (25,382,864: +15.7 %) | spark / popcorn record blocks 1.6 MB (0.23 MB of it the initial state), scrape emitters 1.1 MB, camera history 0.9 MB |
| roadrage1 (13,474 polls) | 136,539,400 bytes (119,182,392: +14.6 %) | camera history 10.4 MB (sparks live in 8,603 of 9,552 race ticks), record blocks 5.2 MB, scrapes 2.2 MB |

| Session | Capture on | Capture off | GS stream |
|---|---|---|---|
| race2 | 5,181 of 5,181 polls exact, 0 other EE cycle counts | 5,181 of 5,181, 0 other | **identical in every column over 8,030 frames to main's capture-on run** (hud4, and hud2d); against capture off the 5 frames 4385/4386, 7426..7428 differ in p0/p1/p2 exactly as hud2d's own on / off pair does (main's HUD capture, not this block) |
| roadrage1 | 13,474 of 13,474 exact, 0 other | 13,474 of 13,474, 0 other | against hud4's capture-on run: p0/p1/p2 identical in all 17,645 frames, 79 front-end frames (1787..4683) differ in full/hdr/masked only (the host-record variance of §12.2) |
| race2 spark check capture | 5,181 of 5,181 exact, 0 other (30,593,600 bytes) | | identical to the capture-on run's prefix digest (6870a561...) |

Live feed: `b3livefeed` race2-fx2 at 600 ticks/s, `b3replay --live --verify`: 5,181 ticks identical (`same_tick` compares
the contact block), 0 differ; at 120 ticks/s the live shots at polls 3260 and 3465 equal the offline `--offscreen`
frames (0 pixels differ).

### 19.5 The programs against the captures (`b3vucheck_spark --capture`, `b3vucheck_popcorn --capture`) [C]

| Capture | Ticks checked | Quads or sprites | Vertices compared | Result |
|---|---|---|---|---|
| sparks, race2-fx2chk (every tick with the whole history) | 1,571 (3,122 banks, 1,508 crash) | 14,743,260 quads | 58,973,040 | every field max error 0; chains rebuilt = captured 3,122 / 3,122; +0xA4 recomputed = captured |
| sparks, roadrage1-fx2 (every 2nd) | 4,303 (5,556 banks, 1,253 crash) | 11,169,258 | 44,677,032 | max error 0; chains 5,556 / 5,556 |
| popcorn, race2-fx2 (every tick with a live particle) | 1,918 | 2,359,487 sprites | 4,718,974 | max error 0 |
| popcorn, roadrage1-fx2 | 9,364 | 2,527,611 sprites | 5,055,222 | max error 0 (0x0046 particle drawn elsewhere 33,823 times) |

| `--info` (contact) | race2 segment 0 (2,229 ticks) | roadrage1 (9,552 race ticks) |
|---|---|---|
| scrape gate on / drawn | 1,424 / 1,369 ticks (3253..4622) | 2,504 / 1,904 (the player's crashes: 5475..5807, 6150..6660, ...) |
| strips per gated tick (max) | 9.17, 32 segments (10, poll 3337) | 3.51, 21.8 segments (9, poll 5793) |
| sparks alive per tick (crash banks), max | 469 (408), 2,633 at poll 3465 | 601 (411), 3,013 at poll 7558 |
| ticks with a renderable spark array | 1,447 | 8,603 |

### 19.6 Pictures (640 x 448 offscreen, game camera unless noted; game-derived, not committed)

`%B3_WIN%\fx2\pictures\`, each as `-contact` / `-nocontact` (`--no-particles`):
race2 3260 (the impact in Impact Time: long glowing streaks, popcorn chips), 3465 (sparks where the upturned car grinds
the wall; `-sparks-x3-before-after`), 3337 free view over the junction (`--free-eye 4852,153,1636 --look-at
4858,149,1644`: the scrape marks on the road, `-x4-before-after`); roadrage1 4500, 5100 (trading paint at 168 mph: long
streaks off the rival), 5600 and 7558 (Road Rage crashes: sparks, popcorn, scrapes).

### 19.7 The crash explosion's full-screen flash (explosion1, 2026-11-16) [C asm + C matched source unless tagged]

`CB3EffectsManager::RenderPostFogEffects` 0x261E00 draws it after the sparks: when the view's crash flag (gWorld + 0x12C63A)
is set, `D_005179C0[idx] & 0x2000` is clear and the player block's physics object (+0x12D180 = +0x2430) has a crash time
`t` = `+0x3534` > 0, it calls `RenderFullScreenFlash(D_00522680 - t)` (0x399BA0). That function (matched) draws only for
`x = elapsed - 0.025` in [0, 0.12): `u = 8.333334 (0.12 - x)` and the fill `func_002276C0(gm, {2.0, 1.6, 1.0, u^2}, 1)`.
0x2276C0 (the graphics manager's full-view colour fill, nonmatching draft) packs `min(c x 127.95 (alpha 128.9), 255.9)` to
bytes (255, 204, 127, alpha) and sends one GS sprite over the camera's viewport: TEST_1 0x33001 (alpha test NEVER, AFAIL
RGB_ONLY), ALPHA_1 = `D_004B9DF0[1]` = 0x48 (Cs As + Cd, additive). So the flash is 0.12 s long, orange-white, strongest first.

Who stamps `+0x3534` (the only writers in the asm): the player physics' crash handler for the Crashbreaker button
(`GetCrashingDrivingControls` 0x1BB6E0 +0x4586 / crash mode: `t = clock`; it resets `t` to -1 0.9 s later,
`D_004E0E14`), `func_0023B000` and `HandleCrashingVehicleVehicleCollision` (crash mode). The scripted `crash` / `crashresults`
scenarios (the car runs into the junction's wall, no Crashbreaker pressed) never stamp it (`crash_time` stays -1 through
the whole run, `crashed` = 1), so they draw no flash; the Crashbreaker detonation is what the flash belongs to. The flash
is drawn at the displayed time (the world clock is interpolated between ticks), as an effects-stage, post-fog, untextured
quad just in front of the near plane (reverse Z: it passes every depth test) in `Effects::draw_explosion_flash`.
The inputs are the existing post1 capture (`PostIn`: `crash_time`, `flags`, the radial blur's `crashed`).
`B3_FLASH_FORCE=<tick>` (render proof aid, drawn only) plays the flash as if the time stamp had been written at that tick.

What the explosion still lacks: the fireball itself (DoExplosion's explosion fire / smoke / flash particle arrays 14-23 through
VU 0x46D750, and `UpdateExplosionTrails` 0x399C30's ballistic trails, which feed the same particle arrays [C asm: it calls the
particle spawner func_0025F0C0 with array indices 0x17..0x19]); the particles are a separate piece of work.

### 19.8 Running it on Windows

```
%B3_WIN%\fx2\b3replay.exe %B3_WIN%\perf\scene\race2-fx2.b3scene
%B3_WIN%\fx2\b3replay.exe %B3_WIN%\perf\scene\roadrage1-fx2.b3scene
```
(`b3replay.exe` sha256 14a39aa3...) K toggles scrapes / sparks / popcorn, F all effects; the other keys as §12.4.
race2: the crash junction (polls 3250..4620: sparks, scrapes, popcorn); roadrage1: trading paint at 4500 / 5100, the
Road Rage crashes at 5475.., 6150.., 7300... The local data/scene recordings have no contact block until the main
session recaptures them (then the effects appear in both).

### 19.9 Not done

- Debris (VU1 0x475910). The particles are §19.10.
- Sparks between ticks: the tick's quads are drawn until the next tick (exact at tick frames); a display-time f[0]
  (`spark_frame_set` of the interpolated camera with BeginRender's 1/96 s shift rule) would move them smoothly.
- Capture size: the camera history (10.4 MB of roadrage1's +17.4 MB) could be rebuilt from its newest record per
  render (the check capture `B3PORT_SCENE_SPARK_CHECK=1` would prove the rebuild).
- Scrape strips and sparks are post / pre-fog as the game orders them; the bloom and motion blur post passes (survey
  item 10) are what make the streaks look like ribbons [L].
- Split screen (player 1's history D_01E31A30 + 0x6C0 and its gate) is not captured.

### 19.10 Particles (CB3ParticleArray x 24; VU1 0x46D750; effects3)

Tyre / exhaust smoke, dust, gravel, snow, glass shards, fire and explosion smoke / fire, drawn from the game's own program.
Unlike sparks and popcorn there is **no hand-written model**: `include/b3asset/particleshade.h` + `src/b3asset/particleshade.cpp`
run the program image (captured from RAM 0x46D750, 0x1980 bytes) in the b3asset VU interpreter on the captured data, with the
VIF stream the game sends [C EndRender 0x254580: camera rows 1017..1023 from the camera record; DispatchArray 0x254340: the
array's rows 1000..1016 (colours, the lighting set by SetLighting, the light direction in view space, class / lifetime /
time, the FOV-scaled size row); CB3ParticleBank::Construct 0x2558A0: per <= 60 particles UNPACK V4-32 / V4-16 / V4-8 with
stride 3, ITOP = count | 0x100 on the last chunk, MSCNT]. The program's GIF packets (SPRITE for classes 0 / 1, TRISTRIP for
the rotating classes and the gouraud class, ST / Q, RGBA, XYZ in screen space, ALPHA_1 and TEST_1 as A+D) are read back; each
vertex is mapped to the world through the program's own world-to-clip rows (VU memory 1011..1014 after its set-up, vf05 and
row 1022: s = off + vf05 (x/w, y/w, 1/w)), as the spark vertices are. Which banks: RenderParticleArray 0x254970's rule (live
mask, crash flag, D_004E29E4 / params +0x5D). Capture: the arrays (0x150 each, gEffectsManager + 0x170), each bank's record
allocation, maStandardParams, the names, the program, D_004E29E4 (`grab_contact_effects`; the camera record was already
captured). Drawn per tick (the quads stay at the tick's values, like sparks) after the popcorn and before the coronas, GS state
per packet as the program sends it (no Z write, Z GEQUAL, MODULATE, CLAMP 5). Levels: see pc-settings §7.18.

Build change: the shipped renderer (libb3render.so / b3render.dll) now links the VU interpreter (`vu1.cpp`) and
`particleshade.cpp`; the launcher and modtool still do not. A hand-written model checked against the interpreter (as for
sparks) would remove that: not done. GS state: ALPHA_1 / TEST_1 are sent only with the first packet of a chunk; later packets draw with the state still set, so a packet without its own A+D inherits the previous one (before: alpha 0 = opaque, no test: hard-edged squares). Quads whose vertices all have RGBA alpha under 2 are dropped; one persistent vertex buffer (triangles expanded in index order) and a grow-only identity index buffer are updated once per tick. Dev log: `B3PORT_PARTICLE_LOG=1` prints one line per rebuilt tick (with its build time). Not checked against a
PS2 capture (none exists for these effects); the pictures were judged by eye. Limits: particles outside the game camera's
frustum are culled by the program, so at ultrawide the edges lack them (as sparks).

## 20. Live session fixes: car paints, the display clock, the pop-in search (live2, 2026-11-04)

From the first windowed live session `race1` (2026-11-04 morning; a recorded play session, 53,760
polls: EU/M2_V2 Road Rage from poll 4971, then two races on EU/C1_V1 from 27477 and 39806). Capture (not committed):
`$P/scene/race1-live2.b3scene` (460.8 MB; **53,760 of 53,760 polls, 0 differ, 0 other EE cycle or step counts**).
Tags as in the rest of this document.

### 20.1 Car colours: the paint's CLUT comes from a table, not 4 x paint [C]

**Cause.** `CB3VehicleRenderer::RenderVehicle` calls `SetTexture(vehicle data +0x60, EssentialParams +0x59)` for every
vehicle kind (the draft `RenderVehicle__18CB3VehicleRenderer...cpp` line 296: race cars, traffic and crashed vehicles all
go through it). `SetTexture` 0x21FF60 (matched, `B3VehicleRendererRuntimePS2_0021FF60.cpp`) builds TEX0's high word as
`raster +0x40 + (u16 table at raster +0x90)[paint] << 5` when `paint < raster +0xA0` (the palette count), else the base
`raster +0x40`: CBP + table[paint] blocks. b3asset used CBP + 4 x (paint mod count). The tables in the 107 car files:

| Files | Paints | Table at +0x90 |
|---|---|---|
| 57 | 8 | 0, 4, 16, 20, 8, 12, 24, 28 |
| 14 | 6 | 0, 4, 16, 8, 12, 24 |
| 4 / 3 / 2 | 3 / 2 / 4 | 0, 4, 16 / 0, 4 / 0, 4, 8, 12 |
| 27 | 1 | 0 |

So paints 2..5 of the 8-paint cars came out as each other (2 <-> 4, 3 <-> 5), the 6-paint cars mixed 2..5, and a paint
past the count wrapped instead of falling back to the base CLUT (race1: HEVY/CAR18 paint 3 of 2). The paint byte itself
was right; capture and live feed read the same +0x59 through the same `View::car_model`, so the live and file paths
never differed (§13.2: every tick from the ring identical to the file's).

**Fix.** `b3asset::palette_cbp_add(file, raster, paint)` = SetTexture's rule; `decode_raster` (cars), the HD-pack read-ahead
(`hdpack.cpp`) and `b3hdkeys` use it (hud.cpp already read the table). b3view's P key wraps at the file's count.

**How many** (`b3replay --info` paint census, drawn vehicle-ticks; race cars every tick):

| race1 segment | Wrong CLUT before live2 | Race cars (file, paint) |
|---|---|---|
| 0 (M2_V2, Road Rage) | 86,237 of 134,783 (64.0 %) | SUPR/CAR5 3, SUPR/CAR8 0 and 2, SUPR/CAR9 2: three of the four |
| 1 (C1_V1) | 50,251 of 96,280 (52.2 %) | HSPC/CAR20 4, SPRT/CAR6 1 and 2, SPRT/CAR9 2: three of the four |
| 2 (C1_V1) | (same cars as 1) | as 1 |

Traffic and crashed vehicles: every COMP / HEVY file with 6 paints, paints 2..5.

Test: `real_car_paint_clut_table` (test_b3asset): COMP/CAR1's table, the fallback, paint k through `read_car` equal to
the raster decoded at the table's block, paints 2 / 4 and 3 / 5 different.

### 20.2 The display clock (db55faa) on the session's real arrivals; ticks arrive in pairs [C]

**Method.** Headless replays do not arrive like a play session: with other lanes' replays on the machine the guest ran
below real time (roadrage1 with the offscreen viewer's `--bench`: 349 s wall for 294 s of vblanks) and the bench's
60 Hz class was dominated by a 2.5 s hold after the route load, under either rule (race2: 17-19 % of those frames at
the newest tick, both rules). So the clock was run on a real session's arrivals instead: `tools/render/b3clocksim.cpp`
(new, `make render`) takes the arrival of poll k as the host release time of the guest vblank it ran in (`pads.log` +
the session's pace1 `frames.csv`) and runs `src/b3replay/clock.h` (the live viewer's clock, moved there unchanged
apart from the rule below) at a fixed display rate. Age = now - arrival time of the shown tick position (the latency
the viewer adds to what the game sends).

**What it shows.** race1 delivers ticks in groups: 2-tick frames (`CB3Game +0x2DA84` = 2) run their two polls inside
one vblank and then wait 33 ms (segment 0: 7,546 single ticks, 5,647 pairs; segment 1: 8,295 / 1,954; segment 2:
10,839 / 1,478). db55faa's clock extrapolates at most 1 tick past the newest arrival, so in pairs it reaches the
newest tick before the next pair and stops there until it arrives.

| race1, 500 fps display, delay 1.5 | Age p50 / p99 (60 Hz ticks) | Frames at the newest tick: 60 Hz / 30 Hz ticks | Hold events per minute (all / >= 8 ms) | Speed p1 / p99 (60 Hz) |
|---|---|---|---|---|
| seg 0, db55faa | 25.0 / 41.4 ms | 1.50 % / 1.10 % | 369.8 / 5.4 | 0.65 / 1.03 |
| seg 0, live2 groups | 29.1 / 43.1 ms | 0.25 % / 1.03 % | 24.4 / 3.6 | 0.98 / 1.02 |
| seg 1, db55faa | 25.0 / 40.6 ms | 0.95 % / 0.77 % | 208.5 / 6.8 | 0.98 / 1.03 |
| seg 1, live2 groups | 25.1 / 44.7 ms | 0.23 % / 0.42 % | 21.4 / 6.3 | 0.98 / 1.02 |
| seg 2, db55faa | 25.0 / 40.3 ms | 0.74 % / 1.36 % | 170.8 / 6.6 | 0.98 / 1.03 |
| seg 2, live2 groups | 25.0 / 42.4 ms | 0.20 % / 0.93 % | 24.4 / 4.6 | 0.98 / 1.02 |

The 30 Hz rows are race1's crash moments (takedown and crash cameras: the game ticks at 30 a second there; 6.5 / 17 /
10 s of display per segment); no windowed Crash-mode session has a frame log yet (`lt1..3`, `test`, `race1` are races).
At a 144 Hz display: db55faa 233 / 129 / 104 hold events per minute (each a repeated 6.9 ms display frame), live2
16.7 / 14.8 / 17.6. **Latency is as intended** (db55faa: p50 = 25.0 ms = 1.5 ticks at 60 Hz in every segment) and the
clock never steps backwards (0 in all runs), but in 2-tick frames it holds still several times a second: a fine judder.
The holds of >= 8 ms that remain (3-6 per minute) are gaps in the arrivals themselves (the game's own stalls, up to
~100 ms), which a viewer cannot hide without more delay. Output: `b3clocksim <play-session folder>/race1 --from 4971 --to
23811 --from 27477 --to 39680 --from 39806 --to 53601 [--fps 144]`.

**Change (live2 groups rule, default; `--clock-db55faa` keeps the old one).** Arrivals less than 4 ms apart form a group
(one rendered frame of N ticks); the rate is measured from the last 5 group starts (db55faa's fixed 8-tick window
aliased over pairs; 17 starts adapted too slowly into crash cameras: 30 Hz holds 1.62 / 1.34 / 1.89 %, 9 starts 1.08 / 0.78 / 1.36 %); the position extrapolates up to N ticks past the newest arrival and stays N - 1 + delay ticks
behind it, so the target is continuous across a group's arrival. Cost: + 1 tick of latency while the game runs 2-tick
frames (seg 0 p50 25.0 -> 29.1 ms); 1-tick frames are unchanged. The 30 Hz ticks (crash replays) are single-tick groups,
treated as before. `--bench` prints the clock statistics per tick-rate class (age, newest-tick share, backwards steps,
speed percentiles).

### 20.3 The pop-in: the river's reflection geometry drawn unmirrored [C]

Seen in the b3live window (the last run of race1): one small section of the map did not draw until the car had gone
over it, then appeared at once, as if something across the road blocked the view; on the bridge over the river, at
the first intersection after the green bridge. That is
EU/C1_V1's green bridge (camera units 36-37) and the junction right after it (units 0-1); the last run (segment 2,
polls 39806-53601) crosses it at polls 40399-41332, 44597-45547 and 48946-50105 (camera unit from the capture).

**Cause.** `CB3StreamedTrack::RenderBeforeVehicles` (nonmatching draft, lines 330-341) ends with: when D_00665F1D is set
and the camera unit's high block (+0x1C4, copied to the scratchpad) has +0x6E >= 0, `CB3StaticTrack::RenderWater(level =
block +0x70, STATIC +0x2C object [+0x6E], +0x6F >= 0 ? &STATIC +0x30 object [+0x6F] : null)`. `RenderWater` 0x21D550
(matched) draws the +0x2C object as plain static objects, and each frustum-visible mesh of the +0x30 object with its
material type forced to **7** and the level in `D_01E32ADC` (track renderer +0xCC). `ReallySetMaterial` 0x26B2F0
(nonmatching draft) runs VU1 program **0x47E300** for type 7 with renderer +0xCC in VU qword 1009.y, and that program
at 0x0055-0x005C [C asm, `vudis.py`] rebuilds the view matrix as y column' = -y column and translation' = translation +
2 level x y column: every vertex is drawn at (x, 2 level - y, z), **mirrored about the water plane** (E-size name
`G3_Gen_ADCReflect` [L]). The +0x30 objects are the river's reflections: copies of the banks, bridge and buildings, drawn
upside down under the translucent water. b3asset gave both tables the same part name and b3replay drew **every water
object of the route all the time, unmirrored** (§12.5: "Water surfaces are all drawn"): the reflection copy of the
bridge end stood at road height, a grey band across the road and a pale slab over the left lanes at the bridge exit,
and big flat green panels along the bridge's sides; the junction appeared once the camera had passed the band.

**Evidence.** At poll 49820 (the last frame before the bridge exit, 2560 x 720 = the session's 32:9 aspect, culling off as
the window draws): leaving out each slot unit (31..37, 0..5) and each backdrop (`--skip-units`, `--skip-backdrops`)
changes 0 % of the band's rows except unit 37 (the road under it); leaving out the +0x30 object changes 17.64 % of the
frame, the +0x2C object 0.00 % (`--skip-water=30 / =2c`). C1_V1's units 0, 1 and 25-37 select +0x6E = +0x6F = 0, level
9.0; units 2-24 none [C STREAMED.DAT high blocks; the bytes equal the capture's in-memory block in every tick where
+0x1C4 == +0x1C8 (12,869 of 13,796 in segment 2; +0x1C8 runs 8 polls ahead of +0x1C4)]; D_00665F1D = 1 throughout.
The scans that ruled the rest out (new `b3replay --pop-scan` modes: every race tick drawn twice from its own camera,
320 x 224, share of pixels changing > 32 levels) found no other candidate: world-state changes between ticks (slots /
LOD / backdrops / barriers / props / crash objects) <= 0.21 % apart from coronas switching (<= 4.94 %); the vehicles the
window draws but the game does not <= 0.04 %; units the game culls by its visibility bits <= 0.20 %; LOD vs high meshes
of the LOD slots (segment 0) <= 0.16 %. No unit is ever in two slots and the camera unit always has a high slot (0 of
44,841 track ticks).

**Fix.** b3asset names the +0x30 meshes `water+30 <g>`; `View` reads each unit's high block +0x6E / +0x6F / +0x70 from
STREAMED.DAT at load, and per frame draws only the camera unit's +0x2C object and its +0x30 object mirrored about the
level (model matrix diag(1, -1, 1) + 2 level y, no back-face culling), nothing when the capture's D_00665F1D is 0
(older captures without it: drawn). The capture is unchanged (D_00665F1D is in the 0x665F10 block crash2 already reads).

| race1, poll (32:9, 2560 x 720) | 49780 | 49800 | 49820 | 49840 |
|---|---|---|---|---|
| pixels changed by the fix | 27.6 % | 29.0 % | 34.9 % | 13.7 % |

race2 (US/C3_V1, every unit selects a reflection) at 640 x 448: 0.6-2.5 % of the pixels change at polls 2500-4300
(distant reflection pieces). 27 of the 37 route variants have +0x30 objects (all but EU/C3, US/C2, US/M1, US/P2).
Pictures: `%B3_WIN%\live2\pictures\water-race1-bridge-before-after.png` (49780-49840, before |
after) and `water-race1-p49820-skip.png` (with water, without, water alone). Not modelled yet: the type-7 program's own
shading (its colours / alpha path beyond the mirror; drawn with the meshes' track material) and the water surface's
look (§11.7); b3view still draws every water object unmirrored (it has no camera unit).

### 20.5 Long sessions in b3replay: memory, and the tools added

- **Memory** [C `/usr/bin/time -v`]: `b3replay` decodes and keeps every tick of a capture; race1 (53,760 ticks, 460.8 MB)
  took 7.9 GB resident (~150 KB per decoded tick) and two such processes pushed WSL to 2 GB free (08:30). New `--polls A:B`
  decodes only those polls (the runs before them still build the image): 2,000 polls 0.73 GB, a whole 12,204-tick
  segment 1.85 GB; poll 6006 rendered from `--polls 5000:7000` is pixel-identical to the full load (0 of 286,720 pixels).
  The live viewer keeps 20 s of ticks (`kKeepTicks`) and is not affected. The offline window and `--info` on an
  18-minute session still need the full load (or one call per segment).
- `--offscreen <dir> --sweep A:B:S [--no-cull]`: one process renders a series (`native_<poll>.png`); `--no-cull` draws
  what the live window draws.
- `--pop-scan <dir> [--cull-scan | --vis-scan | --lod-scan]` (§20.3); `--skip-units a,b`, `--skip-backdrops a,b`, `--skip-water[=2c|=30]` (diagnostics).
- `--info`: the paint census (§20.1) and a track-slot check (units in two slots, camera unit without a high slot).
- `--bench` (live, offscreen): display-clock statistics; `--clock-db55faa`: the previous clock (§20.2).
- `tools/render/b3clocksim.cpp` (`build/b3clocksim`, in `make render`): the clock on a session's recorded arrivals.

## 21. Water, the sun corona and lens flare, the boost radial blur (water1, 2026-11-04)

Branch `water1`. Three post-live2 gaps of the native renderer (b3replay, the live viewer, b3view): the water's own
shading, the sun's corona / lens flare with its occlusion test, and the boost radial blur. Decomp citations: HEAD
b21a8ccc3e (read only). Tags as in the rest of this document. No capture change: everything below reads captured
regions that exist since render4 / crash2 / corona1, or the game files.

### 21.1 Water: what RenderWater draws [C unless tagged]

- **Who.** `CB3StreamedTrack::RenderBeforeVehicles` 0x21C780 ends with `CB3StaticTrack::RenderWater` 0x21D550 (matched)
  for the camera unit: the STATIC +0x30 object [+0x6F] mesh by mesh with the material type forced to 7, then the STATIC
  +0x2C object [+0x6E] as ordinary static objects (live2 §20.3). So the **reflection is drawn first and the surface over
  it**, both **before the vehicles and before the full-screen specular** (`CB3World::Render` 0x276300: track before
  vehicles, cars, track after vehicles, `RenderFullScreenSpecular`, track after specular).
- **Type 7 = the type-0 program without culling, plus the mirror.** `ReallySetMaterial` 0x26B2F0 (nonmatching draft)
  runs VU1 0x47E300 with qw 1009.y = the level and the material's own texture, TEST_1 (+0x20), ALPHA_1 (+0x26/+0x27)
  and UV-scroll word (1005); `a` / `b` (1007/1008) are not set for type 7. Disassembled with `tools/render/vudis.py` and
  compared with the other track programs: **0x47E300 is 0x4745C0 (type 0 / 6, flag 0x20: no back-face test)
  instruction for instruction, plus 8 pairs at 0x0055-0x005C (the mirror of the view matrix's y column about the level)
  and 8 pairs at the exit restoring 1011/1012/1014-1017** (`diff` of the listings with branch targets masked: nothing
  else). So the reflection's colour, alpha, UV scroll and fog are the type-0 model's; only the mirror and no culling
  differ.
- **What the files hold** (census of all 37 routes, scratch tool over `read_track` + the material table): every +0x30
  mesh is **type 0** (flags 0x0/0x1/0x3/0x8/0xA/0x20; opaque ALPHA 0x64 FIX 0x80 with vertex alpha 4 and TEST GREATER 0
  on EU/C1, or blended: 0x44 with vertex alpha 128 / 64, or FIX 0x26 / 0x40); every +0x2C mesh is **type 6** (the type-0
  program, trilinear filter) **blended by As** (ALPHA 0x44, TEST 0x50003, vertex alpha 64-128), and **animated** (cycling
  `water` frames or UV scroll `ATB_River`, `Water`, `WC_Water`: anim1 already advances them). Test
  `real_water_materials_c1_v1`.
- **The full-screen specular does not shine on the water.** Both objects are drawn before the specular
  pass, so if the GS wrote the surface's As into the frame alpha the pass would add `S x As / 128` (EU/C4 S = 0.65 x 127.5
  = 83 GS units: up to +165 display levels). A first version did that (`pre_specular`, folded into the item) and turned the
  rivers white. So the frame alpha under the water is not the surface's As [L: the track's FRAME / FBA state for blended
  materials is not read yet] and b3gfx's order (blended items after its specular pass) gives the game's picture. The
  sheen version is removed; b3replay's water is pixel-identical to main's again (roadrage1 11506, race1 bridge view: 0
  pixels).
- **b3view** draws water the same way (`b3view::WaterTable`, shared with b3replay): the +0x2C / +0x30 objects of the
  camera's unit, the reflection mirrored; b3view has no streaming state, so the unit is the one whose centre (unit block
  +0x10) is nearest the camera [G]; a region view (merged routes) draws the surfaces and no reflections. Before, b3view
  drew every water object of the route unmirrored (the live2 pop-in's green panels).
- **b3view bug fixed on the way** [C code]: `add_enviro` prepends the sky items, but `animate()` indexed `scene.items` by
  draw number, so in b3view (not b3replay, whose sky is a scene of its own) each animated material's texture and scroll
  went to the item three places earlier (a sky layer or a neighbouring draw). `Scene::item_base` now offsets it.

Pictures: `%B3_WIN%\water1\pictures\water-gs-roadrage1-11500-vs-native-11506-main-sheen.png` (GS,
main = water1, the rejected sheen) and its crop `water-gs-crop-gs-main-sheen.png`; `b3view-c1v1-water-main-vs-water1-and-c4v1-sun.png`
(b3view at the green bridge: main's unmirrored reflection panels vs the mirrored river; the sun in b3view);
`REJECTED-sheen-*.png` show the removed version.

### 21.2 The sun corona and lens flare [C callers matched, L drafts of the callees; every constant read from the asm]

`CB3World::Render` calls, after the 3D, the post-fog effects and the 2D race position, `SunOcclusionTest` 0x2BBFA0, then
`CB3Bloom::Render`, then `RenderSunCorona` 0x2BBF50 (both matched; they return unless env +0x14C and +0x13C).

- **SetupCamera 0x24D110** (nonmatching): the point camera (record +0xD0) + 50 x unit(to-sun) through the camera record's
  +0xE0 matrix; **behind** (no corona) when its z <= 0.01; screen position = (x / z, y / z) x the record's size
  (+0x218/+0x21C = 640 x 448). Colour `D_01D9B970` = sun colour (ENVIRO +0x70) x its w (light entry +0x30 = ENVIRO +0x20,
  1.0) x 128. Corona radius = ENVIRO +0x90 x graphics manager +0 (**640** in every captured tick [C capture]); y radius
  x `mrAspectRatio` = record +0x200 x +0x20C x 448 / 640 (**0.9333 = 448 / 480** in race: round on the 4:3 display [C
  capture]).
- **OcclusionTest 0x24C7E0**: no corona unless the position lies within 6.5 px of the screen. The box of +-7 px (0x70 in
  1/16 px) around it, clamped to the screen, gives `fade = clamped area / 256 / 196` on the colour (1 when whole). Then
  the GS: alpha-only sprites (TEST 0x51001 = alpha NEVER with AFAIL FB_ONLY, ZTST GEQUAL; ALPHA 0x6A; FRAME unmasked) set
  the box's frame alpha to 0, then **0x40 where Z <= 1, 0x80 where Z == 0** (sprites at Z = 1 and Z = 0): 0x80 where no
  Z-writing draw covered the pixel, i.e. the sky. Three bilinear copies reduce the box to 8 x 8, 4 x 4, 2 x 2 px at the
  frame's origin (alpha only).
- **RenderCorona 0x24BF60**: one sprite with **FRAME = the corona texture's CBP** (FBW 1, FBMSK 0x00FFFFFF) copies that
  reduced alpha into **every entry of the corona texture's CLUT**; then TEX0 = the corona texture reloads the CLUT, and the
  corona and 12 secondaries draw with **ALPHA 0x48 = Cs x As + Cd** (additive), As = the CLUT alpha = the sky share. The
  corona: radius R (x aspect tall) around the sun, the texture's upper half ("suncorona", 128 x 256 PSMT8: corona 128 x
  128 over four 64 x 64 flare shapes). The secondaries: table `D_004BB610` (12 x 0x18: position along the line from the
  screen centre through the sun, size x R, RGB scale, quadrant) [C ELF], enabled by `D_004E1978` (Revenge's `static bool
  lbSecondaries`, 1 in the ELF), colour `e.rgb x ((corona colour x random) / 0xFF00)`, random = 0xFF00 - (xor of the
  three low bytes of a float sequence seeded by the world clock `D_00522680`) << 6, i.e. x [0.75, 1] per secondary per
  frame [C].
- **Routes without a sun**: Prepare 0x2BCA20 skips the sun renderer for four track IDs; decoded as packed route names
  they are US/P2_V1/V2 and EU/M2_V1/V2 [L: the decoding reads Prepare's fallback ID as US/C5_V1, a real route] (race1's
  first segment is M2_V2: no corona there).

**Native** (`b3asset::sun_flare` = the game's integer arithmetic in 1/16 px and the flicker with the EE FPU's truncating
float ops; `b3view::Sun` uploads the corona texture and reads the table from `<game>/SLUS_210.50`; `b3gfx`):
- The occlusion is measured **on the GPU in the same frame**: before the scene pass a 32 x 32 RGBA8 target is cleared to 1
  and every Z-writing draw of the frame (opaque and alpha-tested track with the shader's TEST_1 compare, cars, the
  post-fog B3Im3d objects; not the sky, blended track, effects or 2D) is drawn into it through a projection that maps the
  box onto the target, writing 0; its mip chain's 1 x 1 level is the sky share. The sprites (`sun.vert` / `sun.frag`:
  13 quads from a uniform array) draw after the post-fog stage, before the 2D stage: MODULATE (Ct x Cv / 128, clamped)
  times that share, added to RGB, the frame alpha kept. The GS chain's 0x40 step (Z == 1) and its bilinear reduction are
  read as "uncovered share" [L].
- The game's pixel grid over a window of aspect A: 448 rows, 480 x A columns (hor+: a wider window shows more screen,
  the screen test and the flare line through the centre follow it); the PS2-shaped 640 x 448 offscreen image uses A = 4/3.
- b3replay: inputs per tick from the capture (`SunCam`: graphics manager +0 and the camera record's +0xD0..+0x228; the
  world clock), **`--no-sun` / O** and **`--no-sun-test` / B** (the corona without the test); `--info` lists per segment the route's sun, the inputs and the polls where SetupCamera's own math on
  the captured record puts the corona on screen (roadrage1: 1,309 of 9,552 ticks, 4632..4696, 7803..8120, 8273..8649, ...;
  race2: none). b3view: graphics manager +0 = 640 and aspect 448 / 480 as captured, the flicker on its wall clock, **G**
  sun, **J** its test, `--no-sun`, `--no-sun-test`.
- Tests: `sun_flare_game_arithmetic` (box, fade, edge clamp, radii, UVs, secondaries' placement and colour range, the
  route list), `real_sun_flare_table` (the ELF table).

Pictures (`sun-roadrage1-4660-8400-9100-11270-test-vs-notest.png`, pairs with / without the test): at 4660 and 11270 the
sun is in open sky (the corona and the flare rings along the centre line); at 8400 a tree covers it and the corona is gone
with the test, while without it the glare shines through the leaves; at 9100 the sun is in a gap of the trees.

### 21.3 The boost radial blur [C GS registers; L drafts]

`CB3World::Render`: `if (CB3DirectionalBlurBase::UpdateCamera(+0x60, idx)) CB3DirectionalBlur::Render ... else if (the
view's car +0x18EA == 0) CB3RadialBlur::Render 0x2FA9A0`. The parameters: `CB3OnePlayerStage::Render` 0x134A20 calls
`func_0030A9D0(&D_00666090 = graphics manager +0x1D0, viewport, car)`, which (camera manager +0xB80 & 0x3C) runs
`CB3BoostEffectParams::GetRadialBlurParams` 0x30ABA0: centre (0.5 + the clamped lateral term, 0.5), mask radius
lerp(1, 0.45, boost) (+ 4 |lateral|) / viewport +0x70 (and x 0.75 x +0x74 for y), zoom lerp(0.995, 0.985, boost), colour 1,
amount = boost; else centre 0.5, radius 1, zoom D_004E1028, amount 0. **That block is in every capture** (graphics manager
[0, 0x3C0)): roadrage1 has amount > 0 in 5,346 of 9,552 race ticks (e.g. poll 3844: centre (0.5, 0.5), radius (0.338,
0.450), zoom 0.985, amount 1.0) [C `--info`].

The GS chain (`Render` 0x2FA9A0 with `SetRenderstates` 0x2FA900, `Downsample` 0x2FA770, `SetAlpha` 0x2FA490,
`BlurSetRenderstates` 0x2FA3C0, `BlurOut` 0x2FA130 x 3, `CopyToScreen` 0x2F9F50) on a half-size buffer H:
1. Downsample: the frame into H, TEX0 MODULATE with RGBAQ 0x88 (**x 136/128**, clamped), TEX1 bilinear, no blend.
2. SetAlpha: GLOBAL.TXD "radialblurmask" (32 x 32, alpha 0 at a corner rising to 127 at the far corner [C decoded]) into
   H's alpha only (FBMSK 0xFFFFFF), u = |x - centre| / radius x, v = (y - centre) / radius y below the centre and 0 above
   it (mirrored too when +0x30, the car's 0x200 / 0x2000 state).
3. BlurOut x 3: H over itself (RGB only), ALPHA 0x64 FIX 0x40 = 50 %, each pixel p sampling c + z (p - c), z = the zoom,
   then z^2, z^4 (strips ordered from the edges inwards so each reads pixels nearer the centre first) [L].
4. CopyToScreen: H over the frame by As = H's alpha x amount (ALPHA 0x44, RGBAQ colour x 128, amount x 128), RGB only.

Native: `shaders/blur.frag` (modes downsample + mask, zoom pass, composite) on two half-size targets (ping-pong instead of
in place [G equal]); b3gfx ends the scene pass after the sun corona (MSAA kept with RESOLVE_AND_STORE), runs the chain and
resumes the pass for the composite and the 2D stage. **Off by default** (`--blur` / N): the frame takes the radial blur
only when UpdateCamera 0x4256C0 returns 0, and its inputs are not captured (the crash pulse: the crash object +0x3534 and
`D_005179C0[idx]` & 0x2000; the motion branch: `D_005179C0[idx]` & 0x400, or boost params +0x52 and func_0030A990 = k x
(1 - (+0x20 - 1)^2)(1 - (+0x1C - 1)^2), i.e. car +0x11DE, +0x11A8, +0x11AC; the capture holds +0x118C..+0x11AB). In
normal racing the directional blur (not native yet) takes the frame [L].

## 22. The front end: menus, loading screens, movies, the car pages' car (menu1, 2026-11-04)

Branch `menu1`. The goal: the whole game in the native renderer, not just the races. Decomp citations: HEAD b21a8ccc3e
(read only); names from its `config/symbol_addrs_b3names.txt` (E map rows tagged as such). Pictures (game-derived, not
committed): `%B3_WIN%\menu1\pictures\`.

### 22.1 What the front end is made of [C unless tagged]

- **2D: the same Gt2d layer as the HUD.** The menu pages, the loading screens ("HAVE YOU DRIVEN...", "LOADING"), the
  autosave and results pages all go through the 22 Gt2dRenderer functions (§17.1), so hud1's capture already recorded
  them: race2 has a HUD frame at every one of its 5,181 polls, the front end included (`--info`: 2D space 640 x 448 in
  every frame). Their objects' callbacks are among hud2's hooked ones (`0x1D5FB0`, `0x1D66A0`, `0x3D7B40`, `0x381AB0`,
  ...); the menu session (13,758 polls, no race) has **0 objects with unhooked callbacks** in its 332,335 object calls
  [C hook report]. Draw order (main loop func_00132600 0x132FA4 [C asm read]): the game mode's Render
  (`CB3FrontEnd::Render` 0x1312D0, E map), then `OpenViewport(0)`, `CB3MenuPageManager::Render` 0x30D6E0 (E map), the
  present blit; so the front end's 3D comes first and its 2D over it.
- **Movies are Gt2d objects.** `CGt2dObjectVideoImage::_Render` 0x326960 [C name] draws the decoder's current picture
  as 64 x 64 tiles: per tile Suspend, `CGtVideoImage::SetTileTexture` 0x325BD0 (an IMAGE transfer of four 16 x 64 strips
  straight from RAM into VRAM, in RenderWare's packet, not Gt2d's buffer), Resume, `RenderSpritesTex` [L draft
  src/nonmatching]. So the tiles reached hud1's reader as textured sprites without a texture, drawn as flat grey (race2's
  title and main menu showed a grey panel where the game plays `_FEMAIN.M2V`). The picture is in RAM:
  `CGtVideoDecoder::GetImage` 0x324F10 returns decoder + 0x18 x (decoder +0x34) [C matched], a slot {picture, 64-px tiles
  across / down, macroblock columns / rows, VRAM block} that `Prepare` 0x3254F0 fills [L draft]; the picture is RGBA32 in
  16-pixel-wide strips the full height (strip c at picture + c x rows x 1024, 64 bytes a row) [L SetTileTexture's
  address arithmetic]; `Update` 0x325030 decodes into slot +0x30 and shows +0x34, +0x38 counts pictures [L draft].
- **The movie files** [C headers]: MPEG-2 video elementary streams (`.M2V`, sequence header + 0x1B5 extension),
  audio separately in RenderWare stream files (`.RWS`). `TRACKS/_FEMAIN.M2V` 640 x 352, 29.97 fps, 21.7 MB (the menu
  loop, `_FEMAIN.RWS` 1.6 MB); `OVID/*.M2V` 640 x 480 or 640 x 352 (intros `TITLES30`, `CRI_RW30`, `ENGLIS30`, unlock
  movies `MUUNLOCK` ..., crash headers `CRSHHD01-10`, signature takedowns `SIGTKD01-20`, `CREDITS`); the per-route
  previews `TRACKS/<route>*.M2V` 192 x 144.
- **The car pages' car** (garage, car select). `CB3FrontEnd::Render` calls `CB3World::RenderFrontEnd` 0x275EF0 (E map
  [high]; $a1 the car index, when the menu page is one of four [C asm read]). It draws the static track's backdrops
  (the showroom), then the race car `*(gWorld + 0x12AD28 + 4 index)` twice through `CB3RaceCar::Draw` 0x151A40 [C
  matched], the race renderer: once as a floor reflection (up row and position y negated, another lighting set), once
  for real with the lighting it leaves at +0x5C; then `CB3ShadowMgr::RenderFrontEndShadow` 0x303750. The camera is
  viewport 1 (graphics manager +0x90): the car view component's CGtCamera orbits the car at 20 degrees a second and is
  attached there (`AttachCameraToViewport` 0x19EA70) [L drafts func_00429BE0]; the car itself does not turn.
  `CB3CarViewerGameMode::Render` 0x24D520 is another game mode (a debug viewer with arrow line lists), not this [G].
- **The World Tour globe**: `CB3Globe2dObject::_Render` 0x3B2EE0 (an object callback) draws between Gt2d Suspend /
  Resume through B3Im3d's own VIF packets: invisible depth polygons (`DrawInvisiblePolys` 0x3B2830), an anti-aliased
  wireframe, the textured sphere as 65 strips (u scrolled by the longitude), a highlight patch, halo quads
  (`DrawGlobeHalo` 0x3B17F0); geometry generated in code (gmpStaticGlobeData 0x4E2EC8: 33 x 65 grids for the sphere,
  a cylinder and a plane, morphed), viewport 0 [L read of the drafts]. None of it is in Gt2d's buffer.

### 22.2 Capture (no version change; every addition detected by its presence)

`src/gameview/hud_capture.cpp`, in its own blocks; events appended to hud1's index:

| Event | Hook (entry) | Payload | Switch |
|---|---|---|---|
| VIDEO (12) | `CGt2dObjectVideoImage::_Render` 0x326960 (hooked since hud2) | object, decoder, shown slot index, the slot's 6 words, decoder +0x30, +0x38 .. +0x48, the picture's host slot, its bytes, data +0x00 / +0x04 (picture size in tiles), +0x14 | always |
| movie picture | the same entry: the picture copied at that moment through a const view of RAM (no side effects), published at the next poll in a host-only region **0x40A00000 + k x 0x140000** (k < 4) | RGBA32 strips | `B3PORT_SCENE_MOVIE=N`: every Nth poll; default every poll with the live feed, off for a capture file alone (a picture changes every 2 polls: ~27 MB/s) |
| FECAR (13) | **new entry: `CB3World::RenderFrontEnd` 0x275EF0** | car index ($a1), the car, viewport 1's RwCamera | `B3PORT_SCENE_FRONTEND=0` leaves the entry out |
| front-end car | at the next poll: `b3scene_grab_vehicle(car, car + 0x80)` (the scene capture's race-car grab, now exported) + [0x18E0, 0x19A0) | | with FECAR |
| GLOBE (14) | `CB3Globe2dObject::_Render` 0x3B2EE0 (hooked since hud2) | data block (position, latitude / longitude, shape, alpha, reveal, highlight), object space, krB3GlobeSpherePosition 0x4CA140, the 5 globe rasters (D_01EA4608, ..460C, ..4618, ..4614, ..461C; grabbed like the HUD's), viewport 0's matrix, +0x48..+0x54, +0x84, +0x88 | always (not drawn yet) |

**Hook addresses for the runtime-upgrade lane's re-check:** new entry 0x275EF0; extended entries 0x326960 and 0x3B2EE0; host region
0x40A00000..0x40F00000. Nothing else changed in the hook list (48 entries with the new one).

### 22.3 Reader and renderer

- **Front-end segments** (`Capture::fe_segments`, `frontend_segments`): the runs of ticks outside the race segments
  that hold a HUD frame, at least 30 ticks. `b3replay --info` lists them with what the ticks hold (camera, cars, movies,
  pictures); `--fe-segment N` picks one, `--poll P` picks the segment (race or front end) holding P, a capture without a
  race (the menu session) starts in its longest front-end segment; **Tab** cycles race and front-end segments in capture
  order.
- **A front-end frame** (`View::frontend_frame`): the frame cleared black, the game's present model (2D into the
  half-intensity frame, doubled on present); if the tick has the front end's car (FECAR), the car through the race-car
  path with the tick's camera (viewport 1; Hor+ at the output aspect), interpolated between ticks within the car's run;
  then hud_list's 2D (sharp text, the wide layout's edge rules: the menu panels, bands and the grid reach the window
  edges at 16:9 and 21:9; components' anchors do not apply, the menus have none).
- **Movies** (`HudDraw::video`, `HudVideo`, `HudPicture`): a movie object's textured tiles get u, v over the whole
  picture from the vertex's place in the object's space (the object maps onto the picture's size in tiles); the
  captured picture is un-stripped to RGBA rows and drawn from a dynamic texture per guest picture buffer (new b3gfx
  `update_texture`); tiles without a captured picture (older captures, capture files without `B3PORT_SCENE_MOVIE`, M /
  `--no-movies`) get a **placeholder**: dark slate with diagonal stripes, plainly not game art.
- **Live viewer**: outside a race on screen (front end, loading screens, a race's first half second before its route
  is matched) it draws the front-end frame of the tick the display clock is at (same delay as races) instead of the
  dark frame; the title says "(front end)". Key M as offline. `--offscreen-live --shots` and `--bench` cover front-end
  frames too.
- Not drawn yet: the globe (captured), the showroom's backdrops and sky behind the car, the car's floor reflection and
  front-end shadow, Gt2d's 64-pixel offscreen passes (FBP 362, as in races: §17.1), TCC 0 (the garage's `Camchange*`
  static noise is a TCC-0 texture drawn at vertex alpha: we draw it with the texture's alpha, §22.5).

### 22.4 Checks

**Exactness** (build sha256 65c118c2..., the menu session with movie pictures every 10th poll, then the same with
the capture off) [C]:
| Session | Capture on (48 entries: hud4's 47 + RenderFrontEnd; pictures every 10th poll) | Capture off (same exe) | GS stream on vs off |
|---|---|---|---|
| menu (13,758 polls, front end only) | 13,758 of 13,758 polls exact, 0 other EE cycle / step counts; 13,758 HUD frames, 892,108 batches, 0 missed flushes, 0 objects with unhooked callbacks; 11,200 movie renders, 1,121 pictures grabbed; 1,958 front-end car renders; 0 globes (this session never opens World Tour); 1,057 MB | 13,758 of 13,758, 0 other | p0 / p1 / p2 identical in all 27,785 frames; 86 frames in 7 short runs (stream frames 20989..22087) differ in `full` / `hdr` / `masked` only (2 with `bytes` too): the front-end host-record class accepted since §12.2 / §17.11 |

**Live = offline** (Linux: `b3livefeed` of the menu capture at 60 polls a second into a ring, `b3replay --live
--offscreen-live --shots 900,3300+0.5,12300 --verify`): 13,758 ticks identical to the file reader's, 0 resyncs; the
three front-end shots (main menu with the movie picture, the garage car at +0.5, the unlock movie) **0 of 286,720
pixels differ** from `b3replay --offscreen` of the file. race2 likewise (polls 700, 1900, 2300, 4700, 5100: 0 pixels
differ; 5,180 ticks identical). Front-end frame cost (`--bench 1280x720`, WSL's Vulkan, slow): p50 13.5-15.8 ms against
106 ms for race2's race frames on the same device (front-end frames are 2D plus at most one car).

Tests: `hud_movie_tiles_and_picture` (a movie object's VIDEO event, u / v over the picture, the strip layout decoded),
`frontend_segments_between_races` (test_hud).

### 22.5 Running it on Windows (after the merge and `tools/render/build_win.sh`)

```
:: the menu session (recapture below; 1 GB, so not in data/scene)
%B3_WIN%\b3view\b3replay.exe %B3_WIN%\perf\scene\menu-menu1.b3scene --game %B3_WIN%/perf/install/game
:: race2's front end (data/scene, captured before menu1: movie placeholder, no garage car)
%B3_WIN%\b3view\b3replay.exe C:\path\to\burnout3-native\data\scene\race2.b3scene --game %B3_WIN%/perf/install/game --fe-segment 0
```
Tab cycles race and front-end segments, M movie pictures / placeholder. This branch's viewer:
`%B3_WIN%\menu1\b3replay\b3replay.exe` (sha256 2316bbd5...). Not tried in a window (headless only). The live
window shows the front end too, the movies included (every picture is grabbed with the live feed).

### 22.6 Not done, and what each needs

(fe2, §24, drew the globe, the showroom, the floor reflection and the front-end shadow.)

- **The globe** (World Tour): captured (GLOBE events, its rasters) but not drawn; needs a session that opens World Tour
  and a native globe built from the state: the sphere / cylinder / plane grids and their morph, the wireframe, the
  textured strips with the longitude scroll, the highlight, the halo, under viewport 0 [G 1.5-2 d].
- **The showroom around the car**: `RenderFrontEnd`'s backdrops of the static track (which file the front end loads
  there is not identified), its sky and env map, the floor reflection pass (its lighting set is in func_00275EF0's
  constants) and `RenderFrontEndShadow` [G 1-1.5 d].
- **Movies without the guest's picture**: the capture copies the decoded picture from the decoder slot above. Without
  it the renderer needs its own: an MPEG-2 decoder (FFmpeg's libavcodec or libmpeg2) for the `.M2V` elementary streams, the RWS audio streams (the audio port), and the decoder object's timing
  (`Update`: frame period +0x40, time +0x3C, loop flag +0x4B) to pick pictures [G 2-3 d, a later lane]. Capture files
  carry pictures only with `B3PORT_SCENE_MOVIE` (27 MB/s at every poll; =10 gives 3 per second).
- **TCC 0**: the reader takes TCC from the TEX0 writes in Gt2d's buffer, which a texture-cache raster without a palette
  never gets (RenderWare's packet sets it), so the flag is not reliable for those draws; the garage's `Camchange*` static
  layer (vertex alpha 0x2A) is the visible case; not settled.
- Gt2d's 64-pixel offscreen passes (FBP 362; races too, §17.1) stay undrawn; split-screen front end not looked at.
- The picture copy's cost on the game thread with the live feed (every poll while a movie plays: a 880 KB view
  copy and the delta writer's block compare, which looks its page up per 16-byte block) was not measured [L: a few ms a
  poll]; if the game's window stutters in the menus with the live window on, `B3PORT_SCENE_MOVIE=2` (pictures change
  every 2nd poll) halves it, and caching the page in `grab` would cut the compare.

### 22.7 font1 (2026-11-04): the unbound-texture detector, a larger live ring

**Detector** (font1): `HudStats::unbound_texture` counts the textured triangles after a TEX0 that no SetTexture
announced. `b3replay --info` prints it ("textured triangles without a raster"), and the live viewer warns once. Counts:
0 on data/scene race2 / roadrage1. Any later host change that hides a SetTexture path shows at once.

**Live ring** (cuts the lap pressure; not the cause): `b3live::kDefaultCapacity` goes from 32 to 128 MB. With the live
feed the front end's movie pictures stream ~27 MB/s. Each changed picture goes out once, only its changed blocks, so
capping the grab at the movie's 29.97 fps would save nothing. 32 MB held ~1.2 s of menus, and races14's reader lapped
early in the menus; 128 MB holds ~4.7 s. `tests/test_livelap.cpp` plays data/scene/race2 into a real ring (4 MB, the
writer's rules). One reader is lapped on purpose after poll 200; a second opens at poll 1900, after the font was
grabbed. Both decode poll 2000's HUD frame (Select Car) as the file reader does: the same draws, every texture with the
same pixels, GlobalFont included.

## 23. The renderer inside the game process (inproc1, 2026-11-04)

Goal: one window, no ring, no second process: the native renderer runs inside the game process and reads the game
state at the hook points; later textures and fonts come from disc (b3asset), movies are decoded natively. Capture files (.b3scene) stay the test and regression format. Tags as above.

### 23.1 Design (M0)

**How it gets in: a DLL with a C ABI, `b3render.dll`** (`src/b3replay/inproc.cpp`, built by `tools/render/build_win.sh`
with MSYS2 GCC 16.2). The scene hook loads it with `LoadLibraryExW` only when `B3PORT_RENDER=<dll>` is set. The DLL
links SDL3 3.4.2 **statically** (MSYS2 `libSDL3.a`), which gives it its
own event queue, its own hints and no `SDL3.dll` name clash in the loader. It registers its own window class
(`SDL_RegisterApp("b3render", ..., its module)`). libstdc++, libgcc and winpthread are static too. The DLL imports only system DLLs (msvcrt, user32,
gdi32, ...) and exports only `b3r_open(args)`, `b3r_want_key()`, `b3r_submit(kind, body, n)` and `b3r_close(timeout)`
[C objdump]. Only bytes cross the ABI and the DLL copies them, so the two C++ runtimes never share an object. stdout
and stderr are the one shared msvcrt, so the renderer's lines land in the run's log.txt.

**Threads: the game never waits.** The hook stays on the game thread. Once per retail poll it builds the record it
already builds for the capture file and the ring (§12.1; `build_body`). `b3r_submit` copies it onto a lock-free
single-producer list (`include/b3live/inproc.h`: one malloc, one memcpy, a compare-exchange against a consumer that
only swaps the whole list out). Nothing blocks and nothing waits on the reader. The render thread (below-normal
priority) drains the list every frame and decodes the records with the offline reader's code (`LiveFeed`,
`src/b3replay/livefeed.cpp`, here with `inproc` set instead of a ring name). It draws with the view code of the offline
replay and the live viewer (`view.cpp`, `live.cpp`'s `run_live_offscreen` / `run_live_window`; `b3replay_main` with
`--inproc`). Unlike the ring, nothing is ever overwritten. Every record arrives in order, so the only keyframe is the
first one, and the lap-then-keyframe class of bugs (§22.7's font) cannot happen. A byte cap (512 MB queued) is only a
safety net: a record over it is dropped, and the producer's next record is a keyframe. The one wait is at the session's
end: `b3scene_finish` closes the feed and gives the renderer up to
`B3PORT_RENDER_WAIT_MS` (120 s) to finish its shots and summaries. The guest reads nothing new: the hook reads what it
read before, so pace and physics are unchanged. Proof: §23.2.

**The game view.** The renderer consumes only `Tick` (capture.h) and the model table. Today
`Tick = decode(sparse guest image)`, and the image is rebuilt from the records. Next (M3): capture.cpp's decoders become
generic over a guest-memory accessor, so the hook can decode straight from guest memory on the game thread and hand over
a ready `Tick`, with no shadow image, delta or copy. In the final port, native structs fill `Tick`. Records stay for
.b3scene files; `same_tick(record decode, direct decode)` per poll is the regression check.

**Window, input, sound.** The render thread creates its window with SDL video only: no joystick, gamepad or audio
subsystem, no raw keyboard (SDL3's default), no relative mouse. Window messages go to the thread that made the window.
Its keys drive only the viewer. The video driver is set with override priority (`offscreen` / `windows`), so a
`SDL_VIDEO_DRIVER=dummy` in the environment does not reach ours.

### 23.2 Proofs (M1; headless, navproof14)

Build 2452f385... (branch inproc1 with post1 and fe2 merged), DLL `$WIN/inproc1/b3render.dll` (09a9e870...);
navproof14 (5,517 polls, menus with the `_FEMain` movie to poll 1206, then a race on EU/M1_V1) [C run logs, `build/inproc1/res3`]:

| Run | Polls exact (same EE cycles) | GS stream | Other |
|---|---|---|---|
| in-process on + capture file + 7 shots + 1280x720 bench (offscreen) | 5,517 of 5,517, 0 other cycles | IDENTICAL to the off run, 6,682 frames, every column | feed: 5,517 records, 1 keyframe, 0 dropped |
| off (capture off, same exe) | 5,517 of 5,517, 0 other cycles | (reference) | |
| in-process only (no capture file) | 5,517 of 5,517 | IDENTICAL to the recording's | hook times below |

**Pixels:** the 7 shots (poll 1000 front end; 2000, 3000+0.5, 4000, 4500, 5000+0.5, 5400 race) against
`b3replay --offscreen` on the same run's capture: **0 pixels differ in each** (640x448). Since post1 the bloom keeps its
picture between frames and `--offscreen` warms it over the 24 ticks before (`--bloom-warmup`). Live shots now do the
same (`LiveOptions::bloom_warmup`); without it, poll 3000+0.5 differs in 3.2 % of its pixels (max 38) [C pngdiff].

**The game thread's cost** (`B3PORT_SCENE_TIMES`: per poll, host steady clock, written at the end; in-process only, no
capture file), µs median / p99 / max:

| Stretch | Whole hook (guest reads, record, hand-over) | Hand-over (body + `b3r_submit`) | Record |
|---|---|---|---|
| menus with movie, polls 1-1206 | 444 / 1,182 / 3,311 (was 1,437 / 2,423 / 3,757) | 56 / 269 / 1,078 | 591 KB median |
| race, polls 1207-5517 | 197 / 366 / 2,193 (was 907 / 1,258 / 3,764) | 3 / 36 / 400 | 6 KB median |

"Was" is the same hook before inproc1 changed its reads; the capture file and the live ring paid the same.
Almost all of it was one `read_u8` call through a function pointer per guest byte. The hook now reads through a
const view of main RAM. The view is checked once against `read_u8` over RAM samples at the
first poll, and the hook does one shadow-page lookup per 4 KB page instead of per 16-byte block. The capture written
this way is **byte-identical** to the per-byte reader's (632 MB, `cmp`; before post1's block was added). The hand-over
itself is a few µs in a race; in the menus it scales with the movie pictures (~0.6 MB per poll, two copies: body, then
the queue). The pacer's per-vblank slack (pace1's frames.csv) was too noisy to separate 0.2 ms with other lanes'
runs on the machine (the off run had 53 menu intervals over 20 ms, the on run 3), so the per-poll times are the measure.
M3's decode-on-the-game-thread has to stay within these numbers.

**Render thread** (in-process bench, 1280x720, MSAA 4x, every display frame drawn and read back, its own uncapped
loop at below-normal priority): race 205 fps, frame p50 / p95 / p99 4.33 / 8.10 / 16.58 ms (CPU drain + frame + draw
list 1.35 / 4.63 / 13.36); front end 936 fps. An earlier run on a loaded machine (load average ~13) gave 12.5 fps in
the race with the game still exact: the render thread yields, the game does not.

**Surprises:** none (headless). Two settings were needed: the video driver hint at override priority (a
`SDL_VIDEO_DRIVER=dummy` env would otherwise reach our SDL), and our own window class name.

### 23.4 Later (M3+, notes only)

- **Direct state instead of records.** capture.cpp's decoders read an `Image` through `u32`/`f32`/`bytes` accessors.
  Template them over a guest-memory reader (`Image` today, guest memory at the hook) and run them on the game
  thread at the poll, handing the render thread a finished `Tick` (std::move onto the same list). That drops the
  shadow image, the delta and the decode on the render thread. It costs decode time on the game thread (to be measured
  against the hook's current per-poll cost, §23.2), so the alternative is to keep copying the needed blocks (today's
  record) but skip the delta and the map lookups: a flat per-poll snapshot. Regression: per poll,
  `same_tick(decode(record), direct)` over the data/scene captures. In the final port, the producer fills `Tick` from
  native structs and nothing else changes.
- **Disc assets instead of captured textures.** Tracks, cars and their TXDs already come from disc (b3asset). The HUD
  rasters (fonts, icons, movie frames) still come from guest memory through the HUD capture: map each captured raster
  (name, size, CLUT) to its TXD entry in DATA/GLOBAL.TXD / FRONTEND.TXD / the per-mode TXDs, load those once, and send
  only the raster identity per draw.
- **Movies decoded natively.** The front-end movies are MPEG-2 (`_FEMain.m2v` etc., IPU on the PS2). Decode them in
  the renderer from the disc files (FFmpeg's avcodec or libmpeg2 would do),
  synchronised by the movie clock the HUD capture already reads, so the 27 MB/s of captured movie pictures (§22.7)
  drop out of the feed.
- **GS off for good** needs the globe, the showroom, split screen (the post passes are drawn since post1, §25), and
  the keyboard path (b3pad reading our SDL keyboard).

## 24. The front end's showroom and the World Tour globe (fe2, 2026-11-04)

Branch `fe2`. menu1 (§22) drew the car pages' car on black and captured the globe without drawing it. This lane draws
what `CB3World::RenderFrontEnd` puts around the car, and the globe. Decomp citations: HEAD b21a8ccc3e (read only). Names
from its `config/symbol_addrs_b3names.txt`, else placeholders. Pictures (game-derived, not committed):
`%B3_WIN%\fe2\pictures\`.

### 24.1 The showroom is a static track on disc: `TRACKS/US/C5_V1` [C]

- **Which file.** The front end keeps a static track loaded at `gWorld + 0x12A330` (0x64C990). The scene capture
  already reads its STATIC.DAT header every tick (the route fingerprint, §12.1). On every front-end tick of race2 the
  fingerprint is **size 753,664, 4 units**: `TRACKS/US/C5_V1/STATIC.DAT`. That folder holds only STATIC.DAT,
  ENVIRO.DAT and an empty STREAMED.DAT (b3asset's `read_track` already knew it as "a static-only route"). Its top view
  is a car park: floor, pillars, roof girders, cones, ducts, pipes, ceiling lights.
- **What `RenderFrontEnd` 0x275EF0 draws** [C matched `src/b3/_unclassified/func_00275EF0.cpp`]: the environment's
  backdrop texture, sky, sun blob and clouds; the sky and the sun into the env-map cameras 1..n;
  `RenderAllBackdrops(range 0, main, env)`, `(1, -, env)`, `(2, -, env)`; the car twice through `CB3RaceCar::Draw`;
  `RenderAllBackdrops(1, main, -)`, `RenderFullScreenSpecular`, `RenderAllBackdrops(2, main, -)`;
  `RenderFrontEndShadow`; the sun's occlusion test and corona.
- **`RenderAllBackdrops` 0x21D2E0** [C asm]: it walks STATIC +0x24 (the backdrop table) over a range taken from the
  static data header: range 0 = [0, s8 +0x14), 1 = [s8 +0x14, s8 +0x15), 2 = [s8 +0x15, s16 +0x1C). Its two bools pick
  camera 0 (main) and the env-map cameras (1 .. D_00667054). For C5_V1 that is [0, 6), [6, 7), [7, 9). Range 0 is the
  room (cones, roof, walls, ceiling, ducts, pipes, lights, decals). Range 1 is one untextured blended mesh. Range 2 is
  the armco, the hard shoulder and `CP_floor`, which is blended: the floor drawn over the reflected car.
- **The floor reflection** [C matched]: before its first `Draw`, `RenderFrontEnd` sets +0x5B = 1, +0x5C = 0.625 and the
  8 floats +0x60.. (lighting set A: -0.4957, -0.1863, -0.3, 0.29375, 0.1704, -0.1571, 0.2375, 0.13125), and negates the
  car's up row (+0x10) and position y (+0x30). Before the second `Draw` it sets set B (the same with the signs of
  +0x60, +0x68, +0x70, +0x78 flipped) and negates them back. +0x5C.. are the 9 SH coefficients
  `BeginRenderingSingleVehicle` copies (§12.6). The capture grabs set B (race2 poll 1897: 0.625 0.4957 -0.1863 0.3 ...,
  as the constants). The car stands at the origin, upright (main = identity, y 0.349), so the reflection is the mirror
  in y = 0.
- **`RenderFrontEndShadow` 0x303750** [L nonmatching C, constants C asm]: `RenderAmbientVehicleShadow` (§14.3) with the
  frame up (0, 1, 0), right = normalise(up x the car's at row), position **(0, 0.005, 0)** (not the car's), the box
  +0xE80 / +0xE90 and the wheel base (wheels 1 and n-2) unscaled, floats (1, 1, 0): alpha int((1 x 90 + 0) x 1) = 90.
- **The car's env map** [L draft `BuildCustomCameraData` 0x223130]: `BuildCustomCameraData(idx)` builds the env-map
  cameras around the car's box middle in world space (+0xE80 / +0xE90 through the car's matrix), looking at the main
  camera. `RenderFrontEnd` draws the sky, the sun and all three backdrop ranges into them, so the car reflects the
  garage. C5_V1's ENVIRO sky alone is a bright blue sky with clouds (`envmapclouds`): with it the car came out light
  and blue. The race path's env map is the sky with the streamed track's LOD blocks in front of it (car-shading.md §9:
  RenderBeforeVehicles draws them into env cameras 1..3; the "sky alone" read before 1.1 was wrong).

### 24.2 Renderer

- `View::Showroom` (view.h): found by the tick's fingerprint (`find_route`), loaded once and kept apart from the race
  route, so the live viewer's race segments keep theirs. It holds the backdrops (`read_track` with backdrops only),
  `RenderAllBackdrops`' three ranges, the ENVIRO sky / sun / fog / specular, the car globals and GLOBAL.TXD's
  `blobbyshadow`. `Frame` carries the tick's fingerprint (`route_size`, `route_units`).
- `View::frontend_frame` on a FECAR tick: `frame_params` of the showroom sky (fog and full-screen specular as a race),
  the sky's draws, range 0, the **reflection** (`floor_reflection`: main' = up row and position y negated; wheels and
  panels follow, X' = X inv(main) main', as capture.cpp composes them; set A), the car, ranges 1 and 2, the **shadow**
  (`Effects::draw_frontend_shadow`; box and wheel base from the car file at the vehicle data's offsets [L: the vehicle
  data is the file image]), the sun corona. b3gfx's phases keep the game's order except that range 1's blended mesh
  lands after the specular [G: no visible effect found].
- **The env map** (`View::bake_room_env`): six 90-degree offscreen views (a little wider) of the sky and the three
  ranges from the car's box middle, read back in frame units (present scale 1, the GS env map's range), resampled into
  the env pass's dome mapping (car.frag `env_sky`, 256 x 128). Rebaked when the centre moves by more than 1 cm (a car
  change). Directions are looked up by the reflected ray, as in races; the game's 3-face sphere map warp is not
  modelled. New car-shader flag: `CarParams::env_frame_units` (mode.w = 2: x 1 instead of the sky's display-unit x 0.5).
- Switches: `--no-showroom` / **R** (menu1's car on black), `--no-reflection`, `--no-fe-shadow`, `--no-room-env` (the
  sky alone in the env map). Live viewer: the same (`b3replay --live`), key R.

### 24.3 The globe (`src/b3replay/globe.{h,cpp}`, `View::globe_items`)

- **Where it draws** [C globe1]: the CRASH NAV pages (main menu row 0, then SELECT REGION). The session `globe1`
  (a scripted World Tour navigation, 2,667 polls) has **1,949 GLOBE
  events**. Every event: position (75, -10, 500), colour (0.5, 0.5, 0.5, 1), the open viewport's view window +0x48 / +0x4C
  = (0.268, 0.201) (tan 15 degrees x 3/4), near 0.5, far 10000. Viewport 0's +0x00 matrix is **all zero** on these
  pages, so the projection cannot come from it.
- **Space** [L]: `DrawInvisiblePolys` 0x3B2830 puts its depth-only bands at (+-half_w, +-half_h, z 1): the screen's
  corners at depth 1 in camera space. So B3Im3d gets viewport 0's camera-space points (an identity camera). 2D x = 640 x
  (0.5 - 0.5 x / (z half_w)) (+x is the screen's left, as the RwCamera frame's right row), y = 448 x (0.5 - 0.5 y / (z
  half_h)). The picture confirms it: Britain west of the continent, north up.
- **What `_Render` 0x3B2EE0 sends** (`globe_batches`, from the decomp's drafts; constants and loads C asm, e.g. the
  data's u16 fields are `lhu`):
  - `DrawInvisiblePolys`: the top band (y half_h .. 0.7042 half_h) and the bottom band (-0.7167 .. -1) at z 1, and the
    rectangle +-0.8125 half_w z, +-1.0833 half_h z around the centre at its depth. Colour 0, so the GS leaves the
    colour (ALPHA 0x44, As 0) and writes Z: the far half of the globe fails the depth test.
  - The wireframe: meridians (every second column, rows 2..30, starting at the longitude cell's parity), polar ends
    (every fourth column), parallels (every second row). Untextured, Z not written, PRIM AA1. Colour (0.8157 f, 0.898
    f, f, 1) x the object colour, f = 0.75 alpha + 0.25, packed (x 128, truncated, capped (255, 255, 255, 128)), alpha
    0x80, then doubled by `RenderLinelist` [C asm 0x364920: paddw, pminw].
  - The surface: 65 strips of the 33 x 65 grid, u = (cell + 64 - c) / 64 (scrolled by the longitude cell), v = (32 - r) /
    32. Texture D_01EA4608, Z written, back faces culled. Each point is the unit-sphere point through the globe matrix
    (R(kUp, -frac) then R(kRight, -lat (1 - shape)), x 100, at the position), lerped towards the flat map point by the
    shape value. Column 65 reads the next row's column 0, as retail's row-major reads [C].
  - The highlighted region (region 0 / 1 / 2: D_01EA460C / ..4618 / ..4614): a 16-column patch of the unmorphed sphere.
    Colour alpha x highlight / 255.
  - `DrawGlobeHalo` 0x3B17F0: the halo texture as four mirrored quarter quads, half size 500 x (0.8125 half_w, 1.0833
    half_h) at the centre's depth. The left two only from 180 degrees of the reveal angle on, turned about kUp by
    (angle - 270) degrees up to 270 (0 after).
  - Not built: the cylinder grid (`Interpolate(cylinder + offUp, sphere, 1.0)` is the sphere point) and
    `gmpStaticGlobeData`'s generator's trig (the library's sin / cos for SinCosFast [L]).
- **Drawing**: at the GLOBE event's place among the 2D draws (`HudGlobe::draw_index`: the draw count at the Resume that
  ends its callback, so the draws its Suspend flushes come first; the decoder no longer merges a draw across it). New
  b3gfx overlay flags `overlay_ztest` / `overlay_zwrite` (GEQUAL, reverse-Z): the globe's batches test and write depth
  among themselves, kGlobeDepth / z. Strips go through a camera-space-to-clip matrix (perspective-correct UVs, REPEAT
  [L]). Lines are quads one 2D pixel wide with their depth per vertex [G: the GS's AA1 coverage is not modelled].
  Back-face culling is left to the depth rectangle [G: B3Im3d's BFC rule not read]. In the wide layout the two bands
  reach the output's edges, so the globe is masked the same way in the side margins [G display choice].
  `--no-globe` leaves it out.

### 24.4 Checks

**No capture change.** The capture is menu1's (the FECAR / GLOBE events, the static-track fingerprint), so there is no
exactness run beyond menu1's. The globe1 capture: 2,667 of 2,667 polls exact, 0 other EE cycle or step counts; 1,949 globes, 0 objects with unhooked
callbacks.

**Live = offline** (`b3livefeed` into a ring, `b3replay --live --game <game> --offscreen-live --shots --verify`; the
live viewer needs `--game` on Linux): globe1 2,667 ticks identical, shots 760, 1019, 1139+0.5 **0 of 286,720 pixels
differ** from `--offscreen`; race2 5,180 ticks identical, shots 1897, 1960+0.5, 2040 (showroom, reflection, shadow,
env map) 0 pixels differ. The showroom reloads after the live viewer's race route load (which clears the GPU
resources).

**Cost** (`--bench 1280x720`, WSL's Vulkan, slow: race frames p50 103 ms): globe1's front end p50 15.1 ms (15.8 with
`--no-globe`; merging the globe's batches per state took the frame from 190 draw items to 43); race2's front end with the car pages 24.9 ms.
The showroom's env map bake (6 offscreen 128 x 128 views) runs once per car (the box centre).

Tests: `globe_event_place_and_batches` (test_hud: the GLOBE event's place among the draws, no merge across it, the
static grids, the batch counts, the line colour bytes, the depth rectangle, the surface on the radius-100 sphere).
`make -j8 render` and `make -j8 test` clean.

### 24.5 Running it on Windows (this branch's viewer `%B3_WIN%\fe2\b3replay.exe`)

```
:: the car pages' showroom (race2's front end; Tab cycles segments, R the showroom on / off)
%B3_WIN%\fe2\b3replay.exe C:\path\to\burnout3-native\data\scene\race2.b3scene --game %B3_WIN%/perf/install/game --fe-segment 0 --poll 1960
:: the globe (CRASH NAV; the globe1 capture, 244 MB, not in data/scene)
%B3_WIN%\fe2\b3replay.exe %B3_WIN%\perf\scene\globe1-fe2.b3scene --game %B3_WIN%/perf/install/game --poll 1019
```
Pictures: `%B3_WIN%\fe2\pictures\` (1920 x 1080 frames with and without).
Not tried in a window (headless only). After the merge the live window shows both (main's b3view folder after
`tools/render/build_win.sh`).

### 24.6 Not done

- **B3Im3d's back-face cull and AA1 lines**: culling is left to the depth rectangle, and lines are 1-pixel quads. Both
  need the VU1 program 0x47A060's cull test and the GS AA1 coverage [G: no visible difference at 640 x 448].
- **Range 1** of the backdrops lands after the full-screen specular in b3gfx's phases (the game draws it before).
- **globe1's other pages**: the session only visits SELECT REGION (USA / Europe / Far East). The zoom
  (krB3GlobeZoomEndPosition, the shape morph to the flat map) is drawn by the same code but not seen in a capture yet.
  A nav that picks a region would show it.
- The globe's position in the wide layout follows the 4:3 box (the page has no component anchors).

## 25. The post passes: directional blur, the blur choice, bloom (post1, 2026-11-04)

Branch `post1`. The last three passes of `CB3World::Render` 0x276300 [C matched]: `CB3Bloom::Render` 0x2659C0 (after the
post-fog effects and the 2D race position, before the sun corona), then `if (CB3DirectionalBlurBase::UpdateCamera(D_00666274
+ 0x60, idx)) CB3DirectionalBlur::Render 0x426C10 (unless Is2PlayerSplitScreen && IsCrashMode && !logic 0xAC) else if (the
view's car +0x18EA == 0) CB3RadialBlur::Render 0x2FA9A0`. Decomp citations: HEAD b21a8ccc3e (read only); constants read
from the ELF. Code: `include/b3asset/postfx.h` + `src/b3asset/postfx.cpp` (the arithmetic), `src/b3gfx/shaders/post.frag`
(bloom), `shaders/blur.frag` (both zoom blurs), b3gfx's `zoom_blur_chain` / `bloom_chain`, `decode_post` /
`PostIn` (capture.h), `View::blur_frame` / `bloom_frame`, `info_post` (`--info`), `grab_post` (the hook).

### 25.1 Where the state lives [C]

D_00666274 (graphics manager +0x3B4) is the open viewport's params pointer: during view 1's render it points at
graphics manager +0x1D0 (D_00666090, the radial blur params of §21.3). So **the bloom's params (+0x220) and UpdateCamera's
state and outputs (+0x230..+0x257) are in every capture since render4** (graphics manager [0, 0x3C0)) [C values:
CB3DirectionalBlurBase::Prepare 0x426030's defaults 0.5 / 0.5 / 0.9999 / 0.9999 at +0x240..+0x24C; at a poll the
pointers +0x3B0 / +0x3B4 are 0 again, CloseViewport]. CB3DirectionalBlurBase: +0x00 the camera point 10 m ahead, +0x10 /
+0x14 the blur centre (fractions of the view), +0x18 / +0x1C the zoom, +0x20 the amount, +0x24 the time of the call
(D_004F50A0). CB3Bloom (+0x50 of the params): +0x0 threshold, +0x4 intensity, +0x8 feedback. Values: roadrage1
(EU/C4) threshold 0.65, race2 0.75, race1 (EU/C1) c = 175; intensity 1.0; feedback 0.5 at rest rising to 0.9 while
boosting (roadrage1 1,882 ticks at 0.9); D_00665F28 (bloom on) set in every race tick.

### 25.2 UpdateCamera's decision and the capture block

UpdateCamera 0x4256C0 [L nonmatching draft read with the asm; constants D_004E0F5C 0.3, D_004E0FC4 0.012, D_004E0FCC
1/49, D_004E0FD0 1/127.5, D_004E0FD4 0.998, D_004E0C6C 0.01, D_004E0FC8 0.65]: stamps the new point and time, sets the
amount to 0, then returns 0 without the player block's +0x2430 pointer; 1 with the crash pulse (that object's +0x3534 >
0, D_005179C0[idx] & 0x2000 clear, the world clock within 0.3 s of it); otherwise 0 unless flag 0x400, or the boost switch
(the player block's CB3BoostEffectParams +0x52) with func_0030A990 = 0.65 (1 - (+0x20 - 1)^2)(1 - (+0x1C - 1)^2) > 0
[C matched]. The motion branch (`b3asset::directional_motion`): the old point in this call's camera space minus 10 m on z,
over dt: amount (|v| - 1) / 49 (none below 1/127.5: still returns 1), at most 1, x func_0030A990 with flags 4 / 0x20;
centre 0.998 v.xy / max(v.z, 0.01) + 0.5; zoom 1 - 0.012 (0.5 + 0.5 a) / |centre - the far corner|.

The capture block (`grab_post`, no version change, detected by presence) adds UpdateCamera's other inputs: D_004F50A0,
D_004D1540 / D_004D1550 (the pulse's CosFast4 scales: 0 in the ELF), D_005179C0 + 0xBB0 idx [0, 0x30) (flags; +0x26 =
D_005179E6, the bloom's reset), the player block gWorld + 0x12AD50 + 0x27D0 idx +0x118C [0, 0x60) and +0x2430 with that
object's +0x3534, and ourGlobals 0x670BD0 -> +0x60's raster [0, 0x20) (the bloom's size), for idx 0 and 1.

**Exactness** (build sha256 a080326b...) [C]:

| Session | Capture on | Capture off | GS stream |
|---|---|---|---|
| race2 (5,181 polls) | 5,181 of 5,181 exact, 0 other EE cycle counts; 29,496,544 bytes (fx2's 29,361,544: +0.46 %) | 5,181 of 5,181, 0 other | **identical in every column over 8,030 frames to fx2's capture-on run** (prefix digest 6870a561...); against capture off the same 5 frames 4385/4386, 7426..7428 as fx2's own on / off pair (main's HUD capture) |
| roadrage1 (13,474) | 13,474 of 13,474 exact, 0 other; 138,764,144 bytes (fx2's 136,539,400: +1.6 %) | | against fx2's capture-on run: p0 / p1 / p2 identical in all 17,645 frames; 81 front-end frames (1787..4683) differ in full / hdr / masked only (the host-record variance of §12.2; fx2 saw 79 against hud4) |
| race1 (53,760) | 53,760 of 53,760 exact, 0 other; 541,966,008 bytes | | |

**The reading against the game's own numbers** (`b3replay --info`, `info_post`) [C]:

| | race2-post1 segment 0 (2,229 ticks) | roadrage1-post1 (9,552 race ticks) |
|---|---|---|
| UpdateCamera returned 0 / crash pulse / motion | 1,979 / 0 / 250 | 8,085 / 0 / 1,467 |
| CB3World::Render draws: neither / directional (amount >= 1/255) / radial (amount > 0) | 1,355 / 151 / 624 | 2,233 / 1,455 / 4,959 |
| ticks that returned 0 with amount != 0 (must be 0: the first store) | 0 | 0 |
| motion branch recomputed (previous call's point and time, this tick's camera) | 207 of 207 calls agree; centre 6e-4 relative, zoom 9e-6, amount 3e-4 | 1,331 of 1,331; centre 8e-2 relative (one call, poll 7674), zoom 4e-5, amount 7e-4 |

So the radial blur is a large part of normal racing (it takes the frame whenever UpdateCamera returns 0 while the boost
params' amount is set: the boost glow's fade-out and the stretches between boosts), and the directional one takes
the boosts with camera motion (race2 3051..3131, 3199..3231, 3350..3385; roadrage1 3557..3641, 4355..4435, ...).

### 25.3 The directional blur [C SetRenderstates 0x426B70 / BlurSetRenderstates 0x426920 matched; L drafts of Render, Downsample, BlurOut / BlurIn, CopyToScreen]

The radial blur's chain without the mask and the 136/128: Render returns unless D_00665F2A and amount >= 1/255 (gp
D_004E1010); SetRenderstates (RGBAQ 0x80 = x 1, TEX1 bilinear, TEST colour only); Downsample 0x4269F0 copies the frame
into the half-size buffer (ALPHA 0x2A); three passes at 50 % (ALPHA 0x64 FIX 0x40, RGB only, REGION_CLAMP to the buffer):
BlurOut (zoom s), BlurIn (1 / s^2), BlurOut (s^4) (Render alternates them; gp D_004E25E0 = -1 keeps the alternation),
each sampling c + z (p - c) about the centre (half-buffer pixels, `int(W/2 x centre + 9999.5) - 9999`); CopyToScreen
0x426110 lerps the frame towards it by FIX = int(128 x amount + 0.5) (ALPHA 0x64). The centre is often far outside the
view (the vanishing point of the camera's motion: roadrage1 had -4.6 / 3.4), which makes the zoom a near-parallel
streak. Native: `blur.frag` with the RGBAQ gain and the mask as uniforms; one ping-pong chain for both blurs.

### 25.4 The bloom [C Render 0x2659C0, Blend 0x264C30, SetRenderstates 0x265950, RestoreRenderstates 0x264920 matched; L drafts of FirstDownsample, SecondDownsample, Blur, CopyToScreen]

1. **FirstDownsample** 0x2653B0: c = int(255 threshold + 0.5) (at most 254); s = 32768 / (255 - c) x 1.05, then k1, k2,
   k3 = int(s + 0.9999) at most 255 with s x= 128 / k after each (`b3asset::bloom_gains`: 0.65 -> 166, 255, 195, 128; 0.75
   -> 191, 255, 255, 136). Per 64-column strip of the frame, two sprites draw the frame (nearest) over a fill of c with
   ALPHA 0xA4 FIX k1 = (frame - c) k1 / 128, clamped at 0 by COLCLAMP, into a 128-wide scratch in the Z buffer's memory:
   the strip's ZBUF aliases the other half of the scratch, so the same sprites' Z writes (Z = c, c, c) fill the next
   strip's destination with c. A context-2 sprite copies the strip 2:1 (bilinear, +1 texel) into the half-size buffer H
   (at the Z buffer + 2 x ((h + 31) / 32) pages) by MODULATE RGBAQ k3 and ALPHA_2 0xA8 FIX k2. Net: H = the 2 x 2 mean of
   max(0, frame - c) x ~1.05 x 256 / (255 - c), each step clamped to 255.
   Open: CLAMP_2 is 0x30003 (REGION_REPEAT, UMSK 0, UFIX 12) [C asm 0x265820: the register value TEST_1 shares], which
   read literally pins the context-2 copy to column 12. The port reads the copy as the 2:1 average [L]; whether the
   hardware bands is not settled.
2. **SecondDownsample** 0x2651C0: H -> the quarter Q, 2:1 bilinear x 135/128 (RGBAQ 0x87).
3. **Blur** 0x264E60: four in-place passes on Q, Q + (min(1, Q(p + d) x 138/128) - Q) x 84/128 (RGBAQ 0x8A, ALPHA 0x64 FIX
   0x54), d = (+1.5, 0), (-1.5, 0), (0, +1.5), (0, -1.5) texels (UV +2 / -1 texels with the GS's half-texel bilinear),
   strips ordered so each reads unwritten texels.
4. **Blend** 0x264C30: the kept picture P (D_006662D0, 192 wide) = Q + P x int(118 x feedback) / 128 (ALPHA 0x29); P = Q
   when D_005179E6[idx] (reset); unchanged when D_0051BA94 != -1 (race2: 182 ticks of the Crash event). **P persists from
   render to render**: the trails the sparks leave.
5. **CopyToScreen** 0x2649D0: frame += P x int(128 x intensity) / 128 (ALPHA 0x29 FIX 0x80; RGB only, bilinear upsample).

Native (`post.frag` modes 0-4, b3gfx `bloom_chain`): H and Q at the GS's sizes (224 and 112 rows, as wide as the output's
aspect) in both qualities, P ping-ponged in the device per target (restarted when the size changes or `restart`).
The frame is read clamped to 1 (the GS frame holds at most 255). Over n renders since the last drawn frame P takes
keep^n and Q (1 - keep^n) / (1 - keep): one game render per tick in game quality; the displayed time in ticks in PC
quality (smooth at any frame rate, the same trail length in seconds). `--offscreen` draws the 24 ticks before the
asked one first (`--bloom-warmup N`) so a single picture has the trails.

### 25.5 Quality, keys, flags

- **PC** (default): the zoom blurs at the output size (sharp detail inside the streaks); the bloom's bright pass from
  every output pixel of an H texel's footprint (n x n taps, n = output rows / 224, 2..8: thin sparks at 4K still
  bloom), the composite upsampled with a cubic B-spline (no blocky edges). **Game**: the GS's half / quarter buffers and
  its 2 x 2 taps, bilinear upsample. The effect (constants, pass order, spread in screen terms, trail length) is the same.
- b3replay and the live viewer: **N** the zoom blur (now on by default; `--no-blur`), **T** the bloom (`--no-bloom`),
  **Z** PC / game quality (`--post pc|game`); `--blur-radial` draws the radial blur on captures without post1's inputs
  (there a directional amount >= 1/255 still decides the directional blur; the radial one is otherwise left out);
  `--bloom-warmup N`. Diagnostics: `B3_POST_TRACE=1`, `B3_BLOOM_C=<threshold>`, `B3_FRAME_UNITS=1` (the output shows the
  GS frame's bytes, not the doubled display).
- With `--no-blur --no-bloom` the frame is pixel-identical to main's (race2 3259, roadrage1 4949: 0 pixels), and
  `--blur-radial --no-bloom --post game` to water1's `--blur` (0 pixels).

### 25.8 Blur strength (gsoff1, 2026-11-05)

Reported: the zoom blur looked very strong (split2404, player 1). That frame's blur is the **radial** one (UpdateCamera
returned 0, choice 2, amount 1.0 [C B3_POST_TRACE]), not the directional one.

- **The chain against the decomp** [C nonmatching drafts and the matched SetRenderstates; GS register values]: every
  register equals our model.
  - SetRenderstates: RGBAQ 0x80888888, so the passes are modulated by 136/128 and Aq is 0x80.
  - SetAlpha: the mask (TCC 1, MODULATE) into H's alpha (FBMSK 0xFFFFFF), CLAMP 5.
  - BlurSetRenderstates: ALPHA 0x64 FIX 0x40.
  - CopyToScreen: RGBAQ (colour x 128, amount x 128), ALPHA 0x44, As = H alpha x Aq / 128.
  - The mask's 127 is decoded as 254/255 (= 127 / 128).
  - One real difference was found and tried: SetAlpha's sprites only cover H's rows from the centre down (y = hv..h/2),
    and the rows above keep Downsample's alpha (the frame's).
    - Using our frame alpha there barely blurs the upper half (our scene alpha is near 0 there): 20.0 at any amount, not
      better.
    - Not kept; the rows above still take the mask's first row [G].
- **Change**: `View::radial_scale`, the radial amount x 0.5 by default (a choice from comparisons of frames; the
  decomp's model is x 1, cause not found). `--radial-blur S`
  (1 = the decomp's model).
  - With `--radial-blur 1`, 18 of 18 offline frames are pixel-identical to before (race2 / roadrage1 x 3 polls x 3 sizes).
  - With the default, only the radial-blur ticks change (roadrage1 5000 / 9000, choice 2), and 49835 equals the x0.5
    sweep render (0 pixels differ).
- **Resolution and frame rate** [C code]: the zoom passes work in fractions of the view (resolution-free). The chain
  starts each displayed frame from that frame's picture with no history, and the amount is the tick's (held), so
  interpolated high-fps frames cannot stack it. Only the bloom keeps a picture between frames, and it counts the
  renders by the displayed time (§25.4).

### 25.7 Not done

- The CLAMP_2 region-repeat question of 25.4 (a GS test of FirstDownsample's context-2 copy would settle it).
- The radial blur's mask direction and the zoom passes in place vs ping-pong are water1's reading [L / G]; checked
  only by reading.
- Split screen: view 2's blur and bloom are drawn from its own params since split2 (§29.6); the 2D race position
  (CB3RacePosition::Draw) the game draws before the bloom is in the 2D stage here (after it).

## 26. The look-back camera: the player's own car hidden (cam1, 2026-11-04)

**Symptom** (live viewer): the view while L1 / LB is held was broken. In the capture of
races14b (exe 038eecb9; L1 held at polls 10043-10053 and 10204-10229, decoded from pads.log bit 10 =
HostControllerState::left_shoulder), b3replay drew the look-back ticks as dark panels filling the screen: the inside
of the player's own car (`%B3_WIN%\cam1\pictures\races14b-p10050-before.png`, `-p10215-before.png`).
races14 and races14d have no L1 presses.

**How the game makes the view** [C asm / drafts]:
- `CB3OfflineStageLogic::UpdateCameras` 0x395310 sends event 8 to the player camera manager's FSM (vtable slot 0x14 =
  `CGtFSM::ApplyEvent`) every tick while the look-back input flag is set.
- The camera state's Action, case 8 (0x2E8250 draft), calls `SetActiveBehaviour(4)`. Camera behaviour 4 sets bit 4 of
  the manager's +0xB80. When the key is released, the state goes back to behaviour 2 or 5.
- The behaviour is an ordinary CGtCamera; `CGtViewport::Update` writes it into the viewport's RwFrame as usual. The
  captured camera at the look-back ticks: the eye at the car's own origin + 0.25 m up, `at` = -(the car's at),
  view window 1.0 x 0.75 (90 degrees; the chase camera at those ticks was 1.43 with the boost zoom), near 0.36
  [C races14b-cam1 capture, every look-back tick].
- `IsPlayerVisible` 0x2E7340 [C matched] is false while +0xB80 bit 4 or 5 is set. `CB3World::RenderRaceCars` 0x199A20 (draft,
  offsets C from the asm) then leaves the view's own race car (+0x1914 == 0, +0x27C0 == the view) out of its list.

**Cause.** b3replay already used IsPlayerVisible for the race car's shadow (§14) and coronas (§16), but not for the car
itself: `game_draws` only decides traffic and crashed vehicles, so the player's car was drawn around a camera inside it.

**Fix.** `Vehicle::own_hidden` (decode_effects, from inputs every capture since corona1 holds: +0x1914, +0x27C0 and the
manager's +0x9E0 / +0xB80): the view's own race car while IsPlayerVisible is false. The race view's
`View::vehicle_draws` call (`hide_own`) skips it with or without V, because it is game state, not our camera model. The
front end's calls (showroom, car pages) leave it off. No new capture block.

**Checks.**
- The races14b capture: 16,320 / 16,320 polls, 0 other EE cycles.
- Over the whole session, `own_hidden` is set only on the look-back ticks: 10044-10054 and 10205-10230. The camera
  switches one tick after the press and returns one tick after the release, and IsPlayerVisible follows the camera.
  Nothing is hidden in the intro, the crashes or the menus.
- After the fix: the rear view of the road (`-p10050-after.png`), and the rival's car behind on the bridge
  (`-p10215-after.png`). The chase view before the press is unchanged (`-p10043-*`).
- Interpolation across the switches: the camera turns 180 degrees, so the cut rule (§12, > 60 degrees) holds the key
  camera and does not blend. The half-tick frames 10043.5 (chase, car drawn) and 10054.5 (rear, car hidden) are not
  smeared (`-p10043.5-after.png`, `-p10054.5-after.png`). The car's visibility is the displayed tick's, like the camera's.
- `tests/test_capture.cpp`: synthetic images. Flags 0x10 / 0x20 hide the view's own car; 0, 0x8 and 0x400 (no
  timer) do not. A car with +0x1914 set, or with +0x27C0 = another view, is never hidden.

### 26.1 The car pages' thumbnail cars at sizes other than 4:3 (wide layout, §17.11)

**Symptom** (fe2, main f52c5a8): on the car list, the thumbnail cars on the left were cut off at 640 x 448 and
1280 x 896, and gone at 1920 x 1080 (race2-fe2gs1960 poll 1897). They are 2D sprites (MSCLCAR1 / MSCLCAR2 and a frame),
drawn inside a Gt2d clip region (GS scissor x 32..159, y 67..328).

**Cause** [C B3HUD_DEBUG of that frame]: one Gt2d object (no listed component) draws both the bottom band and the car
list. The band's `small_curve` piece touches x 640, so the wide layout's vote made the whole object right-anchored, and
every one of its draws moved by +ext. The clip region moves only with its component (shift 0 here), so the thumbnails
slid out of it.
- 640 x 448 is not 4:3 (aspect 1.43), so the wide layout is on there too, with ext 22.9 2D units: the cars were cut.
- At 1920 x 1080 ext is 106.7, and the cars left the region entirely.
- `--hud-layout 43` drew them whole.

**Fix** (`View::hud_list`): a draw inside a clip region narrower than the raster ignores its object's edge anchor and
moves with the clip region (its component's shift), so the clip region and its contents stay together. The thumbnails now
sit above their list arrow, where the 4:3 layout puts them.

**Checks** (1920 x 1080, old against new binary):
- roadrage1-post1 polls 5454 and 9000, and race2-b14f polls 2450 / 3000 / 4200: pixel-identical.
- roadrage1 6879 and 13105 differ only where two runs of the old binary already differ (3,735 and 20 px, the same boxes
  in the 3D view).
- The car pages (race2 1840 / 1897 / 1960 / 2040) change as intended.
- Pictures: `%B3_WIN%\cam1\pictures\fe-1897-<size>[-fix].png`.

## 27. Menus at any aspect (wide1, 2026-11-05)

### 27.1 The front end's wide layout (`View::hud_list(..., frontend)`, `hud_frontend_rule` / `hud_frontend_vertex`)

hud2's rules (§17.11) anchor each Gt2d object to the side its textured edge pieces touch. In races the components carry
the layout and these rules only place the edge pieces. Front-end pages have no components (menu1 §22.3), so the vote moved
whole menu objects:
- the logo 0..320 to the left edge;
- the main menu's ticker object, and the d-pad prompt in it, to the right edge whenever a glyph's edge landed on x 640
  (33 of 224 frames: the flashing);
- the Crash Nav map's SATMAPE1/3 tiles to the edges, with gaps where the movie showed (the dark rectangles).
Front-end frames now keep the 4:3 picture and extend only:
- **spans**: a draw with a triangle covering x 0..640. Its vertices at or past an edge move out by ext, the rest stay.
  This covers `bg`'s three strips (the main menu's backdrop fades into the movie's window: the window keeps its place),
  the bands (`small_curve` 0..640) and the selection bar (`FE`). A repeating texture (the grid, 16 px tiles, u 0..41 over
  0..656) moves its u / v by the triangle's gradient, so tiles keep their size.
- **fills**: an untextured, non-movie draw reaching an edge from inside (both ends: a full line). Only its edge vertices
  move, so a band body (76..640) still meets its curve piece (`big_curve` at 0..272 stays in the box). A line lying past
  the edge (Crash Nav's crosshair at 639.9..644.9) is neither.
Everything else, text included, stays in place and keeps the 4:3 clip. The selection's ghost text sweeps from -192 to
706 and ends at the screen edge, as on the PS2. Movies (VIDEO tiles) never stretch. A draw in a clip region narrower than
the raster no longer votes for its object's anchor (races and front end); cam1's rule already kept such draws from
following the anchor. `--fe-layout edges` restores hud2's behaviour in the front end. The 3D behind the car pages stays
Hor+ (§22.3).

### 27.2 The host UI layer

`HostUi` and `View::host_ui_list` draw a host-made layer of quads (text, images, rectangles); the PC settings page
(pcpage.cpp) draws through them.
- `View::host_ui_list` draws after the 2D in every path (front end, race, live, bench, shots). It uses the 640 x 448 ->
  4:3 box mapping and the box's scissor, GS ALPHA 0x44.
- Colours: vertex 64 x c and alpha 128 x a, so after the present pass's doubling the layer shows at full intensity
  (straight alpha blend).
- PNGs: `read_png_rgba` (8-bit grey / RGB / RGBA / grey-alpha, not interlaced; zlib). A Windows path that does not open
  is tried as /mnt/<drive>/... for offline replays on Linux.

### 27.3 The signature takedown "postcard"

There is no in-race postcard to draw:
- The race HUD's scenes hold the 8 components of §17.11's table (D_0066C3F0 is `_gCrashHeading`, D_0066C430 `_gImpactTime`). Award messages,
  signature takedowns included, go through `_gPrimaryMessageComponentP1` [C inproc2's race].
- Postcards are front-end awards (`CB3MenuFinishedEvent::manPostcardAwards`) and the Driver Details > SIGNATURE TAKEDOWNS
  page (rasters `polariods`, `<place>-st1` / `-st2`), which draws [C inproc2 polls 17958..18443].
- Retail's behaviour at the moment of a new signature takedown in a race is not checked.


## 28. One window: the FPS panel, split screen, the race start (gsoff1, 2026-11-05)

### 28.1 The FPS panel

The window draws an FPS counter for its own presented frames (src/b3replay/fpspanel.{h,cpp}; redesigned in fpsoverlay2,
2026-11-15). It is the port's own plate: the cut-corner dark plate with an amber edge and a short amber accent bar of
photo mode's plates, in the 2D stage's colour scale. It is built from solid rectangles only (a 5 x 7 pixel font, a few slanted
quads for the gauge and the cut corners, a 1 x 1 white texture): one batched indexed draw item whose vertices are uploaded at
most twice a second (the plate's place in the window is in the item's matrix, so a notice moving it uploads nothing); no bitmap
is drawn on the CPU and no texture is updated after the first (the first bitmap design cost about 0.7 ms a frame on a Mac at
3200 x 1800). Per frame: a cached layout, the eased place and one draw item, about 0.0002 ms.

- **Views** (PC SETTINGS > GAME > INTERFACE > FPS COUNTER: Off / Minimal / Standard / Detailed, ini `fps_view` 0-3; `--fps-counter`
  shows at least Standard). Minimal: a small plate with the frame rate (three digits wide so it does not shift), the word FPS
  and a small three-cell signal gauge. Standard: the same with the 1 % low under it, no graph (both at most about 8 % of
  the window's width, redrawn twice a second). Detailed: the big plate, also the frame-time graph, the average frame rate,
  the current and the worst frame time, the input-to-screen latency (`--latency`, when measured), the render size, MSAA
  and quality preset, and the GPU and its backend. The **FPS COUNTER VIEW** shortcut (a port action, `fps_view` in
  controls.ini, F3 by default, remappable on both pages; no gamepad chord) cycles Minimal, Standard, Detailed, Off for the
  session; a settings change (a new settings record) sets the view from the setting again.
- **Numbers**: the large number is the frames per second of the last 0.5 s. Everything else is over the last 8 s of
  frame times (one per presented frame): the average (frames / time), the 1 % low (the rate of the mean of the slowest
  1 % of the frames, at least one), and the slowest frame.
- **Graph**: 40 columns of 0.2 s, each the slowest frame in it, as a filled step graph, each column filled and edged by
  cost (mint at the target, amber over it, red for a spike) and a dashed amber line at the target frame time, labelled in
  ms. The target is the FRAME RATE LIMIT's frame time, else 60 Hz's (16.7 ms). The scale runs from 0 to 2, 3 or 4 targets
  (the slowest frame plus a tenth, in whole targets); a spike is a frame over twice the target and at least 8 ms over it.
- **State without colour**: the number and gauge are mint at the target (95 % of it or more) with three lit cells, amber
  below it with two, red far below it (under 75 %) with one; the 1 % low is coloured the same way against the target. The
  graph's own shape and its target line carry it too. Before the first frames the number reads `--` and no cell is lit.
- **Layout** (`FpsPanel::layout`, pure; tests/test_fpspanel.cpp): sizes are those of a 720-line window scaled by the
  window's height and HUD SIZE (100 / 115 / 130 / 150 %), never under 0.75; the plate sits in the chosen corner (FPS
  COUNTER POSITION: top left by default, ini `fps_corner` 0-3) 4.5 % of the height from both edges, the same on every
  side and at every aspect, so a 21:9 or 32:9 window keeps it at its edge; a plate that does not fit a small window
  shrinks until it does. In a race the game's own HUD plates sit in all four corners (position and lap at the top, the boost
  bar and speed at the bottom, grown about their corners by HUD SIZE), so there the plate starts below the top ones (about 26 % of
  the height at 100 %, so the time attack's two rows of times are cleared) and ends above the bottom ones (15 %), at the same side margin (`FpsPanel::game_hud_top` /
  `game_hud_bottom`). A notice that meets the plate moves it down (top corners) or up (bottom corners) clear of the
  notice, eased over 0.15 s. While photo mode's own display is up (its readouts at the top right, its controls at the
  bottom centre; the game's HUD is not drawn then) the counter takes the top left, at the plain margin, whatever its corner; with that display hidden it keeps its corner.
- **Not in screenshots**: the window draws the counter after the F12 screenshot is taken (also in photo mode, as photo
  mode's plates), so a screenshot never holds it. Offscreen shots (`--shots`, race_check) draw the counter when the
  settings in effect at the shot ask for it, with no frames counted (`--`), the render size and the settings' MSAA and preset.
- **Track loads**: `View::clear` (every track load, so every Crash mode junction and its results) calls `Device::clear_resources`, which
  restarts the buffer and texture ids from 0. The plate keeps its two buffers and its 1 x 1 texture between frames, so it compares
  `Device::resource_epoch()` (counts the clears) with the value they were made under and makes them again when it changed; before,
  the plate drew with one of the game's textures (a dark, washed-out box with unreadable text) and wrote its quads into a game
  buffer (tests/test_fpspanel.cpp `a_track_load_clears_the_plates_buffers_and_texture`).
  The other 2D panels that keep ids between frames (notice, skip hint, speedrun timer, car look and tuning boxes, photo mode's plates,
  the code mods' overlay) do the same through `b3gfx::ResourceEpoch` (tests/test_panel_epoch.cpp); the mod overlay keeps its pictures to upload them again.
- It is presentation only: nothing here reaches game memory, the pad or the replay; the counter is off by default, so the
  golden runs are unchanged.

`make build/fpsoverlay_preview` builds tools/native/fpsoverlay_preview.cpp, which composites the counter's quads over a
PNG with a synthetic frame timeline (`--timeline steady|dips|slow|spiky`, `--view`, `--corner`, `--hud`, `--limit`,
`--notice`, `--zoom`; `--bench`: the CPU cost of draw_list per frame), like tools/native/photohud_preview.cpp does for photo mode.

### 28.2 Split screen (2 players): not drawn yet

Recording `gsoff1mp2` (headless scripted navigation; player 2 is a keyboard player; Multiplayer > RACE, default
options, EU/C4_V1, 4 rivals). Pads log: pad 2 connected throughout, its Cross at polls 1252 / 1352 / 1453 (car select)
and held from 1703; the race has 6 cars (POS x/6). Picture `%B3_WIN%\gsoff1\pictures\` (nat2404).
- **Retail** [C the captures, §29.1]: two 640 x 224 views stacked, player 1 on top, player 2 below, each with its own HUD (POS, LAP,
  speed, boost bar, its own award messages).
- **Ours** [C b3replay `--offscreen` on the same run's capture, poll 2404; the live window draws the same]: one view, player 1's
  camera (graphics manager +0x90), full screen, with player 1's HUD stretched over the whole picture (speed at the
  bottom right instead of the top view's bottom right). Player 2's view and HUD are missing.
- **Why**:
  - 3D: `decode_tick` takes player 1's viewport (+0x90), else the full-screen one (+0x310). The capture already grabs
    +0x130 (player 2's), but the renderer has one camera and one `FrameParams` per frame.
  - 2D: the HUD capture of this race holds one 2D pass per frame, with Begin's scissor 640 x 224 at (0, 0)
    (`--info`: 328 frames; front end 640 x 448). That is player 1's pass. Player 2's pass, at (0, 224), is not in the
    capture [L: the hook keeps one Begin per frame].
- **Fix (not small, a lane)**:
  1. Decode both cameras when `Is2PlayerSplitScreen` (D_0051BA88 == 0x51AEC0, the rule capture.cpp already uses for
     `split_crash`).
  2. Draw the scene twice into the two halves. This needs a viewport rect per pass in b3gfx and the post passes (bloom,
     blurs, sun) per view.
  3. Keep each player's car visibility, LOD and traffic choice per view.
  4. Capture both Gt2d passes with their scissor and map each into its half.
  Crash mode's split rule (`split_crash`, no directional blur) is already decoded.

### 28.3 Black frames at a race's start: the route waited for 30 race ticks (fixed)

- **What showed** [C gsoff1k1, EU/M1_V1 Time Attack, race segment from poll 1207]:
  - The game already shows the race intro there: the 3D under the TIME ATTACK band.
  - Ours drew only the 2D on black until **poll 1235**, 1,101 ms after the race's first tick arrived. The bench of
    the in-process run logged 562 displayed frames without the race. Shots at 1207 / 1208 / 1210 / 1217 / 1223 are the
    band on black.
- **Why** [C live.cpp before]: `LiveScene::update` matched the route only after `kRouteTicks` = 30 race ticks, by camera
  samples, because V1 / V2 of a route share STATIC.DAT size and unit count (14 such pairs on the disc). It then loaded the
  track on the render thread (607 ms). Until `ready()`, the window falls back to the front-end path (2D only). It was
  not the hook (the first race tick is in the feed), not a keyframe, not the fade and not the interpolation.
- **Fix** (`route_by_header`, capture.cpp; `LiveScene::preload`): the capture already holds the STATIC.DAT header
  [0, 0x60) at *0x64C994. Its words equal the file's, or the file's plus the load address (relocated offsets), and V1 /
  V2 headers differ. So the header names exactly one route [C 3 captures + synthetic test].
  - The game loads it during its loading screen, so the renderer loads the route as soon as the header names one. Times
    from header to first race tick: race2 161 ticks, roadrage1 166, gsoff1k1 166, gsoff1mp2 288, inproc2 166
    (US/C2_V2).
  - The race's segment then matches at its first tick, with no sample matching or rescore. The old rule stays as the
    fallback when no header matches, and `--no-route-header` selects it.
  - Routes under 16 units are not preloaded: US/C5_V1, the front end's own static track (4 units, while race routes
    have 36..162), would change front-end frames (the view's sky).
- **After** (gsoff1k1 replayed before and after, `--bench 640x448`):
  - The route loads at poll 1041 (584 ms, before the race).
  - The first race frame shows poll 1207: 0 frames without the race, 0 ms after the segment's first tick.
  - Shots 1207 .. 1260 are all race frames.
  - Pictures: `%B3_WIN%\gsoff1\pictures\racestart-{before,after}-{1207,1217,1240}.png`.
  - A race restart on the same route (race2 4623 -> 4748) now also draws from its first tick (it waited 30 ticks).
- **With FAST LOADING** (loadlag1, 2026-11-13; pc-settings §7.8 "The race's picture at a load's end"): the 166
  ticks from the header to the race pass in 0.2-0.4 s, less than the route's build, so the build froze the window on
  the loading screen while the race was already heard. The route is now read and decoded on a worker
  (`View::prepare_track`), only its GPU half runs on the render thread (`View::load_prepared`), and native_play holds
  the race's first VBlank (wall time only) until the route is built (`b3r_scene_wait`). The race-start log line now
  also gives the time from the game's hand-over of the segment's first tick (the line's own "ms after the segment's
  first tick" counts from when the render thread drained it, which a blocked render thread makes late).

## 29. The 2-player split screen (gsoff1, 2026-11-05)

Follows §28.2 (what was wrong). Tags as above.

### 29.1 What the game does, from the captures [C gsoff1mp2 polls 1751..2896]

- **Split flag**: `Is2PlayerSplitScreen` is `*0x51BA88 == 0x51AEC0` (the rule capture.cpp already used for `split_crash`).
- **Cameras**: player 1's view is viewport +0x90 (top half), player 2's is viewport +0x130 (bottom half). Both are
  already in every capture (§12.1's grab). Each view is 640 x 224.
- **Camera records**: at poll time camera record 0 holds **player 2's** values. Its +0xE0 matrix equals viewport
  +0x130's screen matrix, with size 640 x 224 and offset (0, 224). Record 1 is neither player's. In 1-player frames record
  0's matrix equals the viewport's screen matrix. So the records are rewritten per view as the game renders and keep the
  last view's. They feed only the VU checks and the sun trace. Coronas, the sun and the other effects are drawn with
  our per-view `Camera`, so both views share the records.
- **2D**: the HUD capture holds every pass of the frame (both views' and the HUD scenes').
  - The HUD components of both players and the award messages are in frame space: player 2's speedometer is at y
    409..427.
  - The rival markers (DrawPositionMarker, through their viewport's matrix) are in their viewport's own space. Player 2's
    are at y 86..107, in a pass whose Begin scissor starts at row 224.
  - The other draws of those passes go to 64 x 32 effect targets (fbp 362, fbw 1), which hud_list already leaves out.
  - Before this change, the reader took the frame's 2D space from the first pass (640 x 224, an effect pass), so player
    1's HUD was stretched over the window and player 2's fell outside it.

### 29.2 What the renderer does now

- **Capture reader**: `Tick::split`, `Tick::view2` (player 2's viewport camera). `Frame::split` / `cam2` are interpolated
  as `cam` is (`interp_camera`, the same rule).
- **HUD** (`HudFrame::split`):
  - In a split frame, marker draws move by their pass's origin (`HudDraw::pass_ox / pass_oy`, Begin's scissor) and
    scissors are frame rows.
  - The 2D space is the main buffer's 640 x 448. Draws of different passes no longer merge.
  - One-player frames are unchanged.
- **b3gfx** (`Device::draw_views` / `draw_views_offscreen`):
  - Each view is drawn as a frame of its own (`render_scene`: 3D, effects, the sun, its own post passes and bloom
    history) into its own targets, sized to its rect.
  - The views are then placed into the frame (the post pipeline's copy), the 2D stage is drawn over the whole frame, and
    the frame is presented.
  - `render()` is now `render_scene()` + `present()`, with the same passes in the same order for one view.
- **View** (`View::split_frame`):
  - Player 1's frame (cam) goes in the top half, player 2's (cam2) in the bottom half.
  - Each view has its own aspect: in a window, Hor+ at the half's shape (twice the window's aspect). In offline
    640 x 448 shots, the game camera's own 640 x 224 aspect.
  - Player 2's view swaps in its own effects / coronas builders and bloom history (`view2_state`), so the two views
    never share a dynamic buffer or a kept picture.
  - The 2D (both HUDs) is laid out once with hud2's wide rules for the whole window. Each player's components anchor to
    the window's edges in their own half.
  - Markers are drawn at the game's own places (hud4's per-frame reprojection is off in split frames, §29.4).
- **Used in**: the live window, offscreen shots and the bench (live.cpp), and offline `b3replay --offscreen` (main.cpp).
  The offline window (`b3replay <capture>`) too, with the game camera (the free camera keeps one view).
- **Not modelled** [G]:
  - (Player 2's zoom blur and look-back hiding: per view since split2, §29.6.)
  - The 2D blends that read the frame's alpha start from alpha 0 after the views are placed (one view keeps the 3D's
    alpha).

### 29.3 Proofs

- In-process REPLAY OK with split shots equal to the offline ones (0 pixels differ).
- 18 one-player frames pixel-identical to the pre-split build.

### 29.4 Player 2's zoom blur, look-back, marker reprojection: looked at, not done (blur and look-back: done in §29.6)

- **Zoom blur** (directional / radial) and **look-back hiding**: the inputs are per player.
  - The player camera managers are `D_004EE040 + 0x28E00 + i x 0xBB0` [C decomp func_002E6F60].
  - The blur's player block is `gWorld + 0x12AD50 + 0x27D0 x idx` (src/gameview/scene_capture.cpp). The radial blur's parameter
    blocks are graphics manager +0x1D0 / +0x270.
  - The capture holds only player 1's manager and block, so both need a capture change and a recapture.
  - Not small. Player 2's view draws no zoom blur and hides no car.
- **Marker reprojection in split frames**: tried (`hud_project_split`: the view's NDC into its half of the 640 x 448
  space).
  - With the camera exactly the marker's own viewport (tick camera's screen matrix == the marker's captured one, max
    diff 0.000000 at polls 1994 / 2404), the captured world point still landed 0..2.9 px (x) / up to 0.8 px (y) from
    the game's projection for on-screen markers. hud4's one-player rule gives 0.5 px.
  - So the half mapping is not exact. It was backed out: split frames draw markers where the game put them (exact at the
    tick; they follow the 3D only per tick). `hud_project_split` stays as a diagnostic (B3HUD_MARKER_TRACE on offline
    split shots).
  - Kept: every marker, drawn or not, is in frame space with its view (`HudMarker::view` / `pass_oy`, set at its MARKER
    event).

### 29.5 Player 2's streamed track and traffic (split1, 2026-11-11)

**Symptom** [C SCENARIO=multiplayer, be0a317c, shot 17000]: once the two cars are apart, player 2's half shows its car
over the sea (the water plane and the sky; no road, no buildings). Player 1's half is right.

**Why** [C decomp]: the game keeps one streamed track per view. `CB3World` holds two `CB3StreamedTrack`s (0x1CC each)
at gWorld+0x12A348 (track 0) and +0x12A514 (track 1); `func_0019B380` constructs both, `func_0019A550` updates both,
and `CB3World::Render(idx)` 0x276300 draws view idx from `trk = gWorld + 0x12A348 + 0x1CC x idx`: its frustum test
(`func_00301D50(trk)`), `RenderBeforeVehicles(trk)` (units, backdrops, water), `RenderAfterVehicles`,
`RenderAfterSpecular`, `func_0021C010(trk)` (the STATIC +0x28 barriers). `CB3TrafficSystem::Draw(lePlayer)` walks the
same track's safe area and the list of the view's race car (gWorld + 0x12AD50 + 0x27D0 x lePlayer + 0x19AC). The
capture held only track 0 and player 1's list, so view 2 drew player 1's resident units, culled by player 1's
visibility bits (offscreen shots and V in a window: `game_cull`), and player 1's backdrops, water and traffic choice
(the 275 m distance from player 1's camera hid every traffic car near player 2, in every mode).

**Now**:
- Capture (`scene_capture.cpp`, only when Is2PlayerSplitScreen, so one-player records are unchanged): track 1 [0, 0x1CC),
  its resident blocks' headers, its camera unit's block and backdrop list, player 2's race car block +0x18E0 (the
  barrier test) and player 2's race car index +0x27D0 + 0x19AC.
- Reader (`capture.cpp`): `View2In` holds track 1's state as `Tick` holds track 0's (slots, kinds, visibility, backdrops,
  camera unit, the barrier decision through `decode_barriers(..., track 1)`), and the safe-area visibility for traffic.
  `game_draws` makes Draw's decision twice: `Vehicle::cull` for view 1 as before, `Vehicle::cull2` for view 2 from
  player 2's camera, list and track 1.
  The traffic's unit, frustum and camera-inside results in guest memory are the last Draw's, which in a split frame is
  Draw(1) (Render(1) runs after Render(0)), so view 2's decision uses them as they are [C order in RenderTwoPlayer].
- Renderer (`View::split_frame`): view 2 swaps in track 1's slots, visibility, backdrops, camera unit (water) and
  barriers, and `cull2`. View 1 and one-player frames take the same path as before.

**Proofs** (Linux, offscreen, STRICT=1; before = be0a317c, after = split1):
- SCENARIO=multiplayer with SHOTS=17500,...,19500 VBLANKS=40000 (AS/C1_V1, both players accelerating): player 1's half
  is pixel-identical in every split shot (15500 .. 19000); player 2's half equals at 15500, differs by 39 / 126 pixels
  at 16000 / 19000 (cars close together; [L] a traffic car or a unit's visibility) and is redrawn at 17000 .. 18500 (95,941 ..
  129,090 pixels): before, the sea, the sky and a few LOD buildings; after, the road, the kerbs, the buildings and
  street furniture around player 2's car.
- One-player frames: race, pause, photo, takedown, timeattack, crash, worldtour shots equal to be0a317c's (photo's three
  free-camera shots 14050 / 14492 / 14842 move with the wall clock and differ run to run). The one-player path reads and
  draws nothing new (track 1 is grabbed, decoded and swapped in only in split frames).
- In a window with DRAW DISTANCE at the whole track (the default, `--track-units all` without V) the units were already
  all drawn for both views (culled by each view's camera), so there only player 2's backdrops, water, barriers and
  traffic were wrong; offscreen shots, V (game culling) and `--track-units resident` lost the road as above.

**Still not per view** [G]: view 1's traffic unit / frustum results (the guest holds view 2's at poll time, as
before), coronas (the corona submit flags are view 1's), player 2's zoom blur and look-back hiding (§29.4), and the
2D (both HUDs, as §29.2). All but the 2D are per view since split2 (§29.6).


### 29.6 Per-view results, post passes, look-back, coronas, shadows and traffic fade (split2, 2026-11-11)

**What the game does per view** [C RenderTwoPlayer 0x276870 matched: Render(0), then Render(1); CB3World::Render
0x276300 matched; L drafts of CB3TrafficSystem::Draw 0x3078F0, RenderRaceCars 0x199A20, RenderShadows 0x3033F0]:
- `Draw(lePlayer)` writes each listed traffic car's unit +0xCA, frustum +0xCB and camera-inside bit +0x17A and each
  crashed vehicle's +0x13BA / +0x13BB; `RenderRaceCars(view, track)` each listed race car's +0x5A / +0x5B / +0x10A2.
  So at the poll they hold view 2's results; view 1's decisions (`game_draws`, shadows, corona submits) used them.
- `RenderRaceCars` and `RenderShadows(idx)` test the view's own camera manager (`D_004EE040 + 0x28E00 + 0xBB0 idx`,
  IsPlayerVisible: the look-back camera); RenderShadows walks the view's traffic list.
- `OpenViewport(idx + 1)` points D_00666274 at the view's params: graphics manager +0x1D0 for view 1, +0x270 for view 2
  [C func_00227360, case 2]. The radial blur (+0x00), the bloom (+0x50) and UpdateCamera's directional blur state
  (+0x60) are per view, with `D_005179C0 + 0xBB0 idx` and the view's player block (all already in the capture).

**Now** (renderer and capture only; no guest writes):
1. **View 1's results** (item 3 of the split1 report): the native capture watches `CB3World::Render`'s entry
   (gameview_native.cpp, `b3gv::kWorldRender`). At Render(1)'s entry in a split frame, `b3gv::on_world_render` copies
   view 1's per-vehicle bytes above (race cars, active traffic, crashed vehicles, keyed by guest address) and publishes
   them at the next poll as the host record 'HSV1' at 0x41120000 ({'HSV1', poll, entries, 0}, 8 bytes an entry). The
   reader keeps them as `Vehicle::v1_*` when the record is the tick's own poll (`Tick::view1_snap`). View 1's
   traffic decision, its traffic / crashed shadows and its race-car corona submits use them; view 2 keeps the poll's.
   One-player polls publish nothing (the copy is made only when Is2PlayerSplitScreen).
2. **Coronas** (item 1): `Corona::submit2` / `sort2` for view 2: traffic and crashed from `cull2` (Draw(1)'s choice),
   race cars from RenderRaceCars(1, 1)'s rule (+0x27C0 != 1 or player 2's IsPlayerVisible, track 1's unit test, the
   poll's frustum results), sorted by player 2's camera; `listings2` (player 2's list) for the per-entry repeat.
   `View::split_frame` swaps them in for view 2.
3. **Zoom blur and bloom** (item 2): `decode_post(im, t, 1)` and the radial blur block +0x270 fill `View2In::post` /
   `blur`; view 2 draws its own choice (directional / radial / none), amounts and bloom inputs (before: no blur, view 1's
   bloom inputs).
4. **Look-back** (item 2): the capture grabs player 2's camera manager's IsPlayerVisible inputs in split frames;
   `own_hidden2` (+0x1914 == 0, +0x27C0 == 1, player 2 not visible) hides player 2's car in view 2, `Shadow::cast2`
   is RenderShadows(1)'s decision (player 2's list and visibility, the poll's results).
5. **Traffic fade** (item 4): trafficfade.cpp keeps since / removals per view (`within_radius(v, view)` on cull /
   cull2). Player 2's view fades its traffic in and out against its own 275 m radius (`Vehicle::since2` / `gone2`); a
   fading-out copy removed within one view's radius only is hidden in the other view. One-player ticks take the old
   path (same copies, same order).

**Diagnostics**: `B3_SPLIT_TRACE=1` prints per split tick whether view
1's copy is there, how many vehicles' view-1 results differ from the poll's, each view's drawn traffic count and the
vehicles whose lights each view submits.

**Not per view yet** [G]: the 2D (both HUDs in one pass, §29.2), the spark camera history (§19: player 1's), the boost
glow's randomiser state (both views start from the poll's), and the traffic-light / unassigned corona inputs (shared;
their sizes use each view's camera).

### 29.7 The layout choice: side by side (splitlayout1, 2026-11-13)

The setting and the game's own two layouts (CB3GraphicsManagerBase::SetViewportsSplitScreen 0x19E760, [C]) are in
docs/pc-settings.md §7.11; the maths in `src/b3replay/splitlayout.{h,cpp}` (tests/test_split_layout.cpp).

- **Top / bottom** (`View::split_layout` 0, the default): §29.2's path, untouched.
- **Side by side** (1), in `View::split_frame`:
  - **Views**: `ViewPass::rect` = the left / right halves (`split_views`).
  - **Projection**: each view's camera gets the game's side-by-side vertical half extent for the captured camera
    (`split_view_half_h`: t = half_w / 1.35 (1.62 with the 16:9 flag, told apart by the window's 8/3 or 32/9 shape),
    then 0.55 t / (2/3), or 0.75 t / (8/9)). Its horizontal is half_h x the view's shape, so pixels are square. The
    coronas, post passes and `render_aspect` follow from that camera.
  - **Culling**: `draw_list` runs with `game_cull` off. The game's unit visibility and traffic frustum results are for
    its top / bottom frustums, and this view is taller and narrower.
  - **2D**: `hud_list` lays the frame's 2D out at 4:3 over the whole output (no wide shifts), then `split_hud` moves
    each item into its view by a left-multiplied clip-space affine (`split_hud_model`), so a matrix hud_list built
    (hud4's marker scale) is kept:
    - A draw's view is the band its box lies in (rows 0-223 player 1, 224-447 player 2). A draw over both bands is
      the frame's.
    - Its anchor is its HUD component's EHUDAlign point (HudComponent::fx / fy), else its box's third of the band.
      An edge piece (at x 0 or 640) goes to that side, one at both stretches over the view.
    - Its scale is min(out_h / 448, view width / 640): the game's HUD size unless the band's width would not fit.
    - Its scissor is moved the same way and clipped to the view.
    - Markers: the marker's point, through its own view's view-projection, with the game's size rule at that depth
      over this view's focal length.
    - A frame whose 2D is an overlay of the front end's panels (`hud_overlay_panels`: the pause menu, the event intro)
      is laid out as top / bottom does (`hud_list` at the window's aspect), over both views.
  - `hud_list` records each item's draw index (`View::hud_item_draw`), its only change. The top / bottom output is
    byte-identical.
- **Proofs**: docs/pc-settings.md §7.11.
- **Not done [G]**: the game's own side-by-side HUD variant (narrower takedown banner, its 260-unit HUD rects). The
  HUD is the top / bottom one, scaled to fit, because only a game-side flag (+0xF before the HUD's Prepare) would
  give the other, and it would change the game's viewports.

### 29.8 Smooth at high frame rates, and the particle worker's crash (split2p, 2026-11-19)

- **Cadence [C, a paced 2-player race, 6,976 race ticks]**: every frame is 2 ticks (CB3Game +0x2DA84 = 2 for the whole
  event; `CB3TwoPlayerSplitScreenStage::Prepare`, frame-rate.md §1). Each view's camera changes on every second tick (49.2
  % and 48.5 % of ticks), and both players' cars on every tick (99.4 %, 97.1 %). Natively the frame's two polls latch back
  to back: 48.4 % of the latch-stamp gaps are under half a VBlank, and the next pair comes a frame (33 ms) later.
- **Why it was choppy**: the low-latency display clock (clock.h `step_sched`, lead 0) shows the newest tick L fully at
  stamp(L) + margin + 1/60 s. With both ticks of a frame stamped at once, the display reached the newest tick half way
  through each frame, held it, then jumped when the next pair came: 30 steps a second at any frame rate. This hit the
  cameras and cars of both views alike. Each view's own interpolation (`interp_camera` on cam / cam2 between their change
  ticks, cars per tick) was right. Single-player crashes (also 2-tick frames) had the same steps.
- **Fix** (`DisplayClockT::vstamp`): a tick latched less than half a VBlank after the one before is shown one VBlank
  after it (at most 3 ticks back to its frame's first). 1-tick frames are unchanged. It is drawn only.
- **Numbers**: the clock replayed on that run's recorded arrivals and stamps at 190 fps, with positions drawn as
  `frame_at` draws them. Held frames 21.4 % -> 1.8 % (both cameras, both cars); frames whose speed is off the local
  median by over 25 %: 61 % -> 16 %; backward steps 550 -> 118. A single-player race on the same (loaded) machine: 0.4 %
  held, 20 % off. The remaining spread is that machine's pacing jitter in both modes. `tests/test_clock.cpp`
  `schedule_clock_two_tick_frames_one_latch` holds the native model: no held frames, no backward steps.
- **The crash (Windows, ~25 s into a 2-player race)**: `View::swap_view2` `std::swap`s the two views' `Effects` each split
  frame. The particle build (`Effects::draw_particles`, a `std::async` worker) was handed `&particle_vu_`, a pointer into
  the object. After a swap that address held the other view's VU. While the swap ran, its program pointer was moved
  out (null), so `ParticleVu::run`'s `prog_->mp` -> `Vu1::run` faulted (the report's frames, symbolised against an
  unstripped build of the same commit: Vu1::run+0x7c <- ParticleVu::run <- build_particles <- the async state). The VU is
  now a `shared_ptr` the worker holds, so it moves with its view's future.
- The latency log (`--latency-log`) now has each tick's ticks per frame, cameras (view 2's too) and the two players'
  cars on its T rows, and the drawn ones on its F rows.

## 30. Menus at ultrawide: the panel layout, the backdrop's side fill, the boost bar in the takedown camera (fe3, 2026-11-05)

Session: the recorded play session `home1` (in-process window, 2848 x 976, 43,101 polls), captured as `$P/scene/home1-fe3m.b3scene` (movie pictures every 20th poll).

### 30.1 The boost bar during the crash / takedown camera [C]

- **Symptom** (40.webp, home1 poll 30664; roadrage1 8140..8270): in the letterboxed takedown camera the boost bar was
  drawn at its 4:3 place (2D x 26..230) instead of at the window's left edge. It snapped back when the camera returned.
- **Cause**: `CB3HUDBoostBar::UpdateTransition` 0x1A3F40 [C matched], in HUD mode 3, hangs the bar's object (+0x560;
  object +0x38) on the bar's **own second frame** (+0x69C), and back on the component's frame (+0x8) after.
  `CB3HUDBoostBar::Prepare` 0x1A52D0 [C matched] builds that frame under the HUD root at a copy of the component frame's
  place (+0x6A0 = *+0x8; it slides in from +150 in y). The reader resolves an object to a component by its frame chain
  (§17.11), so in those frames the bar (object 0x6675D8, chain 0x6684F0 -> root 0x6682B0) had no component and no
  bottom-left shift. Not the edge vote, not the letterbox.
- **Fix** (`hud.cpp`, OBJECT event): an object of `C2dObjectBoostBar::_Render` 0x1A6290 without a component goes to
  `_gBoostBarComponentP1` / `P2` (0x66A610 / 0x66B7B0) of the HUD whose root frame (the SCENE event's +0x20, now kept as
  `HudComponent::root`) ends its chain. Its 2D x is the same in both modes (26..230), so it now sits exactly where it sits
  in normal driving.
- **Its tail** [C roadrage1 8220]: in mode 3 the bar's background runs to x 726.6. The PS2 clips it at 640, but the wide
  layout widened the scissor, so its 640..727 tail showed in mid-window once the bar moved left. Now a draw that moves
  whole with a side-aligned component (no edge piece, no object anchor, not in a clip region) keeps the PS2's clip on its
  far side, moved with it (`View::hud_list`). Edge pieces and absolute components (the scrolling combo text) keep the
  widened scissor (hud2). The same tail also appears in some normal-driving frames (roadrage1 5820, 7740, 8400, 8420,
  10700, 10720, 12400: one ~85-unit tread block at 2D y 401..427, past x 640 in the bar's own space) and is clipped
  there too. Nothing else in a race frame changes (identity sweeps).

### 30.2 The front end's panel layout (`--fe-layout panels`, the default; `43` = wide1, `edges` = hud2)

wide1 (§27.1) decided per draw by shape. That stretched every textured triangle spanning 0..640: the loading pictures
(2.2 x at 2848 x 976), the Crash Nav zoom's map layers (36.webp's blocky map across the window) and a flying-in text line.
It also extended every untextured fill touching an edge: the map's highlight box (a grey block beside the map) and its
crosshair lines. The prompt bars' corner caps stayed at the 4:3 edge, so each page had a frame corner in mid-window
(35.webp, 39.webp).

fe3 decides per **element class**, by the Gt2d object's render callback (names from the decomp's tables) and by the
texture's content (`hud_frontend_plan`, hud.cpp; census over home1's three front-end stretches, one frame in 25):

| Element | Class / evidence | Wide rule |
|---|---|---|
| Backdrop strips `bg` (vignette), the grid | `CGt2dObjectImage`; roughness 5..8; grid REPEAT | stretch to the window edges (grid keeps its tile size) |
| Prompt bar: strip, line 0..608, corner `small_curve` 608..640 | `CPromptBarPanel2dObject` 0x3D7B40 | strip stretches; the cap (a one-sided textured piece whose object's untextured draw ends where it begins) moves to the window edge, the line's end follows it |
| Heading bar: body 76..640, `big_curve` 0..272 with the logo | `CHeadingBar2dObject` 0x31B130 | body extends right; the curve stays with the logo (roughness 76) |
| Selection bar `FE` | `CB3SelectionBar2dObject` 0x3720F0 | stretches (as wide1) |
| Gradients, panels, a movie's backing | `CB32dObjectGradient`, `CB3Panel2dObject`, untextured | extend (as wide1) |
| Boxes: the map's highlight and crosshair, a sliding panel's borders | `CGt2dObjectBox` 0x1D5EA0 | keep 4:3 |
| Ticker text (clip region 16..623) | `CB3ScrollingText2dObject` 0x31C990 | laid out again over the widened bar (menu-font, below); clip 16 units in from the window's edges; glyphs fade out over the last 48 units at both ends (vertex alpha) |
| Loading bars 140..640 | `CGt2dObjectImage`; roughness 4 | one-sided stretch to the right edge |
| Pictures: `GNRL0004` / `CARS0004` loading pictures, map tiles, zoom layers `SATMAPU_L*` / `USA1` / `US_LSAT`, logos | roughness 43..218 | keep 4:3 and the 4:3 clip |
| Movies | `CGt2dObjectVideoImage` 0x326960 | keep 4:3 (side fill below) |

Roughness (`hud_texture_roughness`, view.cpp) is the 99th percentile of the largest channel change between neighbouring
texel columns over the columns and rows a draw samples. Below 16 (`kFeSmooth`) a span or a one-sided piece stretches:
`bg` 5-8, loading_bars 4, the strips 0. Pictures, shapes and glyphs measure 43-254 (CARS0004 43, USA1 45-75, big_curve
76). An HD pack replacement is measured at the raster's own scale. With the HD pack `bg` is 11-14 (still stretches)
and loading_bars 66 (keeps 4:3). The Crash Nav zoom
and the location page are now consistent: both keep the map 4:3, because the tiles exactly cover 0..640 and there is no
more map to show; the zoom's layers no longer stretch. Text keeps the 4:3 clip everywhere. Untextured draws without an
object (the memory-card dialog's first frames) keep wide1's fill rule.

**The ticker in widescreen (menu-font, 2026-11-11; reported: DRIVER DETAILS > TROPHIES at ~2000 x 1110, the hint cut at
the 4:3 edge in mid-bar).** `CB3ScrollingText2dObject::_Render` (0x31C990) lays its text out itself, word by word, only
over its clip region: cursor x = left - (100 x data +0x04 - 608, wrapped by +0x08) x scale, words that end left of the
strip skipped, none drawn once the cursor passes its right end; scale = kHelpBarRecipSize (0x4C51F0) x the object
space's size; glyphs from the font *D_00665ED0 [C decomp B3HelpBarComponent_0031C990.cpp, the _BuildBatchData draft
0x31C5D0]. So the words outside 16..624 never exist, and widening the clip shows nothing more. The capture records the
render data at the callback (a TICKER event, src/gameview/hud_capture.cpp observe_ticker: the text, the scroll
+0x04, the extent +0x08, the gap +0x0C, kHelpBarRecipSize, the font's raster; reads only), and in the panel layout
`hud_ticker_layout` (hud.cpp) runs the game's layout again over the strip widened by ext each side, its scroll span
608 + 2 ext / scale: the text enters at the window's right end and leaves at its left, at the game's speed (the game's
layout shifted by ext, same period). The draw keeps its object, colour and fade; its clip widens by ext (16 units in
from the window's edges, the PS2's inset from the bar's ends). At ext 0 the layout is the game's draw exactly
(B3_TICKER_CHECK=1: every triangle with an area equal to 1/16 unit, the GS grid, and its UVs equal, at both of the
trophies page's tickers); B3_TICKER_43=1 draws the game's ticker as before. 4:3 output (and race_check's default 640 x
448 shots) is unchanged.

### 30.3 The backdrop's side fill (`--movie-fill blur`, the default; `black` = none, as wide1)

The frame's backdrop is its first movie reaching an edge (all of its draws), else, in frames without a movie, the first
full-width picture (a span kept 4:3 for its texture; not the selection bar in two halves) exactly at 0..640 with some
alpha (the loading pictures). Right after it (so the game's later draws,
the `bg` fades and the grid, go over it as over the picture) each side from the backdrop's edge to the window edge gets a
quad. The quad samples a blurred copy (`fill_blur`: 8 x 8 box average, then two [1 2 1] passes each way; per guest movie
picture buffer, remade when the picture changes; per still texture once). It is mirrored about the backdrop's edge, and the
whole side samples only the outer 96 units (`kFillDepth`): an extension of the edge's colours, not a reflection (the intro
movie's centred EA logo would otherwise appear on both sides). Brightness is 0.8 x the backdrop's vertex colour at the
seam and 0.35 at the window edge, with the backdrop's blend. Movies need captured pictures (`B3PORT_SCENE_MOVIE`, always
on with the live feed). Without them (a plain capture file) the placeholder gets no fill.

### 30.4 Not changed

- Race frames: hud2 / hud3's rules, except §30.1's frames.
- The pause menu over a race (a race frame) keeps hud2's rules.
- 4:3 output (b3replay's default aspect at 640 x 448): front-end and race frames are pixel-identical to main.
- `--size 640x448` is aspect 1.43, not 4:3 (the PS2's 640 x 448 is shown at 4:3), so the wide rules apply there with
  ext 22.9 units.

## 31. Our PC settings in the renderer (settings1, 2026-11-05)

The page, its rows and the proofs: `docs/pc-settings.md` §6. In the renderer:
- **Record.** The settings code publishes our settings as host record HSET at `kHostSetBase` 0x41101000:
  `{u32 'HSET', u32 serial, u32 bytes, 0}`, then the ini text (`include/b3settings/settings.h`). It does so at start and
  after each edit, in the scene poll. `host_settings_decode` reads it per tick into `Tick::host_settings` (shared while
  the serial is unchanged).
- **Window.** The live window applies the latest tick's record at once (`apply_settings` in `run_live_window`). The
  view-level rows go through `apply_view` (pcsettings.cpp); interpolation, road shine, frame cap, fullscreen and the
  FPS counter are the window's own.
- **Offscreen.** Each live offscreen shot applies its own tick's record, so one run can show before and after an edit.
- **Start.** `--settings <ini>` (b3replay and b3render.dll) gives the start values,
  including the device rows that need a restart: MSAA, the texture filter (`b3gfx::Options::point` / `mip_nearest` /
  `anisotropy`; the 2D stage stays bilinear), VSync and the HD pack.
- **Default.** Without `--settings` or a record, everything is as before (0 pixels against main at 5 frames).
- **One flag per effect.** `Effects::shadows / trails / scrapes / sparks / popcorn`, `View::glass_damage` (state 0
  drawn), `View::deform`, `View::car_lod` (3 = highest).
- **Field of view, drawn only.** `View::apply_fov` scales the game camera's view window by
  tan(fov / 2) / tan(45 deg) and moves the eye along its view direction so the chased car keeps its size (rear offset 1.75 m, gap 1.6 m, chase box 2.5-12 m
  ahead, 3 m to the side), on our view, not on the game's camera. The chased car is the nearest race car inside that box [L].
  - Race frames only; front-end frames keep the game's camera.
  - Split screen's second view is not changed [G: left for the split-screen lane].


## 32. The game view: one capture for the native backend (pic1, 2026-11-06)

Goal: when the strict native build reaches its frame loop, our renderer shows its frames, with nothing left to do but
the boot itself. The scene and HUD capture that feeds the renderer (§12, §17, §23) now depends only on a small backend
interface. The native backend calls it at the game functions the capture hooks, and the in-process renderer (b3render)
opens beside it. Tags as above.

### 32.1 The interface (`include/b3gameview/gameview.h`)

- **The capture** is `src/gameview/scene_capture.cpp` and `src/gameview/hud_capture.cpp`: the guest memory is a
  `b3gv::Memory`, the argument registers are `arg[8]` (a0..t3), and the record's schedule stamp and the extra
  publishers are a `b3gv::Host`.
- **Memory**: read-only views of main RAM and the scratchpad. A backend without views passes callbacks.
- **Hook points**:
  - `b3gv::on_entry(pc, arg, mem)` before the first instruction of each function in `b3gv::entry_points()`: 48 with
    the default switches (the six Gt2d entries, CB3HUDManager::Render, DrawPositionMarker, RenderFrontEnd, the object
    render callbacks);
  - `b3gv::on_poll(poll, vblank, mem)` once per retail controller poll;
  - `b3gv::finish()` at the end.
- **Outputs and switches are unchanged**: `B3PORT_SCENE_OUT` / `_LIVE`, `B3PORT_RENDER` (+ `_ARGS`, `_WAIT_MS`), the
  `B3PORT_SCENE_HUD*` / `_MOVIE` / `_FRONTEND` switches. The renderer library loads with `LoadLibrary` (Windows) or
  `dlopen` (Linux, macOS), through the same C ABI.
### 32.2 Native hook points

- **Entries.** `b3host::traced_entry<A>` (every native table entry) gets a watch set beside boot2's oracle hook:
  `g_watch_hook` plus `g_watch_bits`, one bit per 4-byte code address in 0x100000..0x500000, checked only while the hook
  is set (`include/b3host/native_backend.h`). Before a watched function the hook passes `CallArgs` a0..t3 to
  `b3gv::on_entry`. The observer only reads: no guest writes, no cost.
- **The poll** is the return of UpdatePort(0), `UpdatePort__22CGtInputManagerPS2Pad2F15EGtPS2InputPort` 0x214610.
  native_boot's poll point uses the same place. With both on, the latch (oracle hook) runs before the capture (watch
  hook): latch, then scene record [C test_gameview_native]. vblank = `platform::vblank_count()`. The stamp is the host steady clock at
  the poll, because natively the poll is the latch.
- **`native_boot --render offscreen|window [--render-lib L] [--render-args "..."]`** (tools/native/gameview_native.cpp):
  - sets `B3PORT_RENDER` (default `libb3render.so` / `b3render.dll` next to the exe) and `_ARGS` (`--game` default,
    `--offscreen-live build/gameview` for offscreen, `--activate` for a window);
  - installs the watch set and the poll;
  - closes the feed after the boot's reports.
  - Off by default; nothing else changes. Windowed runs are checked by hand.
- **Pacing.** The renderer follows the records' arrival, one per retail poll. Natively they arrive when the frame
  scheduler releases the virtual VBlanks, so the native boot's own pacing paces the picture.
- **One SDL.** Off Windows, `libb3render.so` links SDL3 dynamically, so it shares the process's SDL with the
  standalone's audio / input.
- **Window mode off Windows** (`src/b3replay/inproc.cpp`): the "windows" video driver hint is now Windows only.
  Elsewhere SDL picks the driver itself, and offscreen keeps "offscreen".

### 32.3 Direct links: what the watch hook cannot see (`tools/native/watch_census.py`)

A C++ callee from a shared decomp header that another selected unit defines is linked directly (decomp2host
`call_stubs`), and such a call never passes traced_entry. The census lists the direct links per watched address:
- across units: an undefined symbol (`nm`) in the callers' objects;
- inside the defining unit: a call or jmp relocation against the symbol (`objdump -dr`). Taking the address, as the
  callback registrations do, does not count.

On boot2's generation of 2026-11-06 (753c469, build/gen_native_ps2) [C `build/pic1-census.txt`]:
- **36 of the 48 HUD entries are generated, plus the poll point** (37 of 49 watched).
- Every one is reached through dispatch: Gt2d Begin 9 units, SetTexture 3, Resume 2, Suspend 2, End 9, _Dispatch 12;
  CB3HUDManager::Render 3; UpdatePort 1. The object callbacks are called through the scene's function pointers.
- **One direct link**: `CGt2dObjectImage::_Render` 0x1D5FB0 is called directly from `Gt2dObjectImage_001D60B0`
  (same source file). Those calls would miss their OBJECT event. It is covered by a watch prologue (below).
- The 12 not generated yet are object callbacks without C: 0x1A5740, 0x1A6290, 0x31C990, 0x33ED30, 0x371BE0, the
  globe 0x3B2EE0, 0x3D6050, 0x381030, 0x381AB0, 0x3D62A0, 0x3D6C30, 0x3CD920.
- Rerun the census whenever the selection changes:
  `python3 -I tools/native/watch_census.py --gen build/gen_native --obj build/gen_native_ps2`. A direct link that is
  not covered by a prologue goes into config/native_watch.txt.

**The watch prologue (boot2's option A, agreed with gen1 on the decomp2host touch points).**
- **Generator.** decomp2host reads `config/native_watch.txt` (`DECOMP2HOST_WATCH=<file>` instead). For each listed
  function a selected unit defines, it puts `b3host::watch_self<ADDR>(this, args...)` first in the definition
  (`watch_prologues`). Arguments go as the EE holds them: references by address, pointers, scalars, floats. A struct by
  value is refused. It also writes the list as `b3port/native_watch.h`.
- **Run time.** `b3host::watched<A>()` is false at compile time for listed functions, so traced_entry leaves them to the
  prologue and each call is reported once, dispatched or direct. The prologue reports the entry only. It reads its
  arguments, converts host pointers with `try_guest_address` (0 outside the windows, never a bounce copy), and adds no
  cost.
- **Proof** [C]:
  - Generation of 0x1D5FB0 + 0x1D60B0 with no list, an empty list and main's unmodified generator: identical output.
  - With the list: the only differences are the include, the prologue line `watch_self<0x001D5FB0u>(&space, data)` and
    native_watch.h; the unit compiles with the native flags.
  - tests/test_decomp2host.py has two new cases.
  - test_gameview_native calls `watch_self` with arena pointers: an OBJECT event with the guest addresses; an unwatched
    address gives nothing.

### 32.4 Proofs

**`gameview_replay`** (tools/native/gameview_replay.cpp; `make gameview`; Windows: tools/render/build_win.sh):
- **How it drives the capture.**
  - The .b3scene (.gz read directly) is applied record by record to a 32 MB arena, as the native backend's RAM.
  - `b3gv::on_poll` then runs on it with a plain RAM view, so the capture re-reads the same addresses through the same
    pointers and writes its own record.
  - The HUD part of a record comes from entry observations, which a file cannot replay. It is always the record's tail,
    so it is passed through at the HUD's publish point (`set_hud_publish_override`).
- **Checks.** Per poll, the re-captured scene runs equal the original record's leading runs byte for byte. The whole
  output file equals the input.
- **One derived value.** The capture does not record one value it reads: each skid-mark emitter's +0x8. The emitter is
  captured only when another field changed, while func_002BD560 swaps +0x8 / +0xC every frame; the reader derives it
  (§14.4). The replay derives it the same way before each poll and restores it after. Without this, race2 differed at
  80 polls from poll 2844. So §12.1's "the image holds exactly the bytes the hook read" has this one exception.
- `--speed S` releases the polls by their VBlanks at S x real time, as a frame scheduler does. The live renderer needs
  a paced feed: each shot warms the bloom over 24 frames, and at 10x the feed outran the shots in race2.

| Run [C logs in the pic1 scratch, numbers here] | Scene part per poll | Output vs input | Renderer (`--verify`, shots vs `b3replay --offscreen` on the original, 640x448) |
|---|---|---|---|
| race2, Linux, no renderer | 5,181 of 5,181 identical | IDENTICAL (29,496,560 bytes) | - |
| roadrage1, Linux, no renderer | 13,474 of 13,474 | IDENTICAL (138,764,160 bytes) | - |
| race2, Linux, libb3render.so (dlopen), Vulkan offscreen, `--speed 2` | 5,181 of 5,181 | IDENTICAL | 5,181 ticks identical, 0 dropped; shots 1000 (front end), 2500, 3000, 3600+0.5, 4300+0.5, 4800, 5100 (front end): **0 pixels differ in each** |
| roadrage1, Linux, the same | 13,474 of 13,474 | IDENTICAL | 13,474 ticks identical; shots 4000, 6000+0.5, 9000, 12000+0.5, 13000: **0 pixels differ in each** |
| race2, Windows (gameview_replay.exe + b3render.dll via LoadLibrary, quietrun, `--speed 2`) | 5,181 of 5,181 | IDENTICAL | 5,181 ticks identical; the same 7 shots against that build's b3replay.exe: **0 pixels differ in each** |

**`test_gameview_native`** (in `make test`): the native hook path without the generated table or a renderer.
- Calls go through `traced_entry` as table entries do (Gt2d Begin, SetTexture with a0 / a1, another function,
  UpdatePort(0), UpdatePort(1), UpdatePort(0)), with boot2's oracle hook on UpdatePort.
- **Order.** The hooks fire in the expected order, latch before capture.
- **Records.** Only UpdatePort(0) makes a record (polls 1 and 2, VBlanks from the backend). Poll 1 carries the HUD
  region with the Begin and Texture events and SetTexture's registers. 17 checks, 0 failures.

**Not done yet.** native_boot with `--render` compiles. Linking it against boot2's generated set waits for the native
boot to reach UpdatePort in its main loop (boot2 is in the menu Prepare start-up now).

### 32.5 For boot2's frame loop

When the native boot reaches UpdatePort in its main loop, run
`native_boot --render offscreen --render-args "--shots P,..."` (offscreen, shots in build/gameview).
- The scene part needs nothing else.
- The HUD needs the not-yet-generated object callbacks above (their objects are counted as `unhooked`, and those
  objects go undrawn), and the one direct link fixed.
- The capture reads only RAM at retail addresses, so anything the native boot computes differently shows as a
  different picture, never as a crash in the capture.

### 32.6 native_play: the native game, interactive (play1, 2026-11-07)

`tools/native/native_play.cpp` (`make play`: `build/native_play` + `build/libb3render.so`) is the playable shell
around boot2's backend. native_boot stays the headless, strict proof tool; native_play is a separate program with
native_boot's setup (table, ELF check, hardware model, RW Audio IOP, fallback on unless `--strict`, crt0 steps).
nofallback1 (2026-11-14): the fallback (the R5900 interpreter `tests/r5900_mini.h` running retail code,
`tools/native/native_fallback.h`) is compiled into native_play only with `make NATIVE_FALLBACK=1` (developer and
diagnostic builds: `--explore-retail` / `--explore-movies` / `--explore-return`, `B3NATIVE_DIFF`); the default and every
release build have none (`nm build/native_play | grep -i r5900` is empty) and log `fallback: none (compiled out)`. A call
to a function with no native version then takes the stop path, as `--strict` does in a fallback build: the log line
`native_play: STOPPED: the game called <address> <name>, ...`, the window held with the message and F10's bug report
until it is closed (`b3r_game_stopped`), a message box, and the run left marked as not ended cleanly in
`run_state.ini` (the launcher's "Last run", safe mode after two). `B3NATIVE_TEST_MISSING=<hex>` switches a native
function off to test it.

    build/native_play --game <disc folder> [--offscreen] [--hz 59.94 | --no-pace] [--quit-after S]
                      [--keys V:SC:D,...] [--render-args "<b3replay live options>"] [--strict]

- **Threads.** The renderer runs on the process main thread: b3render's `b3r_open_main` + `b3r_run_main`, the
  in-process renderer on the caller's thread, because macOS gives windows to the main thread only. The boot runs on
  a worker thread with a 16 MB stack. The window opens at start: the renderer is opened before the boot, so
  `b3r_open` is now idempotent and the capture's open at the first poll is a no-op.
- **Per virtual VBlank** (`platform::add_vblank_hook`: after the INTC handlers, right before `sif::pad_vblank()`):
  - **pace:** the VBlank waits for its wall-clock time, 59.94 Hz (`--hz`). A host that falls more than 250 ms
    behind resyncs (counted) instead of bursting. The frame rate stays uncapped in the renderer, which interpolates
    between the per-poll records (§13 / §20).
  - **pad:** `b3r_pad_sample` → `sif::set_pad_sample(0, ...)`.
  - **end:** the window's close request, or `--quit-after`.
- **The pad** (`b3replay --native-pad`, src/b3replay/nativepad.*). The live window's keys (src/native/input_keys:
  default table, ramped steering; docs/mac.md "Keys") and every SDL gamepad become one
  DS2 report. The viewer's keys need Ctrl / Cmd. `--keys` scripts keys through the same path
  (`b3r_pad_key`), for headless checks.
- **The end.** Closing the window, `--quit-after` and `B3NATIVE_MAX_VBLANKS` all end the same way: main's call
  unwinds, the feed is closed, the renderer ends, exit 0. The summary lists VBlanks per wall second, resyncs, pad
  reports set (not neutral, changes, the first bytes) and the fallback count (`fallback: none (compiled out)` in a
  build without it).
- **Sound** (boot2's `install_rwa_iop(b3audio::AudioSink*)`, src/native/audio_sink.h). The platform mixes RW Audio
  on virtual IOP time (SPU2-equivalent mixer, 800 frames per VBlank) and pushes to native_play's sink. The sink hands
  the bytes to b3render (`b3r_audio_push`), whose SDL plays them through audio1's AudioDevice (drift guard 60 ms ±
  100 ms; `b3replay --native-audio`, window mode only; `--no-audio` turns it off). Offscreen runs mix the same and stay
  silent.
  - Linux window path (SDL offscreen video, dummy audio), 30 s: 1,424,800 frames mixed and pushed, 0 dropped / 0
    padded, 11,702 audible (peak 2759).
  - Windows offscreen, 20 s: 957,600 frames, 11,702 audible, peak 2759, as on Linux.
- **Not yet:** port 1.
- **Windows** (win1): `OUT=<dir> RENDER=1 tools/native/build_win.sh` builds native_boot.exe and native_play.exe with
  the verified MinGW toolchain, plus b3render.dll / SDL3.dll beside them. The source lists and compile flags come from
  the Makefile. Runs go through perf's quietrun.exe (job object, no window, no dialogs):
  - `native_boot.exe` (`B3NATIVE_MAX_VBLANKS=600`) matches the Linux build of the same sources and generation:
    208,457,156 dispatch calls, identical per-function counts and stub log;
  - `native_play.exe --offscreen --quit-after 15 --keys ...`: 900 VBlanks in 15.01 s (0 resyncs); 123 non-neutral
    pad reports, as on Linux; exit 0.

## 33. Crash frame rate, low-latency motion, the finish line, widescreen race overlays (perf1, 2026-11-06)

Sessions: the recorded play sessions `fri1` (LATENCY=low, 60,573 polls, F1 race on EU/C3_V1 from poll 41736) and `fri2` (LATENCY=smooth, 11,566 polls, the same race), both in
the in-process window at 2848 x 976. Captures: `$P/scene/fri1-inproc-perf1a.b3scene` (main's capture, with movie
pictures: 6.9 GB), `$P/scene/fri1-perf1.b3scene` and `fri2-perf1.b3scene` (perf1's capture: the finish-line fields).

### 33.1 Tools

- `b3replay <capture> --profile <csv> A:B[:N] --size WxH [--msaa N] [--polls ...]`: every tick of polls A..B shown at N
  display times (alpha k / N), each frame built as the window builds it and drawn offscreen and waited for (no read-back).
  Per frame: CPU (lists, with their uploads) and GPU (render + submit + fence) ms, items, indexed draws, uploads and KB
  (`Device::take_stats`, new), buffer grows, vehicles, 2D draws, the effects' counts, the blur / bloom state, the camera,
  and draw_list's stages (static, vehicles, effects, game data, popcorn, coronas, sparks; `View::prof_on`), post and 2D.
  `B3PROFILE_NODRAW=1`: lists and cameras only (the cameras per tick for b3latsim / motion.py).
- `tools/render/b3latsim.cpp` (`make render`): clock.h's step_sched replayed on a run's ticks (the latency log's T rows,
  or `--session <frames.csv> <pads.log>`: a windowed session's own latch / hand-over times), for several leads, clamps and
  correction blends, scored against the true camera path: display speed, extrapolation error, per-frame snaps, velocity
  discontinuities (the change of the camera's velocity from frame to frame less the true path's), latch age. Checked
  against run A's own frames: the replayed display position equals the logged one at p50, p99 0.88 ticks.
- `motion.py <lat-render.csv> <cams.csv> A B`: the same scores for what a run's renderer drew (the
  latency log's F rows carry the drawn camera since perf1).
- frames.csv (pace1's per-vblank log) has four more columns: `hostwait_us` / `hostwait_top` (the host waits since the
  previous line, every site but the pacing ones, and the largest site), `hook_us` (the poll's scene
  hand-over) and `late_us` (wait end -> latch: the late pump, key pipe and pad latch). frametimes.py reads columns by name.
- `tools/render/build_win.sh` compiles at most `JOBS` (4) files at a time.

### 33.2 The crash frame rate: spark quads rebuilt every displayed frame [C]

fri1's F1 race, 2848 x 976, MSAA 4, `--profile` per frame (4 per tick), Windows / NVIDIA, CPU + GPU ms p50 / p95 (fps):

| fri1 polls | before | after | spark quads (max) |
|---|---|---|---|
| 43750-44100 (crash) | 14.60 / 22.32 (69 / 45) | 2.34 / 4.99 (427 / 200) | 24,223 |
| 46500-47100 (crash) | 23.80 / 87.07 (42 / 11) | 2.43 / 6.85 (412 / 146) | 54,006 |
| 48150-48800 (crash) | 16.57 / 39.33 (60 / 25) | 2.66 / 7.88 (376 / 127) | 34,746 |
| 51100-51450 (crash) | 16.07 / 20.39 (62 / 49) | 2.27 / 5.12 (440 / 195) | 14,521 |
| 45000-45400 (driving) | 2.97 / 4.09 (336 / 245) | 1.41 / 3.36 (709 / 297) | 2,538 |

- Cause: `build_sparks` (fx2, §19.2) runs the spark VU program's math (bit-exact PS2 floats) for every spark of every ring
  and interval, ~1.7 µs a quad, and the result (up to 54,000 quads, 10 MB of vertices) was rebuilt and uploaded for every
  displayed frame: CPU p50 17.6 ms, max 134 ms in sparks alone at 46500..47100; GPU p50 4.5 ms (the upload). Everything
  else in those frames: popcorn 1.0 ms, the other effects 0.5 ms, the rest < 0.1 ms. The in-process bench of run A (fri1
  replayed headless, 2848 x 976 with read-back) showed the same stretches at 14-53 fps against 143 on average.
- The spark quads depend only on the tick (its contact effects, camera history and camera record), not on the display
  time. Now (`Effects::draw_sparks`): built and uploaded once per tick (the key holds the tick's objects, so no address is
  reused while it is cached), the work split into units (one batch of one bank of one array) built on up to 8 threads and
  appended in draw order, and `spark_project` without the GS screen XYZ the renderer never used (four divides a quad).
  Worst rebuild frame 10 ms (was 65-89 ms at the same polls). In the live window (`Effects::async_sparks`) a new tick's
  build runs on a worker and the last finished build is drawn until it is ready (at most a tick late): at 46500..47100
  CPU p95 6.33 -> 2.57 ms, rebuild frames 9.7-10.2 -> 2.2-2.8 ms (`B3PROFILE_ASYNC=1` with `--profile`). Offline replays,
  the bench and shots build synchronously.
- Identity: 15 of 15 crash frames at fri1 46700..46980 (up to 54,000 quads, threads in use) byte-identical to main's
  b3replay.exe (`--offscreen --sweep`, 1280 x 720).

### 33.3 Low-latency motion: what made it "framey" [C measured, L the reading]

Measured on fri1's own windowed timeline (frames.csv + pads.log: latch = the line's time, hand-over = latch - wait) with
b3latsim at a steady 300 fps, polls 41736..60573 (96,656 frames). "Velocity jumps": the change of the shown camera's
velocity from one frame to the next less the true path's, over 10 m/s (a visible step at 300 fps).

| variant | display speed off > 25 % | camera error p50 / p99 | velocity jumps > 10 m/s | latch age p50 |
|---|---|---|---|---|
| **lat1 as shipped** (lead 1, poses held past the newest tick) | 1.51 % | 153 / 1,355 mm | **26,942 (28 %)** | 5.7 ms |
| lead 1, extrapolated | 1.51 % | 0.4 / 62 mm | 1,775 | 5.7 ms |
| lead 1, extrapolated, blend 16 ms (sim only; off by default, see below) | 1.51 % | 5.3 / 159 mm | 438 (0.45 %) | 5.7 ms |
| lead 0.5, extrapolated | 1.53 % | 0 / 14 mm | 829 | 15.2 ms |
| smooth (before lat1) | 0.63 % | 0 / 0 mm | 0 | 26.2 ms |

1. **The extrapolation never moved anything** (the main cause). `lerp(Mat34)` returned `b` for t >= 1, so every frame
   past the newest tick (lat1's lead: much of each tick at lead 1) showed the newest tick's camera and cars held, and the
   motion jumped when the next tick came: steps at the tick rate with uneven holds, at any frame rate. Seen in the
   renderer's own log (fri2 run E, the drawn camera per frame): at newest 5000, frac 0.053, the camera was tick 4999's;
   past the newest tick it never moved. Now `lerp` extrapolates for t in (1, 3] (offline frames never pass 1, so replays,
   shots and captures are unchanged); deformation zones stay clamped (no crumple overshoot). lat1's latency figures stand:
   the newest tick was shown fully on time; only the motion past it was frozen.
2. **Extrapolation corrections.** The game's chase camera is not linear from tick to tick: a tick's linear extrapolation
   misses by 4.4 mm median, 2.75 cm p90, 23.6 cm p99 (fri1's race), 17 % of ticks by more than 2 cm. A correction blend
   (`MotionBlend`, live.cpp, `--blend MS`: the difference between the pose computed with the old ticks and with the new one
   kept as a rigid correction per object, the camera and each car, decaying to the identity) helped in the sim but made the
   renderer's motion worse (table below: corrections piled up in the 2-tick stretches, which the position-only sim does not
   model), so it is **off by default** and kept as an option.
3. **2-tick frames.** Where the game runs 2-tick frames (8 % of fri1's race vblanks and 15 % of fri2's had no new
   picture) its camera changes every second tick, and `interp_camera` blends between those keys. Live, the next key is
   often not there yet, and the camera held at the last key (fri2 run E: polls 5001 / 5003 drawn at the key for the whole
   tick; smooth mode too, less often). Now, live only (`frame_at(..., live)`), it continues from the last two keys at their
   spacing for at most one key interval.
4. The FOV setting was not applied to extrapolated frames (`LiveScene::frame`). Fixed.
**Measured in the renderer** (fri2 replayed headless with the in-process bench at 1280 x 720, ~190 fps, LAT_LOG=1; the
drawn camera per frame against the captured path, `motion.py`, polls 2700..11380; all four REPLAY OK 11,566 / 11,566):

| run | camera error p50 / p99 | velocity jumps > 2 m/s / > 10 m/s | latch -> frame done p50 |
|---|---|---|---|
| lat1 as shipped (run E lowb0: blend 0, before the lerp fix = main's rendering) | 111 / 1,466 mm | 76.2 % / 69.7 % | 15.5 ms |
| main's DLL (run H) | (no camera in its log) | | 14.9 ms |
| **perf1, low (extrapolation, camera keys)** | 0.6 / 294 mm | 17.9 % / **4.6 %** | 14.6 ms |
| perf1, low, blend 16 ms | 13 / 1,021 mm | 33.5 % / 22.2 % | 14.1 ms |
| perf1, smooth | 0.08 / 6.7 mm | 1.0 % / 0.4 % | 45.5 ms |

5. The display clock itself is steady in both modes (speed p5..p95 0.992..1.002 low, 0.990..1.009 smooth).
6. Sim notes: run A's renderer was a read-back bench at ~150 fps (a tick waited 6 ms p50, 34 ms p99 between arrival and
   drain), so the steady-rate rows take a tick from its arrival; replayed at run A's own frame times the sim's display
   position equals the logged one (p50 0, p99 0.88 ticks). The sim models positions only (linear between ticks).

### 33.4 The game-side stalls in fri1 [C not reproduced; open]

fri1 (windowed, low) has 15 vblank intervals over 40 ms: 4 at boot (fri2 has the same), 49 ms at the first race's load,
275 ms before segment 2, and two clusters in the F1 race at polls 42661..42767 (139, 146, 170 ms) and 56946..57381
(183, 150, 137, 272, 138, 152 ms), each a single interval in steady 16.7 ms frames with 8-9 ms of slack, wait 0 (the poll
came late: the time went before the poll, in the guest or a host wait). Within each cluster they repeat ~93 vblanks apart.
- The headless replay of fri1 with LATENCY=low, the in-process bench and the capture (run A) has none at those polls
  (3 intervals over 40 ms in 70,410, all at loads): not the guest's work.
- Earlier sessions: home1 / inproc2 (smooth) none after boot.
- Not the EA TRAX song change (no EA TRAX draws at 42600..42700 or 56900..57000).
- The new frames.csv columns name the host-wait site and the poll's hand-over and late-input times per vblank, so the
  next windowed session shows where such a stall goes (frametimes.py and a look at the lines over 40 ms).

### 33.5 The finish line [C asm, matched units]

"The finish line thing" at the end of the F1 race is the overhead FINISH sign (a "FINISH" panel with a down arrow), which
the native renderer never drew (§12.9 "not done").
- `CB3World::Render` calls `CB3GameData::Draw(view)` 0x14E8C0 after the fog. With s1 = the stage logic's
  `GetCheckpointMarkerFlags` (vtable +0x44: 3 for races, 0x12 Burning Lap, 9 Lap Eliminator, 0 Road Rage and online single
  races [C ELF]) and the view's race car block gWorld+0x12AD50 + 0x27D0 view (+0x10D4 the lap, from 0; +0x1384 the laps;
  +0x139C the split checkpoints passed in this lap):
  - start lines (s1 & 1) only while lap < 0, through `CB3StartFinishLine::Draw` 0x15E720, which returns when its +0xC is 0:
    `CB3GameData::Prepare` passes false for start lines, so they never draw;
  - finish lines (CB3GameData +0x3A8, 2 x 0x10, count track data +0x3F0) when s1 & 2, +0x139C > 0 and lap >= laps - 1
    (the last lap after its first split), or with s1 & 0x18 from lap 0 as "check" until the last lap; none in crash mode;
  - split checkpoints (s1 & 4: no logic sets it).
- `CB3SplitTimeCheckpoint::DrawCheckpoint` 0x15E340: B3Im3d, one tristrip of 4 vertices, side = normalise(up x the
  trigger's direction) (vopmula / vopmsub order read from the asm), corners pos + (0, 11, 0) +- up x D_004E1568 (5.0) +-
  side x D_004E156C (10.0), UVs (0, 0) (1, 0) (0, 1) (1, 1), colour (0.5, 0.5, 0.5, 1) x 128, alpha test 0.25. Textures
  "finish" (D_004B7108) / "check" (D_004B7100) from the global dictionary (GLOBAL.TXD). Drawn as the crash ramps (track
  type 0, post-fog, Z written, no cull), first among CB3GameData::Draw's draws.
- Capture (no version change): each start / finish line's and split checkpoint's trigger struct
  (0x20), D_004E1568 [0, 8), race car blocks 0 / 1 +0x10D4, +0x1384, +0x139C, +0x18E0, the logic's vtable +0x44 and its
  function's first 0x24 bytes. Older captures draw no finish line (`finish_known` false).
- fri1 (perf1 capture): finish drawn at polls 54046..56717 (the last lap from its first split to the line), at
  (-3186.5, -26.9, 1545.5) facing (0.76, -0.04, 0.65). fri2 never reaches its last lap (the run was quit on lap 2), navproof14
  (Burning Lap: +0x1384 and +0x139C stay 0) draws none, as the game.
- Pictures (`%B3_WIN%\perf1\pictures\56690-on.png`,
  `-off.png`): the FINISH panel and its arrow over the road, text readable (not mirrored).
- Tests: `capture_finish_line_on_the_last_lap`, `capture_finish_line_needs_its_inputs` (test_capture). `--no-finish`
  leaves it out; `b3replay --info` lists the polls.

### 33.6 Widescreen race overlays: the EA TRAX panel, the pause menu [C draws, L layout choice]

- **EA TRAX**: its draw (texture "EATrax", 2D x 26..426, y 373..433) is not a HUD component object; the reader credits it
  to the last object event, the LAP counter's text (component 0x66A5B0, align 7, top right), so hud2 shifted it right by
  the extension: mid-right in wide windows (fri2 3000). It now goes with the left edge, its 4:3 place relative to it (the
  PS2 draws it over the boost bar's corner, as here). Its text (artist, title, album) is drawn and moves with the
  panel (eatrax1, 2026-11-13, native_play offscreen, race and music scenarios at 640 x 448, 1280 x 720, 1680 x 720 and
  1920 x 1080: the panel's font draws are in the race's 2D capture, polls ~11000..11280). The game's callback
  func_00439620 draws the text only at the panel's full width: it fades in after the slide-in and out before the
  slide-out [C decomp draft: text alpha 4 (t - 1) for t 1..1.25]. So a shot taken during either slide shows the panel
  without text, as on the PS2 (the empty panels in earlier shots were such frames).
- **Overlays over a race** (`hud_overlay_panels`: a CHeadingBar2dObject 0x31B130 or CPromptBarPanel2dObject 0x3D7B40 and
  no draw in a HUD component: the pause menu, its quit question, the event intro, the results): fe3's panel plan was tried
  and left the heading bar's big_curve in mid-window; hud2's bands and caps stay, and the content goes to its side
  (`hud_overlay_shifts`): the heading band's title (PAUSED, ROAD RAGE RESULTS) and the prompt row (Start Resume, Change,
  Select) each move as a group to the side of their centre; the menu column (entirely left of x 280) goes with the left
  edge unless a CB3RoundedPanel2dObject dialog drawn after it covers it (the quit question: the PS2 shows the column
  dimmed under it); the selected row's CB3SelectionBar2dObject bar has its vertices at the 4:3 edges at the window edges and
  its brackets with the column; centred content (the dialog, the event's text, the results' stat panels) stays.
- **Stat labels on their plates** (1.2.1, the known issue "Takedowns label at 32:9 on the record screen"): a race's
  results stats page draws each stat as a plate (func_003CD920, `hud_element01`, 2D x 80..560) with its label on it
  (CGt2dObjectItalicText 0x33CCD0 at x 110: BEST LAP, TAKEDOWNS, LONGEST DRIFT, ...) and its value and RECORD tag at
  the plate's right end [C crashnav's ELIMINATOR results, poll 17300]. The labels lie left of x 280, so the column rule
  sent them to the window's left edge while their plates stayed centred: a label just off its plate at 16:9, a
  window's width away from it at 32:9. A draw lying inside such a plate's box (2 units of slack) is now the plate's and
  stays with it; it is not counted for the column either. The CONTINUE / RETRY page (plates at 260..600, labels at
  290) is unchanged: its column still goes left. Test `race_results_stat_labels_stay_on_their_plates` (test_hud).
- **Identity** (b3replay --offscreen, main 5509737's b3replay.exe): 4:3 (default size) race2 2229..5181 and roadrage1
  3600..13400 every 20th poll: 587 of 587 byte-identical. 712 x 244: race2 111 of 111; roadrage1 457 of 476, the other 19
  being the event intro (3600..3720, 3840..3880) and the results (12920..13100), as intended.
- Test `race_overlay_panels_shift_by_side` (test_hud).
### 33.7 The sky with a free camera looking straight up (perf1, 2026-11-07)

A report (b3replay on macOS, roadrage1 poll 3807 +0.45, "Get Ready!", free camera pitched up): a thin ring around
the screen centre, blue glow blobs and stretched clouds. Reproduced offscreen (`--free-eye -4053.6,-25.2,262.6 --look-at
-4053.5,75,262.7`); the same picture 400 m higher (the sky is camera-centred, not track geometry). Taken apart with
`--no-blur`, `--no-coronas`, `--no-sun`, `--no-bloom` and `B3SKY_SKIP=<bits>` (new diagnostic: leaves sky items out, bit
0 / 1 the dome below / above, 2 the clouds):
- **The ring: the game's.** The cloud layer (sky component 2) is a dome open at the top: e 0..0.75, i.e. it ends at
  height 0.9375, radius 0.4375, 65 degrees up [C maDefinitions 0x4B9CA0]; its top edge samples the panorama's top row
  (v = 1 - h / 0.9375, clamped). The dome under it (components 0 / 1, e 0..1) is closed. Gone with the clouds left out.
  The game's chase camera never looks that high.
- **The stretched clouds: the game's** panorama mapping, pinching towards that open top (in the clouds alone too). Kept.
- **The blue blobs: ours.** The bloom's kept picture (§25) came from the game camera's frames, and while paused it is
  frozen (no renders since the last frame), so the free view showed the game view's bright spots as glows. Gone with
  `--no-bloom`.
- **Fix, free-camera views only** (`View::free_view`: b3replay's window with the camera detached, `--offscreen --free-eye`):
  the cloud layer is drawn with its alpha falling to 0 over its top three rings (e 0.5625..0.75, 50 to 65 degrees up;
  `Scene::cloud_free`), so its open edge no longer shows; and the bloom is the free view's own steady state (accumulation
  over 60 renders of this frame: the old picture x keep^60 < 0.7 %), started again when the camera reattaches.
- **Game camera unchanged**: roadrage1 3600..13100 every 100th poll, 96 of 96 frames byte-identical to main (57aa8e5's
  b3replay.exe) at 640 x 448 and 96 of 96 at 1280 x 720.
- Pictures (`%B3_WIN%\perf1\pictures\`): `sky-up-3807-before.png` / `-after.png` (1600 x 900), and the
  diagnostics `sky-up-3807-diag-nobloom.png` (blobs gone), `-diag-noclouds.png` (ring and clouds gone), `-diag-cloudsonly.png`
  (the ring is the cloud layer's edge).

### 33.8 Banding in the sky gradient: 8-bit output steps, an optional dither (perf1, 2026-11-07)

The concentric rings left in §33.7's after picture are not the dome's facets: along a ray from the zenith (blue channel,
1600 x 900) every band edge is a step of exactly one 8-bit level (136 -> 146 over 420 px, plateaus ~25 px), the same with the
radial blur, bloom and clouds off. So it is the final 8-bit quantisation of a very shallow, smooth gradient, and it shows in
game views too (sky gradients at high resolution). The scene target is not the cause: an 8-bit half-intensity target
would give 2-level steps after the present's x 255/128. Option `--dither` (b3replay offline and live;
`b3gfx::Options::dither`): a 4 x 4 ordered Bayer dither of +-1/2 output step in the present pass. Off by default: without
it roadrage1 3600..13100 (every 100th poll) is byte-identical to main at 640 x 448 and 1280 x 720 (192 of 192). With it, the
8 x 8 local mean along the ray has 11 flat 4-pixel steps of 99 (50 of 99 without): the bands become a smooth ramp.
Pictures: `%B3_WIN%\perf1\pictures\sky-up-3807-bands-plain.png` / `-bands-dither.png`. PC SETTINGS:
GRAPHICS > IMAGE > DITHER (off / on, off by default; ini key `dither`, kept in the player's settings file; no preset sets it;
applies at once in the window through `Device::set_dither`; `--dither` forces it on). Test `dither_row` (test_settings).

## 34. Frame timing capture (perf3, 2026-11-16)

A profile that works on any platform, so a frame-rate report from the Mac comes with where the time went. Header only:
`include/b3gfx/perf.h`. Off, it costs one relaxed load of a bool per call site (no clock reads, no memory traffic); on, a scope costs two
steady-clock reads and an add, the samples sit in fixed ring buffers (no allocation per frame), and a line is written once a second.

**Turning it on.** `B3_PERF_LOG=<file>` in the environment of the game (`native_play`) or of `b3replay --live`; nothing else (no
settings row, not in the F9 console). In a window it times every frame; offscreen only the `--bench WxH` frames
(`RENDER_EXTRA="--bench 1600x900"` for `race_check.sh`). The file starts with a `# b3perf` header (driver, MSAA, present mode, GPU) and
a column list; then one line per second: time, frames, fps, size, and for each stage `avg / p95 / max` in ms, then the second's
totals of draws, render passes, command-buffer submits, uploads and upload bytes.
`tools/native/perf_summary.py <log> [<log2>]` prints one table (two logs: before / after).

**Stages** (frame stages are summed over the frame and exclusive: a scope inside another is taken out of it, so they add up to
`frame` minus the code no scope covers: events, the pad, the title string):

| stage | what |
|---|---|
| `frame` | wall time from one frame's end to the next's (the rate) |
| `wait_swapchain` | `SDL_WaitForGPUSwapchain` (the low-latency modes, top of the loop) |
| `feed_drain`, `scene_update` | the game's records decoded; scene state, models, geometry uploads |
| `drawlist` | the draw list built from the tick on screen (view, HUD, panels, banners) up to the draw call |
| `upload`, `upload_submit` | `update_*` / `create_*`: the CPU copy and the copy pass's recording; the upload command buffers' own submit |
| `cmd_scene`, `cmd_post`, `cmd_overlay`, `cmd_present` | command recording of the scene pass (3D draws, sun, pass splits), the post passes, the 2D stage, the present pass |
| `acquire` | `SDL_WaitAndAcquireGPUSwapchainTexture`: waits for a free drawable |
| `submit` | `SDL_SubmitGPUCommandBuffer` of the frame |
| `fence` | `SDL_WaitForGPUFences` (offscreen draws and read-backs only) |
| `limiter` | the frame limiter's deliberate wait (FRAME RATE LIMIT, battery saver) |
| `game_step` | game thread: the emulated game between two VBlank hooks (EE, VU, the game's code), without the pacing wait |
| `game_pace` | the pacing wait (idle: the game is faster than real time) |
| `game_poll`, `game_handover` | the per-poll scene hook (guest reads, the record) and its hand-over to the renderer; one sample per poll |

Reading it: a GPU-bound frame shows its wait in `acquire` or `submit` (the CPU ran ahead and blocks); a CPU-bound one has them near
zero and `drawlist` / `cmd_*` / `upload` large. `game_step` against 16.7 ms is the game thread's headroom; its p95 and max are the stalls.
The draws / passes / submits / uploads counters are the per-frame work independent of the machine.

### 34.1 What the first captures showed (and one change tried)

Measured on the Linux software path (SDL_GPU on Vulkan with a software rasteriser: the GPU numbers mean nothing, the CPU ones and
the counters do), a paced race (`PACE_FROM`, `--bench 480x270`), steady seconds only:

- the game thread has headroom: `game_step` 6.4 ms mean (p95 of the seconds 7.5 ms) against a 16.7 ms VBlank, `game_poll` 0.23 ms,
  the hand-over 5 us;
- the render thread's CPU per frame: `drawlist` ~0.8 ms, `cmd_scene` ~1.1 ms for ~1100 draws (about 1 us a draw: the pipeline,
  buffer and sampler binds are already skipped when unchanged), `feed_drain` ~0.3 ms per game record (a frame that drains
  several records pays all of them at once), uploads ~1 ms with spikes of 10-20 ms where a texture is created;
- per frame: ~15 render passes, ~10 uploads of ~100-300 KB in all, and one command-buffer submit
  per upload, ~14 submits a frame.

Tried and rejected: the frame's dynamic uploads (`update_vertex_buffer` / `update_index_buffer` / `update_texture`) batched into one
staging transfer buffer, one copy pass and one command buffer per frame (submits per race frame 14 -> 3.5; the CPU gain on the
software path was small, `upload_submit` 0.20 -> 0.12 ms). The golden `crashnav` tour then differed in one shot (`live_11000`:
the showroom car's body reflection); flushing after each buffer update brought the golden hash back, so the order or batching
of the buffer copies matters to that picture and the cause was not isolated. A change that cannot be shown identical stays out.
The finding is worth a look on its own: something in the front end depends on when a dynamic buffer's copy reaches the GPU.

Looked at and left alone: the seven-phase item loop in `render_scene` (a few thousand predicate tests a frame, microseconds);
the per-draw pipeline `std::map` lookups (tens of ns against ~1 us a draw); render-pass load / store actions and the post
chain's targets (GPU side: nothing to measure here, left for a Mac capture); `create_texture` (a command buffer, a blit and a
mip chain each: seconds of loading on the software path, but only at track / car loads).

### 34.2 Asking for a Mac capture

`B3_PERF_LOG=$HOME/b3perf.txt` before starting the game (any settings; race a lap or two, then quit). Send the file. In it:
`acquire` or `submit` large with `cmd_*` small = GPU-bound (the swings are the GPU: look at which passes the settings add);
`drawlist` / `cmd_scene` / `upload` large = the render thread; `game_step` p95 / max near 16 ms = the game thread; `frame` p95 and
max against its mean show the swings, and the stage that moves with them is the cause.

### 34.3 The render-cost guard (perf_check)

`tools/render/perf_check.sh [native_play] [out dir]` plays the race and the crash scenario of `race_check.sh` offscreen with
`B3_PERF_LOG` on and `--bench 1280x360 --msaa 8`, once with the defaults (`base`) and once per feature switched off (`no-bloom`,
`no-reflections`, `no-particles`, `no-coronas`, `no-blur`, and `bloom-split`, the old MSAA bloom form). Each run prints one row:
fps, the render thread's CPU ms per frame (drawlist, cmd_scene, cmd_post, cmd_overlay, upload), and draws, render passes,
submits, uploads and upload KB per frame. A feature costs what its row differs from `base` by: CPU ms is real, and passes and
upload KB are what the GPU pays for (the offscreen GPU here is a software rasteriser, so its ms mean nothing). `SCENES`,
`VARIANTS`, `SIZE`, `MSAA` select; `BUDGET_MS=<ms>` makes `perf_check: FAIL` (exit 1) if the base row's CPU sum is over it.
Run it before shipping a render feature: a new effect must leave the base row's CPU, pass count and upload KB near where they
were (the budget is "close to nothing at default settings"), and a new switch belongs in its variant list.

### 34.4 Upload batching, DrawItem size (perf4)

- A frame's buffer uploads (`update_vertex_buffer` / `update_index_buffer`) share one staging buffer, one copy pass and one command
  buffer. Submits per race frame 12.6 -> 2.2 (5120 x 1440, real GPU). The 34.1 attempt changed the crashnav showroom reflection
  because the open batch was submitted after the command buffers of later `create_texture` / `make_buffer` / `update_texture`
  calls; those now submit the open batch first, so the GPU work keeps the call order, and every golden shot is unchanged. Also
  tried and rejected: using the batch's command buffer as the frame's own (one submit fewer): `carselect` shots differed.
- `DrawItem` holds its `CarParams` (~700 bytes of zone matrices) by pointer (`car()` / `car_mut()`, copy on write): a track item is
  ~250 bytes instead of ~1 KB (test_panel_epoch).
- Measured (Windows, RTX 4080 SUPER, offscreen bench with read-back, race, 5120 x 1440 MSAA 8; before = cdaec9e1): racing fps
  78-84 -> 85-90, particles off 98 -> 111; CPU p50 (drain + frame + draw list) 3.4 -> 2.55 ms, particles off 1.95 -> 1.20 ms;
  upload_submit 0.72 -> 0.03 ms. The particle layer's per-tick rebuild is still the largest CPU item (~2.2 ms per rebuilt tick).
