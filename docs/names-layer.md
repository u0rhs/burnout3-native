# Readable names layer

Roadmap phase 5. The decomp (burnout3-retail-decomp) never invents a name: anything without evidence stays a
placeholder (`func_XXXXXX`, `UnkStruct_*`, `field_XX`, `D_XXXXXXXX`, `vfunc_XX`, `UnkEnum_*`). The port may carry
descriptive names (a design decision), but only here: a generated alias table plus a generator that applies it to copies of
decomp files. Nothing from this layer is ever written back into the decomp. Tags: **[C]** confirmed, **[L]** likely,
**[G]** guess, each with its evidence.

## Pieces

| Path | What |
|---|---|
| `tools/names/build_aliases.py` | builds `config/aliases.tsv` from a decomp snapshot |
| `config/aliases.tsv` | the alias table (generated, committed; header line names the decomp commit) |
| `tools/names/readable.py` | one decomp file (or `--tree LIST`) with the aliases applied -> `gen/readable/<same path>` |
| `tools/names/check_readable.py` | converts files and checks each gives g++ exactly the original's diagnostics |
| `tools/names/host_check.py` | runs `tools/decomp2host` on the original and on the readable version, compiles both, compares the disassembly |
| `tools/names/common.py` | snapshot, lexer (comments/literals masked), aggregate and base-class parser, include closure, table I/O |
| `tools/names/retail.py` | the snapshot's retail ELF (`orig/SLUS_210.50`), function starts from its `assembly/asm` glabels, a small MIPS register tracker that gives each call's argument registers (the vdb and vtable sources) |
| `gen/readable/` | generated readable files (prototype: two units) |

Every tool reads a snapshot of the decomp's **committed** tree (`git archive <rev>` into `build/names/snap-<rev>`),
never its working tree, which other work edits.

## Sources

Best first. A placeholder that already has an evidenced name in the decomp is not an alias: the decomp's source simply
uses that name. Nothing else feeds the table; no row exists without the citable reason in its `evidence` column.

| Source | Input (decomp) | Scope | Tag | Rows (decomp 93ad906821) |
|---|---|---|---|---|
| `symbols` | `config/symbol_addrs_b3names.txt`: the address already has a decomp name but the source still writes the placeholder | function, global | the symbol file's own ([C] when untagged) | 4 + 2 |
| `dnat` | `research/proto/new_names.csv`: B3's own prototype DNAT_200.45 aligned to retail by code (decomp N159; hold-out error high 2/664, medium 5/259, low 8/51). Identifier `Class_Method`; operators, templates and destructors skipped | function | [L] high/medium, [G] low | 578 [L], 343 [G] |
| `emap` | `research/retail_data_names.csv`: build E data names transferred by code reference, high/medium, that the symbol file left out (mostly function-local statics `name$N`; compiler temporaries `@N` skipped) | global | [L] | 51 |
| `vdb` | retail's variable-debugger registrations, decoded from the retail ELF at each call site (below): a global's `D_` placeholder, or `field_X` of the method's class (or its single-inheritance base) at the registered `this` offset. Identifier from Criterion's label and group strings (`vdb_ident`, below). Replaces a Revenge row for the same member | global, field | [C] | 68 global, 239 field |
| `vtable` | the retail vtables `__vt__<Class>` (symbol file) read from the ELF: `vfunc_XX` of class C is the method name of the function at C's vtable +XX (below) | field (member function) | the slot function's name: symbol file's tag ([C] untagged), else DNAT [L]/[G] | 115 [C], 56 [L], 7 [G] |
| `revenge` | `research/names/revenge_member_map.tsv` (decomp `tools/revenge_member_map.py`): the Revenge (Jul 14 prototype, stabs) member at the same offset of the paired class. A member path becomes one identifier: `mAvoidance.mrLeftRisk` -> `mAvoidance_mrLeftRisk`, `marTimes[2]` -> `marTimes_2` | field of a named class, every file | the row's tier | 913 [L], 92 [G] |
| `view` | `research/names/unkstruct_bindings.tsv` (decomp `tools/bind_unkstructs.py`): an `UnkStruct` view proven/believed to be class C at base B. Type alias `<C>View` (B = 0) or `<C>View_<B hex>`, numbered when one file has several views of one place | struct, in its home file (the .cpp or header defining it) | the binding's tier | 634 [C], 1064 [L], 35 [G] |
| `view+vdb` | a bound view's `field_X` = the vdb registration at C + B + X (same identifier as the class's own row) | field of that view | the binding's tier | 75 [C], 1 [L] |
| `view+vtable` | a view bound to C at base 0: its `vfunc_XX` = C's vtable slot +XX | field of that view | weaker of binding and slot name | 51 [C], 103 [L], 4 [G] |
| `view+revenge` | a bound view's `field_X` = the Revenge member at C + B + X (when vdb has none) | field of that view | weaker of binding and member | 463 [L], 77 [G] |

Left out (`build/names/conflicts.tsv`, 208 at 93ad906821): bindings for files that no longer exist (139, the
binding table predates later decomp renames), views whose definition is gone (8), one view bound to two classes, two
addresses with two DNAT names (folded code), 18 Revenge members superseded by a vdb registration of the same member,
28 vtable slot names no confirmed slot supports, vtable slots whose method name a second slot of the same class also
has (overloads, or a DNAT error: both left out), and 6 slots declared by a base and a derived class where only one has
a name (below). Within one class two
members never get the same alias.

### vdb: the variable-debugger registry

Retail registers its tunables with `GtComms::CGtCommsDatabase::AddEntry` (E map [C]; upstream's names `vdbRegisterVector`
0x21B9B0, `vdbRegisterFloat` 0x21BB60, `vdbRegisterInt` 0x21BD80 until N298): `AddEntry(db, T *var, const char *label,
const char *group, const char *cfgfile, ...)`. `build_aliases.py` walks every game function (glabels below 0x469E00),
tracks registers forward (constants from `lui`/`addiu`/`ori`, moves, `$gp`; the entry `a0` as `this`; a `lw` from a
constant address as that word's initial value; calls clobber the caller-saved registers) and reads `a1`, `a2`, `a3`,
`t0` at each call. 705 calls: 251 register a global (68 still `D_` placeholders in the source, 128 already named, 55
placeholders the source never uses), 435 `this` + offset inside a named method (class from the mangled name: 239
`field_X` placeholders; 13 are array members, left out since the label names element 0 only; 183 have no `field_X`
there: 108 inside `CB3SoundRaceCarManager`'s per-surface arrays, 73 in `CB3AIAttribs`, which the decomp does not
define yet, 2 in `CB3StaticTrack`), 19 a member reached through a loaded pointer (left out). Every label is a constant
string; a group is sometimes the initial value of a `char *` global (noted in the evidence column).

**Identifier mangling** (`vdb_ident`): each string is cut into its alphanumeric runs, each run's first letter
capitalised and the rest kept (`'Minimum Air Distance (Metres)'` -> `MinimumAirDistanceMetres`, `'3d Slam Vol'` ->
`3dSlamVol`, `'LockUp'` stays); the alias is the last group component, `_`, the label (`'Physics/Suspension/Front'` +
`'Spring Length'` -> `Front_SpringLength`); empty components (`'Camera/Follow/'`) are dropped. If two registrations of
one class (or two globals) get the same alias, both take one more group component (`Shunt_Angular_Force`), up to four,
then the call site's address; a leading digit gets `n`. Criterion's spelling is kept (`Attatch`, `Multipler`). The group
qualifier is always there (not only on a clash) so a name does not change when the decomp declares one more field.

**Check against the game's `DATA/VDB.XML`** (`build_aliases.py --vdb-xml <path>`, prints a statistic only; the file is
game data, never read into the table or committed): despite its name it is a binary table (header words 2, 7525, 452,
348, then key/value records) with no strings, so it names nothing by itself. Its keys are `CGtHash::CalculateHash` (CRC32 with
retail's table, initial ~0, arithmetic shift, no final inversion) of `label + group + "/" + cfgfile`, the string
`AddEntry` builds: 528 of the 543 decoded registrations whose cfg file is a constant have their key in it (the other
162 use a per-instance file name buffer), which confirms the decoded strings, including the groups read from `char *`
initial values (443 of 449).

### vtable: member functions by slot

MWCC's vtable here is two header words, then the slots: `vfunc_XX` (the decomp's placeholder for the call `lw t9,
XX(vptr)`) is the word at vtable + XX, the first slot at +8. Checked [C]: all 111 named `__vt__` vtables (simple class
names) start with the words 0, 0 (no RTTI pointer emitted); their named slots hold 330 of the class's own methods and
261 inherited ones; decomp N102 (components {0, 0, Update, Release}) and N291 (`CB3PlayerCarPhysics` slot +0x28 is
`DoCrashBreaker`, read from a call) agree with the table. A vtable's extent is measured from the data (slots while the
words point into game code, up to the next vtable), not taken from the symbol's size: `__vt__26CB3OnlineRoadRageRace
Logic` says 0xEC, but a data label split it and its slots run to +0x108 (`DamageRaceCar`, which the decomp calls as
`vfunc_108(car, car, 7)`). The method name is the slot function's mangled name up to `__` (`Update__10CB3RaceCarFv` ->
`Update`; inherited slots keep their base's method name). A slot whose name is only [L]/[G] (a DNAT-transferred name,
in the symbol file or `new_names.csv`) is kept only if its class is among the vtable's confirmed classes (the class,
the classes of its [C]-named slots, and theirs, transitively) or a [C] slot at the same offset in another vtable has the
same method name (one slot of one base is one virtual everywhere; siblings share folded code, so DNAT often credits
a sibling class). 28 fail and are left out, among them the clearly wrong ones (`CGTFileSystem::FClose`/`FExist`/
`FPuts`, `CGtSphereTreeSphere::GetFromSiblingListLink`, `CB3Profile::IncreaseBurnoutPointTotal` in game-mode logic
vtables). An override must keep overriding: where a class and one of
its bases or derived classes (the decomp's own base clauses) both declare `vfunc_XX`, the alias is kept only if both
get the same one (build_aliases over the whole decomp; readable.py again per unit, since a unit can skip one side).
The decomp often declares a method twice, by name (non-virtual) and as its `vfunc_XX` (e.g. `CB3AICar::Prepare` and
`vfunc_8`); the alias then already exists as an identifier and the safety rule leaves `vfunc_XX` alone.

## Table format

`config/aliases.tsv`, tab-separated, `#` header lines (generator, decomp commit):

| Column | Meaning |
|---|---|
| `scope` | `field` (a member, incl. member functions), `struct`, `function`, `global` |
| `file` | `*` (everywhere) or the decomp path the alias is local to (a view type and its fields) |
| `container` | the aggregate a `field` belongs to (a class name, or the view's `UnkStruct_*` name), else `-` |
| `placeholder`, `alias` | the decomp identifier and the port's name |
| `tag`, `source`, `evidence` | evidence tier, source (table above), one-line citation |

## How readable.py applies it

- **Identifiers only.** Comments and string/char literals are masked before tokenising; they are never changed (decomp
  comments keep citing the placeholders, which the header comment maps).
- **Token aliases** (`struct`, `function`, `global`): every token of the placeholder in the file and the decomp headers
  it includes.
- **Member aliases** need the aggregate each `field_X` use belongs to. The compiler decides, not a heuristic: a probe
  copy renames every *declaration* of a candidate name per aggregate (`field_X` -> `field_X__R<k>`); g++'s diagnostics
  for the now-broken uses (`'struct S' has no member named 'field_X'; did you mean 'field_X__R3'?`, or the enclosing
  `Class::Method` for a bare name) give each use's aggregate. Resolved uses are fed back and the probe recompiled until
  nothing new resolves (`a->field_8->field_4` resolves in two rounds). Errors the unchanged original already has are
  excluded first (a unit that fails plain g++ cannot lend evidence where it fails).
- **Safety rules.** An alias that is already an identifier anywhere the file sees is not used (no shadowing, no clash);
  a member name with any use the probe could not resolve is left alone in the whole file; a `vfunc_XX` declared by a
  class and by a base or derived class the unit sees gets one alias in both or none (an override must keep overriding);
  the GtMath headers (replaced by the port's shadow headers) are never rewritten.
- **Tree mode** (`--tree LIST`): headers are shared, so a header edit is kept only if every unit that includes the header
  makes it; the aliases behind any other edit are excluded everywhere and the units redone until stable (one round
  and one alias at cf06408364).
- Each written file starts with a comment listing every alias it uses: placeholder, alias, tag, source, container.

## Checks (2026-11-02, decomp 93ad906821)

Before = the generator without the vdb and vtable sources at the same decomp commit (same rows as at cf06408364).

| Check | Command | Before | After |
|---|---|---|---|
| Every decomp source converts and compiles as before | `python3 -I tools/names/check_readable.py --files <all src/*.cpp>` | 4,612 / 4,612 same diagnostics; 22,608 identifier edits; 203 skipped | 4,612 / 4,612 same; 25,285 edits (vdb 1,432, view+vdb 227, vtable 924, view+vtable 184; revenge 9,015 -> 8,925 where vdb superseded it); 385 skipped (the 182 more: vtable aliases already an identifier in the file, the double declaration above) |
| The 780 matched units the port generates for the host | `python3 -I tools/names/check_readable.py` | 780 / 780 same; 4,916 edits | 780 / 780 same; 5,208 edits |
| Host pipeline, one unit | `python3 -I tools/names/host_check.py src/b3/_unclassified/B3AIArbitrator_0019D6D0.cpp` | IDENTICAL | IDENTICAL (22 aliases; B3CrashCombo_00362F60: 17 aliases, IDENTICAL) |
| Host pipeline, every generated unit | `readable.py --tree <manifest files> --scopes field,struct --out build/names/tree_out`, then `host_check.py --tree build/names/tree_out` | 825 readable files, 4,084 edits; 966 / 966 identical host code | 825 readable files (780 units + 45 headers), 4,353 edits; 966 / 966 identical host code |
| Negative control (vfunc) | the readable tree with one renamed virtual call moved to the neighbouring slot (`field_200.Construct()` -> `Destruct()` in B3PlayerCameraManager_002E7810) | | 965 identical, 1 differs: B3PlayerCameraManager_002E7810 (the check sees a misnamed slot) |
| vdb decoding against the game data | `build_aliases.py --vdb-xml <game>/DATA/VDB.XML` | | 528 of 543 checkable keys present |

`check_readable` compares g++ `-fsyntax-only` diagnostics (aliases mapped back, header-comment line offset removed,
g++'s "did you mean" spelling hints ignored since they depend on the names around). `host_check` runs the port's own
`tools/decomp2host` on both versions, compiles the generated units with the Makefile's generated-unit flags
(`-O2 -DB3_PS2_FLOAT=1`, no debug info; decomp2host as in the port's working tree at the time) and compares `objdump -d -r -C` listings, aliases mapped back (view types and
virtual-slot aliases, which can reach mangled names, e.g. a devirtualised call on a member held by value; also inside
mangled section names), COMDAT sections compared as a sorted set (their order follows the names). Negative control: two
members swapped in the readable generated unit changes the listing.

## Priorities (2026-11-01)

The names layer serves the remaster; it is not on the critical path to a playable port (phases 4 → 6 → 7).
Order of work:

1. **Confirmed names first, cheap and mostly mechanical** (one design pass, then batch jobs): the game's own
   variable-debugger registry (`CGtCommsDatabase::AddEntry` calls pair a variable's address with Criterion's name string;
   RegisterStaticVariables__8CB3ScoreFv alone registers 82), and `vfunc_XX` slots from the retail vtables plus E-map
   names. **Done 2026-11-02** (sources `vdb`, `vtable`, `view+vdb`, `view+vtable`); what grows them is next steps 7-9.
2. **Readable source becomes the port's source**: decomp2host learns the aliases (next step 1), the host build compiles
   readable source, and the identical-machine-code check (`host_check.py --tree`) gates every build. Modders then edit
   readable code, with the decomp as its byte-exact reference.
3. **Targeted naming before each remaster feature**: before building the settings menu, input remapping, custom
   soundtrack and the rest (`docs/remaster-features.md`), name the classes that feature hooks into, using recorded
   experiments in live runs (trace a field per frame against the HUD; change it on a replayed race and compare
   frames). Experiment names are [L] with the run cited; watched-only names are [G].
4. **Everything else stays as offsets** until the main goals are done. Naming the whole codebase is icing for later.

Effort split: roughly 70% port, 15% names, 15% decomp upkeep.

## Open questions and next steps

1. **decomp2host does not know aliases.** It derives guest addresses from the names: `D_XXXXXXXX` carries its address,
   named globals and called functions come from the decomp's symbol file. A readable file with `function`/`global`
   aliases therefore stops it (`extern _kaeLandingType: no address`), which is why the host checks run with
   `--scopes field,struct`. Fix (in decomp2host): look up `config/aliases.tsv` in reverse in T4 and
   in the call stubs.
2. **Coverage.** The layer names a small part of the placeholders (about 25,300 identifier edits over the decomp's 4,612
   source files; the decomp has ~126,000 `field_` uses, 13,000 `vfunc_`, 38,000 `D_`). Growth comes from the decomp side: more Revenge class pairings and
   view bindings (the binding table is stale for 139 files), and the DNAT renames the decomp itself has planned (N159).
3. **Not yet sources.** `UnkEnum_*` (86 uses): no source. Nahelam's retail names (`research/burnout3-symbols`) are
   unverified hints and stay out unless the decomp verifies them.
4. **[G] member rows.** The Revenge map's [G] rows can describe an embedded struct at one offset and its members far
   away (`field_2B0` -> `mAvoidance` [G], `mAvoidance_mDesiredDirection` at 0x720: consistent here, since field_2B0 is a
   CB3AIAvoidanceMap passed to its own methods). A path-consistency filter (a member path's offsets inside its parent's
   extent) would catch contradictions.
5. **Committed output.** `gen/readable` holds three prototype units (B3VehiclePhysicsAttribs.cpp: the vdb names). Converting the whole tree is one `--tree` run
   (12 s); whether to commit it, and whether the host build should consume readable source instead of the decomp
   directly, is a phase-5 decision.
6. **Names inside comments** stay placeholders by design; a reader uses the header comment to map them.
7. **The Revenge pairing of `CB3VehiclePhysicsAttribs` is off by 4 from 0x8C** (decomp side, `tools/revenge_member_map.py`):
   retail's registrations put Front "Attatch Height" at 0x8C, where the map has `mrCrashingMass`, and every Front/Rear
   suspension member 0x8C-0xA8 sits 4 below the map's (retail has no `mrCrashingMass` there). The vdb rows supersede
   all 8 here; the decomp's table and anything built on it (view+revenge rows of views of this class) still carry the
   shift. Of the other 10 Revenge rows vdb also names, all agree in meaning (`mrDrivingMass` / `Vehicle_MassKg`,
   `mInGameFollow.mCameraOffset` / `Follow_CameraOffset`, ...).
8. **vdb coverage waits on the decomp**: 73 registrations name `CB3AIAttribs` members (`Register__12CB3AIAttribsFv`),
   a class the decomp does not define yet; 108 name elements of `CB3SoundRaceCarManager`'s per-surface arrays
   (`float field_0[10]`: one label per element, e.g. 'Tarmac Min Speed'), which an alias cannot name (an array name
   from its element labels would be a guess); 55 registered globals are placeholders the source never uses yet; 19
   register a member through a loaded pointer (the pointee's class is not known here).
9. **vfunc coverage**: 1,108 of ~13,000 `vfunc_` uses renamed. The rest sit on `UnkStruct` pointees with no binding to
   a class (400 of the 422 aggregates that declare `vfunc_` slots). Next source: the pointer member's type: class C's
   `field_X` typed `UnkStruct_Y *` where Revenge has a `CB4Foo *` member at C+X gives Y = the paired B3 class
   ([L], via `revenge_class_pairs.tsv`), hence its vtable. And in the named classes the decomp often declares a method
   both by name and as its `vfunc_XX` (`CB3AICar::Prepare` / `vfunc_8`): the decomp could declare the named one virtual
   at its slot, which removes the placeholder rather than aliasing it.

## Regenerate

```
python3 -I tools/names/build_aliases.py --rev <decomp commit>          # config/aliases.tsv (+ build/names/conflicts.tsv)
    [--vdb-xml <game>/DATA/VDB.XML]                                    # + how many decoded vdb keys the game data has
python3 -I tools/names/readable.py <decomp path> --rev <decomp commit> # gen/readable/<decomp path>
python3 -I tools/names/check_readable.py --rev <decomp commit>         # compile check, matched host units
python3 -I tools/names/host_check.py <decomp path> --rev <decomp commit>
```
