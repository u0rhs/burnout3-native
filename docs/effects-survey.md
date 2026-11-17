# Effects survey for milestone 2: skid marks, particles, debris, water, shadows, glass, coronas, post, HUD (props1, 2026-11-03)

Desk study for milestone 2 ("the whole game drawn through our native renderer instead of the GS stream"): for every
effect family, which Criterion renderer draws it, which VU1 program (if any) it runs, what the native renderer must read
from guest memory per tick, and a size. Nothing was run (no game or replay). Sources: decomp
`../burnout3-retail-decomp` HEAD fd4f39a072 (read only): matched `src/b3/...`, `src/nonmatching/...` drafts, Ghidra
drafts `build/vt/ghidra/<ADDR>.c`, asm `assembly/asm/...`; names only from `research/retail_names.csv` (C names),
`research/retail_data_names.csv` (data names), `research/E_dnat_symbols.csv` / `research/proto/new_names.csv` (E map,
[L] for retail); data tables read from the retail ELF `orig/SLUS_210.50` (byte dumps, nothing copied); texture names
checked against the disc's `DATA/GLOBAL.TXD` (grep). Tags: **[C]** seen in code/data (address, file:line, dump),
**[L]** likely (inference from code), **[G]** guess/estimate. Sizes are working days [G].

## Summary and recommended order

All game effects except the post passes and the sun corona are owned by one object, the effects manager at
**0x1D9D200** (E map `gEffectsManager`, [L]), which holds 3 corona arrays, 4 debris arrays, 24 particle arrays, 4
popcorn arrays, 6 spark arrays, the trail system (= **skid marks**: its only caller is `UpdateWheelEffects`) and the
scrape system, plus a bump-allocated memory block at 0x1DBCA80 for the per-element buffers [C layout from
`RenderPreFogEffects` / `RenderPostFogEffects` / `Construct`, below]. Almost all effect state is **CPU-resident and
capturable**: debris, skid marks, scrapes, shadows and coronas are positioned on the EE; particles, sparks and popcorn
store only spawn records (position, velocity, spawn time, packed parameters) and the VU1 program evaluates their motion
and fade from the current time, so those three VU programs' math has to be ported (the records themselves are in RAM).
Because particles, sparks, popcorn and debris are closed-form in time, the native renderer can evaluate them at the
exact display time, which makes them interpolate correctly at any frame rate. Shadows are not
"render-state packets": they are 8-vertex `B3Im3d` strips with the GLOBAL.TXD texture "blobbyshadow". The trails
program 0x4771B0 is the skid-mark program. Water draws track meshes with material type 7 forced at runtime (program
0x47E300), so it was never seen in the track data.

| # | Family | Size [G] | Why at this point |
|---|---|---|---|
| 0 | Shared: effects capture module, GS ALPHA/TEST -> blend-state table, GLOBAL.TXD effect textures, a billboard/ribbon pipeline | 3-4 d | everything below depends on it |
| 1 | Car shadows (race cars, traffic, crashed vehicles) | 2-3 d | visible under every car in every frame; without them cars float; all inputs CPU-side |
| 2 | Skid marks (trail system, VU 0x4771B0) | 4-5 d | the most frequent effect in a race (every drift and brake); 15 s persistence |
| 3 | Sparks + popcorn (VU 0x46D090, 0x47C6F0) | 4-6 d | every wall scrape, side swipe and takedown; small programs |
| 4 | Coronas: car lights, boost glow (VU 0x478260) | 3-4 d | tail and head lights on every car, boost flame; night routes (**done, corona1**: native-renderer.md §16) |
| 5 | Particles: tyre smoke, dust, gravel, snow, glass shards, fire, explosions (VU 0x46D750) | 8-10 d | biggest program and parameter set; crashes look empty without it |
| 6 | Debris (VU 0x475910) + scrapes (B3Im3d) | 3-4 d | crash-mode detail; mostly CPU, cheap once 0-1 exist |
| 7 | Glass: car glass states (render5 car path) + shards (= particles 5) | 1 d | a texture-slot swap; shards come with step 5 |
| 8 | Water (track material type 7, VU 0x47E300) | 4-5 d | a few routes only; program not read yet |
| 9 | Sun corona + lens flare + occlusion | 3-5 d | depends on a depth-based occlusion design |
| 10 | Post: bloom, radial blur (boost), directional blur, explosion flash, contrast filter | 6-9 d | GS framebuffer tricks to re-derive (native-renderer.md §8) |
| HUD | 2D/HUD/text/menus/movies (Gt2dRenderer API) | 10-15 d + movies 4-5 d | parallel lane from day 1: needed to play at all; independent of 3D |

**Done (fx1, 2026-11-03; docs/native-renderer.md §14):** item 1 (car shadows) and item 2 (skid marks), both from the
game's own code on captured inputs, and of item 0 the capture module (capture version 3), the GS ALPHA -> blend
translation for the effects stage (0x44, 0x48, FIX; Z GEQUAL without write; RGB-only writes), GLOBAL.TXD textures by
name and per-frame strip geometry; billboards / point sprites (sparks, coronas, particles) are not built yet.

**Done (fx2, 2026-11-04; docs/native-renderer.md §19):** item 3 (sparks, VU1 0x46D090, and popcorn, VU1 0x47C6F0: both
programs as bit-exact C++ checked in the interpreter on captured state) and the scrapes of item 6 (CB3ScrapeRenderer's
B3Im3d strips), from capture blocks detected by presence. Corrections: the scrapes (and popcorn) are drawn only while the
view's race car +0x18EA is set (the crash); spark streaks span the 18-frame camera history, and only their first
49 ms is visible (fxspark's last column has alpha 0 under CLAMP).

Total 3D effects + post ~45-55 working days [G] (native-renderer.md §7.2 rows 6-7 estimated 6-9 weeks); the HUD lane
in parallel. Details and reasons in §5.

## 1. Where the effects live and when they draw

### 1.1 The effects manager (0x1D9D200) [C offsets, L meanings]

| Offset | What | Evidence |
|---|---|---|
| +0x0C | `CB3CoronaDynamicArray[3]`, 0x34 each: 0 "coronaglow" (126 slots), 1 "coronaboost" (72), 2 "coronaboostred" (72) | `RenderPostFogEffects` 0x261E00 (`B3EffectsManager_00261E00.cpp`); `_gaCoronaParams` 0x4BC900 dump [C] |
| +0xB0 | `CB3DebrisArray[4]`, 0x30 each (geometries 0..3) | `RenderPreFogEffects` 0x2620C0 (`B3EffectsManager_002620C0.cpp`), `Construct` 0x262CD0 |
| +0x170 | `CB3ParticleArray[24]`, 0x150 each (`EParticleArrayID` 0..23) | 0x261E00 class layout; `Construct` |
| +0x20F0 | `CB3PopcornArray[4]`, 0x40 each | 0x261E00 |
| +0x21F0 | `CB3SparkArray[6]`, 0xA8 each | 0x261E00 |
| +0x2600 | `CB3TrailSystem` (skid marks), up to +0x14638 | `Construct` (`field_2600.Construct()`), `func_002BD560` |
| +0x16C80 | `CB3ScrapeSystem` | `Construct`, 0x2620C0 |
| +0x1F7A0 | 6 axis vectors (+-x, +-y, +-z) | `Construct` |
| +0x1F800 | particle lighting vector (CGtV4d), passed to every `SetLighting` | 0x261E00 |
| +0x1F810 | written per frame by `func_002622A0` before the corona arrays' `BeginUpdate` | `build/vt/ghidra/002622A0.c` |
| +0x1F830 | `CGtTexture*[3]` corona textures (from `_gaCoronaParams` names) | 0x261E00; `Prepare` draft 0x262770 :86-96 |
| +0x1F848 | float passed to debris render and the corona set-up (effects clock [G]) | 0x2620C0, 0x261E00 |

Element buffers come from `CB3EffectsManager::Alloc` 0x2630F0, a bump allocator over **D_01DBCA80** (offset in
`_gnEffectsMemoryAllocation` 0x4E1980, reset to 0 in `Construct` and set to -1 at its end) [C matched]. The E map names
the block `_gaEffectsMemoryBlock`, 0x7D000 bytes, right after `gEffectsManager` (0x1FA80) [L]; retail's manager ends
where the block starts (0x1D9D200 + 0x1F880 = 0x1DBCA80) [C arithmetic]; in retail's .bss listing the next named
object is `gMenuPages` at 0x1E25E80 (`assembly/asm/data/game/unit_00254C90.bss.s`), so the block is at most 0x69400
bytes [C], smaller than E's. The capture should follow the element pointers rather than copy the block (§4).

Clock: `D_00522680` = `gWorld`+0x20 is "now" and `D_0052267C` = `gWorld`+0x1C the frame step, as `AddTrailSegment`
uses them [C `B3TrailSystem_002BD320.cpp`]; debris, sparks and popcorn compare spawn times against "now".

### 1.2 Frame order (`CB3World::Render` 0x276300, `src/b3/_unclassified/B3WorldPS2_00276300.cpp`) [C calls]

| Line | Call | Effects drawn |
|---|---|---|
| 150 | `func_002622A0` (unnamed) | `BeginUpdate` of the 3 corona arrays for this player |
| 153-155 | `Draw__16CB3TrafficSystem`, `RenderRaceCars` | coronas are **submitted while cars draw**: `CB3RaceCar::Draw` -> `ActuallyRenderRaceCarImmediateEffects` 0x2621D0 (`RenderLightCoronas` if light bits +0x58; `func_00257230` unless crashed, pushes into the corona arrays: boost glow [G]), traffic -> `RenderTrafficImmediateEffects` 0x2621A0 |
| 156 | `EndCoronaSubmit` 0x262240 | closes the 3 arrays (zeros unused slots' w) |
| 163 | `CB3ShadowMgr::RenderShadows` (0x1E86D70) | car / traffic / crashed-vehicle shadows |
| 164 | `RenderPreFogEffects` 0x2620C0 | debris (4 arrays), skid marks (`func_002BD560`), scrapes (if player flag +0x1CBA) |
| 167 | `CB3Fog::Render` | fog post pass (fogs everything above) |
| 171 | `RenderPostFogEffects` 0x261E00 | popcorn (crash only), particles (24), coronas (3), sparks (6), explosion full-screen flash (crash) |
| 219-221 | `SunOcclusionTest`, `CB3Bloom::Render`, `RenderSunCorona` | sun occlusion, bloom, sun corona/flare |
| 226 / 232 | `CB3DirectionalBlur::Render` / `CB3RadialBlur::Render` | motion blur / boost blur |
| -- | `CB3OnePlayerStage::Render` 0x134A20 -> `CB3HUDManager::Render` 0x1A0790 (instance 0x66A3D0) | HUD after the 3D, in the full-screen viewport [C `B3OnePlayerStage_00134A20.cpp:60-90`] |

Pass enable bytes in the graphics manager: effects `D_00665F2C`, shadows `D_00665F27`, HUD `D_00665F2B`, water
`D_00665F1D` [C reads; L meaning].

## 2. The VU programs (correction of native-renderer.md §2.2)

The E map's `.vutext` lists one `.dsm` source per program; in address order with sizes taken to the next file, the
first eleven equal the retail images in size and order [C `E_dnat_symbols.csv` vs
`assembly/assets/vu/`], so their names are [L] for retail:

| Retail image (size) | E source (size) | Uploader [C] | Unit |
|---|---|---|---|
| 0x46D090 (1728) | `Spark.dsm` (1728) | `CB3SparkRenderer::EndRender` 0x2636B0 | VU1 |
| 0x46D750 (6528) | `particles.dsm` (6528) | `CB3ParticleRenderer::EndRender` 0x254580 | VU1 |
| 0x475910 (2080) | `debris.dsm` (2080) | `CB3DebrisRenderer::UploadCodeAndCameraData` 0x2F3B60 | VU1 |
| 0x4771B0 (4272) | `B3TrailRenderer.dsm` (4240) | `CB3TrailRenderer::BeginRender` 0x2F38B0 | VU1 |
| 0x478260 (1392) | `Corona.dsm` (1920) | `CB3CoronaRenderer::UploadCodeAndStaticData` 0x306E80 | VU1 |
| 0x47A060 (9872) | `B3Im3d.dsm` (9936) | `B3Im3d::BeginRendering` 0x365640 | VU1 (shadows, scrapes, debug, wrong-way walls) |
| 0x47C6F0 (7184 image) | `Popcorn.dsm` (1728) + `G3_Gen_ADCWater.dsm` (5456) | `CB3PopcornArray::Render` 0x365920 (first program only) | VU1 |
| 0x47E300 (5072) | `G3_Gen_ADCReflect.dsm` (5072) | `ReallySetMaterial` 0x26B2F0, material type 7 = water | VU1 |
| 0x479610 (2640) | `B3InstanceVU0.dsm` (2640) | `BeginInstanceRender` via `CGtVu0CodeManager::UploadVu0Code` | **VU0** |

- **0x47C6F0 holds two programs** [C bytes]: its DMA tag at +0 is `ret` with qwc 0x6B (108 qw = 1728 B, popcorn), and a
  second `ret` tag at +1728 with qwc 0x154 (5456 B), exactly the E size of `G3_Gen_ADCWater.dsm`. No code references
  0x47CCB0 (asm grep) [C], so that water program is unused in retail [L].
- **Type 7 is water at runtime**: `CB3StaticTrack::RenderWater` 0x21D550 (matched,
  `B3StaticTrackPS2_0021D550.cpp:154-180`) saves the mesh's material type, sets it to **7**, draws with `RenderMesh`,
  restores it; it also writes the water level to `D_01E32ADC` (track renderer +0xCC) [C]. So
  track-format.md's "type 7 / 0x47E300: not used by any track" is true of the files only.
- **0x479610 is VU0**, like 0x47F6D0 (car-shading.md §1), 0x4787D0 and 0x4839E0, so the game has **18 VU1 images**
  (19 programs with the unused water one) and 4 VU0 programs, not "20 VU1 + 2 VU0" [C uploaders].
- Effects use 6 VU1 programs (sparks, particles, debris, trails, coronas, popcorn) plus `B3Im3d`. Every program's
  per-frame input is a DMA chain built in RAM, so the same bytes are readable by the capture [L].

## 3. Families

Each table: functions (retail address, name, where the code is: **M** matched `src/b3`, **N** `src/nonmatching`,
**G** Ghidra draft only), VU program, capture inputs, where the motion is computed, size.

### 3.1 Shadows (car, traffic, crashed vehicles, front end)

| Item | Content |
|---|---|
| Code | `CB3ShadowMgr::RenderShadows(int)` 0x3033F0 N/G; `RenderAmbientShadow` 0x303170 N; `RenderFrontEndShadow` 0x303750 N/G; `CB3ShadowRenderer::BeginShadowRendering` 0x303EC0 M, `BeginShadowPlaneRendering` 0x303C10 M, `RenderAmbientVehicleShadow` 0x303A40 N, `EndShadowPlaneRendering` 0x3039A0 M, `EndShadowRendering` 0x303EB0 M |
| VU | none of its own: `B3Im3d::RenderTristrip(matrix, verts, 8)` -> VU1 0x47A060 [C 0x303A40] |
| What it draws [C] | per vehicle one **8-vertex tristrip** (3 bands front / wheelbase / rear: V = 0, 0.1875, 0.8125, 1; U 0/1) in a shadow frame on the ground: up = ground normal, right = normal x forward, at = right x normal, origin = car position lowered by the ground distance; corners from the vehicle data box (+0xE80 / +0xE90, y = 0) and the wheelbase (z of wheels 1 and n-2), scaled by 1 + 0.4 x (distance - wheel radius); y = 0.06 above the ground. Vertex RGB 0x80, alpha = lerp(0, 90, darkness) x opacity, opacity = 1 - min((distance - wheel radius) / 1.25, 1) (none at <= 0), darkness = min(1, `D_004E0FC0` x essential +0x60) |
| GS state [C 0x303C10] | texture GLOBAL.TXD "blobbyshadow" (`D_004C4550`), ALPHA 0x44 (Cs-Cd)As+Cd, TEST 0x50003 (Z GEQUAL), ZBUF with ZMSK (no Z write), FRAME with FBMSK 0xFF000000 (frame alpha = the specular mask, native-renderer.md §11.1, kept) |
| Capture | race cars: essential params (captured) + **car +0x510** (ground normal xyz, distance w), +0x18E8 drawn, +0x1914, `IsPlayerVisible` for the player car; vehicle data +0x18 (wheel radius), +0xE80/+0xE90, wheel table (captured file data); traffic: vehicle +0x70 essential, **+0x80** ground vector, distance from the object at `gWorld+0x4DE0+0x343F0`[+0x176 x 4] +0x18, active list `gWorld+0x4DE0+0x35C24` (count +0x36350), skip flags +0x174 bit 2 / +0x17A bit 1 / +0xCB; crashed vehicles: list `gWorld+0x122A10`, +0x1360 essential, **+0x1870** ground vector, +0x13BB [C RenderShadows draft] |
| Computed on | CPU only (all inputs per tick) [C] |
| Size / deps | **S, 2-3 d**: the extra vectors in the capture, a textured strip with the blend table; interpolates with the car matrix. Front-end shadow (showroom) later with the front end |
| Done (fx1) | `b3replay::shadow_strip` (src/b3replay/effects.cpp), capture version 3; native-renderer.md §14.3. Correction: the traffic "ground vector" vehicle +0x80 is EssentialParams +0x10, i.e. the vehicle's own up row (RenderShadows passes it as the normal) [C draft]; the player car's `IsPlayerVisible` 0x2E7340 is evaluated from its captured inputs (flags +0xB80, the behaviour's elapsed time) [C matched]. race2: 14.3 strips per tick (max 24), roadrage1 4.7 (max 10) |

### 3.2 Skid marks (CB3TrailSystem / CB3TrailRenderer)

| Item | Content |
|---|---|
| Code | `CB3TrailSystem::AddTrailSegment` 0x2BD320 M (`B3TrailSystem_002BD320.cpp`), `CB3TrailEmitter::AddTrailSegment` 0x2BCF60 N, `func_002BD560` M (per-frame render + buffer swap; E-map order suggests `CB3TrailSystem::Render`, unnamed in retail tables), Update 0x2BD6A0 (E-map name [L]), Prepare/Release/Construct 0x2BD7F0/0x2BD7E0/0x2BD9A0 M; `CB3TrailRenderer::BeginRender` 0x2F38B0 N, `Render` 0x2F3600 M, `EndRender` 0x2F3870 M |
| Caller | **only** `CB3EffectsManager::UpdateWheelEffects` 0x2603F0 (N) calls `CB3TrailSystem::AddTrailSegment` [C jal scan of `assembly/asm`]: type 2 on surfaces 4/14, type 3 on surface 11, type 1 for the wheel slip states 2-6 [L draft notes]. So "trails" are tyre marks; fire/dust "trails" are particles |
| Parameters [C dump 0x49FFF0, `CB3TrailSystem::mgaDefaultParams`, 5 x 0x50] | name "fxskid" for types 0-3 (GLOBAL.TXD); +0x20 ALPHA, +0x24.. floats: type 1 ALPHA 0x44, (0.25, 0.25), colour (0,0,0,64) -> (0,0,0,0), 15; type 2 (0.15, 0.15), (10,10,10,64) -> (10,10,10,0), 15; type 3 (0.25, 0.25), (89,89,89,32) -> (89,89,89,0), 15; type 0 ALPHA 0x48 (additive), widths 0.25 -> 0.0 (fx1-trailvu: not constant), (96,96,96,64) -> (32,32,32,32), 0.3 (no caller found); type 4 empty. Meanings (width, start/end colour, lifetime in s) [L] |
| State [C] | trail system (manager +0x2600): emitters 128 x 0x14 at +0x13000 (free list +0x13A00, count +0x13A04); per type a list of 128 emitter pointers at +0x13C0C + type x 0x208, count at +0x200 of it; emitter +0x0 last time, +0x4 params, +0x8 data pointer (uncached alias, `& ~0x30000000`), +0x10 s8 point count (<= 16), +0x11 type, +0x12 flags; data = 16 x V4-32 (point xyz, time w) + 16 x 3 s8 (direction) = 0x130; two 0x9800 halves at +0 / +0x9800 copied each frame (`func_002F34C0`), half index at +0x14634 (128 x 0x130 = 0x9800: the emitter data lives in these halves; fx1 [C]: `func_002BD560` swaps every emitter's +0x8 / +0xC after rendering and copies the drawn half into the other, and +0x8 = half +0x14634 x 0x9800 + the emitter's slot x 0x130 for 177,412 of 177,412 listed emitters of race2); DMA params per type `D_01E865E0` (E `CB3TrailRenderer::maDMAParams`) |
| VU | 0x4771B0 (4272): per emitter UNPACK V4-32 x16 + V3-8 x16, ITOP = point count, MSCNT; texture via the cache, ALPHA FIX from params +0x20 [C 0x2F3600]. Builds the ribbon from points + direction, fades by age, clips (E labels `_$trailsLoop`, `_$TSETrueClipper*`) [L] |
| Computed on | positions on the CPU; ribbon expansion and fade on VU1 -> port 0x4771B0's math (one read) |
| VU 0x4771B0 read (fx1-trailvu) [C] | `include/b3asset/trailshade.h`: rows 1008 = (w0/2, (w1-w0)/life/2, 1/life), 1009 start colour, 1010 end - start (`CB3TrailRenderer::Construct` 0x2F3A60); per point a = min((now - t)/life, 1), half width w0/2/127 + rate/127 x a times the s8 side vector, colour lerp by a (FTOI0), s = k & 1, t = 0/1 per edge (perspective ST/Q); one TRISTRIP +e0 -e0 +e1 -e1 ... (PRIM 0x5C, no fog bit); point 0 always zero width (ftoi0 where the loop uses itof0); trailing points not newer than point 0 dropped; dead points at a = 1; the clipper (`TSETrueClipper`) is ordinary clip-space clipping. Checked bit-exact against the interpreter on synthetic and on every captured emitter of race2 / roadrage1 (`b3vucheck_trail`, native-renderer.md §14.4). TEST_1 is never set by the trail code |
| Done (fx1) | drawn by `b3replay::Effects` (src/b3replay/effects.cpp) from capture version 3, at the displayed time; race2 67.8 drawn emitters / 858 points per tick, roadrage1 97.3 / 489 |
| Size / deps | **M, 4-5 d** (read + interpreter check of 0x4771B0 like render3's checks, capture, ribbon shader). Pre-fog: needs the fog pass order |

### 3.3 Sparks

| Item | Content |
|---|---|
| Code | `CB3SparkArray::SpawnBouncySpark` 0x263C40 G; `CSparkBank::Construct` 0x264170 N, `Prepare` 0x2640E0 M; `CB3SparkRenderer::BeginRender(int,bool)` 0x2638C0 M, `RenderSparkArray` 0x2637F0 M, `UploadStaticDataAndCallCode` 0x263420 G, `CSparkFrameData::Set` 0x263120 N, `EndRender` 0x2636B0 M, `SetRenderstates`/`RestoreRenderstates` M. Spawners: `func_0025DF30` (E map `SpawnSparksFromPoint` [L]), `func_0025D930` (burst along a segment, N), `DoCrazySparksWithWorld` 0x255EE0, `HandleBattleSparks` 0x2612A0 (N) |
| Parameters [C dump 0x4C7710, `CB3SparkArrayBase::mgaArrayParams`, 6 x 124 B] | texture "fxspark" (+0x68) for all six; lifetimes +0x40..+0x4C, gravity +0x50, bounce factor +0x54, motion-blur time +0x5C [L from use in 0x263C40 / 0x2637F0] |
| State | spark array (0xA8): normal bank at +0, crash bank at +0x4C, +0x44 / +0x90 last spawn times, +0x98 params [C 0x2637F0]; bank ring: base +0, capacity +4, write index +8; records at base + (i/4) x 0x90 + (i%4) x 0x20 (+0x10 position, +0x20 velocity, after the bounce solve) [C 0x263C40 draft] |
| Computed on | **VU1** (0x46D090, 1728 B): the spawn solves one ground bounce on the CPU (time and velocity after it) and the program evaluates the ballistic path from "now", drawn as motion-blurred streaks (`mrMaxMotionBlurTime` 0x4E2A08) [L] |
| Size / deps | **M, 3-4 d** (small program; closed form in time) |

### 3.4 Popcorn (crash-mode sparks and chips)

| Item | Content |
|---|---|
| Code | `CB3PopcornArray::SpawnBouncyPopcorn` 0x365E80 N, `Render` 0x365920 N, Construct/Prepare M; `CB3PopcornRenderer::BeginRender`/`RenderPopcornArray`/`EndRender` 0x365910/0x3658E0/0x365890 M. Spawner `func_0025D430` (E map `SpawnPopcornFromPoint` [L]) |
| Parameters [C dump 0x4A4DB0, 4 x 80 B] | 0 "fxpopcornspark" (900), 1-3 "fxpopcorndebris" (900, 300, 900); blend modes `_gu64PopcornBlendModes` 0x4C7A20 |
| When | drawn only when the player's crash flag (`gWorld` + p x 0x27D0 + 0x12C63A) is set [C 0x261E00] |
| GS / VU | 0x47C6F0 (first 1728 B); TEX0 / TEST 0x53001 (alpha test NEVER + RGB only: no Z or alpha write, Z GEQUAL) / CLAMP; view rows with z-clip terms, time, view window, two colours [C `Render__15CB3PopcornArrayFv.cpp:87-102`] |
| Computed on | VU1 from spawn records, like sparks [L] |
| Size / deps | **S-M, 2 d** after sparks (same shape) |

### 3.5 Particles: tyre smoke, dust, gravel, snow, glass shards, fire, explosions

| Item | Content |
|---|---|
| Code | `CB3ParticleArray::Construct` 0x255240 M, `Prepare` 0x254F20 M, `BeginUpdate`/`EndUpdate` M, `SetLighting` 0x254D50 G, `CB3ParticleBank::Construct` 0x2558A0 G, `::Update` 0x2556F0 M; `CB3ParticleRenderer::BeginRender` 0x254A50 M, `RenderParticleArray` 0x254970 M, `DispatchArray` 0x254340 G, `EndRender` 0x254580 N. Spawn: `func_00254A90` N (one particle; DNAT hint `SpawnParticle` [G]); showers `SpawnParticleShowerFromPoint` 0x25E410 N, along a line `func_0025F0C0` N; users `UpdateWheelEffects`, `UpdateExhaustSmoke` 0x260210, `DoVehicleFire`/`Steam`/`Glass` 0x25FB60/0x25FCF0/0x25FE70, `HandleBattleSmoke` 0x261870, `UpdateVehicleFireTrail`, `UpdateBodyPartDustTrail` |
| Arrays [C dump 0x49C570, `CB3ParticleArrayBase::maStandardParams`, 24 x 128 B: texture, normal / crash capacity, render class +0x8] | 0-1 fxdebris1/2 (100/200), 2 fxgravel (60/0), 3 fxsnow (100/0), 4-5 fxglass (180/540, 80/160), 6-13 fxsmoke (8 arrays, up to 240/240), 14-15, 17-18, 20, 22 fxexplosionsmoke, 16 / 19 fxfire, 21 fxexplosionfire, 23 fxexplosionflash (0/50). Total capacity ~2,080 normal + ~3,550 crash particles [C sum] |
| State [C offsets, L meanings] | array +0xE4 params, +0xE8 normal bank, +0x114 crash bank (0x2C each), per-array colours/curves +0x0..+0x60 (from params x 0.5 or 0.25 scale), +0x70 render class, +0xB0 ALPHA (0x44 etc. by params +0xC) [0x255240]. Bank: +0 / +4 pre-built DMA chains (one per 60 particles), +8 V4-32 records (position xyz, spawn time w), +0xC V4-16 (scaled velocity, one parameter), +0x10 V4-8 (four packed parameters), +0x14 capacity, +0x18 write index, +0x1C newest spawn time per 60-block, +0x24 lifetime [C 0x254A90, 0x2558A0, 0x2556F0] |
| VU | 0x46D750 (6528 B, the largest effect program): `DispatchArray` DMA-calls each bank's chain with the array's parameter rows, view rows and time; textures per array collected in `EndRender` (`CB3TextureCollectionPS2`) [C/G drafts]; lighting from `SetLighting(manager +0x1F800, -sun direction)` [C 0x261E00] |
| Computed on | **VU1**: motion, size, colour and alpha over age from the spawn record and the array curves [L]; the CPU only spawns and retires 60-blocks |
| Size / deps | **L, 8-10 d**: read the program (6 render classes from +0x70), parameter rows, lighting, crash banks, soft sort/blend per array; interpreter check against captured banks |

### 3.6 Debris (CB3DebrisArray / CB3DebrisRenderer)

| Item | Content |
|---|---|
| Code | `CB3DebrisArray::SpawnDebris` 0x2F46F0 N, `Construct`/`ConstructGeometryData`/`UploadStaticDataAndGeometry` 0x2F49A0/0x2F4A20/0x2F4650 M; `CB3DebrisRenderer::BeginRender` 0x2F4600 M, `RenderDebrisArray(CB3DebrisArray*, float)` 0x2F4320 G, `DispatchBuffer` 0x2F3C80 N, `UploadCodeAndCameraData` 0x2F3B60 M, `EndRender` 0x2F42C0 M. Only spawner: `func_0025CD60` (unnamed, in the collision-effects block; `jal SpawnDebris` at 0x25D3A4) [C jal scan] |
| Data [C dumps] | `CB3DebrisArrayBase::maGeometries` 0x4C41F0: 4 tiny meshes (9/16, 6/6, 14/36, 6/6 counts + two .rodata pointers each); `kHalfDebrisAcceleration` 0x4C4230; `_gaDebrisArrayParams` 0x4C4250 |
| State [C 0x2F4320 draft] | array +0x14 element pointer, +0x18 count, +0x20 size scale; element 0x40: +0x00 position, +0x10 velocity, +0x20 spin axis (+0x2C spin rate), +0x30 spawn time, +0x34 colour, +0x38 size; alive for 4 s |
| Computed on | **CPU**: p = p0 + v t + kHalfDebrisAcceleration t^2, angle = frac(rate x t + rate) x 360, sphere frustum test, then 16 pieces per batch (rotation matrix rows + position + colour, `DispatchBuffer`) to VU1 0x475910 (2080 B), which transforms the shared mesh [C/L] |
| Size / deps | **S, 2 d**: closed form, the program is a plain instanced transform [L]. Distinct from the breakable props / physics debris that props1 captures from the object system |

### 3.7 Coronas and light glows (car lights, traffic lights, boost)

| Item | Content |
|---|---|
| Code | `CB3EffectsManager::RenderLightCoronas` 0x256690 N (seven light groups chosen by car +0x58 bits, facing test, size 1 + 0.02 d), `AddTrafficCorona` 0x256FD0 N (slot write), `func_00257230` N (three pushes, w 800/400/200, into the arrays; boost glow [C corona1: the exhaust items +0x1684, CB3BoostEffectParams +0x118C; native-renderer.md §16.1]), `EndCoronaSubmit` 0x262240 M, `func_002622A0` N; `CB3CoronaDynamicArray::BeginUpdate`/`EndUpdate` 0x307500/0x307440 M, `ConstructDMAData` 0x3076D0 G; `CB3CoronaRenderer::UploadCodeAndStaticData` 0x306E80 M, `RenderDynamicArray` 0x3073A0 M, `EndRender` 0x3070B0 G, 0x307430 (named `Construct()` in the table, but it stores the player index; open question) |
| Slot [C 0x256FD0] | 32 bytes: position xyz, w = 400; colour x 64 xyz; size; colours from `kHeadMainColour`, `kBrakeColour`, ... (E map .rodata) [L] |
| State [C 0x307500/0x307440] | per array and player two DMA buffers (`field_10[player]`, half `field_2E[player]`, flipped in `BeginUpdate`), count per player at +0x2A; the buffer is written during the car draw, so a per-tick capture reads the half that the last `EndCoronaSubmit` closed [L] |
| GS / VU [C 0x306E00, 0x306E80] | VU1 0x478260 (1392 B) with the camera rows and view constants; ALPHA 0x68 (Cs x FIX + Cd, additive), TEST 0x53001 (Z GEQUAL, no Z / alpha write). Textures "coronaglow", "coronaboost", "coronaboostred" |
| Computed on | CPU positions; billboard expansion on VU1 [L] |
| Alternative | recompute from captured state: car light bits +0x58, car matrices, the light positions in the car files; then they interpolate like the car |
| Size / deps | **M, 3-4 d** |

### 3.8 Scrapes (sparks trail on the road from a scraping body)

| Item | Content |
|---|---|
| Code | `CB3ScrapeSystem::Construct` 0x375E90 G, `Render` 0x375AA0 M; `CB3ScrapeRenderer::BeginRender`/`Render`/`EndRender` 0x376260 M / 0x375F90 G / 0x376230 M |
| Parameters [C 0x375E90 draft] | `D_01EA4910` (E `CB3ScrapeSystem::mgaDefaultParams`): texture "fxscrape" (`D_004C9008`), ALPHA 0x80000044, floats 0.625 / 1.75 / 1.5 / 15 (lifetime [G]); UVs `karScrapeUVs` 0x4C8FF0 |
| State [C 0x375AA0] | scrape system (manager +0x16C80): emitter list +0x8A8C, count +0x8B0C; emitter double-buffered segment data at +0x20, current +0x440 (count at its +0), segments 0x20 each (+0x10, +0x20 vectors) |
| VU | `B3Im3d` (0x47A060) tristrips built on the CPU, Z write off [C 0x376260] |
| Computed on | CPU only |
| Size / deps | **S, 1-2 d** after shadows (same Im3d path) |

### 3.9 Glass (windscreens, side windows, shards)

| Item | Content |
|---|---|
| Car glass | `CB3VehicleObject::SetGlassState` (inline in `RenderVehicle` 0x21E6F0, N): state `CB3VehicleParams` +0x1014 (captured): 0 intact (texture slot 2, FIX = the mesh's default), 1 cracked (slot 3, FIX 0x48), 2 broken (slot 4, FIX 0x80), on the glass mesh index of the body and each part [C draft :38-52]. Drawn by the car program 0x46BB80; no separate effect (car-shading.md §3) |
| Shards | `DoVehicleGlass` 0x25FE70 (N): per glass piece of the vehicle model (+0x1688, count +0x16B5; +0x1678 / +0x167C, counts +0x16B1 / +0x16B2) a particle shower (spawn types 12 / 13 of `_gaSpawnParams`) and popcorn (`func_0025D430`) [C draft]; which particle array the spawn types map to is in the spawn table (fxglass arrays 4/5 [G]) |
| Track glass | windows on buildings are track material type 2 (0x481DD0 / 0x4803F0, E `G3_*_Glass.dsm` [L]), already in render3's model (native-renderer.md §11.2) |
| Size / deps | **S, 1 d** in the render5 car path (texture slots 2-4 per car TXD); shards come with §3.5 |

### 3.10 Water

| Item | Content |
|---|---|
| Code | `CB3StaticTrack::RenderWater(float, const CB3BackdropObject&, const CB3BackdropObject*)` 0x21D550 M; caller `RenderBeforeVehicles` 0x21C780 N (`:328-340`: gate `D_00665F1D`, objects STATIC +0x2C[table +0x6E] and +0x30[+0x6F], level = current block +0x70) |
| What it does [C] | for each water object in the frustum (`IntersectFrustum` on its box), each mesh: material type forced to **7**, `func_0026AE30` (material set-up), `RenderMesh`, `EndMaterial`, type restored; then the second object's static objects |
| VU | 0x47E300 (5072 B; E-name match `G3_Gen_ADCReflect`) [L]; the water level in `D_01E32ADC` [C]; program math not read |
| Capture | nothing new for geometry (water meshes are in STATIC.DAT, already decoded); the level (+0x70 of the camera unit's block, already in the track capture) and the clock if the program animates [G] |
| Size / deps | **M, 4-5 d**: read and check 0x47E300 like render3's material programs (reflection texgen, alpha); depends on the track path (done) |

### 3.11 Sun blob, sun corona, lens flare

| Item | Content |
|---|---|
| Code | `CB3EnvironmentManager::RenderSunCorona` 0x2BBF50 M -> `CB3SunRenderer::RenderCorona` 0x24BF60 N; `SunOcclusionTest` 0x2BBFA0 M -> `CB3SunRenderer::OcclusionTest` 0x24C7E0 N; blob `RenderBlob` 0x24CDF0 N (sky-enviro.md); env-map versions `RenderEnvmapCorona` 0x24B6E0 G, `PrepareEnvmapSunCoronaData` 0x24B9E0 G |
| What [C draft notes] | corona sprite (radius x aspect) + **12 lens-flare secondaries** from table `D_004BB610` with a frame-clock flicker; occlusion: a +-0x70 (1/16 px) box around the projected sun is drawn alpha-only, copied and halved twice, and the fade (box area / 256 / 196) scales the corona colour into `mnCoronaRed/Green/Blue` 0x4E29BC.. (data names) -- a GS readback trick |
| VU | GS-direct (no microprogram) |
| Native | sprites are trivial; occlusion becomes a depth-buffer sample or occlusion query over the same box [L], one frame late like the original [G] |
| Size / deps | **M, 3-5 d**; after the sky (render3) |

### 3.12 Post passes (short)

| Pass | Code | Notes |
|---|---|---|
| Fog | `CB3Fog::Render` 0x2419C0 M, `ZGreen2FBAlpha` 0x2416E0 N, `GenerateClut` 0x2413E0 N, `RenderIndexedFog` 0x241040 N | modelled by render3 (sky-enviro.md S.5, b3view F); order matters: shadows, debris, skids, scrapes are fogged, particles/coronas/sparks are not |
| Bloom | `CB3Bloom::Render(bool,bool)` 0x2659C0 M, `FirstDownsample`/`SecondDownsample`/`Blur`/`Blend`/`CopyToScreen` 0x2653B0/0x2651C0/0x264E60/0x264C30/0x2649D0 | GS-direct; args from `CB3World::Render:220` |
| Radial blur (boost) | `CB3RadialBlur::Render` 0x2FA9A0 G, `Downsample`/`BlurOut`/`SetAlpha`/`CopyToScreen`; parameters `CB3BoostEffectParams::GetRadialBlurParams` 0x30ABA0 N; 2D variant `CB3RadialBlur2dObject::_Render` 0x3D2FD0 N (front end) | GS-direct |
| Directional blur | `CB3DirectionalBlur::Render` 0x426C10 G, `BlurIn`/`BlurOut`/`Downsample`/`CopyToScreen`, `CB3DirectionalBlurBase::UpdateCamera` 0x4256C0 G | GS-direct; plus the present blit's own feedback blur (native-renderer.md §11.1) |
| Explosion flash | `CB3ExplosionManager::RenderFullScreenFlash` 0x399BA0 (E-map name [L]) from 0x261E00 when the crash timer (player +0x12D180 -> +0x3534) > 0 | full-screen sprite; **done** (native-renderer.md §19.7) |
| Full-screen specular | `RenderFullScreenSpecular` 0x2274B0 | done (render3) |

None of these upload a VU program [C uploader scan]; each is a chain of GS sprites over framebuffer copies, so the work
is recovering the intent from the C (native-renderer.md §8). **Size 6-9 d** for bloom + both blurs + flash + the
PAL/HDTV contrast filter [G].

### 3.13 HUD, 2D, menus, fonts, movies

| Item | Content |
|---|---|
| API | 22 `Gt2dRenderer` functions 0x217850-0x21A4F0 (`Gt2dRendererPS2.cpp`, most matched or in `src/nonmatching`): Begin 0x21A0F0, End 0x219F20, Suspend/Resume, SetTexture x2 (0x219A20, 0x219C90), SetBlendMode 0x217E70, filter/address modes, clip region, Z, `RenderTriStrip{Flat,Gouraud}{Tex,Untex}`, `RenderTriListFlatTex`, `RenderSprites{Tex,Untex}` (0x219830, 0x219530), `_Dispatch` 0x21A4F0 (PATH2 copy) |
| Scene / objects | `CB3HUDManager::Render` 0x1A0790 M (one `CGt2dScene::Render` 0x1D75C0 per HUD, count +0x170); `CGt2dFont::RenderString` 0x1D7B90 M; ~40 `CB3HUD*` components, `C2dObjectBoostBar::_RenderParticles` 0x1A7B00 (HUD particles drawn through Gt2d); menus `CB3MenuPageManager::Render` 0x30D6E0 M (also a direct packet builder); `CB3Globe2dObject::_Render` 0x3B2EE0 (direct packets + B3Im3d halo 0x3B17F0) |
| Movies | `CGtVideoDecoder` (`GtVideoDecoderPS2.cpp`: Prepare 0x3254F0 N, Update 0x325030 N, GetImage 0x324F10 M) -> `CGtVideoImage::SetTileTexture` 0x325BD0 N (direct GS uploads of tiles) -> `CGt2dObjectVideoImage::_Render` 0x326960 G |
| Native path | native-renderer.md §6-§7.1: record the Gt2d calls (float pixel positions, UVs, colours, the texture's guest `RwRaster`) and draw them at window resolution; textures through b3asset's raster decoder (done, §10.2) |
| Capture | the Gt2d record stream per rendered frame (not memory state: the HUD is immediate-mode); for movies either the decoder's output image in guest RAM or host decoding of the disc's MPEG files [G, open] |
| Size / deps | **L: 10-15 d** for the HUD/menus/text layer (1a, 1c, 1e of §7.1 minus what live1 and b3gfx already provide), anchoring table extra; movies **4-5 d** |

## 4. What the scene capture needs (additions to `b3port_scene.cpp`, native-renderer.md §12.1)

| Range / pointer chain | Size | For |
|---|---|---|
| `gEffectsManager` 0x1D9D200 .. +0x1F850 | ~126 KB (mostly static; the delta stream sends changes only) | all array headers, trail system with its segment buffers, scrape system, lighting, corona textures, clock +0x1F848 |
| particle banks: array +0xE8 / +0x114 -> bank +0x8 (capacity x 16), +0xC (x 8), +0x10 (x 4), +0x1C (capacity / 60 x 4) | up to ~160 KB, changes only at spawns | §3.5 |
| spark rings (array +0 / +0x4C -> base, capacity) and popcorn arrays' buffers | tens of KB | §3.3, §3.4 |
| debris elements (array +0x14, count +0x18, 0x40 each) | < 50 KB [G] | §3.6 |
| corona buffers (array +0x10[player] halves, count +0x2A[player]) | 270 slots x 32 B per half | §3.7 |
| race car +0x510, traffic +0x80, crashed +0x1870 (inside structs already read) | 16 B each | §3.1 (captured, version 3) |
| clock `gWorld`+0x1C / +0x20 | 8 B | every time-based effect |
| per-player flags: `gWorld` + p x 0x27D0 + 0x12C63A (crash), + 0x12A980 + 0x1CBA (scrapes), `D_005179C0` + p x 0xBB0 | bytes | gating |
| pass enables `D_00665F1D`, `D_00665F27`, `D_00665F2B`, `D_00665F2C` | bytes | gating |

All of it is plain main RAM (the uncached 0x20000000 / 0x30000000 aliases the code writes through map to the same
bytes; strip them when following pointers) [C address use]. Split screen: every renderer takes the player index and
keeps per-player buffers (coronas, scrape offset p x 0x27D0); capture both players' halves.

## 5. Recommended order for milestone 2, and why

What a player notices first in a race, roughly in order [G, from play experience of the game's look]: the HUD (without
it the game is not playable), car shadows (cars float without them), skid marks (every corner), sparks on contact,
car lights / boost glow, tyre smoke and crash smoke/fire/glass, then water, sun flare and the blur/bloom look.

1. **HUD/2D lane, start now, in parallel** (10-15 d + movies 4-5 d). Independent of every 3D item; the API is small and
   fully named; live1's window and b3gfx are its presenter.
2. **Shared effects base** (3-4 d): capture module (§4), the GS ALPHA/TEST/ZBUF/FBMSK -> blend-state translation used
   by every family (0x44, 0x48, 0x68 and FIX; Z GEQUAL with and without write; alpha-only and RGB-only writes),
   GLOBAL.TXD effect textures by name (all 15 checked present), billboard + ribbon + strip pipelines in b3gfx.
3. **Shadows** (2-3 d): highest impact per day; CPU-only, 8 vertices per car.
4. **Skid marks** (4-5 d): first VU program read of the milestone (0x4771B0), with render3's interpreter check.
5. **Sparks, then popcorn** (4-6 d): two small programs of the same shape; closed form in time.
6. **Coronas** (3-4 d): car lights everywhere, boost glow; decide capture-the-buffer vs recompute-from-light-bits.
   **Done (corona1, native-renderer.md §16)**: recomputed from the captured inputs; checked against the game's own arrays
   (counts equal, boost slots bit-exact) and the VU program (bit-exact).
7. **Particles** (8-10 d): the big one; smoke on every launch and drift, crash fire/smoke, glass shards.
8. **Debris + scrapes + car glass states** (4-5 d): crash-mode detail, all CPU-side.
9. **Water** (4-5 d): read 0x47E300; visible on a few routes only.
10. **Sun corona/flare + occlusion** (3-5 d), then **post** (6-9 d): these change the overall look and need the
    framebuffer-trick analysis; doing them last lets the native frame be compared against the GS frame pass by pass
    without blur/bloom hiding differences.

Interpolation (native-renderer.md §6.4): CPU-positioned effects (shadows, coronas recomputed from cars, debris)
interpolate with their owners; particles, sparks, popcorn and debris evaluate their closed forms at display time; skid
marks and scrapes are static geometry with an age fade (evaluate the fade at display time).

## 6. Open questions

- **Water program**: is 0x47E300 (type 7, E-size match `G3_Gen_ADCReflect`) the real water shading, and is the 5456-B
  tail of image 0x47C6F0 (E-size match `G3_Gen_ADCWater`) dead? Settle by disassembling both (`tools/render/vudis.py`)
  and looking for the E labels' structure (`_$fresnelLoop`, `_$uAnimationLoop`).
- **Retail effects memory block size**: E 0x7D000, retail at most 0x69400 (up to `gMenuPages`); the exact size only
  matters if the capture copies the block instead of following pointers (the labels between, e.g. `D_01DBEBD6`, are
  splat splits referenced from `lgcodec.data`, probably spurious [G]).
- **Particle program 0x46D750**: render classes +0x70 (0..5), the packed V4-8 parameters, the 60-particle block retire
  rule, lighting use: not read.
- **Spark motion blur** (`mrMaxMotionBlurTime`) and how streak length relates to the frame step (matters for uncapped
  frame rates: tie it to the 1/60 s tick, not the display frame [G]).
- **Corona capture timing**: buffers are filled during the car draw and flipped per player in `BeginUpdate`; confirm
  which half holds the last frame at the poll point, or recompute from light bits. **Answered (corona1, §16.5)**: at a poll the current half
  (+0x2E[player]) holds the last render's slots; in an N-tick frame, records 2..N hold cars moved on since that render
  (the native renderer recomputes from the inputs instead).
- **0x307430**: the table name `CB3CoronaRenderer::Construct()` but the body stores the player index and the call site
  passes (player, time) [C `B3CoronaRendererPS2.cpp:39`, asm 0x261F44]: a mis-pairing worth re-checking in the decomp
  ledger.
- **Decomp comment slip**: `src/b3/_unclassified/B3TrailSystem_002BD320.cpp` labels `CB3TrailSystem::AddTrailSegment`
  "// 0x002BCF60" (that address is `CB3TrailEmitter::AddTrailSegment`); the file name and asm say 0x2BD320. For the
  decomp lane, not a port issue.
- **Trail types 0 and 4** have no caller (jal scan); type 0's additive 0.3 s parameters look like a light streak
  [G]: dead data or reached through a table.
- **Glass shard spawn types 12 / 13** -> which particle arrays (`_gaSpawnParams`, E 0x5B0 bytes): dump the retail table.
- **Sun occlusion** native design (depth sample vs occlusion query) and whether its one-frame lag must be kept.
- **Movies**: decode the disc's MPEG on the host vs read the decoder's image from guest RAM. (hud1: still open; the
  HUD capture does not touch `CGtVideoImage`.)
- **HUD capture form** (answered, hud1, native-renderer.md §17) [C]: the call stream, recorded at Gt2dRenderer's
  Begin / SetTexture / Resume / Suspend / End / `_Dispatch` entries with every flushed batch copied (0 missed flushes),
  plus the guest rasters it uses; the buffer is in the scratchpad.
- **Boost bar particles** (answered, hud1) [C]: drawn through Gt2d. The captured stream holds the boost bar's
  `BoostFireCore01..30` / `BoostFireEdge` / `BoostBits` sprites in the 640-wide render buffer, and the pictures show
  the flames. `C2dObjectBoostBar::_RenderParticles` 0x1A7B00 needs no separate path. A mask-and-fill pass into a
  64-pixel-wide offscreen target (FBP 362) runs in 1,979 of race2's 4,471 HUD frames and is not drawn [L: a texture
  the 3D samples].
- **HUD anchoring** (partly answered, hud1): the Gt2d stream carries no component identity, so b3replay draws the 4:3
  layout scaled to the window height (pillarbox). The anchor table needs the issuing component per draw (next slice:
  a capture entry at the `CGt2dScene::Render` 0x1D75C0 / `CB3HUDManager::Render` 0x1A0790 level that tags the
  batches between its entry and exit).
- **HUD anchoring** per component (native-renderer.md §8) and whether `C2dObjectBoostBar::_RenderParticles` needs its
  own path.
