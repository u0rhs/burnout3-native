# The code-mod build template (the mod kit's SDK, docs/modding/DESIGN.md §2.6; sdk/README.md). Builds one mod's library
# from its src/*.c and src/*.cpp files with nothing but this folder's include/b3mod_api.h:
#
#   make -C sdk MOD=<mod folder>                    this system: Linux (.so), macOS (.dylib), Windows (.dll) in an
#                                                   MSYS2 or Git Bash shell
#   make -C sdk MOD=<mod folder> SYSTEM=windows     Windows from Linux or macOS (MinGW-w64's x86_64-w64-mingw32-gcc)
#   make -C sdk MOD=<mod folder> clean
# (A POSIX shell runs the commands. Windows with only MinGW-w64 and cmd.exe: sdk\build_windows.cmd; any system with
# CMake: sdk/CMakeLists.txt.)
#
# The library goes to <mod folder>/bin/<system>/<LIBNAME>.<ext>, the path its mod.ini names under [code]:
#   windows = bin/windows/<LIBNAME>.dll   linux = bin/linux/<LIBNAME>.so   macos = bin/macos/<LIBNAME>.dylib
# LIBNAME defaults to the mod folder's name with '-' as '_' (hello-overlay -> hello_overlay).
MOD ?=
ifeq ($(strip $(MOD)),)
$(error MOD=<mod folder> (the folder with mod.ini and src/))
endif
SDK := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
MODDIR := $(abspath $(MOD))
LIBNAME ?= $(subst -,_,$(notdir $(MODDIR)))

# the system the library is for (default: this one)
ifeq ($(OS),Windows_NT)
SYSTEM ?= windows
else ifeq ($(shell uname -s),Darwin)
SYSTEM ?= macos
else
SYSTEM ?= linux
endif

# a Windows library: MinGW-w64's compilers unless CC / CXX name others
ifeq ($(SYSTEM),windows)
ifeq ($(origin CC),default)
CC := $(if $(filter Windows_NT,$(OS)),gcc,x86_64-w64-mingw32-gcc)
endif
ifeq ($(origin CXX),default)
CXX := $(if $(filter Windows_NT,$(OS)),g++,x86_64-w64-mingw32-g++)
endif
endif
CFLAGS ?= -O2
CXXFLAGS ?= -O2
# -fvisibility=hidden: only what B3MOD_EXPORT marks is exported (b3mod_main, b3mod_built_for)
# -fexceptions: the port may unwind through a hook's frame when a run ends inside a game call
FLAGS := -I$(SDK)/include -fvisibility=hidden -fexceptions -Wall -Wextra
ifeq ($(SYSTEM),windows)
EXT := dll
LINK := -shared -static-libgcc -Wl,--no-undefined
else ifeq ($(SYSTEM),macos)
EXT := dylib
FLAGS += -fPIC
LINK := -dynamiclib -undefined error
else
EXT := so
FLAGS += -fPIC
LINK := -shared -Wl,--no-undefined
endif

C_SRC := $(wildcard $(MODDIR)/src/*.c)
CXX_SRC := $(wildcard $(MODDIR)/src/*.cpp)
OBJDIR := $(MODDIR)/build/$(SYSTEM)
OBJ := $(C_SRC:$(MODDIR)/src/%.c=$(OBJDIR)/%.o) $(CXX_SRC:$(MODDIR)/src/%.cpp=$(OBJDIR)/%.o)
OUT := $(MODDIR)/bin/$(SYSTEM)/$(LIBNAME).$(EXT)
LINKER := $(if $(CXX_SRC),$(CXX) $(if $(filter windows,$(SYSTEM)),-static-libstdc++),$(CC))

all: $(OUT)

$(OUT): $(OBJ)
	@mkdir -p $(dir $@)
	$(LINKER) $(LINK) $(OBJ) -o $@
ifeq ($(SYSTEM),macos)
	codesign --force -s - $@ 2>/dev/null || true
endif
	@echo "b3mod: built $@"

$(OBJDIR)/%.o: $(MODDIR)/src/%.c $(SDK)/include/b3mod_api.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(FLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(MODDIR)/src/%.cpp $(SDK)/include/b3mod_api.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(FLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(OUT)

.PHONY: all clean
