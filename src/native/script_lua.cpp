// script_lua.cpp -- Lua 5.4 (src/third_party/lua-5.4.9, unmodified) compiled as C++ in this one unit, for the script
// mods (script_mods.h; docs/modding/DESIGN.md §10, docs/modding/lua-reference.md).
//
// Why C++: a Lua error then unwinds with a C++ exception instead of longjmp, so the destructors of the port's C++ frames
// between a Lua call and its error (the bindings in script_mods.cpp) run. Every Lua symbol therefore has C++ linkage;
// the port includes "lua.h" / "lauxlib.h" directly (never lua.hpp, whose extern "C" would not match).
//
// Only the core, the auxiliary library and the libraries a script may have are compiled: base, coroutine, math,
// string, table, utf8. io, os, package (loadlib), debug and linit are not in the program at all.
//
// Determinism (DESIGN.md §10): Lua seeds its string hash from the clock and from addresses (lstate.c luai_makeseed),
// which would change the order `pairs` gives for string keys from run to run. The macro below replaces that seed with a
// constant, so the same script builds the same tables in every run. (math.random is seeded per mod by script_mods.cpp.)

#define luai_makeseed(L) (static_cast<unsigned int>(0x62336D6Fu))   // "b3mo": any constant will do
// numbers in script text read with '.' whatever the process's locale (luaconf.h would ask localeconv())
#define lua_getlocaledecpoint() '.'

#include "../third_party/lua-5.4.9/src/lprefix.h"

// every standard header the Lua sources include, first: Lua's short macros (llex.c `next`, ...) must not reach the C++
// library's headers, which a later #include of <math.h> would otherwise open after them
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LUA_CORE
#define LUA_LIB
#define ltable_c
#define lvm_c
#include "../third_party/lua-5.4.9/src/luaconf.h"

// the internal functions stay inside this unit
#undef LUAI_FUNC
#undef LUAI_DDEC
#undef LUAI_DDEF
#define LUAI_FUNC static
#define LUAI_DDEC(def) /* empty */
#define LUAI_DDEF static

// the core
#include "../third_party/lua-5.4.9/src/lzio.c"
#include "../third_party/lua-5.4.9/src/lctype.c"
#include "../third_party/lua-5.4.9/src/lopcodes.c"
#include "../third_party/lua-5.4.9/src/lmem.c"
#include "../third_party/lua-5.4.9/src/lundump.c"
#include "../third_party/lua-5.4.9/src/ldump.c"
#include "../third_party/lua-5.4.9/src/lstate.c"
#include "../third_party/lua-5.4.9/src/lgc.c"
#include "../third_party/lua-5.4.9/src/llex.c"
#include "../third_party/lua-5.4.9/src/lcode.c"
#include "../third_party/lua-5.4.9/src/lparser.c"
#include "../third_party/lua-5.4.9/src/ldebug.c"
#include "../third_party/lua-5.4.9/src/lfunc.c"
#include "../third_party/lua-5.4.9/src/lobject.c"
#include "../third_party/lua-5.4.9/src/ltm.c"
#include "../third_party/lua-5.4.9/src/lstring.c"
#include "../third_party/lua-5.4.9/src/ltable.c"
#include "../third_party/lua-5.4.9/src/ldo.c"
#include "../third_party/lua-5.4.9/src/lvm.c"
#include "../third_party/lua-5.4.9/src/lapi.c"

// the auxiliary library and the libraries a script may have
#include "../third_party/lua-5.4.9/src/lauxlib.c"
#include "../third_party/lua-5.4.9/src/lbaselib.c"
#include "../third_party/lua-5.4.9/src/lcorolib.c"
#include "../third_party/lua-5.4.9/src/lmathlib.c"
#include "../third_party/lua-5.4.9/src/lstrlib.c"
#include "../third_party/lua-5.4.9/src/ltablib.c"
#include "../third_party/lua-5.4.9/src/lutf8lib.c"
