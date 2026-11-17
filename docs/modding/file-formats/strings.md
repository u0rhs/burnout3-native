# Text: the string tables (GLOBALUS.BIN, HEADUS.BIN)

All the game's text on the US disc is in two files:

- `DATA/GLOBALUS.BIN` (214,850 bytes, 4,006 strings): menus, messages, hints, event and car names, the credits.
- `DATA/HEADUS.BIN` (336 bytes, 11 strings): the big headings ("ONLINE", "CRASH", ...) [D].

Each is a list of UTF-16 strings addressed by number (the **id**, 0-based). The game's code asks for a string by its
id; there are no names or keys in the file.

Evidence tags: see [index.md](index.md#evidence-tags).

## Status

| | |
|---|---|
| Decoded by the port | the game's own code reads them (native); the mod kit's reader is modtool1's `src/b3modtool/strings.cpp` |
| Tool | `b3modtool strings export / import` ([b3modtool.md](../b3modtool.md)): `<id> = <text>` lines in UTF-8 |
| Round trip | **importable**: 2 of 2 files, 4,017 of 4,017 strings identical after export -> text -> import (`b3modtool roundtrip`) |

## Layout

A serialized `CGtArray<unsigned short *>`: a header, a table of offsets, then the strings. All integers
little-endian; offsets are from the start of the file.

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 8 | bytes | `08 03 F6 69 FA 22 36 B9` in both files; not read by the loader. A type tag of the container format [G] | [D both files], [C FixUp skips it] |
| 0x08 | 4 | u32 | string count n (4,006 / 11) | [C `CGtArrayBase::FixUp` 0x2719B0] |
| 0x0C | 4 | u32 | offset of the offset table: 0x10 in both files | [C 0x2719B0] |
| 0x10 + 4 i | 4 | u32 | offset of string i; fixed up into a pointer (an offset of 0 would stay a null pointer) | [C 0x2719B0] |
| after the table | | u16[] | the strings: UTF-16LE, each ended by a 0x0000, packed one after another in id order with no padding, up to the end of the file | [D both files: every offset is where the packing puts it] |

The packing rule matters for the tools: because both files follow it exactly, a table rebuilt from the strings alone
(with new offsets) gives the original bytes back, and strings may grow or shrink freely.

## The last string is a version GUID

The last string of each table is a GUID in braces: id 4005 of GLOBALUS.BIN and id 10 of HEADUS.BIN [D]. The boot code
passes the same GUID and that id to `CB3Strings::Prepare` 0x271A20 (`"{696544B6-...}", 0xFA5` and `"{3A4B53B4-...}",
10`) [C func_00133190], evidently so that a table from another build could be detected; the retail `Prepare` ignores
both arguments: it stores the buffer and runs `CGtArrayBase::FixUp`, nothing else [C B3Strings_00271A20.cpp].
**Keep the GUID string as it is** (and keep it last if you add strings: see below).

## How the game loads and uses them

1. At start-up the boot sequence (`func_00133190`, states 3..4) acquires memory blocks 0xA (global strings) and 0xB
   (head strings) and queues `QueueLoadRequest(<name>, ..., block, GetBlockSize(block))` for both [C nonmatching
   func_00133190.cpp].
2. When both are loaded, `CB3Strings::Prepare` 0x271A20 fixes each one up into the two global `CB3Strings` objects at
   0x51BA44 (global) and 0x51BA48 (head) [C].
3. `CB3Strings::GetString(id)` 0x271A00 returns `table[id]` through `CGtArray<unsigned short>::GetAt` 0x271A60
   **without a range check** [C B3Strings.cpp]. The decomp has about 1,000 calls with a literal id (e.g. 452, 899)
   [C grep]; menu components take `EGlobalStrings` ids the same way.
4. Text is drawn with `CGt2dFont` (GlobalFont, HeadFont, SmallFont; none of them is in a disc TXD). A character is
   looked up in a bucket by its code & 0x7F and then by its exact code; **a character the font does not have is drawn
   as the font's last glyph** [C `CGt2dFont::UnkInline_FindGlyph`, Gt2dFont.h].

The file names: the program holds `Data/GlobalEn.bin`, `GlobalDu.bin`, `GlobalSp.bin`, `GlobalUs.bin` and
`Data/HeadEn.bin` ... `HeadUs.bin`; the US build picks the `Us` pair, and only those two are on this disc [C strings in
the program, D]. Language number 1 (US) also sets the number separators: `,` for thousands, `.` for decimals
(`ustrSetSeparators` 0x2130F0) [C func_00133190].

## Characters and markup seen in the text

- GLOBALUS.BIN uses 156 distinct characters: ASCII, Latin-1 letters with accents, `Œ œ Ÿ Ń ń`, typographic quotes
  and apostrophe (U+2019, U+201C, U+201D), the ellipsis U+2026, ™ © ® £ ½, and U+00A0 (no-break space) [D].
- `\n` (U+000A) breaks lines (17 strings) [D].
- A few strings carry placeholders the code fills in: `%1K` (7 strings), `%2m`, `%2f` [D]; their exact meaning is not
  traced [G: argument number and kind]. Keep them where the sentence needs them.
- Other characters are safe only if the font has them; otherwise they come out as the font's last glyph (above).

## Limits and pitfalls

- **Ids are fixed.** The code asks for numbers; never reorder or delete strings. Changing a string's text (and length)
  is fine.
- **Adding strings** at the end gives new ids that no code asks for; only a code mod could show them. If you add any,
  note that the version GUID is then no longer the last string (harmless for the retail loader, which ignores it).
- **File size.** The file is loaded into a fixed memory block, and the load request is given the block's size: block
  0xA is 0x41800 bytes (268,288) and block 0xB 0x800 (2,048) [L `CB3MemoryManagerPS2::Construct` 0x222650 /
  `CB3MemoryManager::GetBlockSize` 0x3E7850]. A GLOBALUS.BIN larger than that would be cut short. The US file leaves
  about 53 KB (roughly 26,000 characters) of room; HEADUS.BIN about 1.7 KB.
- **Line length.** Nothing in the file limits a string's length, but menu boxes do: long text overflows or is cut on
  screen. Check every changed string in the game.
- **Ship a patch or only your lines.** `b3modtool strings import` takes a text file with only the lines you changed;
  share that text or a `b3patch` of the result, never the whole table ([DESIGN.md](../DESIGN.md) §8).
