/* Not part of libchdr: the port's own copy of dr_flac (../../../dr_flac.h, src/native/third_party/README.md) stands in
 * for the one libchdr bundles (v0.13.4), so one dr_flac is in the tree. src/b3asset/chd_libchdr.c compiles it with
 * DRFLAC_API static: its functions stay inside that unit, apart from the custom soundtrack's (music_decoders.cpp). */
#include "../../../dr_flac.h"
