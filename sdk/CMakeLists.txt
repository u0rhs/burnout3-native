# The code-mod build template for CMake (the mod kit's SDK, sdk/README.md): one mod's library from its src/*.c and
# src/*.cpp files with nothing but this folder's include/b3mod_api.h, on Windows (MinGW-w64 or MSVC), Linux and macOS:
#
#   cmake -S sdk -B build/hello-overlay -DMOD=examples/mods/hello-overlay
#   cmake --build build/hello-overlay
#
# The library goes to <MOD>/bin/<system>/<NAME>.<dll|so|dylib> (windows, linux, macos), the path the mod's mod.ini names
# under [code]; NAME defaults to the mod folder's name with '-' as '_'.
cmake_minimum_required(VERSION 3.16)
project(b3mod_template C CXX)

set(MOD "" CACHE PATH "the mod folder (with mod.ini and src/)")
if(NOT MOD)
  message(FATAL_ERROR "-DMOD=<mod folder> (the folder with mod.ini and src/)")
endif()
get_filename_component(MOD_DIR "${MOD}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(NOT EXISTS "${MOD_DIR}/src")
  get_filename_component(MOD_DIR "${MOD}" ABSOLUTE)   # relative to the current folder instead
endif()
get_filename_component(MOD_FOLDER "${MOD_DIR}" NAME)
string(REPLACE "-" "_" NAME_DEFAULT "${MOD_FOLDER}")
set(NAME "${NAME_DEFAULT}" CACHE STRING "the library's name (without lib or an extension)")

if(WIN32)
  set(B3MOD_SYSTEM windows)
elseif(APPLE)
  set(B3MOD_SYSTEM macos)
else()
  set(B3MOD_SYSTEM linux)
endif()

file(GLOB B3MOD_SOURCES "${MOD_DIR}/src/*.c" "${MOD_DIR}/src/*.cpp")
if(NOT B3MOD_SOURCES)
  message(FATAL_ERROR "no .c or .cpp files in ${MOD_DIR}/src")
endif()

add_library(b3mod_library MODULE ${B3MOD_SOURCES})
target_include_directories(b3mod_library PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
# only what B3MOD_EXPORT marks is exported (b3mod_main, b3mod_built_for)
set_target_properties(b3mod_library PROPERTIES
  OUTPUT_NAME "${NAME}" PREFIX "" C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
  LIBRARY_OUTPUT_DIRECTORY "${MOD_DIR}/bin/${B3MOD_SYSTEM}"
  RUNTIME_OUTPUT_DIRECTORY "${MOD_DIR}/bin/${B3MOD_SYSTEM}")
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)   # (multi-config generators: no per-configuration subfolder)
  string(TOUPPER "${cfg}" CFG)
  set_target_properties(b3mod_library PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${CFG} "${MOD_DIR}/bin/${B3MOD_SYSTEM}"
                                                 RUNTIME_OUTPUT_DIRECTORY_${CFG} "${MOD_DIR}/bin/${B3MOD_SYSTEM}")
endforeach()
if(APPLE)
  set_target_properties(b3mod_library PROPERTIES SUFFIX ".dylib")
elseif(WIN32)
  set_target_properties(b3mod_library PROPERTIES SUFFIX ".dll")
else()
  set_target_properties(b3mod_library PROPERTIES SUFFIX ".so")
endif()
if(MSVC)
  target_compile_options(b3mod_library PRIVATE /W3 /EHsc)
else()
  # -fexceptions: the port may unwind through a hook's frame when a run ends inside a game call
  target_compile_options(b3mod_library PRIVATE -Wall -Wextra -fexceptions)
  if(MINGW)
    target_link_options(b3mod_library PRIVATE -static-libgcc)   # (needs no MinGW DLL beside it)
    if(B3MOD_SOURCES MATCHES "\\.cpp")
      target_link_options(b3mod_library PRIVATE -static-libstdc++)
    endif()
  endif()
endif()
if(APPLE)   # ad-hoc signed (Apple silicon loads no unsigned code)
  add_custom_command(TARGET b3mod_library POST_BUILD COMMAND codesign --force -s - "$<TARGET_FILE:b3mod_library>" || true)
endif()
