/* chd_libchdr.c -- libchdr (src/native/third_party/libchdr, BSD-3; README.md there) as one C unit: the CHD reader that
 * src/b3asset/discfs.cpp opens .chd disc images with, and its decompressors (LZMA, miniz for deflate, zstd, dr_flac).
 * Upstream's unity.c with the AV codec it leaves out (libchdr_chd.c's codec table names it) and the port's settings:
 *   - miniz: inflate only (no archive, deflate, stdio or time APIs), as upstream's unity.c;
 *   - dr_flac: the port's copy (include/dr_libs/dr_flac.h there points at it) with every function static, so the
 *     custom soundtrack's dr_flac (src/native/music_decoders.cpp) is the only one a program exports;
 *   - libchdr's defaults otherwise: each hunk checked against its stored CRC (a damaged image reads as an error). */
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define DRFLAC_API static
#define DRFLAC_PRIVATE static

#include "../native/third_party/libchdr/deps/lzma-26.02/src/LzmaDec.c"
#include "../native/third_party/libchdr/deps/miniz-3.1.2/miniz.c"
#include "../native/third_party/libchdr/deps/zstd-1.5.7/zstddeclib.c"

#include "../native/third_party/libchdr/src/libchdr_bitstream.c"
#include "../native/third_party/libchdr/src/libchdr_cdrom.c"
#include "../native/third_party/libchdr/src/libchdr_chd.c"
#include "../native/third_party/libchdr/src/libchdr_codec_avhuff.c"
#include "../native/third_party/libchdr/src/libchdr_codec_cdfl.c"
#include "../native/third_party/libchdr/src/libchdr_codec_cdlz.c"
#include "../native/third_party/libchdr/src/libchdr_codec_cdzl.c"
#include "../native/third_party/libchdr/src/libchdr_codec_cdzs.c"
#include "../native/third_party/libchdr/src/libchdr_codec_flac.c"
#include "../native/third_party/libchdr/src/libchdr_codec_huff.c"
#include "../native/third_party/libchdr/src/libchdr_codec_lzma.c"
#include "../native/third_party/libchdr/src/libchdr_codec_zlib.c"
#include "../native/third_party/libchdr/src/libchdr_codec_zstd.c"
#include "../native/third_party/libchdr/src/libchdr_flac.c"
#include "../native/third_party/libchdr/src/libchdr_huffman.c"
