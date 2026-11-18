# Audio decoders (custom soundtrack, remaster-features §6)

Single-file decoders used by `src/native/custom_music.cpp` for the user's own music folder. Unmodified copies, fetched
2026-11-10 (the projects' master branches, via cdn.jsdelivr.net/gh):

| File | Project | Version line | License |
|---|---|---|---|
| `dr_mp3.h` | mackron/dr_libs (MP3, minimp3 inside) | dr_mp3 v0.7.5 | public domain (Unlicense) or MIT-0, `LICENSE-dr_libs.txt`; minimp3: CC0, `LICENSE-minimp3.txt` |
| `dr_flac.h` | mackron/dr_libs (FLAC) | dr_flac v0.13.5 | public domain (Unlicense) or MIT-0, `LICENSE-dr_libs.txt` |
| `dr_wav.h` | mackron/dr_libs (WAV) | dr_wav v0.14.7 | public domain (Unlicense) or MIT-0, `LICENSE-dr_libs.txt` |
| `stb_vorbis.c` | nothings/stb (Ogg Vorbis) | v1.22 | MIT or public domain (Unlicense), `LICENSE-stb_vorbis.txt` |

SHA-256 of the copies:

```
e5e8cc2a449ad312fbb2e97bd8b085aab9362bf99ddd8e34684f317ef048381f  dr_mp3.h
3f010d3142a5b9df80a0bd8b325de54317468b1aff85abb257c86dbfefe9bd2a  dr_flac.h
7c9660d75361ad6175e6217c63a1075c9512445c06bc951197fc479179f76b39  dr_wav.h
4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636  stb_vorbis.c
```

AAC is not decoded (no permissively licensed single-file decoder).

`libchdr/` (`.chd` disc images, `include/b3asset/discfs.h`): libchdr with the LZMA SDK, miniz and zstd decoders it
bundles, its FLAC through the `dr_flac.h` above. Version, source and hashes: `libchdr/README.md`.
