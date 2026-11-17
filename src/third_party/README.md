# Third-party source

| Folder | What | Licence | Used by |
|---|---|---|---|
| `lua-5.4.9/` | Lua 5.4.9, from the official source tarball `lua-5.4.9.tar.gz` (Lua.org, PUC-Rio), unmodified: `src/`, `README`, `doc/readme.html` (the manual and the man pages are left out) | MIT: `lua-5.4.9/LICENSE.txt` (the notice at the end of `src/lua.h`) | native_play's script mods (`src/native/script_mods.h`, [docs/modding/lua-reference.md](../../docs/modding/lua-reference.md)), compiled as C++ in one unit (`src/native/script_lua.cpp`) |

`lua-5.4.9.tar.gz.sha256` holds the SHA-256 of the tarball the source was taken from, as Lua's download page publishes
it (read 2026-11-12 from the PUC-Rio host of the Lua site, `www.tecgraf.puc-rio.br/lua/ftp/`, the same page as
`www.lua.org/ftp/`; Debian's `lua5.4_5.4.9-2.dsc` gives the same value for its `lua5.4_5.4.9.orig.tar.gz`). To check
the folder against a fresh download:

```sh
sha256sum -c lua-5.4.9.tar.gz.sha256        # beside the downloaded lua-5.4.9.tar.gz
tar -xzf lua-5.4.9.tar.gz && diff -r lua-5.4.9/src <this repo>/src/third_party/lua-5.4.9/src
```
