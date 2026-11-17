# Car shading: what the car microprograms compute (render3 car fork, 2026-11-02)

For merging into docs/native-renderer.md §11. Offline only: decomp read (HEAD 8e81b8b0f9 tree), microprogram images
`assembly/assets/vu/game_{0046BB80,0047F6D0}.textbin.bin` disassembled with `tools/render/vudis.py` (listing addresses
below are instruction indices in `build/vu/game_*.txt`), programs run in the b3asset VU interpreter
(`include/b3asset/vu1.h`) on real car batches. Tags: **[C]** code/data/run, **[L]** likely, **[G]** guess.
Everything below comes from the listings and was checked by running the programs.

Code: `include/b3asset/carshade.h`, `src/b3asset/carshade.cpp` (the model), `tools/render/b3vucheck_car.cpp` (the
check). Build: `make build/b3vucheck_car` once the Makefile has the rule
`$(BUILD)/b3vucheck_car: tools/render/b3vucheck_car.cpp $(B3ASSET_OBJ)` (same recipe as b3vucheck).
Run: `build/b3vucheck_car <game>/PVEH/*/CAR*.B[GT]V [-v] [--dump]`.

## 1. Who draws a car, and with what [C]

- **0x47F6D0 is a VU0 microprogram**, not VU1: `CB3VehicleRenderer::BeginVehicleRendering` 0x220540 uploads it with
  `CGtVu0CodeManager::UploadVu0Code` and only 0x46BB80 goes to VU1 (MPG + BASE 0 / OFFSET 0x9C + MSCALF 0)
  [C `src/nonmatching/BeginVehicleRendering__18CB3VehicleRendererFv.cpp`]. So the car path has one VU1 program.
- Per car: `RenderVehicle` 0x21E6F0 -> `BeginRenderingSingleVehicle` 0x220040 (scratchpad 0x70002000 constants:
  `(h,h,h,38.4 f0)`, `(0,0,0,19.2 f0)`, `(t,t,t,128 f0)`, `128.1 f0`, t = 128.1 f1, h = t/2; f0 = shininess scale,
  f1 = lighting scale, both 1.0 unless damaged/exploding [C RenderVehicle draft :281-295]; the car's 9 SH irradiance
  coefficients (essential params +0x5C) to scratch +0x310; car - camera position to +0x340), `SetTexture` (paint:
  TEX0_1 with the palette's CBP), `SetGlassState`.
- Per part: `RenderObject` 0x21FBB0 / `RenderDeformedObject` 0x21F790 (`vcallms 0x7D8` = VU0 instruction 0xFB), header
  UNPACK V4-32 to VU1 **962..973** (+ 4 qw per damage zone from 974); per mesh entry `vcallms 0xB50` (= 0x16A) and a
  block UNPACK to **1006..1013**: 1006 GIF tag (3 A+D), 1007 TEX0_1 from the renderer slot `material byte 3`, 1008
  ALPHA_1, 1009 ALPHA_2, 1010/1011/1012 colours, 1013 = (render type, no-env, zone count). Camera block 1014..1023 =
  camera data +0x120 (same as the track).
- GS state set once (BeginVehicleRendering): context 1 TEST_1 0x50003 (no alpha test, Z GEQUAL); context 2 (the env
  pass) TEX0_2 = the sphere map at D_006662D8 (PSMCT32 256x256, TCC 1, MODULATE), TEX1_2 0x60 (bilinear), CLAMP_2 5
  (clamp), TEST_2 0x51001 (alpha test NEVER + AFAIL FB_ONLY: colour and alpha written, **no Z write**, Z GEQUAL),
  FRAME_2 with FBMSK 0 (alpha written).

## 2. VU0 per part (0x0FB..0x169) -> VU1 962..973 [C, checked]

- 962..965: the part matrix rows (right, up, at, pos), 965.w = 0 -> VU1 combines it with the view matrix once (0x004).
- **966..969: the Ramamoorthi-Hanrahan irradiance matrix** of the 9 SH coefficients (VU0 constants 0.429043,
  0.511664, 0.743125, 0.886227, 0.247708 at 0x002..0x0F5), coefficient order **L00, L1-1 (y), L10 (z), L11 (x),
  L2-2 (xy), L2-1 (yz), L20, L21 (xz), L22 (x²-y²)** [C: one coefficient at a time through the interpreter]:
  `M = [[c1 L22, c1 L2-2, c1 L21, c2 L11], [c1 L2-2, -c1 L22, c1 L2-1, c2 L1-1], [c1 L21, c1 L2-1, c3 L20, c2 L10],
  [c2 L11, c2 L1-1, c2 L10, c4 L00 - c5 L20]]`, rotated into the part's space when the bool argument is set
  (body, wheels; the bits reuse the body's matrix). Band 1 rotates exactly; **band 2 does not**: yaw-only parts give
  E(n) = exactly R M R^T on unit normals (max |err| 4.8e-7), pitched/rolled parts differ (max 0.013 in E for pitch
  <= 0.2 rad; the off-diagonal yz term comes out halved for a pure pitch) [C]. The routine at VU0 0x000..0x0FA is a
  ZYZ-Euler SH rotation with sqrt(3)/2, sqrt(3)/4 constants (Green's "Gritty Details" form [L]); a faithful port runs
  that routine (it is per part, ~330 VU cycles; b3asset::Vu1 runs it) or transcribes it, an approximate one uses
  R M R^T. The coefficients come from `CB3StreamedTrack::GetIrradiance` 0x301340: a ray down from the car hits a
  collision triangle whose three vertices carry `CGtIrradianceMapCompressed` (s8[9], scales 2 / 0.75 / 0.5 x 1.6/128
  per band) in the **LOD block's +0x94 array**, blended barycentrically [C names, L body] (answers track-format
  open question 5: LOD +0x94 is per-collision-vertex SH irradiance).
- 970: xyz = normalise(R (car - camera)), the camera-to-car direction in part space (one per part, not per vertex);
  w = the normal bias, CB3VehicleObject +0x08 = 65537.0 (the list's 0x47800080) [C].
- 971..973: a view basis Z = normalise(camera - part position), X = normalise(camera up x Z), Y = Z x X, stored as
  rows i = (-R_i.X, -R_i.Y, R_i.Z) with w = R_i.(camera - part position) (the eye in part space) [C].

## 3. VU0 per mesh entry (0x16A..0x19F) -> 1008..1013 [C, checked on all 31 material/colour words of the 107 cars]

Mesh entry = mesh, continuation, material word (u16 mask, u8 ALPHA_1 byte, u8 texture slot), colour word (u8 default
FIX, u8 current FIX; SetGlassState sets slot 3/FIX 0x48 cracked, slot 4/FIX 0x80 broken).

| Case | base (1010) | full (1011) | env colour (1012) | ALPHA_1 (1008) | ALPHA_2 (1009) | env pass |
|---|---|---|---|---|---|---|
| light bit (mask & lit & 0xFC) on | 192,192,192,0 | same | w 0 | byte, FIX | as below | **no** |
| paint/body (default) | 0,0,0, 19.2 f0 | t,t,t, 128 f0 | 128,128,128, 128.1 f0 | byte, FIX x fade | | |
| glass (mask & 0x300) | h,h,h, 38.4 f0 | t,t,t, 128 f0 | | | | |
| texture slot 4 (broken glass) | alpha = FIX both | | 20,20,20,0 | 0x44, 0 | 0x58 | yes |
| mask bit 1, ALPHA C = As | alpha 128 both | | w 0 | | 0x6A (Cd: invisible) | yes |
| mask bit 1, C != As | alpha 0 both | | w 0 | | 0x6A | **no** |
| bit 1 clear, C = As (0x44 meshes) | alpha 128 both | | 20,20,20, 128.1 f0 | | 0x58: Cd += Cs Ad | yes |
| bit 1 clear, C != As (0x64: paint, glass, unlit lights) | Fresnel alpha | | 128,128,128, 128.1 f0 | | **0x54: Cd = lerp(Cd, Cs, Ad)** | yes |

Data [C]: 0x00640001 (2,621 meshes) / 0x00640002 (2,759) / 0x00440001 / 0x00440002 / 0x01640002 (slot 1); lights
0x006400{04..80}; glass 0x02640100 / 0x02640200 with FIX bytes 0x04..0x80 (glass opacity). So bit 0 = reflective,
bit 1 = not reflective [L meaning]; the ALPHA byte 0x64 = (Cs-Cd) FIX + Cd (opaque at FIX 0x80, glass at its FIX).

## 4. VU1 0x46BB80 per batch [C, checked]

Input per vertex: @3i position (+ ADC 0x8000 in w), @3i+1 u16 lanes (UV and influences interleaved per 4 vertices),
@3i+2 normal 65536 + b/128 (STMOD offset) -> n = raw - 65537 = (b - 128)/128.

1. **Deformation** (1013.z zones, 0x02C..0x099): per vertex two influences u = weight<<8 | zone<<2 (from the slots of
   vertices 4g+1 and 4g+3, lane j); a = u/65535, b = u'/65535, rest = 1 - a - b; zone matrix at (974 + u) mod 1024
   = 974 + 4 zone (all weights are multiples of 4 [C data]); blended rows = a Za + b Zb + rest I (translation row
   without I); p' = p * rows, n' = n * rows3x3 (**not renormalised**).
2. **Lighting** (0x0B3 / 0x0F3): E = (n,1) M (n,1)^T; f = |n . V970|; F = 2f - f^2;
   rgb = base + E (full - base); alpha = max(0, full.a) - F (full.a - base.a); FTOI0 (truncation). No clamp: values
   outside 0..255 wrap in the GS (low 8 bits of each RGBAQ lane) [C; 270 of 3.77 M lanes with the check's random SH].
3. **UV** (0x1B5 / 0x193): vertex 4g+j: u,v = slot 4g + (j odd ? 2 : 0), lanes (j < 2 ? x,y : z,w), unsigned / 4096.
4. **Projection** (0x1C5; 0x1EC with 1013.x bit 1): same RW view matrix as the track (x/z - 0.5) * scale + shift,
   z = shift.z + scale.z / w, STQ = (u q, v q, q). **No back-face culling** (ADC = data; the GS draws both sides);
   with the clip flag, ADC is also set when any of the last three vertices is outside the guard band
   (|x'/a|, |y'/b|, |z0 + z1 w| <= w with 1022/1023). No per-vertex fog (F = 0, PRIM 0x5C has no FGE).
5. **Env pass** (1013.y == 0, 0x12F..0x1B2, 0x219/0x24C): d = p - eye_part; r = d - 2 (n.d) n; rv = sum_i env_i.xyz r_i;
   m = (rv.x, rv.y, rv.z + |d|); (s, t) = rv.xy / (2 |m|) + 0.5 (a sphere map); emitted as a second GIF tag in the
   same packet: PRIM 0x25C (context 2), STQ = (s q, t q, q), RGBA = 1012, same XYZ.
6. 0x289: the optional 4-qw A+D prefix (1006..1009) is kicked first, then the strip(s) [C].

## 5. For the GPU shader (GLSL-ready; 128 = 1.0 in GS colour units, the display blit then doubles)

Per part uniforms: `mat4 uMV` (part -> view), `mat4 uSH` (966..969), `vec3 uV` (970), `mat3 uEnv` (rows 971..973
xyz), `vec3 uEye` (971..973 w), `vec4 uBase, uFull, uEnvCol` (/128), `mat4x3 uZone[8]`, flags.

```glsl
// vertex
vec3 p = aPos, n = aNormal;                       // n = (b - 128) / 128, not normalised
if (uZones > 0) { mat4x3 Za = uZone[aZoneA], Zb = uZone[aZoneB];
  float a = aWeightA, b = aWeightB, rest = 1.0 - a - b;   // weights = u16 / 65535
  mat4x3 R = a * Za + b * Zb + rest * mat4x3(1.0);        // identity in the 3x3 only
  p = R * vec4(p, 1.0); n = mat3(R) * n; }
vec4 n1 = vec4(n, 1.0);
float E = dot(n1, uSH * n1);
float f = abs(dot(n, uV)), F = 2.0 * f - f * f;
vColour = vec4(uBase.rgb + E * (uFull.rgb - uBase.rgb), max(0.0, uFull.a) - F * (uFull.a - uBase.a));
vUV = aUV;
vec3 d = p - uEye; vec3 r = d - 2.0 * dot(n, d) * n; vec3 rv = transpose(uEnv) * r;   // rv_c = sum_i uEnv[i][c] r_i
vec3 m = vec3(rv.xy, rv.z + length(d)); vEnvST = rv.xy / (2.0 * length(m)) + 0.5;
gl_Position = uProj * uMV * vec4(p, 1.0);
// fragment, pass 1 (atlas): c = texture(atlas, vUV) * vColour  (MODULATE, both in 0x80 = 1.0 units)
//   ALPHA_1 0x64: out = mix(dst, c, FIX/128); 0x44: mix(dst, c, c.a); destination alpha := c.a
// pass 2 (sphere map, no depth write, depth test GEQUAL/equal): e = texture(envmap, vEnvST) * uEnvCol
//   ALPHA_2 0x54: dst.rgb = mix(dst.rgb, e.rgb, dstA); 0x58: dst.rgb += e.rgb * dstA; 0x6A: unchanged;
//   destination alpha := e.a (FB_ONLY writes it) -> RenderFullScreenSpecular later adds specColour * dstA [L]
```

So the paint reflection weight is the Fresnel alpha (19.2 f0 facing -> 128 f0 grazing, /128) times the atlas alpha
(the "mask" render2 found in the atlas alpha is the per-texel reflectivity [L, from ALPHA_2 = lerp by Ad]).

## 6. Check results (`b3vucheck_car`, all 107 car files, LOD 0-4, 2026-11-02) [C]

107 cars, 23,317 batches, 942,220 vertices + 523,920 env-pass vertices; every case kind: undeformed / 8 damage zones,
guard-band clip on/off, lights on, shininess/lighting scales, yawed and pitched parts. Model vs interpreter:

| Output | n | max abs error | over tolerance |
|---|---|---|---|
| VU0 part matrix / view dir / env basis (w: 2e-3 m, float cancellation) | 12,416 / 3,104 / 9,312 | 0 / 1.8e-7 / 5.2e-6 | 0 |
| VU0 SH E(n), yawed parts / pitched parts (exact R M R^T model) | 15,080 / 5,096 | 4.8e-7 / **0.0133** (tol 0.05) | 0 |
| VU0 base / full / env colours, ALPHA_1 byte + FIX, ALPHA_2, no-env | 34,404 x3 / 8,601 x4 | 0 | 0 |
| VU1 pass-1 RGB / alpha (integers) | 2,826,660 / 942,220 | 1 / 1 | 0 |
| VU1 u, v (s/q, t/q) | 938,770 | 4.8e-7 | 0 |
| VU1 w = 1/q (m), env w | 938,770 / 523,920 | 2.3e-5 / 1.2e-5 | 0 |
| VU1 screen x, y (pixels) / depth z (rel) | 937,540 / 937,512 | 0.074 / 1.0e-5 | 0 |
| VU1 ADC (plain / guard-band clip, vertices >= 2) | 590,807 / 301,450 | 0 | 0 |
| VU1 env s, t / env RGBA / env XY = pass 1 | 523,920 | 2.8e-5 / 0 / 0 | 0 |
| GIF PRIM pass 1 0x5C, pass 2 0x25C, vertex counts | | 0 | 0 |

Interpreter notes: 1.2 M Q reads while a DIV was pending (the programs' software pipelining,
handled by the delayed-Q model), 0 branch-VI backups; no vu1.cpp change was needed. The same `Vu1` class runs the VU0
program (data addresses stay below 17, so VU0's 4 KiB wrap does not matter). Cars are placed within 30 m of the
origin for the check: at km-scale positions the projection differs by up to 2.7 px near the camera plane from float
cancellation (VU truncation vs host rounding of the same products), `B3VUCHECK_FAR=1` shows it.

## 7. Still open / guessed

- The env map itself: built per frame by `BuildCustomCameraData` 0x223130 -> `ConstructEnvMap` 0x225440 (env cameras,
  near 0.01 / far 100), `RenderSkyToEnvironmentMap` / `RenderSunToEnvironmentMap`, then
  `DistortEnvmapBuffersIntoSphereMap` 0x222CA0 with `PreBuildSpheremapWarpData` / `_PartialHemisphereCreate` meshes
  [C names, L flow]; what its alpha holds (sun only? [G]) decides the car glint through RenderFullScreenSpecular.
  Not analysed here.
- The exact VU0 band-2 SH rotation (0x000..0x0FA) is not transcribed; the model uses the exact rotation (§2).
- Texture slots 1..4 resolved by render5 [C]: VehicleUnderside, UnbrokenGlass, CrackedGlass, SmashedGlass from
  GLOBAL.TXD via CB3GraphicsManager::Prepare 0x228070 (docs/native-renderer.md §12.6). Mask bit 1 meaning "not
  reflective" [L]; glass FIX bytes per car [C data].
- Whether a game SH ever drives E above 2 (RGB wrap) [G: the check's random SH did, 270 lanes].
- ALPHA_1 FIX x fade truncation and the destination-alpha semantics of FB_ONLY follow the GS manual [L].

## 8. Why a race car's paint looks flat while the garage's shines (reflect1, 2026-11-15)

Checked by drawing the same car both ways with the scene renderer and a debug read-out of the pass-2 weight and colour.

- A race car and the garage car go through the same code (`add_car_draws`, the same mesh words, the same car shader), and in
  both the env pass runs on the paint and the glass: the mask 0x54 weight is the Fresnel alpha times the atlas alpha, about 1
  on the paint seen at a grazing angle (a car seen from behind and above) and 0.15 facing, 0 on lights and plates.
  So the game does draw the env pass on race cars, and the port does not lose it: not a renderer bug.
- What differs is the environment map. In a race it is the track's sky alone (ENVIRO.DAT components 4 / 5 of the sky,
  `RenderSkyToEnvironmentMap`): smooth, mid-bright, and on the cars of the retail livery a blue or grey of nearly the paint's
  own hue, with no sun glint and no hard-edged shapes. The garage's map is built from the lit room around the car (bright
  panels, dark floor, a strong contrast the paint then shows).
- So the reflection is faithful: the game's own weight on a low-contrast map. Not that only the garage draws it.

The port's CAR REFLECTIONS setting (remaster-features §9) keeps the game's pass as `Original` and adds `Enhanced`: a
clearcoat in `car.frag` over the game's lit paint that keeps the paint's colour (the reflection scales the paint's own
colour up or down, plus a white sun glint: never a tint, never a uniform veil), from the same sky map with contrast (clouds,
bright haze along a hard horizon line, a dark ground that darkens the lower body, a hard sun glint), a Fresnel weight
(low face-on, growing to the edges); glass darker with a strong sky and a bright rim. It is gated by `CarParams::sky_open`
(the upward share of the car's SH irradiance: 1 in the open, 0.6 at dusk, 0.2 in a tunnel [C measured]) so a car under cover
shows its dark surroundings instead of a sky; traffic at 0.55 strength. Every number is a live value (docs/car-look.md).
Presentation only: `View::apply_car_reflections` sets `CarParams::reflect` per draw after `add_car_draws`; 0 leaves the
game's math untouched, so Original is byte-identical to before. `tools/render/carlook_check.sh` asserts that the paint's
hue and saturation survive a render.
