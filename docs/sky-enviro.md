# Sky, ENVIRO.DAT, sun and fog (render3 sky fork, 2026-11-02)

For merging into docs/native-renderer.md §11. Offline only (decomp read, disc files read, VU1 interpreter); nothing
ran the game. Tags: **[C]** seen in code/data, **[L]** likely (nonmatching draft or inference), **[G]** guess.
"matched" = byte-matched unit under the decomp's `src/b3/**`; "nonmatching" = `src/nonmatching/*.cpp` draft.
Code: `include/b3asset/enviro.h`, `src/b3asset/enviro.cpp`, `tools/render/b3envdump.cpp`.

## S.1 ENVIRO.DAT

One file per route, `TRACKS/<area>/<route>/ENVIRO.DAT` (37 files, 192 or 320 KiB). Loaded by
`CB3EnvironmentManager::Prepare` 0x2BCA20 as the route path + `"enviro.dat"` (rodata 0x4C1CB8), fixed up by
`CB3EnvironmentData::FixUp` 0x2B7110 (it only relocates the four texture offsets), then copied into the environment
manager `D_01E85900` [C matched `func_002BCA20.cpp`, `func_002B7110.cpp`; strings C ELF]. Layout:

| Off | Type | Meaning |
|---|---|---|
| 0x00 | vec4 | fog colour RGB 0..1 (-> env +0xC0) [C `CB3Fog::Render(fe+0xC0, ...)`, B3WorldPS2_00276300.cpp:167] |
| 0x10 | f32 | "near scale" D0 (-> env +0xD0 -> graphics manager +0x260 per viewport) [L nonmatching Update `func_002BC460.cpp:120`]: near plane = clamp(0.0003 D0, 0.003, 0.4), far = 10000 [C `CB3Fog::CalculateClipPlanes` 0x240EF0 + `OpenViewport` matched `func_00227360.cpp:78-97`; constants C rodata 0x4E0B5C/0x4E0B60/0x4E08B0]. 200..1500 per route; since the fog works on Z, it also scales the fog distance (S.4) |
| 0x14 | f32 | fog maximum D4 (0.1..0.6) [C GenerateClut p0] |
| 0x18 | f32 | fog of depth 0 = sky pixels, D8 (0 in all files) [C GenerateClut p1] |
| 0x1C | f32 | full-screen specular intensity, env +0xDC (0.6..1.1) [C B3WorldPS2_00276300.cpp:160] |
| 0x20 | f32 | 1.0 everywhere; env +0xE0, the w of the sun-blob colour (RenderBlob does not use it) [C/G] |
| 0x24..0x2F | f32 x3 | 0, 0, 1 everywhere [G] |
| 0x30..0x5F | | a second block of the same shape (colour, then 5/80/100, 1, ...); Prepare copies only block 0 into the two per-light entries (env +0xB0 + 0x40 k) [C]; unused as far as seen [G] |
| 0x60 | vec4 | specular colour = env +0x60 = `D_01E85960` (RenderFullScreenSpecular) [C] |
| 0x70 | vec4 | sun colour = env +0x70 (sun blob, env-map sun) [C matched `B3EnvironmentManager_002BC2B0.cpp`] |
| 0x80 | vec3 | sun direction **as light travels**; Prepare negates it: env +0xB0 = to-sun vector (y > 0 everywhere, unit length) [C]. This is the track/particle light vector (`PrepareRenderStatesForRenderingTrack`: renderer +0x30 = -(env +0xB0); `SetLighting(..., -env.B0[light])` B3EffectsManager_00261E00.cpp:101) and the sky's sun position. +0x8C = 0x004222C0 (pad) [G] |
| 0x90 | f32 | sun corona scale (0.15..0.35); Prepare multiplies it by graphics manager +0 [C/G] |
| 0x94 | u32 | 0 [G] |
| 0x98 | off | gradient raster, 32x32 CT32 ("bk_sky", "skygrad1", ...): sky colours by height + sun-glow mask (S.2) |
| 0x9C | off | cloud panorama, 1024x256 (US: 1024x128) PSMT8 ("clouds", "bk_skyclouds") |
| 0xA0 | off | sky for the car environment map, 64/128 px PSMT8 |
| 0xA4 | off | sun corona / flare, 128x256 PSMT8 |
| 0xA8 | u32 | 0x45440002 everywhere [G version/magic] |
| 0xAC | u32 | file size [C data, all 37] |

Offsets are relative to the file start; each target is a serialized CGtTexture (raster decoder, §10.2); the census's
148 ENVIRO rasters = 37 x 4 [C]. V1/V2 routes have byte-identical files except US/C2 and US/P2 (different D0 / sun)
and US/M1 (771 bytes inside the sky env-map raster) [C cmp].
The sun renderer is skipped (no blob, no corona) for four track IDs (0xC4BDA62031BD4000/..31E45000,
0x67E63A1B0EBD4000/..0EE45000) or when +0xA4 is 0 [C Prepare]; its blob texture is GLOBAL.TXD "sunblob" (rodata
0x4C1CC8, found by name in the global texture list) [C].

Runtime changes [L nonmatching Update 0x2BC460]: the env clock t (+0x134) advances by dt/480 and wraps (clouds,
S.2); only with debug flags `D_0064D378/9` set, a per-route position table overrides the sun direction and specular
scale (`func_002BB420`/`func_002BAF30`, hard-coded coordinates). Normal play keeps the file's values [L].

## S.2 Sky: what is drawn and how

Frame order in `CB3World::Render` [C matched B3WorldPS2_00276300.cpp:125-129, 148-167, 220-221]:
`ClearOffscreenBuffer` -> `RenderBackdropTexture` -> `RenderSky` (components 0, 1) -> `RenderSunBlob` ->
`RenderClouds` (component 2) -> env-map sky/sun for cars -> track before vehicles -> cars -> track after vehicles ->
`RenderFullScreenSpecular` -> track after specular -> shadows, pre-fog effects -> `CB3Fog::Render` -> ... -> bloom ->
`RenderSunCorona`.

**Dome geometry.** `CB3SkyDomeRenderer` (BeginRender 0x21DE50 uploads VU1 program **0x46A590** and the camera block;
RenderComponent 0x21DC50 sends 12 parameter qwords to VU1 1003 and MSCALs) [C matched]. The program has no mesh input:
it generates the dome from the component parameters (quarter dome with polynomial sin/cos, mirrored into four
quadrants) [C listing build/vu/game_0046A590.txt]. Parameters per component: `maDefinitions` 0x4B9CA0 (rodata) through
`CB3SkyDomeHemisphereParams::Construct` 0x21E490 [C matched]. Run in the VU1 interpreter and fitted [C
`b3envdump --vu`: all GS vertices of components 0/1/2 match the closed form below, direction error <= 8.7e-4 (the
program's polynomial sin/cos), uv error <= 1.2e-7, colours exact]:

    segment j = 0..S, ring i = 0..R;  e = e0 + (e1 - e0) i / R;  h = 2e - e^2;  r = 1 - e^2;  a = j / S (turns)
    direction = (r cos 2πa, ±h, r sin 2πa)   world axes, camera-centred (the program applies only the view rotation)
    u = u0 + us·a,   v = v0 + vk·h

| Component | Use | S x R | e range | sign | u | v | colour (0x80 = 1) | GS state [C Construct] |
|---|---|---|---|---|---|---|---|---|
| 0 | sky below the horizon | 32 x 12 | 0..1 | -h | a | 0.765625 + 0.25 h | 0x80 RGB, A 0x20 | TEST ZTE ALWAYS, ALPHA 0x2A (copy), CLAMP u repeat v clamp, texture = backdrop |
| 1 | sky above the horizon | 32 x 12 | 0..1 | +h | a | 0.765625 - 0.75 h | 0x80 RGB, A 0 | same |
| 2 | clouds | 64 x 12 | 0..0.75 | +h | 2a + scroll | 1 - h / 0.9375 | 0x40 RGB, A 0x80 | ALPHA 0x44 (texel-alpha blend), TEST alpha >= 0x40 else FB only, FBMSK alpha; texture = cloud panorama (own palette, `Copy8BitTextureAsPSMT8H`) |
| 3/4/5 | the same for the car environment map | 8 x 6 | | | | | | `RenderSkyToEnvironmentMap` draws 4 and 5 [C asm 0x2BC140] |

The dome is not a sphere (height 2e - e^2, radius 1 - e^2: flatter near the horizon); it is drawn at depth 0 (Z = 0 in
the interpreter's output with w = 1 [L]), i.e. behind everything, and fog index 0 leaves it unfogged (D8 = 0).

**Cloud scroll** [L nonmatching SetupCamera 0x21DF80 / Update]: u0 = 2t - (int(2t) + 1) with the env clock t
(+1/480 per second of dt [G unit]); `RenderSky` adds 0.25 to t when per-player race flags 0x8/0x10 are set [C matched;
G meaning reversed route]. `cloud_u_offset(t)` in enviro.h.

**Backdrop texture (64x32, rebuilt every frame)** [L nonmatching RenderBackdropTexture 0x2BBB90; blend modes C
`Gt2dRendererPS2_0021A290.cpp` + `SetBlendMode` func_00217E70]: Gt2d sprites from the gradient raster into a 64x32
CT32 target (`D_006662D4`, scissor 64x32 [C Prepare 0x21E100 / func_0021E350]):
1. copy (ALPHA 0x2A, alpha masked): every column = gradient column `15·c` (c = (D_0064E470 +0x348 + +0x34C) /
   (D_0064D344 -> +8) when that pointer is set, else 0 [G meaning: a progress/time blend]). The gradient's left half
   holds the sky colour by height (rows 0..23 sky zenith -> horizon, 24..31 below the horizon).
2. alpha only (ALPHA 0x6A, FBMSK off): the glow mask = gradient alpha, 13 strips: u = 0.5 - bearing + x/64 (repeat),
   rows y < 24 sample gradient row h(y) = min(24, 24·(0.5 - ((1 - sqrt(y/24)) - alt))) (row 12 = the sun's
   elevation; D_004E0F08 = 1/24 [C rodata]); rows 24..31 sample rows 24..31.
3. ALPHA 0x54 = (Cs - Cd)·Ad + Cd: blend gradient column `16 + 15·c` (right half: the bright, sun-side sky) over the
   copy by that mask.

bearing/alt = `SunPos2BearingAndAltitude` 0x2BB830 of to-sun [L nonmatching]: bearing = atan2(z, x) / 2π (0..1, the
dome's a), alt = 1 - sqrt(1 - y) (y >= 0) or sqrt(1 + y) - 1 (the dome's e). So the glow is centred on the sun's
azimuth and elevation, and the rest of the sky takes the left-half gradient. `build_backdrop()` reproduces the three
passes with bilinear sampling [G filter mode].

**Intensity convention.** The Gt2d sprites use colour 0.5 (0x40 [L Gt2d 1.0 = 0x80]) and the dome 0x80, the clouds
0x40: the frame buffer holds half intensity and `PresentDrawBuffer`'s blit doubles it (render3 main finding), so in
display units sky = the gradient texels as stored, clouds = the cloud texels, blended by their alpha.

**Sun blob** [L nonmatching `CB3SunRenderer::SetupCamera` 0x24D110 / `RenderBlob` 0x24CDF0; caller C matched
0x2BC2B0]: screen sprite with GLOBAL.TXD "sunblob" at the projection of camera + 50·to-sun; half size
12 / viewWindow.x pixels, aspect-corrected to be round, i.e. tan(half angle) = 24 / frame width (0.0375 at 640);
colour = sun colour x 128 (twice the texel in display units, saturating), A 0x80, ALPHA 0x44 (texel alpha), no Z test
or write, alpha not written; skipped when the sun is behind (view z <= 0.01). Between components 0/1 and the clouds,
so clouds cover it. Corona/flare (`RenderSunCorona` after bloom, occlusion-tested) not analysed.

## S.3 Full-screen specular (the consumer of the track's type-1 vertex alpha)

`RenderFullScreenSpecular(colour = env +0x60, f = env +0xDC)` 0x2274B0 [L nonmatching; caller C matched :160]: one
untextured full-screen sprite strip set, RGB = min(colour · 127.5 f, 255), ALPHA 0x58 = Cs·Ad + Cd (adds colour
weighted by the **frame-buffer alpha**), TEST alpha NEVER + FB_ONLY (no Z write), drawn after the after-vehicles track
pass. In display units: frame += min(2, 2·colour·f·127.5/255)·(Ad / 128) ≈ specular_colour · specular_scale · Ad/128.
Frame alpha sources: track type-1 vertex alpha (Phong, render3), sky component 0 alpha 0x20 x backdrop mask (below the
horizon only) [L]; clouds, sun blob, fog and opaque type-0 track (alpha 0) leave it unchanged / zero.

## S.4 Fog

`CB3Fog::Render(colour = env +0xC0, D4, D8)` 0x2419C0 [C matched], gated by `D_00665F29`, after the after-specular
track pass and before the HUD/game data [C :167]:
1. `ZGreen2FBAlpha` 0x2416E0: copies the Z buffer's green byte (Z bits 8..15) into frame-buffer alpha [C function
   name + the PSMZ16/CT16 sprite set-up; exact bit transport L, not replayed].
2. `GenerateClut` 0x2413E0 [L nonmatching, formula C]: a 256-entry CLUT, RGB white, alpha per index i:
   i = 0: 128·D8; i = 1..11: 128·D4; i >= 12: 128·D4·g(t), t = (1.4/(0.4 + (i-12)/243) - 1)/2.5,
   g = t < 0.75 ? (4/3)t^2 : 1 - 4(1-t)^2 (rounded: int(0.5 + 128 v)).
3. `RenderIndexedFog` 0x241040 [L nonmatching]: full-screen sprites reading the frame buffer as PSMT8H (alpha byte =
   index) through that CLUT, vertex colour = fog colour x 64 (half intensity, = the fog colour in display units),
   ALPHA 0x44 = lerp(frame, fog, As), at Z 0xFFFF with ZTST GREATER on the 24-bit Z buffer: only pixels with Z < 0xFFFF
   are fogged.

Depth mapping: the VU1 programs write Z = zShift + zScale / w with RW's camera mapping of 1/far..1/near onto
0..16777215 less 0.01 % margins (RW 3.1 `bacamera.c CameraUpdateZShiftScale`, deep Z via `RpSkySelectDeepZBuffer(1)`
C B3GraphicsManagerPS2.cpp:115, far screen Z 0 [C RW 3.1 sky2 device; L for RW 3.6]). With near from D0 and far 10000,
fog(w) = alpha[(zShift + zScale/w) >> 8] / 128 for Z < 0xFFFF, else 0. Since zShift >= ~1000 > 256, index 0 is only
reached by depth-0 (sky) pixels. Examples (`b3envdump`):

| D0 | near | fog starts (w where Z = 0xFFFF) | 15 % of D4 | 80 % of D4 | full D4 |
|---|---|---|---|---|---|
| 1500 | 0.40 | 104 m | 273 m | 1006 m | 3249 m |
| 800 | 0.24 | 63 m | 166 m | 629 m | 2240 m |
| 300 | 0.09 | 24 m | 63 m | 245 m | 977 m |
| 200 | 0.06 | 16 m | 42 m | 165 m | 673 m |

`fog_model()` / `fog_amount()` in enviro.h implement exactly this (index quantisation included).

## S.5 Still guessed / not done

- ZGreen2FBAlpha's exact bit transport (the doc assumes Z >> 8 as its name and the Z 0xFFFF GREATER test imply) [L].
- The column blend `c` of the backdrop (what D_0064D344 / D_0064E470 +0x348 are) [G]; c = 0 used.
- Gt2d / dome texture filtering (bilinear assumed) and Gt2d colour scale (1.0 = 0x80) [L].
- Second parameter block (+0x30), +0x24..+0x2F, +0x94, +0xA8 [G]; corona, flare, env-map sky (components 3-5) not
  modelled; sun-blob size uses a 640-pixel frame width [G].
- The env clock's dt unit (seconds assumed) [G].

## S.6 Integration note for b3view / b3gfx

1. Load: `read_enviro(ENVIRO.DAT)`; `find_txd_raster(GLOBAL.TXD, "sunblob")`; textures: backdrop
   (`build_backdrop(env, env.to_sun)`, 64x32, u repeat / v clamp; rebuild when the sun moves, i.e. once per route),
   clouds (`env.clouds`, u repeat / v clamp), sunblob.
2. Sky pass first, camera rotation only (view matrix without translation), depth test and depth write off (or depth
   at the far plane), no culling: `build_sky_layer(0)` and `(1)` opaque, colour = texel (display units); then the sun
   blob (additive-looking alpha blend: rgb = min(1, 2·texel·sun_colour), alpha = texel alpha, angular half size
   atan(kSunBlobTanHalf), centre = to_sun direction); then `build_sky_layer(2, cloud_u_offset(t))` alpha-blended by
   texel alpha, colour = texel.
3. Track/cars as now, but the track light vector is `-env.to_sun` (renderer +0x30) and the type-1 specular alpha goes
   into an alpha/specular target.
4. Full-screen specular after the after-vehicles pass: frame.rgb += env.specular_colour · env.block[0].specular_scale ·
   specularAlpha (clamp the added colour at 2.0, the GS clamp at 255 in half-intensity units).
5. Fog after the after-specular pass, before HUD: frame = mix(frame, fog_colour, fog_amount(w)) per pixel, where w is
   the view depth (reconstruct from the depth buffer); uniforms: `FogModel::z_scale`, `z_shift`, the 256-entry `alpha`
   table (as a 1D texture or a uniform array /128) and `colour`; sky pixels (no geometry) use alpha[0] (= D8 = 0).
   A smooth variant (no 256-step quantisation) can evaluate g(t) with t from the continuous index (Z / 256).
6. Frame intensity: everything above is in display units; if the renderer models the PS2's half-intensity frame
   buffer and the ×(255/128) present blit, halve the sky/cloud/fog colours accordingly.

Commands: `b3envdump <route dir> [outdir]` (parameters, fog curve, PNGs); `b3envdump <route dir> --vu
../burnout3-retail-decomp/assembly/assets/vu` (sky dome reference check; prints OK per component). Build (until the
Makefile has a rule): `g++ -Iinclude -std=c++20 -O2 tools/render/b3envdump.cpp src/b3asset/enviro.cpp
build/render/src/b3asset/{vu1,export,model,raster,gs_memory,png}.o -o build/b3envdump` (or add both files to the
render rules: enviro.cpp is picked up by the `src/b3asset/*.cpp` wildcard).
