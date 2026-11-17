// Offline dumps of decoded assets: Wavefront OBJ + MTL with PNG textures (for inspection in any 3D tool).
#ifndef B3ASSET_EXPORT_H
#define B3ASSET_EXPORT_H

#include <string>

#include "b3asset/model.h"

namespace b3asset {

// Writes <dir>/<stem>.obj, <stem>.mtl and one PNG per texture (level 0). Returns false on an I/O error.
bool write_obj(const Model& m, const std::string& dir, const std::string& stem);

// Reads a whole file: a host path or a path in a disc image (b3asset/discfs.h). Returns false if it cannot be read.
bool read_file(const std::string& path, std::vector<std::uint8_t>& out);

}  // namespace b3asset

#endif
