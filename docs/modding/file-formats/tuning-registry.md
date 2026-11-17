# The tuning registry

Every tuning variable the game registers with its variable database (`GtComms::CGtCommsDatabase`). Each registration
gives a variable a name, a group and the config file it belonged to in Criterion's tools. Its default then comes from
[DATA/VDB.XML](vdb.md), under the hash of those three strings. This table is the reference for the tuning console (mod
kit part 4, [DESIGN.md](../DESIGN.md) §4) and for VDB.XML edits.

Tags: [evidence tags](index.md#evidence-tags). The whole table is [C]: the call sites from the decomp's C, the strings
from its data listings. Default values are given only where the code sets one; the game's values from VDB.XML are not
reproduced here, since they are game data.

## The three entry points

| Address | Signature (E map names) | Entry type | Evidence |
|---|---|---|---|
| 0x0021BB60 | `AddEntry(float *var, const char *name, const char *group, const char *file, int flags, float min, float max, int count)` | 2 float | [C] |
| 0x0021BD80 | `AddEntry(int *var, const char *name, const char *group, const char *file, int flags, int min, int max, int count)` | 1 int | [C] |
| 0x0021B9B0 | `AddEntry(GtMath::CGtV3d *var, const char *name, const char *group, const char *file)` | 5 vector (16 bytes) | [C] |

What each one does [C, sources in vdb.md]:
- builds `name + group + "/" + file` (no `/` added when the group ends in one) and hashes it with `CGtHash::CalculateHash`
  0x0021BFA0: that is the VDB.XML key;
- takes the next free one of the registry's 1,736 entries (32 bytes: variable pointer, max at +4, min at +8, the
  three string pointers, the key at +0x18, count at +0x1C, flags at +0x1D, type at +0x1E);
- sets flags bit 0 when min or max is non-zero (a range was given). The caller's `flags` argument is 0 or 2 in retail;
  bit 1 is set on 14 registrations, all of them with a range [D: this table]; what it means is not known
  [G: "show as a slider" in Criterion's tool];
- `count` >= 2 makes the variable an array of `count` words: VDB.XML then holds a block of `count` x 4 bytes for it;
- writes the VDB.XML default over the variable (`SetDefaultElementValue` 0x0024FFC0), then unregisters the entry again
  if its config file is disabled in VDB.XML (`CheckFileValid` 0x0024FDE0).

A type 3 (bool) exists in `SetDefaultElementValue`, but none of the three entry points creates it [C].

## Columns

- **Group, Name**: the strings exactly as the code passes them (group is the second argument, name the first). Some
  are odd but correct as written, such as the surface groups reusing the `Tarmac ...` names: the VDB.XML key matches.
- **Type**: float, int or vector. **Range**: min .. max when given (otherwise 0 / 0, left empty).
- **Flags / count**: the `flags` argument and the element count (0 = one value).
- **Default (code)**: the value the code gives the variable before registering it. "(store)" is a constant the
  registering function stores just before; "(data)" is the global's initial value in the executable. Empty: not a
  constant (a field filled elsewhere, or zero). A value in VDB.XML replaces it.
- **File**: the config file. "(built at run time)" names the pattern of a per-vehicle or per-route name: the pattern is
  found by matching VDB.XML's file records (vdb.md). `runtime: ...` is a name the scan could not resolve.
- **Variable**: the first argument as the decomp writes it: a global's name or placeholder, or `field_X` of the
  registering object.
- **Registered by**: the function (retail symbol where there is one) and its address. **In VDB.XML**: whether the US
  disc's VDB.XML has a value for this key; "yes (n files)" for a per-vehicle or per-route registration counts the
  config files that have one.

Regenerate (the decomp at a path of your choice, read only; `--vdb` adds the last column from your own disc):

```
python3 -I tools/modding/tuning_registry.py ../burnout3-retail-decomp --vdb <disc>/DATA/VDB.XML
```

`--values` adds the disc's values as a column: for your own use, never commit or share that output.

Limits of the scan: it reads the decomp's C (`src/`), whose argument lists for the nonmatching functions are drafts.
Every row's strings are checked by the VDB.XML key wherever the file has the value (685 of 705). The call count per
function equals the retail assembly's `jal AddEntry` count (705 in 26 functions). Registrations reached through other
code are not in it (vdb.md, "Names").

## Summary by registering function

| Registered by | Address | Entries | float / int / vector | Config file(s) | Groups (first part) | In VDB.XML |
|---|---|---|---|---|---|---|
| func_00131990 | `0x00131990` | 1 | 1 / 0 / 0 | Sound/Movie.cfg | Sound | 0 of 1 |
| RegisterStaticVariables__7CB3BurnFv | `0x0014E670` | 5 | 5 / 0 / 0 | Score.cfg | Boost Bar | 5 of 5 |
| Register__10CB3RaceCarFv | `0x00150F20` | 5 | 5 / 0 / 0 | runtime: `field_1790` | Material Properties | 0 of 5 |
| RegisterStaticVariables__8CB3ScoreFv | `0x0015D0A0` | 82 | 51 / 31 / 0 | Score.cfg | Score | 80 of 82 |
| Prepare__22CB3CrashActionDirectorFv | `0x001B2760` | 7 | 7 / 0 / 0 | Physics/cad.cfg | CAD Properties | 0 of 7 |
| Register__24CB3VehiclePhysicsAttribsFUl | `0x00252100` | 9 | 9 / 0 / 0 | VehiclePhysics/<vehicle>.cfg | Physics | 9 of 9 |
| Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics | `0x00252C20` | 64 | 64 / 0 / 0 | VehiclePhysics/<vehicle>.cfg | Physics | 64 of 64 |
| Register__12CB3AIAttribsFv | `0x00271A80` | 73 | 69 / 4 / 0 | AI/defaults.cfg | AI | 73 of 73 |
| Construct__22CB3SoundTrafficManagerFv | `0x00277360` | 51 | 46 / 5 / 0 | Sound/Traffic.cfg, Sound/Boost.cfg | Sound | 51 of 51 |
| Construct__22CB3SoundTrafficVehicleFv | `0x0027A5F0` | 14 | 10 / 4 / 0 | Sound/LTrain.cfg | Sound | 14 of 14 |
| Prepare__20CB3SoundCrashManagerFv | `0x00281C30` | 28 | 28 / 0 / 0 | Sound/Tracks/<route>.cfg | Sound | 28 of 28 |
| RegisterVariables__20CB3SoundCrashManagerFv | `0x00282AC0` | 32 | 32 / 0 / 0 | Sound/Payload.cfg | Sound | 32 of 32 |
| Construct__18CB3HUDSoundManagerFv | `0x00287240` | 14 | 14 / 0 / 0 | Sound/HUD.cfg | Sound | 12 of 14 |
| func_00288C20 | `0x00288C20` | 14 | 14 / 0 / 0 | Sound/Esm.cfg | Sound | 14 of 14 |
| func_0028B580 | `0x0028B580` | 1 | 1 / 0 / 0 | Sound/Voice.cfg | Sound | 1 of 1 |
| Register__13CB3SoundAICarFUl | `0x0028BA20` | 1 | 1 / 0 / 0 | VehicleAudioAI/<vehicle>.cfg | VehicleAudioAI | 1 of 1 |
| Register__17CB3SoundPlayerCarFUl | `0x0028C780` | 11 | 10 / 1 / 0 | VehicleAudio/<vehicle>.cfg | VehicleAudio | 11 of 11 |
| Construct__15CB3SoundRaceCarFv | `0x0028DD80` | 9 | 9 / 0 / 0 | Sound/Misc.cfg | Sound | 9 of 9 |
| Construct__17CB3SoundSkidModelFv | `0x0028ED30` | 65 | 65 / 0 / 0 | Sound/Skids.cfg | Sound | 65 of 65 |
| Construct__22CB3SoundRaceCarManagerFv | `0x002ED690` | 126 | 126 / 0 / 0 | Sound/Misc.cfg, Sound/Surface.cfg | Sound, None, Tarmac, Concrete, Offline Tarmac, Gravel ... | 125 of 126 |
| RegisterVariables__16CB3CameraAttribsFv | `0x002F7E70` | 34 | 2 / 0 / 32 | Camera/Camera.cfg | Crash, InGame | 34 of 34 |
| RegisterVariables__19CB3CameraAttribsCarFv | `0x002F8870` | 6 | 1 / 0 / 5 | Camera/<vehicle>.cfg | Camera | 6 of 6 |
| Prepare__14CB3StaticTrackFv | `0x00332E00` | 2 | 2 / 0 / 0 | TrackBloom/<route>.cfg | Bloom Parameters | 2 of 2 |
| func_00344310 | `0x00344310` | 47 | 37 / 10 / 0 | Input/rumble.cfg | Rumble | 47 of 47 |
| func_0042A4E0 | `0x0042A4E0` | 2 | 2 / 0 / 0 | VehiclePhysics/<vehicle>.cfg | Physics | 2 of 2 |
| func_004547C0 | `0x004547C0` | 2 | 2 / 0 / 0 | Sound/Movie.cfg | Sound | 0 of 2 |
| **all** | | **705** | 613 / 55 / 37 | | | 685 of 705 |

## Every registration

In the order of the registering functions' addresses, then retail's call order.

| # | Group | Name | Type | Range | Flags / count | Default (code) | File | Variable | Registered by | In VDB.XML |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Sound/Front End | Gain | float |  | 0 / 0 | 1 (data) | ../export/ValueDB/Sound/Movie.cfg | `&D_004E1408` | func_00131990 `0x00131990` | no |
| 2 | Boost Bar | Minimum Boost Time (seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Burn::gmrMinimumBoostTime` | RegisterStaticVariables__7CB3BurnFv `0x0014E670` | yes |
| 3 | Boost Bar | Minimum Boost Recovery Time (seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Burn::gmrMinimumBoostRecoveryTime` | RegisterStaticVariables__7CB3BurnFv `0x0014E670` | yes |
| 4 | Boost Bar | Boost Bar sizes (Boost Units) | float | 0 .. 720 | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Burn::gmarBoostBarSizes` | RegisterStaticVariables__7CB3BurnFv `0x0014E670` | yes |
| 5 | Boost Bar | Boost Earning Multiplier | float | 0 .. 10 | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Burn::gmarBurnoutBoostMultipliers` | RegisterStaticVariables__7CB3BurnFv `0x0014E670` | yes |
| 6 | Boost Bar | Boost Rate (Boost Units per Second) | float | 0 .. 720 | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Burn::gmarBoostRate` | RegisterStaticVariables__7CB3BurnFv `0x0014E670` | yes |
| 7 | Material Properties | Reflectivity | float | 0 .. 1 | 0 / 8 |  | runtime: `field_1790` | `(char *)e + 0x8` | Register__10CB3RaceCarFv `0x00150F20` | no |
| 8 | Material Properties | Glossiness | float | 0 .. 1 | 0 / 8 |  | runtime: `field_1790` | `(char *)e + 0x28` | Register__10CB3RaceCarFv `0x00150F20` | no |
| 9 | Material Properties | Specularity | float | 0 .. 1 | 0 / 8 |  | runtime: `field_1790` | `(char *)e + 0x48` | Register__10CB3RaceCarFv `0x00150F20` | no |
| 10 | Material Properties | GlossinessGlass | float | 0 .. 1 | 2 / 0 |  | runtime: `field_1790` | `(char *)e + 0x68` | Register__10CB3RaceCarFv `0x00150F20` | no |
| 11 | Material Properties | SpecularityGlass | float | 0 .. 1 | 2 / 0 |  | runtime: `field_1790` | `(char *)e + 0x6C` | Register__10CB3RaceCarFv `0x00150F20` | no |
| 12 | Score/Boost/Air | Minimum Air Distance (Metres) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrAirMinimumDistanceValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 13 | Score/Boost/Air | Boost value per Air metre (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrAirDistanceBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 14 | Score/Boost/Oncoming | Minimum Oncoming Speed (MPH) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrOncomingMinimumSpeed` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 15 | Score/Boost/Oncoming | Minimum Oncoming Distance (Metres) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrOncomingMinimumDistanceValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 16 | Score/Boost/Oncoming | Boost value per Oncoming metre (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrOncomingDistanceBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 17 | Score/Boost/Drift | Minimum Drift Speed (MPH) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrDriftMinimumSpeed` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 18 | Score/Boost/Drift | Minimum Drift Distance (Metres) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrDriftMinimumDistanceValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 19 | Score/Boost/Drift | Boost value per Drift metre (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrDriftDistanceBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 20 | Score/Boost/Near Miss | Near Miss Distance (Metres) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrMaxNearMissDistance` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 21 | Score/Boost/Near Miss | Minimum Near Miss Speed (MPH) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrNearMissMinimumSpeed` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 22 | Score/Boost/Near Miss | Minimum Near Miss Chain Speed (MPH) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrNearMissMinimumChainSpeed` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 23 | Score/Boost/Near Miss | Boost value per Near Miss (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrNearMissBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 24 | Score/Boost/Near Miss | Near Miss Chain Time (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrNearMissChainTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 25 | Score/Boost/Crash Escape | Crash Escape Radius | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrCrashEscapeRadius` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 26 | Score/Boost/Crash Escape | Time Between Crashing Near Misses (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrCrashNearMissChainTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 27 | Score/Boost/Crash Escape | Minimum Time Clear After Crash Escape | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrCrashEscapeMinClearTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 28 | Score/Boost/Crash Escape | Minimum Crash Escape Speed (MPH) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrCrashEscapeMinimumSpeed` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 29 | Score/Boost/Crash Escape | Boost value per Crash Escape (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrCrashEscapeBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 30 | Score/Boost/Aggressive | Boost value lost or gained per Slam (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrMaxSlamBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 31 | Score/Boost/Aggressive | Boost value for rubbing (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrRubbingBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 32 | Score/Boost/Aggressive | Min Contact Time For Rubbing (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrRubbingMinTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 33 | Score/Boost/Aggressive | Trading Paint Boost value (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTradingPaintBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 34 | Score/Boost/Boost Start | Boost Start Boost value (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrBoostStartBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 35 | Score/Boost/Takedowns | Takedown Boost value (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTakedownBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 36 | Score/Boost/Takedowns | Maximum Crash Wait Time (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTakedownCrashTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 37 | Score/Boost/Takedowns | Maximum Crash Wait Time - No Slam (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTakedownCrashTimeNoSlam` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 38 | Score/Boost/Takedowns | Minimum Collide Time to enable Takedown - No Slam (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTakedownCollideTimeNoSlam` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 39 | Score/Boost/Takedowns | Race Car Clear Wait Time (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTakedownWaitTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 40 | Score/Boost/Takedowns | Time window for getting a double takedown(Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrDoubleTakedownTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 41 | Score/Boost/Takedowns | Time window for getting a takedown spree (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrSpreeTakedownTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 42 | Score/Boost/Takedowns | Time window for getting a double revenge takedown (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrDoubleRevengeTakedownTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 43 | Score/Boost/Props | Prop Hit Chain time (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrPropHitChainTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 44 | Score/Boost/Props | Prop Hit Boost value (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrPropHitBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 45 | Score/Boost/Lap | Perfect Lap Boost value (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrPerfectLapBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 46 | Score/Boost/Lap | Burnout Lap Boost value (Boost units) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrBurnoutLapBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 47 | Score/Boost/Crashing | Ratio of Boost Lost After Crash | float | 0 .. 1 | 2 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrBurnRatioLostInCrash` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 48 | Score/Burnout Points | Prop Hit BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnPropHitBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 49 | Score/Burnout Points | Rubbing BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnRubbingBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 50 | Score/Burnout Points | Tradin' Paint BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnTradingPaintBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 51 | Score/Burnout Points | Boost Start BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnBoostStartBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 52 | Score/Burnout Points | Crash Escape BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnCrashEscapeBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 53 | Score/Burnout Points | Burning Slam Extra BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnBurningSlamExtraBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 54 | Score/Burnout Points | Perfect Lap BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnPerfectLapBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 55 | Score/Burnout Points | Takedown BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnTakedownBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 56 | Score/Burnout Points | Revenge Takedown Bonus BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnRevengeBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 57 | Score/Burnout Points | Psyche Out BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnPsycheOutBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 58 | Score/Burnout Points | Aftertouch Takedown BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnAftertouchTakedownBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 59 | Score/Burnout Points | No Mercy BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnNoMercyBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 60 | Score/Burnout Points | Denied (You Were Lucky) BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnYouWereLuckyBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 61 | Score/Burnout Points | Denied (Takedown Denied) BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnDeniedBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 62 | Score/Burnout Points | Burnout Lap BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnBurnoutLapBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 63 | Score/Burnout Points | Near Miss BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnNearMissBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 64 | Score/Burnout Points | Leading Race At End Of Lap BP | int |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmnLedLapBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 65 | Score/Burnout Points | Air Category BP | int |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanAirBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 66 | Score/Burnout Points | Slam Type BP | int |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanSlamBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 67 | Score/Burnout Points | Super Slam Type BP | int |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanSuperSlamBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 68 | Score/Burnout Points | Oncoming Category BP | int |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanOncomingBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 69 | Score/Burnout Points | Drift Category BP | int |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanDriftBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 70 | Score/Burnout Points | Tailgate Category BP | int |  | 0 / 3 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanTailgateBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 71 | Score/Burnout Points | Rubbing Category BP | int |  | 0 / 3 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanRubbingBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 72 | Score/Burnout Points | Grinding Category BP | int |  | 0 / 3 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanGrindingBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 73 | Score/Burnout Points | Near Miss Category BP | int |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanNearMissBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | no |
| 74 | Score/Burnout Points | Take Race Position BP | int |  | 0 / 5 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanBestRacePositionBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 75 | Score/Burnout Points | Double Takedown BP | int |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanDoubleTakedownBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 76 | Score/Burnout Points | Takedown Spree BP | int |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanSpreeTakedownBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 77 | Score/Burnout Points | Double Revenge BP | int |  | 0 / 2 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanDoubleRevengeTakedownBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 78 | Score/Burnout Points | Road Rage Takedown BP | int |  | 0 / 8 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmanRoadRageTakedownBP` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 79 | Score/BP Categories | Air Category Minima (Metres) | float |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmarMinAir` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 80 | Score/BP Categories | Oncoming Category Minima (Metres) | float |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmarMinOncoming` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 81 | Score/BP Categories | Drift Category Minima (Metres) | float |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmarMinDrift` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 82 | Score/BP Categories | Tailgate Category Minima (Seconds) | float |  | 0 / 3 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmarMinTailgate` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 83 | Score/BP Categories | Rubbing Category Minima (Seconds) | float |  | 0 / 3 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmarMinRubbing` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 84 | Score/BP Categories | Grinding Category Minima (Seconds) | float |  | 0 / 3 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmarMinGrinding` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 85 | Score/BP Categories | Near Miss Category Minima (Counts) | float |  | 0 / 4 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmarMinNearMiss` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | no |
| 86 | Score/Tailgating | Max distance behind for tailgating (Metres) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTailgateMaxDistance` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 87 | Score/Tailgating | Min distance behind for psyche-out when tailgating (Metres) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTailgatePsycheOutDistance` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 88 | Score/Tailgating | Cone angle behind for tailgating (Degrees) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTailgateConeAngle` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 89 | Score/Tailgating | Extra boost per second | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTailgateExtraBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 90 | Score/Tailgating | Minimum tailgating time to count as tailgating (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrTailgateInitialTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 91 | Score/Grinding | Minimum time to grind before scoring (Seconds) | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrGrindingMinTime` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 92 | Score/Grinding | Extra boost per second | float |  | 0 / 0 |  | ../Export/ValueDB/Score.cfg | `&CB3Score::gmrGrindingExtraBurnValue` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 93 | Score/Distance Travelled | Distance Travelled Medals (Miles) | float |  | 0 / 3 |  | ../Export/ValueDB/Score.cfg | `CB3Score::gmarDistanceTravelledMedals` | RegisterStaticVariables__8CB3ScoreFv `0x0015D0A0` | yes |
| 94 | CAD Properties | Air Ram Front Corner | float | 0 .. 1 | 2 / 0 |  | ../Export/ValueDB/Physics/cad.cfg | `((char *)this + 0x57C)` | Prepare__22CB3CrashActionDirectorFv `0x001B2760` | no |
| 95 | CAD Properties | Air Ram Back Corner | float | 0 .. 1 | 2 / 0 |  | ../Export/ValueDB/Physics/cad.cfg | `((char *)this + 0x580)` | Prepare__22CB3CrashActionDirectorFv `0x001B2760` | no |
| 96 | CAD Properties | Air Ram Side | float | 0 .. 1 | 2 / 0 |  | ../Export/ValueDB/Physics/cad.cfg | `((char *)this + 0x584)` | Prepare__22CB3CrashActionDirectorFv `0x001B2760` | no |
| 97 | CAD Properties | Air Ram Front | float | 0 .. 1 | 2 / 0 |  | ../Export/ValueDB/Physics/cad.cfg | `((char *)this + 0x588)` | Prepare__22CB3CrashActionDirectorFv `0x001B2760` | no |
| 98 | CAD Properties | Air Ram Back | float | 0 .. 1 | 2 / 0 |  | ../Export/ValueDB/Physics/cad.cfg | `((char *)this + 0x58C)` | Prepare__22CB3CrashActionDirectorFv `0x001B2760` | no |
| 99 | CAD Properties | Air Ram World | float | 0 .. 1 | 2 / 0 |  | ../Export/ValueDB/Physics/cad.cfg | `((char *)this + 0x590)` | Prepare__22CB3CrashActionDirectorFv `0x001B2760` | no |
| 100 | CAD Properties | Air Ram Low Speed Multiplier | float | 0 .. 2 | 2 / 0 |  | ../Export/ValueDB/Physics/cad.cfg | `((char *)this + 0x594)` | Prepare__22CB3CrashActionDirectorFv `0x001B2760` | no |
| 101 | Physics/Vehicle | Mass (Kg) | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `(char *)this + 0x88` | Register__24CB3VehiclePhysicsAttribsFUl `0x00252100` | yes (100 files) |
| 102 | Physics/Suspension/Front | Attatch Height | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `(char *)this + 0x8C` | Register__24CB3VehiclePhysicsAttribsFUl `0x00252100` | yes (100 files) |
| 103 | Physics/Suspension/Front | Spring Length | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `(char *)this + 0x98` | Register__24CB3VehiclePhysicsAttribsFUl `0x00252100` | yes (100 files) |
| 104 | Physics/Suspension/Front | Spring Force | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `(char *)this + 0x90` | Register__24CB3VehiclePhysicsAttribsFUl `0x00252100` | yes (100 files) |
| 105 | Physics/Suspension/Front | Spring Damping | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `(char *)this + 0x94` | Register__24CB3VehiclePhysicsAttribsFUl `0x00252100` | yes (100 files) |
| 106 | Physics/Suspension/Rear | Attatch Height | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `(char *)this + 0xA8` | Register__24CB3VehiclePhysicsAttribsFUl `0x00252100` | yes (100 files) |
| 107 | Physics/Suspension/Rear | Spring Length | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `(char *)this + 0xA4` | Register__24CB3VehiclePhysicsAttribsFUl `0x00252100` | yes (100 files) |
| 108 | Physics/Suspension/Rear | Spring Force | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `(char *)this + 0x9C` | Register__24CB3VehiclePhysicsAttribsFUl `0x00252100` | yes (100 files) |
| 109 | Physics/Suspension/Rear | Spring Damping | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `(char *)this + 0xA0` | Register__24CB3VehiclePhysicsAttribsFUl `0x00252100` | yes (100 files) |
| 110 | Physics/Vehicle | Mass (Kg) | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_B8` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (100 files) |
| 111 | Physics/Suspension/Front | Attatch Height | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_BC` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (100 files) |
| 112 | Physics/Suspension/Front | Spring Length | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_C8` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (100 files) |
| 113 | Physics/Suspension/Front | Spring Force | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_C0` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (100 files) |
| 114 | Physics/Suspension/Front | Spring Damping | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_C4` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (100 files) |
| 115 | Physics/Suspension/Rear | Attatch Height | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_D8` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (100 files) |
| 116 | Physics/Suspension/Rear | Spring Length | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_D4` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (100 files) |
| 117 | Physics/Suspension/Rear | Spring Force | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_CC` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (100 files) |
| 118 | Physics/Suspension/Rear | Spring Damping | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_D0` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (100 files) |
| 119 | Physics/Race Car/Body Roll | Brake Force Height | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_138` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 120 | Physics/Race Car/Body Roll | Accel Force Height | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_13C` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 121 | Physics/Race Car/Body Roll | Steer Force Height | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_140` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 122 | Physics/Race Car/Body Roll | Drift Force Height | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_144` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 123 | Physics/Race Car/LSDM | Speed Limit | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_17C` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 124 | Physics/Race Car/LSDM | Steering Angle | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_180` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 125 | Physics/Race Car/LSDM | Torque 1 | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_184` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 126 | Physics/Race Car/LSDM | Torque 2 | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_188` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 127 | Physics/Race Car/Misc | Drag Coef | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_130` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 128 | Physics/Race Car/Misc | Downforce Coef | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_134` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 129 | Physics/Race Car/Misc | Wall Collsion Penalty | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_178` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 130 | Physics/Race Car/Misc | Accel Multiplier | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_18C` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 131 | Physics/Race Car/Misc | Braking Factor | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_15C` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 132 | Physics/Race Car/Misc | Engine Braking Factor | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_19C` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 133 | Physics/Race Car/Misc | Max Speed In Boost | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_1A4` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 134 | Physics/Race Car/Misc | Min Drift Angle In Air | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_1B4` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 135 | Physics/Race Car/Misc | In Air Corkscrew Damping | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_1B0` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 136 | Physics/Transmission/Engine | Torque | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_11C` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 137 | Physics/Transmission/Engine | Peak Torque Revs | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_120` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 138 | Physics/Transmission/Engine | Fall Off Torque Revs | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_124` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 139 | Physics/Transmission/Engine | Idle RPM | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_104` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 140 | Physics/Transmission/Engine | Max RPM | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_118` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 141 | Physics/Transmission/Engine | Change Down RPM | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_10C` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 142 | Physics/Transmission/Engine | Change Up RPM | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_108` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 143 | Physics/Transmission/Gear Ratios | Reverse | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_E0` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 144 | Physics/Transmission/Gear Ratios | Neutral | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_E4` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 145 | Physics/Transmission/Gear Ratios | 1st Gear | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_E8` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 146 | Physics/Transmission/Gear Ratios | 2nd Gear | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_EC` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 147 | Physics/Transmission/Gear Ratios | 3rd Gear | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_F0` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 148 | Physics/Transmission/Gear Ratios | 4th Gear | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_F4` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 149 | Physics/Transmission/Gear Ratios | 5th Gear | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_F8` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 150 | Physics/Transmission/Gear Ratios | 6th Gear | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_FC` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 151 | Physics/Transmission/Gear Ratios | Final Gear | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_100` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 152 | Physics/Transmission/Boost Kick | Boost Kick Torque | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_128` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 153 | Physics/Transmission/Boost Kick | Boost Kick Time | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_12C` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 154 | Physics/Steering | Min Angle (degrees) | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_148` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 155 | Physics/Steering | Max Angle (degrees) | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_14C` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 156 | Physics/Steering | Min Velocity | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_150` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 157 | Physics/Steering | Max Velocity | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_154` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 158 | Physics/Steering | Response | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_158` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 159 | Physics/Steering | Drag Coef | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_190` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 160 | Physics/Drift | Slide Max | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_160` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 161 | Physics/Drift | Slide Min | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_164` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 162 | Physics/Drift | Turn Momentum Slow | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_168` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 163 | Physics/Drift | Turn Momentum Fast | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_16C` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 164 | Physics/Drift | Auto Drift Delay | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_170` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 165 | Physics/Drift | Turn Rate | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_174` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 166 | Physics/Drift | Min Drift Speed | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_194` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 167 | Physics/Drift | Max Speed Pressure | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_198` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 168 | Physics/Drift | Max Drift Angle | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_1A0` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 169 | Physics/Aggressive Driving Reaction | Steer Away Time (s) | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_1BC` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 170 | Physics/Aggressive Driving Reaction | Total Out-Of-Control Time (s) | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_1C0` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 171 | Physics/Aggressive Driving Reaction | Steering Max Angle (degrees) | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_1C4` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 172 | Physics/Aggressive Driving Reaction | Steering Max Velocity | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_1C8` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 173 | Physics/Aggressive Driving Reaction | Steering Drag Coef | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&field_1CC` | Register__24CB3RaceCarPhysicsAttribsFP17CB3VehiclePhysics `0x00252C20` | yes (67 files) |
| 174 | AI/Car | Out of range speed decrease rate | float |  | 0 / 0 | 60.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_0` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 175 | AI/Car | Max desDir angle change OOR | float |  | 0 / 0 | 1.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_4` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 176 | AI/Car | Angle you want min spd at | float |  | 0 / 0 | 38.4 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_8` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 177 | AI/Car | Top speed mps | float |  | 0 / 0 | 80.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 178 | AI/Car | Min speed mps | float |  | 0 / 0 | 10.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_10` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 179 | AI/Car | How much carAt affects steering | float |  | 0 / 0 | 0.2 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_14` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 180 | AI/Car | Angle at which drift is started | float |  | 0 / 0 | 15.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_18` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 181 | AI/Car | Max lock at 180 x degrees | float |  | 0 / 0 | 6.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_1C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 182 | AI/Car | Drift Max lock at 180 x degrees | float |  | 0 / 0 | 4.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_20` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 183 | AI/Arbitrator | Risk threshold | float |  | 0 / 0 | 4.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_24` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 184 | AI/Arbitrator | Total risk threshold | float |  | 0 / 0 | 4.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_28` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 185 | AI/Arbitrator | Current risk threshold | float |  | 0 / 0 | 4.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_2C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 186 | AI/Arbitrator | Corner Risk threshold | float |  | 0 / 0 | 4.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_30` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 187 | AI/Arbitrator | Total corner risk threshold | float |  | 0 / 0 | 4.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_34` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 188 | AI/Arbitrator | Current corner risk threshold | float |  | 0 / 0 | 4.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_38` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 189 | AI/Avoidance | AVOID: LookAhead dist racecars | float |  | 0 / 0 | 10.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_3C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 190 | AI/Avoidance | AVOID: Soft No Go offset time | float |  | 0 / 0 | 1.5 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_40` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 191 | AI/Avoidance | AVOID: Soft No Go offset distance | float |  | 0 / 0 | 75.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_44` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 192 | AI/Avoidance | Extra softNoGo offset dist for future | float |  | 0 / 0 | 20.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_74` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 193 | AI/Avoidance | Extra softNoGo offset time for future | float |  | 0 / 0 | 0.5 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_78` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 194 | AI/Avoidance | Hard No Go offset time | float |  | 0 / 0 | 0.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_48` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 195 | AI/Avoidance | Hard No Go offset distance | float |  | 0 / 0 | 200.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_4C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 196 | AI/Avoidance | Distance to discard fatally colliding racecar | float |  | 0 / 0 | 40.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_50` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 197 | AI/Avoidance | Vert dist to discard traffic and racecars | float |  | 0 / 0 | 5.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_54` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 198 | AI/Avoidance | dt between start and end vehicle | float |  | 0 / 0 | 0.5 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_58` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 199 | AI/Avoidance | Speed when car is <10m away | float |  | 0 / 0 | 10.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_5C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 200 | AI/Avoidance | Speed when car is <20m away | float |  | 0 / 0 | 25.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_60` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 201 | AI/Avoidance | Speed when car is <30m away | float |  | 0 / 0 | 40.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_64` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 202 | AI/Avoidance | Speed when risk is >0.95 | float |  | 0 / 0 | 10.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_68` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 203 | AI/Avoidance | Speed when risk is >0.9 | float |  | 0 / 0 | 30.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_6C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 204 | AI/Avoidance | Steering factor big=>extreme | float |  | 0 / 0 | 4.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_70` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 205 | AI/Avoidance | Aggression time variation factor | float |  | 0 / 0 | 1.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_7C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 206 | AI/Avoidance | Aggression dist variation factor | float |  | 0 / 0 | 10.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_80` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 207 | AI/Avoidance | Aggression time variation offset | float |  | 0 / 0 | 2.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_84` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 208 | AI/Avoidance | Aggression dist variation offset | float |  | 0 / 0 | 10.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_88` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 209 | AI/Target | Dist to brake speed factor big=>fast | float |  | 0 / 0 | 0.44 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_8C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 210 | AI/Target | Drift speed threshold | float |  | 0 / 0 | 30.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_98` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 211 | AI/Target | Dist to update target pos | float |  | 0 / 0 | 0.4 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_90` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 212 | AI/Target | Perp dist to update target pos | float |  | 0 / 0 | 0.1 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_94` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 213 | AI/Target | Corner radius to give max offset pos | float |  | 0 / 0 | 300.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_9C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 214 | AI/Target | Time until we hit the apex at which to start drifting (seconds) | float |  | 0 / 0 | 0.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_A0` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 215 | AI/Target | Number of segments to extend the start of any corner | int |  | 0 / 0 | 0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_A4` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 216 | AI/Target | Max. number of extra segments to extend the corner based on your speed | int |  | 0 / 0 | 0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_AC` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 217 | AI/Target | Number of segments to extend the start of any corner (when drifting) | int |  | 0 / 0 | 5 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_A8` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 218 | AI/Target | Max. number of extra segments to extend the corner based on your speed (when drifting) | int |  | 0 / 0 | 0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_B0` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 219 | AI/Target | How far away from the true apex to move the apex point of a corner when drifting (meters) | float |  | 0 / 0 | 0.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_B4` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 220 | AI/Driver | How close the target car must be to start proper speed matching (meters) | float |  | 0 / 0 | 10.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_B8` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 221 | AI/Driver | Maximum distance from player to start doing sticky speed matching (metres) | float |  | 0 / 0 | 10.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_BC` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 222 | AI/Driver | Maximum diffence in speeds for sticky speed matching (MPH) | float |  | 0 / 0 | 40.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_C0` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 223 | AI/Aggressive Driving | Min. aggression before we start attacking | float | 0 .. 1 | 2 / 0 | 0.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_C4` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 224 | AI/Aggressive Driving | Min. time to wait between attacks (seconds) | float |  | 0 / 0 | 0.5 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_C8` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 225 | AI/Aggressive Driving | Max. time to wait between attacks (seconds) | float |  | 0 / 0 | 2.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_CC` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 226 | AI/Aggressive Driving | Max. distance apart to begin attacking when ahead of target (meters) | float |  | 0 / 0 | 25.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_D0` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 227 | AI/Aggressive Driving | Max. distance apart to begin attacking when behind of target (meters) | float |  | 0 / 0 | 50.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_D4` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 228 | AI/Aggressive Driving | Min. target speed to consider attacking (mph) | float |  | 0 / 0 | 60.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_D8` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 229 | AI/Aggressive Driving | How much slower than the player to drive while speed matching to attack position | float |  | 0 / 0 | 0.95 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_DC` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 230 | AI/Aggressive Driving | How far in front to boost when speed matching the player | float |  | 0 / 0 | 30.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_E0` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 231 | AI/Aggressive Driving | Extra distance in front to boost relative to aggression (meters) | float |  | 0 / 0 | -22.5 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_E4` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 232 | AI/Aggressive Driving | How long to wait at the start of the race before doing aggressive driving (seconds) | float |  | 0 / 0 | 7.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_E8` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 233 | AI/Aggressive Driving | How long after hitting something to disable immunity (seconds) | float |  | 0 / 0 | 3.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_EC` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 234 | AI/Aggressive Driving/Slam | Min time to try and block you for (seconds) | float |  | 0 / 0 | 3.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_F0` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 235 | AI/Aggressive Driving/Slam | Max time to try and block you for (seconds) | float |  | 0 / 0 | 15.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_F4` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 236 | AI/Aggressive Driving/Slam | Max distance ahead to start blocking you (meters) | float |  | 0 / 0 | 15.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_F8` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 237 | AI/Aggressive Driving/Slam | Preferred car separation when getting into slam position (meters) | float |  | 0 / 0 | 4.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_FC` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 238 | AI/Aggressive Driving/Slam | Max. time to try and get into slamming position (seconds) | float |  | 0 / 0 | 30.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_100` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 239 | AI/Aggressive Driving/Slam | Max. distance between cars, when ahead (meters) | float |  | 0 / 0 | 5.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_104` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 240 | AI/Aggressive Driving/Slam | Max. difference in speeds (MPH) | float |  | 0 / 0 | 10.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_108` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 241 | AI/Aggressive Driving/Slam | Steer out distance (meters) | float |  | 0 / 0 | 1.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_10C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 242 | AI/Aggressive Driving/Slam | Steer out time (seconds) | float |  | 0 / 0 | 0.25 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_110` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 243 | AI/Aggressive Driving/Slam | Slam time (seconds) | float |  | 0 / 0 | 3.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_114` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 244 | AI/Aggressive Driving/Slam | Max. cos angle off lane to stop attack | float |  | 0 / 0 | 0.8 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_118` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 245 | AI/Aggressive Driving/Slam | How soon after starting to slam is the AI car committed and can't stop | float | 0 .. 1 | 2 / 0 | 0.1 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_11C` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 246 | AI/Aggressive Driving/Reactions | How long a rubbed AI car goes blind for (seconds) | float |  | 0 / 0 | 1.0 (store) | ../Export/ValueDB/AI/defaults.cfg | `&self->field_120` | Register__12CB3AIAttribsFv `0x00271A80` | yes |
| 247 | Sound/Traffic | Bus volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_10` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 248 | Sound/Traffic | Lorry volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_14` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 249 | Sound/Traffic | Van volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_18` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 250 | Sound/Traffic | Car volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_1C` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 251 | Sound/Traffic | Bus Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_20` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 252 | Sound/Traffic | Lorry Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_24` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 253 | Sound/Traffic | Van Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_28` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 254 | Sound/Traffic | Car Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_2C` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 255 | Sound/Traffic | Bus Engine Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_30` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 256 | Sound/Traffic | Bus Engine Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_34` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 257 | Sound/Traffic | Lorry Engine Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_38` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 258 | Sound/Traffic | Lorry Engine Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_3C` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 259 | Sound/Boost | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_40` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 260 | Sound/Boost | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_44` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 261 | Sound/Boost | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_48` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 262 | Sound/Boost | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_4C` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 263 | Sound/Boost | Sample Midpoint | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_50` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 264 | Sound/Boost | Car Length | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_54` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 265 | Sound/Boost | DampTime | int |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_58` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 266 | Sound/Boost | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_5C` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 267 | Sound/Boost | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_60` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 268 | Sound/Boost | Plane Width | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_64` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 269 | Sound/Boost | Centre Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_68` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 270 | Sound/Boost | Edge Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_6C` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 271 | Sound/Boost | Little Pass Scalar | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_74` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 272 | Sound/Boost | Big Pass Scalar | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Boost.cfg | `&field_70` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 273 | Sound/Traffic | Horn Min On Time | int |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_C0` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 274 | Sound/Traffic | Horn Min Off Time | int |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_C4` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 275 | Sound/Traffic | Horn Max On Time | int |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_C8` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 276 | Sound/Traffic | Horn Max Off Time | int |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_CC` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 277 | Sound/Traffic | Horn Speed of Sound | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_A8` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 278 | Sound/Traffic | Horn Max Distance | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_AC` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 279 | Sound/Traffic | Horn Pitch Variance | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_B0` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 280 | Sound/Traffic | Truck Horn Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_B8` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 281 | Sound/Traffic | Car Horn Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_B4` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 282 | Sound/Traffic | Horn Linear Scalar | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_BC` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 283 | Sound/CrashingTraffic/Skids/Large | Large Crashing Traffic Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_90` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 284 | Sound/CrashingTraffic/Skids/Large | Large Crashing Traffic Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_94` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 285 | Sound/CrashingTraffic/Skids/Large | Large Crashing Traffic Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_98` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 286 | Sound/CrashingTraffic/Skids/Large | Large Crashing Traffic Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_9C` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 287 | Sound/CrashingTraffic/Skids/Large | Large Crashing Traffic Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_A0` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 288 | Sound/CrashingTraffic/Skids/Large | Large Crashing Traffic Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_A4` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 289 | Sound/CrashingTraffic/Skids/Small | Small Crashing Traffic Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_78` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 290 | Sound/CrashingTraffic/Skids/Small | Small Crashing Traffic Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_7C` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 291 | Sound/CrashingTraffic/Skids/Small | Small Crashing Traffic Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_80` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 292 | Sound/CrashingTraffic/Skids/Small | Small Crashing Traffic Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_84` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 293 | Sound/CrashingTraffic/Skids/Small | Small Crashing Traffic Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_88` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 294 | Sound/CrashingTraffic/Skids/Small | Small Crashing Traffic Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Traffic.cfg | `&field_8C` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 295 | Sound/Doppler | Point Distances | float |  | 0 / 4 |  | ../export/ValueDB/Sound/Traffic.cfg | `&D_01E86D20` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 296 | Sound/Doppler | Point Scalar | float |  | 0 / 4 |  | ../export/ValueDB/Sound/Traffic.cfg | `&D_01E86D30` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 297 | Sound/Doppler | Base Pitch | float |  | 0 / 4 |  | ../export/ValueDB/Sound/Traffic.cfg | `&D_01E86D40` | Construct__22CB3SoundTrafficManagerFv `0x00277360` | yes |
| 298 | Sound/L Train/ | Max distance | float |  | 0 / 0 | 100 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_grLTrainMaxAudibleDistance` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 299 | Sound/L Train/ | Min distance | float |  | 0 / 0 | 70 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_grLTrainMinAudibleDistance` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 300 | Sound/L Train/ | Max pitch gain | float |  | 0 / 0 | 1.05 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_grTrainPitchVariationMax` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 301 | Sound/L Train/ | Min pitch gain | float |  | 0 / 0 | 0.95 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_grTrainPitchVariationMin` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 302 | Sound/L Train/ | Pitch gain step | float |  | 0 / 0 | 0.01 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_grTrainPitchVariationStep` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 303 | Sound/L Train/ | Squeak base pitch | float |  | 0 / 0 | 0.9 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_grLTrainSqueakPitchBase` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 304 | Sound/L Train/ | Squeak pitch range | float |  | 0 / 0 | 0.2 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_grLTrainSqueakPitchRange` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 305 | Sound/L Train/ | Length of squeak (frames) | int |  | 0 / 0 | 45 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_gnUpdatesDuringSqueak` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 306 | Sound/L Train/ | Frames between squeaks | int |  | 0 / 0 | 120 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_gnUpdatesBetweenSqueaks` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 307 | Sound/L Train/ | Squeak duration variation (frames) | int |  | 0 / 0 | 30 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_gnSqueakDurationVariation` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 308 | Sound/L Train/ | Time between squeaks variation (frames) | int |  | 0 / 0 | 30 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_gnTimeBetweenSqueaksVariation` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 309 | Sound/L Train/ | Rails volume | float |  | 0 / 0 | 1 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_grLTrainRailsVolume` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 310 | Sound/L Train/ | Squeak base volume | float |  | 0 / 0 | 0.9 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_grLTrainSqueakVolumeBase` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 311 | Sound/L Train/ | Squeak volume range | float |  | 0 / 0 | 0.1 (data) | ../export/ValueDB/Sound/LTrain.cfg | `&_grLTrainSqueakVolumeRange` | Construct__22CB3SoundTrafficVehicleFv `0x0027A5F0` | yes |
| 312 | Sound/Prop/Cone/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1B4` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 313 | Sound/Prop/Cone/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1B0` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 314 | Sound/Prop/Cone/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1BC` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 315 | Sound/Prop/Cone/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1B8` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 316 | Sound/Prop/Card Box/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_244` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 317 | Sound/Prop/Card Box/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_240` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 318 | Sound/Prop/Card Box/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_24C` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 319 | Sound/Prop/Card Box/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_248` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 320 | Sound/Prop/Metal Box/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1E4` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 321 | Sound/Prop/Metal Box/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1E0` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 322 | Sound/Prop/Metal Box/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1EC` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 323 | Sound/Prop/Metal Box/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1E8` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 324 | Sound/Prop/Woodbox/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1CC` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 325 | Sound/Prop/Woodbox/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1C8` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 326 | Sound/Prop/Woodbox/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1D4` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 327 | Sound/Prop/Woodbox/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1D0` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 328 | Sound/Prop/Signpost/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_22C` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 329 | Sound/Prop/Signpost/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_228` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 330 | Sound/Prop/Signpost/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_234` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 331 | Sound/Prop/Signpost/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_230` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 332 | Sound/Prop/Barrier/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1FC` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 333 | Sound/Prop/Barrier/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_1F8` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 334 | Sound/Prop/Barrier/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_204` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 335 | Sound/Prop/Barrier/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_200` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 336 | Sound/Prop/Metal Barrier/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_214` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 337 | Sound/Prop/Metal Barrier/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_210` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 338 | Sound/Prop/Metal Barrier/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_21C` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 339 | Sound/Prop/Metal Barrier/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Tracks/<route>.cfg (built at run time) | `&field_218` | Prepare__20CB3SoundCrashManagerFv `0x00281C30` | yes (9 files) |
| 340 | Sound/Prop/Barrel/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_730` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 341 | Sound/Prop/Barrel/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_734` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 342 | Sound/Prop/Barrel/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_738` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 343 | Sound/Prop/Barrel/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_73C` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 344 | Sound/Prop/Crate/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_740` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 345 | Sound/Prop/Crate/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_744` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 346 | Sound/Prop/Crate/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_748` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 347 | Sound/Prop/Crate/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_74C` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 348 | Sound/Prop/Boat/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_750` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 349 | Sound/Prop/Boat/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_754` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 350 | Sound/Prop/Boat/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_758` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 351 | Sound/Prop/Boat/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_75C` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 352 | Sound/Prop/Hay Bale/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_760` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 353 | Sound/Prop/Hay Bale/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_764` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 354 | Sound/Prop/Hay Bale/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_768` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 355 | Sound/Prop/Hay Bale/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_76C` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 356 | Sound/Prop/Hay Drum/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_770` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 357 | Sound/Prop/Hay Drum/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_774` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 358 | Sound/Prop/Hay Drum/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_778` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 359 | Sound/Prop/Hay Drum/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_77C` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 360 | Sound/Prop/Cable Drum/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_780` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 361 | Sound/Prop/Cable Drum/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_784` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 362 | Sound/Prop/Cable Drum/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_788` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 363 | Sound/Prop/Cable Drum/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_78C` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 364 | Sound/Prop/Container/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_790` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 365 | Sound/Prop/Container/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_794` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 366 | Sound/Prop/Container/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_798` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 367 | Sound/Prop/Container/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_79C` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 368 | Sound/Prop/Log/ | Max Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_7A0` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 369 | Sound/Prop/Log/ | Min Impulse | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_7A4` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 370 | Sound/Prop/Log/ | Max Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_7A8` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 371 | Sound/Prop/Log/ | Min Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Payload.cfg | `&field_7AC` | RegisterVariables__20CB3SoundCrashManagerFv `0x00282AC0` | yes |
| 372 | Sound/Hud/General | Popup Message Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x20)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 373 | Sound/Hud/General | Position Message Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x24)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 374 | Sound/Hud/General | Lap Message Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x28)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 375 | Sound/Hud/General | Touch Message Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x2C)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 376 | Sound/Hud/General | Slam Message Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x30)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 377 | Sound/Hud/General | Slammed Message Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x34)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 378 | Sound/Hud/General | Taken down Message Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x38)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 379 | Sound/Hud/General | Taken out Message Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x3C)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 380 | Sound/Hud/General | Crash Escape Message Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x40)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 381 | Sound/Hud/General | Crash Bonus Message Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x44)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 382 | Sound/Hud/General | Lost Boost Segment Gain | float |  | 0 / 0 | 0.8 (data) | ../export/ValueDB/Sound/HUD.cfg | `&D_004E1AFC` | Construct__18CB3HUDSoundManagerFv `0x00287240` | no |
| 383 | Sound/Hud/General | Gained Boost Segment Gain | float |  | 0 / 0 | 1 (data) | ../export/ValueDB/Sound/HUD.cfg | `&D_004E1B00` | Construct__18CB3HUDSoundManagerFv `0x00287240` | no |
| 384 | Sound/Hud/Aggressive Driving | 3d Shunt Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x58)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 385 | Sound/Hud/Aggressive Driving | 3d Slam Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/HUD.cfg | `((char *)this + 0x5C)` | Construct__18CB3HUDSoundManagerFv `0x00287240` | yes |
| 386 | Sound/Esm | Rpm Modulation Max Amplitude | float |  | 0 / 0 | 250 (data) | ../export/ValueDB/Sound/Esm.cfg | `&krRpmModulationMaxAmp` | func_00288C20 `0x00288C20` | yes |
| 387 | Sound/Esm | Oscillator period | float |  | 0 / 0 | 1.2 (data) | ../export/ValueDB/Sound/Esm.cfg | `&krRpmModulationPeriod` | func_00288C20 `0x00288C20` | yes |
| 388 | Sound/Esm | Total Modulation Time (seconds) | float |  | 0 / 0 | 0.5 (data) | ../export/ValueDB/Sound/Esm.cfg | `&krRpmModulationMinTime` | func_00288C20 `0x00288C20` | yes |
| 389 | Sound/Esm | Time Till Full Air Saturation | float |  | 0 / 0 | 0.5 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B28` | func_00288C20 `0x00288C20` | yes |
| 390 | Sound/Esm | Full Air Saturation Scalar | float |  | 0 / 0 | 1.08 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B2C` | func_00288C20 `0x00288C20` | yes |
| 391 | Sound/Esm | Time to modulate on landing | float |  | 0 / 0 | 1 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B30` | func_00288C20 `0x00288C20` | yes |
| 392 | Sound/Esm | Time Till Full Offroad Saturation | float |  | 0 / 0 | 2 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B34` | func_00288C20 `0x00288C20` | yes |
| 393 | Sound/Esm | Offroad Oscillator period | float |  | 0 / 0 | 1.2 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B38` | func_00288C20 `0x00288C20` | yes |
| 394 | Sound/Esm | Offroad Max Amplitude | float |  | 0 / 0 | 250 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B3C` | func_00288C20 `0x00288C20` | yes |
| 395 | Sound/Esm | Offroad Modulation Start Speed (percent of top) | float |  | 0 / 0 | 0.3 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B40` | func_00288C20 `0x00288C20` | yes |
| 396 | Sound/Esm | Offroad Modulation End Speed (percent of top) | float |  | 0 / 0 | 0.8 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B44` | func_00288C20 `0x00288C20` | yes |
| 397 | Sound/Esm | Offroad Modulation Period Min | float |  | 0 / 0 | 10 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B4C` | func_00288C20 `0x00288C20` | yes |
| 398 | Sound/Esm | Offroad Modulation Period Max | float |  | 0 / 0 | 40 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B50` | func_00288C20 `0x00288C20` | yes |
| 399 | Sound/Esm | Offroad Lerp time | float |  | 0 / 0 | 0.5 (data) | ../export/ValueDB/Sound/Esm.cfg | `&D_004E1B48` | func_00288C20 `0x00288C20` | yes |
| 400 | Sound/Voice | Master Sfx Volume | float |  | 0 / 0 | 1 (data) | ../export/ValueDB/Sound/Voice.cfg | `&_grMasterVolumeScalar` | func_0028B580 `0x0028B580` | yes |
| 401 | VehicleAudioAI | Car Volume | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudioAI/<vehicle>.cfg (built at run time) | `(field_68 + 36)` | Register__13CB3SoundAICarFUl `0x0028BA20` | yes (56 files) |
| 402 | VehicleAudio | Car Volume | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `(field_68 + 36)` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (67 files) |
| 403 | VehicleAudio/Turbo | Wave Attack Offset | int |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `field_68` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (67 files) |
| 404 | VehicleAudio/Turbo | Rpm to Turbo Falloff Lag | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `(field_68 + 4)` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (67 files) |
| 405 | VehicleAudio/Turbo | Rpm to Turbo Rising Lag | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `(field_68 + 8)` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (67 files) |
| 406 | VehicleAudio/Turbo | Rpm to Turbo Min Rpm | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `(field_68 + 20)` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (67 files) |
| 407 | VehicleAudio/Turbo | Rpm to Turbo Max Rpm Volume | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `(field_68 + 24)` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (67 files) |
| 408 | VehicleAudio/Turbo | Dump Valve Kick-in Min Falloff | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `(field_68 + 12)` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (67 files) |
| 409 | VehicleAudio/Turbo | Dump Valve Kick-in Max Falloff | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `(field_68 + 16)` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (67 files) |
| 410 | VehicleAudio/Turbo | Dump Valve Volume Min | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `(field_68 + 28)` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (67 files) |
| 411 | VehicleAudio/Turbo | Dump Valve Volume Max | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `(field_68 + 32)` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (67 files) |
| 412 | VehicleAudio | Gear Change Volume | float |  | 0 / 0 |  | ../Export/ValueDB/VehicleAudio/<vehicle>.cfg (built at run time) | `(field_68 + 40)` | Register__17CB3SoundPlayerCarFUl `0x0028C780` | yes (60 files) |
| 413 | Sound/Misc | Boost Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Misc.cfg | `&_grBoostVol` | Construct__15CB3SoundRaceCarFv `0x0028DD80` | yes |
| 414 | Sound/Boost | Boost Ready Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Misc.cfg | `&_grBoostReadyVol` | Construct__15CB3SoundRaceCarFv `0x0028DD80` | yes |
| 415 | Sound/Boost | Boost In Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Misc.cfg | `&_grBoostInVol` | Construct__15CB3SoundRaceCarFv `0x0028DD80` | yes |
| 416 | Sound/Boost | Boost Out Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Misc.cfg | `&_grBoostOutVol` | Construct__15CB3SoundRaceCarFv `0x0028DD80` | yes |
| 417 | Sound/Boost | Boost Chain Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Misc.cfg | `&_grBoostChainVol` | Construct__15CB3SoundRaceCarFv `0x0028DD80` | yes |
| 418 | Sound/Boost | Boost Loop Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Misc.cfg | `&_grBoostLoopVol` | Construct__15CB3SoundRaceCarFv `0x0028DD80` | yes |
| 419 | Sound/Boost | AI Boost In Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Misc.cfg | `&_grAIBoostInVol` | Construct__15CB3SoundRaceCarFv `0x0028DD80` | yes |
| 420 | Sound/Boost | AI Boost Out Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Misc.cfg | `&_grAIBoostOutVol` | Construct__15CB3SoundRaceCarFv `0x0028DD80` | yes |
| 421 | Sound/Boost | AI Boost Loop Volume | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Misc.cfg | `&_grAIBoostLoopVol` | Construct__15CB3SoundRaceCarFv `0x0028DD80` | yes |
| 422 | Sound/Skids | Maximum Slip | float |  | 0 / 0 | 20 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_grMaxSlip` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 423 | Sound/Skids | Maximum Spin | float |  | 0 / 0 | 20 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_grMaxSpin` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 424 | Sound/Skids | Emitter Distance AT | float |  | 0 / 0 | 1 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_grEmitterDistanceAt` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 425 | Sound/Skids | Emitter Distance RIGHT | float |  | 0 / 0 | 3 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_grEmitterDistanceRight` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 426 | Sound/Skids | SlipSpin Lag Min | float |  | 0 / 0 | -0.4 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_grSlipSpinLagMin` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 427 | Sound/Skids | SlipSpin Lag Max | float |  | 0 / 0 | 1 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_grSlipSpinLagMax` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 428 | Sound/Skids | Wheel Mode 'Driving' Multipler | float |  | 0 / 0 | 1 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_garWheelModeMultiplers` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 429 | Sound/Skids | Wheel Mode 'StartLine' Multipler | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_garWheelModeMultiplers + 0x4)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 430 | Sound/Skids | Wheel Mode 'Donuts' Multipler | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_garWheelModeMultiplers + 0x8)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 431 | Sound/Skids | Wheel Mode 'Drift' Multipler | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_garWheelModeMultiplers + 0xC)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 432 | Sound/Skids | Wheel Mode 'Skid' Multipler | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_garWheelModeMultiplers + 0x10)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 433 | Sound/Skids | Wheel Mode 'LockUp' Multipler | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_garWheelModeMultiplers + 0x14)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 434 | Sound/Skids | Wheel Mode 'OutOfControl' Multipler | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_garWheelModeMultiplers + 0x18)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 435 | Sound/Skids | Left Pitch Offset | float |  | 0 / 0 | 1000 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_grLeftPitchOffset` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 436 | Sound/Skids | Right Pitch Offset | float |  | 0 / 0 | -1000 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_grRightPitchOffset` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 437 | Sound/Skids | Crash Volume Multipler | float |  | 0 / 0 | 0.5 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_grCrashVolumeMultipler` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 438 | Sound/Skids | Slip Speed Multipler | float |  | 0 / 0 | 0.025 (data) | ../export/ValueDB/Sound/Skids.cfg | `&_grSlipSpeedMultipler` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 439 | Sound/Skids/Sample1 | Sample 1 Slip Volume Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x8)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 440 | Sound/Skids/Sample1 | Sample 1 Slip Volume Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xC)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 441 | Sound/Skids/Sample1 | Sample 1 Slip Volume Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x10)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 442 | Sound/Skids/Sample1 | Sample 1 Slip Volume Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x14)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 443 | Sound/Skids/Sample1/Variant1 | Sample 1 Slip Frequency Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x18)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 444 | Sound/Skids/Sample1/Variant1 | Sample 1 Slip Frequency Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x1C)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 445 | Sound/Skids/Sample1/Variant1 | Sample 1 Slip Frequency Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x20)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 446 | Sound/Skids/Sample1/Variant1 | Sample 1 Slip Frequency Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x24)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 447 | Sound/Skids/Sample1 | Sample 1 Spin Volume Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x28)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 448 | Sound/Skids/Sample1 | Sample 1 Spin Volume Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x2C)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 449 | Sound/Skids/Sample1 | Sample 1 Spin Volume Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x30)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 450 | Sound/Skids/Sample1 | Sample 1 Spin Volume Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x34)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 451 | Sound/Skids/Sample1/Variant1 | Sample 1 Spin Frequency Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x38)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 452 | Sound/Skids/Sample1/Variant1 | Sample 1 Spin Frequency Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x3C)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 453 | Sound/Skids/Sample1/Variant1 | Sample 1 Spin Frequency Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x40)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 454 | Sound/Skids/Sample1/Variant1 | Sample 1 Spin Frequency Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x44)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 455 | Sound/Skids/Sample2 | Sample 2 Slip Volume Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x50)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 456 | Sound/Skids/Sample2 | Sample 2 Slip Volume Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x54)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 457 | Sound/Skids/Sample2 | Sample 2 Slip Volume Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x58)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 458 | Sound/Skids/Sample2 | Sample 2 Slip Volume Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x5C)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 459 | Sound/Skids/Sample2/Variant1 | Sample 2 Slip Frequency Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x60)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 460 | Sound/Skids/Sample2/Variant1 | Sample 2 Slip Frequency Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x64)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 461 | Sound/Skids/Sample2/Variant1 | Sample 2 Slip Frequency Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x68)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 462 | Sound/Skids/Sample2/Variant1 | Sample 2 Slip Frequency Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x6C)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 463 | Sound/Skids/Sample2 | Sample 2 Spin Volume Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x70)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 464 | Sound/Skids/Sample2 | Sample 2 Spin Volume Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x74)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 465 | Sound/Skids/Sample2 | Sample 2 Spin Volume Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x78)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 466 | Sound/Skids/Sample2 | Sample 2 Spin Volume Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x7C)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 467 | Sound/Skids/Sample2/Variant1 | Sample 2 Spin Frequency Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x80)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 468 | Sound/Skids/Sample2/Variant1 | Sample 2 Spin Frequency Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x84)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 469 | Sound/Skids/Sample2/Variant1 | Sample 2 Spin Frequency Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x88)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 470 | Sound/Skids/Sample2/Variant1 | Sample 2 Spin Frequency Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x8C)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 471 | Sound/Skids/Sample3 | Sample 3 Slip Volume Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x98)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 472 | Sound/Skids/Sample3 | Sample 3 Slip Volume Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0x9C)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 473 | Sound/Skids/Sample3 | Sample 3 Slip Volume Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xA0)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 474 | Sound/Skids/Sample3 | Sample 3 Slip Volume Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xA4)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 475 | Sound/Skids/Sample3/Variant1 | Sample 3 Slip Frequency Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xA8)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 476 | Sound/Skids/Sample3/Variant1 | Sample 3 Slip Frequency Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xAC)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 477 | Sound/Skids/Sample3/Variant1 | Sample 3 Slip Frequency Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xB0)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 478 | Sound/Skids/Sample3/Variant1 | Sample 3 Slip Frequency Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xB4)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 479 | Sound/Skids/Sample3 | Sample 3 Spin Volume Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xB8)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 480 | Sound/Skids/Sample3 | Sample 3 Spin Volume Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xBC)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 481 | Sound/Skids/Sample3 | Sample 3 Spin Volume Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xC0)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 482 | Sound/Skids/Sample3 | Sample 3 Spin Volume Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xC4)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 483 | Sound/Skids/Sample3/Variant1 | Sample 3 Spin Frequency Input Point  0 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xC8)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 484 | Sound/Skids/Sample3/Variant1 | Sample 3 Spin Frequency Output Point 0 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xCC)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 485 | Sound/Skids/Sample3/Variant1 | Sample 3 Spin Frequency Input Point  1 (X) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xD0)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 486 | Sound/Skids/Sample3/Variant1 | Sample 3 Spin Frequency Output Point 1 (Y) | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Skids.cfg | `((char *)&_gaSkidGraphs + 0xD4)` | Construct__17CB3SoundSkidModelFv `0x0028ED30` | yes |
| 487 | Sound/Misc/Vehicle Volume Tailoff | Start Speed (Percent of MaxSpeed) | float |  | 0 / 0 | 0.6 (data) | ../export/ValueDB/Sound/Misc.cfg | `&_grVolumeTailoffStartSpeedPercent` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 488 | Sound/Misc/Vehicle Volume Tailoff | End Speed (Percent of MaxSpeed) | float |  | 0 / 0 | 0.85 (data) | ../export/ValueDB/Sound/Misc.cfg | `&_grVolumeTailoffEndSpeedPercent` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 489 | Sound/Misc/Vehicle Volume Tailoff | End Volume | float |  | 0 / 0 | 0.5 (data) | ../export/ValueDB/Sound/Misc.cfg | `&D_004E1CE8` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 490 | Sound/Misc/Vehicle Volume Tailoff | Race Car Master Gain | float |  | 0 / 0 | 1 (data) | ../export/ValueDB/Sound/Misc.cfg | `&D_004E1CDC` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 491 | Sound/Misc/Vehicle Volume Tailoff | High Detail ESM 2 player gain | float |  | 0 / 0 | 0.65 (data) | ../export/ValueDB/Sound/Misc.cfg | `&D_004E1CD0` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 492 | Sound/Misc/Vehicle Volume Tailoff | Low Detail ESM 2 player gain | float |  | 0 / 0 | 1 (data) | ../export/ValueDB/Sound/Misc.cfg | `&_grVolumeTailoffEndVolume` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | no |
| 493 | None | Tarmac Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `field_0` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 494 | None | Tarmac Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_28[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 495 | None | Tarmac Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_50[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 496 | None | Tarmac Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_78[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 497 | None | Tarmac Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_A0[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 498 | None | Tarmac Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_C8[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 499 | Tarmac | Tarmac Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_0[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 500 | Tarmac | Tarmac Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_28[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 501 | Tarmac | Tarmac Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_50[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 502 | Tarmac | Tarmac Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_78[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 503 | Tarmac | Tarmac Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_A0[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 504 | Tarmac | Tarmac Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_C8[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 505 | Concrete | Tarmac Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_0[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 506 | Concrete | Tarmac Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_28[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 507 | Concrete | Tarmac Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_50[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 508 | Concrete | Tarmac Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_78[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 509 | Concrete | Tarmac Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_A0[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 510 | Concrete | Tarmac Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_C8[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 511 | Offline Tarmac | Tarmac Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_0[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 512 | Offline Tarmac | Tarmac Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_28[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 513 | Offline Tarmac | Tarmac Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_50[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 514 | Offline Tarmac | Tarmac Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_78[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 515 | Offline Tarmac | Tarmac Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_A0[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 516 | Offline Tarmac | Tarmac Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_C8[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 517 | Gravel | Tarmac Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_0[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 518 | Gravel | Tarmac Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_28[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 519 | Gravel | Tarmac Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_50[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 520 | Gravel | Tarmac Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_78[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 521 | Gravel | Tarmac Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_A0[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 522 | Gravel | Tarmac Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_C8[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 523 | Offline Gravel | Tarmac Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_0[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 524 | Offline Gravel | Tarmac Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_28[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 525 | Offline Gravel | Tarmac Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_50[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 526 | Offline Gravel | Tarmac Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_78[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 527 | Offline Gravel | Tarmac Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_A0[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 528 | Offline Gravel | Tarmac Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_C8[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 529 | Pavement | Tarmac Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_0[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 530 | Pavement | Tarmac Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_28[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 531 | Pavement | Tarmac Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_50[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 532 | Pavement | Tarmac Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_78[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 533 | Pavement | Tarmac Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_A0[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 534 | Pavement | Tarmac Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_C8[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 535 | Snow | Tarmac Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_0[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 536 | Snow | Tarmac Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_28[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 537 | Snow | Tarmac Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_50[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 538 | Snow | Tarmac Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_78[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 539 | Snow | Tarmac Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_A0[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 540 | Snow | Tarmac Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_C8[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 541 | Metal | Tarmac Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_0[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 542 | Metal | Tarmac Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_28[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 543 | Metal | Tarmac Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_50[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 544 | Metal | Tarmac Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_78[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 545 | Metal | Tarmac Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_A0[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 546 | Metal | Tarmac Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_C8[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 547 | Wood | Tarmac Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_0[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 548 | Wood | Tarmac Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_28[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 549 | Wood | Tarmac Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_50[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 550 | Wood | Tarmac Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_78[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 551 | Wood | Tarmac Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_A0[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 552 | Wood | Tarmac Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_C8[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 553 | None | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_F0[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 554 | None | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_118[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 555 | None | Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_140[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 556 | None | Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_168[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 557 | None | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_190[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 558 | None | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_1B8[0]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 559 | Tarmac | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_F0[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 560 | Tarmac | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_118[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 561 | Tarmac | Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_140[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 562 | Tarmac | Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_168[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 563 | Tarmac | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_190[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 564 | Tarmac | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_1B8[1]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 565 | Concrete | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_F0[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 566 | Concrete | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_118[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 567 | Concrete | Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_140[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 568 | Concrete | Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_168[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 569 | Concrete | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_190[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 570 | Concrete | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_1B8[2]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 571 | Offline Tarmac | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_F0[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 572 | Offline Tarmac | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_118[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 573 | Offline Tarmac | Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_140[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 574 | Offline Tarmac | Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_168[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 575 | Offline Tarmac | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_190[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 576 | Offline Tarmac | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_1B8[3]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 577 | Gravel | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_F0[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 578 | Gravel | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_118[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 579 | Gravel | Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_140[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 580 | Gravel | Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_168[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 581 | Gravel | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_190[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 582 | Gravel | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_1B8[4]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 583 | Offline Gravel | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_F0[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 584 | Offline Gravel | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_118[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 585 | Offline Gravel | Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_140[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 586 | Offline Gravel | Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_168[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 587 | Offline Gravel | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_190[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 588 | Offline Gravel | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_1B8[7]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 589 | Pavement | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_F0[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 590 | Pavement | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_118[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 591 | Pavement | Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_140[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 592 | Pavement | Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_168[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 593 | Pavement | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_190[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 594 | Pavement | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_1B8[5]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 595 | Snow | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_F0[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 596 | Snow | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_118[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 597 | Snow | Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_140[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 598 | Snow | Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_168[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 599 | Snow | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_190[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 600 | Snow | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_1B8[6]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 601 | Metal | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_F0[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 602 | Metal | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_118[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 603 | Metal | Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_140[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 604 | Metal | Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_168[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 605 | Metal | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_190[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 606 | Metal | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_1B8[8]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 607 | Wood | Min Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_F0[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 608 | Wood | Max Speed | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_118[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 609 | Wood | Min Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_140[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 610 | Wood | Max Vol | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_168[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 611 | Wood | Min Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_190[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 612 | Wood | Max Pitch | float |  | 0 / 0 |  | ../export/ValueDB/Sound/Surface.cfg | `&field_1B8[9]` | Construct__22CB3SoundRaceCarManagerFv `0x002ED690` | yes |
| 613 | Crash/HandyCam/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 614 | Crash/HandyCam/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_10` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 615 | Crash/HandyCam/ | Safe FOV Horiz | float |  | 0 / 0 | 0.1 (store) | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_20` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 616 | Crash/HandyCam/ | Safe FOV Vert | float |  | 0 / 0 | 0.5 (store) | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_24` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 617 | InGame/Effect/Burnout/Position/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_30` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 618 | InGame/Effect/Burnout/Position/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_40` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 619 | InGame/Effect/Burnout/Position/ | Force | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_50` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 620 | InGame/Effect/Burnout/Angular/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_60` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 621 | InGame/Effect/Burnout/Angular/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_70` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 622 | InGame/Effect/Burnout/Angular/ | Force | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_80` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 623 | InGame/Effect/Shunt/Position/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_90` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 624 | InGame/Effect/Shunt/Position/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_A0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 625 | InGame/Effect/Shunt/Position/ | Force | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_B0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 626 | InGame/Effect/Shunt/Angular/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_C0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 627 | InGame/Effect/Shunt/Angular/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_D0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 628 | InGame/Effect/Shunt/Angular/ | Force | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_E0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 629 | InGame/Effect/Shunted/Position/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_F0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 630 | InGame/Effect/Shunted/Position/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_100` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 631 | InGame/Effect/Shunted/Position/ | Force | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_110` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 632 | InGame/Effect/Shunted/Angular/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_120` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 633 | InGame/Effect/Shunted/Angular/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_130` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 634 | InGame/Effect/Shunted/Angular/ | Force | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_140` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 635 | InGame/Effect/Slam/Position/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_150` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 636 | InGame/Effect/Slam/Position/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_160` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 637 | InGame/Effect/Slam/Position/ | Force | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_170` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 638 | InGame/Effect/Slam/Angular/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_180` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 639 | InGame/Effect/Slam/Angular/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_190` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 640 | InGame/Effect/Slam/Angular/ | Force | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_1A0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 641 | InGame/Effect/Shake/Position/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_1B0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 642 | InGame/Effect/Shake/Position/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_1C0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 643 | InGame/Effect/Shake/Position/ | Force | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_1D0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 644 | InGame/Effect/Shake/Angular/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_1E0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 645 | InGame/Effect/Shake/Angular/ | Damping | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_1F0` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 646 | InGame/Effect/Shake/Angular/ | Force | vector |  |  |  | ../Export/ValueDB/Camera/Camera.cfg (built at run time) | `&field_200` | RegisterVariables__16CB3CameraAttribsFv `0x002F7E70` | yes (1 files) |
| 647 | Camera/Bumper/ | Camera Offset | vector |  |  |  | ../Export/ValueDB/Camera/<vehicle>.cfg (built at run time) | `&field_30` | RegisterVariables__19CB3CameraAttribsCarFv `0x002F8870` | yes (67 files) |
| 648 | Camera/Look Back/ | Camera Offset | vector |  |  |  | ../Export/ValueDB/Camera/<vehicle>.cfg (built at run time) | `&field_0` | RegisterVariables__19CB3CameraAttribsCarFv `0x002F8870` | yes (67 files) |
| 649 | Camera/Follow/ | Camera Offset | vector |  |  |  | ../Export/ValueDB/Camera/<vehicle>.cfg (built at run time) | `&field_60` | RegisterVariables__19CB3CameraAttribsCarFv `0x002F8870` | yes (67 files) |
| 650 | Camera/Follow/ | Focus Offset | vector |  |  |  | ../Export/ValueDB/Camera/<vehicle>.cfg (built at run time) | `&field_70` | RegisterVariables__19CB3CameraAttribsCarFv `0x002F8870` | yes (67 files) |
| 651 | Camera/Follow/ | Spring Coeff | vector |  |  |  | ../Export/ValueDB/Camera/<vehicle>.cfg (built at run time) | `&field_80` | RegisterVariables__19CB3CameraAttribsCarFv `0x002F8870` | yes (67 files) |
| 652 | Camera/Follow/ | Down Angle | float |  | 0 / 0 | 1.8 (store) | ../Export/ValueDB/Camera/<vehicle>.cfg (built at run time) | `&field_90` | RegisterVariables__19CB3CameraAttribsCarFv `0x002F8870` | yes (67 files) |
| 653 | Bloom Parameters | Threshold | float | 0 .. 1 | 2 / 0 | 0.75 (store) | ../export/ValueDB/TrackBloom/<route>.cfg (built at run time) | `&self->field_10` | Prepare__14CB3StaticTrackFv `0x00332E00` | yes (8 files) |
| 654 | Bloom Parameters | Intensity | float | 0 .. 1 | 2 / 0 | 1.0 (store) | ../export/ValueDB/TrackBloom/<route>.cfg (built at run time) | `&self->field_14` | Prepare__14CB3StaticTrackFv `0x00332E00` | yes (8 files) |
| 655 | Rumble/Car/Boost | Boost jolt type (0=samll,1=large) | int |  | 0 / 0 | 1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D60` | func_00344310 `0x00344310` | yes |
| 656 | Rumble/Car/Boost | Boost Jolt Amount (0.0 to 1.0) | float |  | 0 / 0 | 0.8 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D64` | func_00344310 `0x00344310` | yes |
| 657 | Rumble/Car/Boost | Boost Jolt time (s) | float |  | 0 / 0 | 0.6 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D68` | func_00344310 `0x00344310` | yes |
| 658 | Rumble/Car/Boost | Boost Fade time (s) | float |  | 0 / 0 | 0.2 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D6C` | func_00344310 `0x00344310` | yes |
| 659 | Rumble/Car/BoostStart | Boost start Accelerator Rumble (0.0 to 1.0) | float |  | 0 / 0 | 0.3 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D70` | func_00344310 `0x00344310` | yes |
| 660 | Rumble/Car/BoostStart | Boost start Brakes Released Rumble (0.0 to 1.0) | float |  | 0 / 0 | 0.38 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D74` | func_00344310 `0x00344310` | yes |
| 661 | Rumble/Car/Chain | Chain Boost jolt type (0=samll,1=large) | int |  | 0 / 0 | 1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D78` | func_00344310 `0x00344310` | yes |
| 662 | Rumble/Car/Chain | Chain Jolt Amount (0.0 to 1.0) | float |  | 0 / 0 | 0.6 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D7C` | func_00344310 `0x00344310` | yes |
| 663 | Rumble/Car/Chain | Chain Jolt time (s) | float |  | 0 / 0 | 0.35 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D80` | func_00344310 `0x00344310` | yes |
| 664 | Rumble/Car/Chain | Chain Fade time (s) | float |  | 0 / 0 | 0.1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D84` | func_00344310 `0x00344310` | yes |
| 665 | Rumble/Car/Kerb | Kerb jolt type (0=samll,1=large) | int |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DB4` | func_00344310 `0x00344310` | yes |
| 666 | Rumble/Car/Kerb | Kerb Jolt Amount (0.0 to 1.0) | float |  | 0 / 0 | 1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D88` | func_00344310 `0x00344310` | yes |
| 667 | Rumble/Car/Kerb | Kerb Jolt time (s) | float |  | 0 / 0 | 0.15 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D8C` | func_00344310 `0x00344310` | yes |
| 668 | Rumble/Car/Kerb | Kerb Fade time (s) | float |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DB8` | func_00344310 `0x00344310` | yes |
| 669 | Rumble/Car/Surface | Wheel Skid rumble (0.0 to 1.0) | float |  | 0 / 0 | 0.7 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D90` | func_00344310 `0x00344310` | yes |
| 670 | Rumble/Car/Surface | Wheel Drift rumble (0.0 to 1.0) | float |  | 0 / 0 | 0.6 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D94` | func_00344310 `0x00344310` | yes |
| 671 | Rumble/Car/Surface | Racing Line Dirt rumble (0.0 to 1.0) | float |  | 0 / 0 | 0.4 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D98` | func_00344310 `0x00344310` | yes |
| 672 | Rumble/Car/Surface | Off-line Dirt rumble (0.0 to 1.0) | float |  | 0 / 0 | 0.55 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1D9C` | func_00344310 `0x00344310` | yes |
| 673 | Rumble/Car/Air | Big Air jolt type (0=samll,1=large) | int |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DBC` | func_00344310 `0x00344310` | yes |
| 674 | Rumble/Car/Air | Big Air Base Jolt Amount (0.0 to 1.0) | float |  | 0 / 0 | 1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DA0` | func_00344310 `0x00344310` | yes |
| 675 | Rumble/Car/Air | Big Air Jolt time (s) | float |  | 0 / 0 | 0.05 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DA4` | func_00344310 `0x00344310` | yes |
| 676 | Rumble/Car/Air | Big Air Fade time (s) | float |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DC0` | func_00344310 `0x00344310` | yes |
| 677 | Rumble/Collision/Crash | Crash jolt type (0=samll,1=large) | int |  | 0 / 0 | 1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DA8` | func_00344310 `0x00344310` | yes |
| 678 | Rumble/Collision/Crash | Crash Jolt Amount (0.0 to 1.0) | float |  | 0 / 0 | 0.8 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DAC` | func_00344310 `0x00344310` | yes |
| 679 | Rumble/Collision/Crash | Crash Jolt time (s) | float |  | 0 / 0 | 0.6 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DB0` | func_00344310 `0x00344310` | yes |
| 680 | Rumble/Collision/Crash | Crash Fade time (s) | float |  | 0 / 0 | 0.2 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DB4` | func_00344310 `0x00344310` | yes |
| 681 | Rumble/Collision/CarVCar | Car v. Car jolt type (0=samll,1=large) | int |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DC4` | func_00344310 `0x00344310` | yes |
| 682 | Rumble/Collision/CarVCar | Car v. Car Jolt Amount (0.0 to 1.0) | float |  | 0 / 0 | 1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DB8` | func_00344310 `0x00344310` | yes |
| 683 | Rumble/Collision/CarVCar | Car v. Car Jolt time (s) | float |  | 0 / 0 | 0.1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DBC` | func_00344310 `0x00344310` | yes |
| 684 | Rumble/Collision/CarVCar | Car v. Car Fade time (s) | float |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DC8` | func_00344310 `0x00344310` | yes |
| 685 | Rumble/Collision/CarVTraffic | Car v. Traffic jolt type (0=samll,1=large) | int |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DCC` | func_00344310 `0x00344310` | yes |
| 686 | Rumble/Collision/CarVTraffic | Car v. Traffic Jolt Amount (0.0 to 1.0) | float |  | 0 / 0 | 1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DC0` | func_00344310 `0x00344310` | yes |
| 687 | Rumble/Collision/CarVTraffic | Car v. Traffic Jolt time (s) | float |  | 0 / 0 | 0.05 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DC4` | func_00344310 `0x00344310` | yes |
| 688 | Rumble/Collision/CarVTraffic | Car v. Traffic Fade time (s) | float |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DD0` | func_00344310 `0x00344310` | yes |
| 689 | Rumble/Collision/CarVTraffic | Crashing Car v. Car jolt type (0=samll,1=large) | int |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DD4` | func_00344310 `0x00344310` | yes |
| 690 | Rumble/Collision/CarVTraffic | Crashing Car v. Car Jolt Amount (0.0 to 1.0) | float |  | 0 / 0 | 1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DC8` | func_00344310 `0x00344310` | yes |
| 691 | Rumble/Collision/CarVTraffic | Crashing Car v. Car Jolt time (s) | float |  | 0 / 0 | 0.1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DCC` | func_00344310 `0x00344310` | yes |
| 692 | Rumble/Collision/CarVTraffic | Crashing Car v. Car Fade time (s) | float |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DD8` | func_00344310 `0x00344310` | yes |
| 693 | Rumble/Collision/CarVWorld | Car v. World jolt type (0=samll,1=large) | int |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DDC` | func_00344310 `0x00344310` | yes |
| 694 | Rumble/Collision/CarVWorld | Car v. World Jolt Amount (0.0 to 1.0) | float |  | 0 / 0 | 1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DD0` | func_00344310 `0x00344310` | yes |
| 695 | Rumble/Collision/CarVWorld | Car v. World Jolt time (s) | float |  | 0 / 0 | 0.05 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DD4` | func_00344310 `0x00344310` | yes |
| 696 | Rumble/Collision/CarVWorld | Car v. World Fade time (s) | float |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DE0` | func_00344310 `0x00344310` | yes |
| 697 | Rumble/Collision/CarVWorld | Crashing Car v. World jolt type (0=samll,1=large) | int |  | 0 / 0 | 1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DD8` | func_00344310 `0x00344310` | yes |
| 698 | Rumble/Collision/CarVWorld | Crashing Car v. World Jolt Amount (0.0 to 1.0) | float |  | 0 / 0 | 0.8 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DDC` | func_00344310 `0x00344310` | yes |
| 699 | Rumble/Collision/CarVWorld | Crashing Car v. World Jolt time (s) | float |  | 0 / 0 | 0.1 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DE0` | func_00344310 `0x00344310` | yes |
| 700 | Rumble/Collision/CarVWorld | Crashing Car v. World Fade time (s) | float |  | 0 / 0 |  | ../Export/ValueDB/Input/rumble.cfg | `&D_004E2DE4` | func_00344310 `0x00344310` | yes |
| 701 | Rumble/Menu | Menu test Jolt time (s) | float |  | 0 / 0 | 0.3 (data) | ../Export/ValueDB/Input/rumble.cfg | `&D_004E1DE4` | func_00344310 `0x00344310` | yes |
| 702 | Physics/Vehicle | Mass (Kg) | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&self->field_10[j].field_0` | func_0042A4E0 `0x0042A4E0` | yes (100 files) |
| 703 | Physics/Race Car/Misc | Max Speed In Boost | float |  | 0 / 0 |  | ../Export/ValueDB/VehiclePhysics/<vehicle>.cfg (built at run time) | `&self->field_10[j].field_4` | func_0042A4E0 `0x0042A4E0` | yes (67 files) |
| 704 | Sound/Front End | Intro Gain | float |  | 0 / 0 | 0.6 (data) | ../export/ValueDB/Sound/Movie.cfg | `&D_004E2648` | func_004547C0 `0x004547C0` | no |
| 705 | Sound/Front End | Movie Gain | float |  | 0 / 0 | 0.8 (data) | ../export/ValueDB/Sound/Movie.cfg | `&D_004E264C` | func_004547C0 `0x004547C0` | no |
