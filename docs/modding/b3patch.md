# The b3patch format

A `.b3patch` file turns one file of the game into a modified version of it. A mod ships the patch, never the game's
file: the patch holds only the bytes the mod author made (plus the numbers that say where the rest comes from), and it
only applies to the exact file it was made from, which each user has on their own disc.

Made by `b3modtool diff` ([b3modtool.md](b3modtool.md)), applied by the game when it starts (a mod's `patches`
folder, below) and by `b3modtool apply`. The code: the writer `src/b3modtool/patch.cpp`, the reader the game and the
tool share `src/b3mod/mods.cpp` (`b3mod::read_patch_info`, `apply_patch`); the tests `tests/test_modtool.cpp`
(`b3patch_diff_apply`) and `tests/test_mods.cpp` (`patches_applied_and_cached`, `patch_mismatch_disables_the_mod`,
`patch_set_hash`). Version 1, as below.

## Layout

All numbers are little-endian. Offsets are from the start of the patch file.

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic: the bytes `42 33 50 41 54 43 48 00` (`"B3PATCH"` and a 0 byte) |
| 8 | 4 | version: 1 |
| 12 | 4 | flags: 0 (a reader refuses any other value) |
| 16 | 8 | source size in bytes |
| 24 | 8 | source hash: FNV-1a 64 of the source's bytes |
| 32 | 8 | target size in bytes |
| 40 | 8 | target hash: FNV-1a 64 of the target's bytes |
| 48 | ... | the ops, then one END op as the file's last byte |

FNV-1a 64 is the hash the port uses for its other identities (the mod set hash, `include/b3mod/mods.h`): start with
`0xCBF29CE484222325`, then for each byte `h = (h XOR byte) * 0x100000001B3` (mod 2^64).

## Ops

Each op is one byte, followed by its operands. Numbers in ops are unsigned LEB128 varints (7 bits per byte, low
group first, the top bit set on every byte but the last; at most 10 bytes).

| Op | Byte | Operands | Appends to the output |
|---|---|---|---|
| END | `00` | none | nothing; it must be the last byte of the file |
| COPY | `01` | varint `offset`, varint `length` | `length` bytes of the source from `offset` |
| INSERT | `02` | varint `length`, then `length` bytes | those bytes |

`length` is never 0. A COPY must lie inside the source (`offset + length <= source size`). The ops' lengths add up to
the target size exactly. COPY offsets are absolute, so runs may come from anywhere in the source, in any order, more
than once.

## Applying

1. Check the magic, the version and the flags.
2. Check the source: its size and FNV-1a 64 must equal the header's. A mismatch means the user's file is not the one
   the patch was made from (another version or region of the game, or an already modified file): stop and say so.
3. Run the ops in order into an output buffer.
4. Check the output's size and FNV-1a 64 against the header's target fields.

A reader refuses a patch with unknown ops, ops cut off by the end of the file, bytes after END, or runs that do not add
up to the target size.

## How b3modtool makes one

`b3modtool diff` indexes the source's 16-byte blocks at 16-byte steps by hash, then walks the target: it first tries to
continue on the diagonal of the last copy (an edit in place leaves the rest of the file where it was; a run of 12 bytes
or more is taken), then a source block with the same hash as the target's next 16 bytes; a match is grown forwards and
backwards and becomes a COPY, the bytes no match covers become INSERTs. Any other tool may write patches differently:
only the format above is fixed.

What a patch costs: an edit in place costs about the edited bytes; inserted or removed bytes cost the new bytes and one
or two ops. A file whose offset tables shift (a string table after a string changes length: every later offset
changes by a few bytes) costs a few bytes per changed table entry.

## In a mod

Patches go in a folder of their own beside the overlay root, at the disc path plus `.b3patch`:

```
my-mod/
    mod.ini
    patches/
        DATA/GLOBALUS.BIN.b3patch
```

The folder is `patches` unless mod.ini names another (`[files] patches = <folder>`). When the game starts it applies
each patch to the player's own file (checked against the source size and hash first), keeps the result in
`<mods folder>/.cache/` and reads it as if the mod had the whole file; a patch that does not fit the player's disc
leaves the mod out with a message. The details: [mod-format.md](mod-format.md) "Patches".
`b3modtool verify --game <disc>` checks each patch against the disc's file before you share the mod.
