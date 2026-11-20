#!/bin/bash
# Compiles the b3gfx shaders (src/b3gfx/shaders/*.vert|frag, GLSL 450) to SPIR-V (Vulkan: Windows, Linux) and MSL
# (Metal: macOS) and writes src/b3gfx/shaders_gen.h (committed, so normal builds need no shader tools).
#   glslangValidator 16.2.0 and spirv-cross (Vulkan SDK 1.4.335) from Ubuntu 26.04 packages, unpacked without root into
#   ../ext/shadertools (apt-get download + dpkg -x; hashes in ../ext/debs/SHA256SUMS). Override with GLSLANG= / SPIRV_CROSS=.
# D3D12 (DXIL) is not generated: no DXC here; SDL_GPU on Windows uses its Vulkan backend (docs/native-renderer.md §10).
WS=${B3_WORKSPACE:-$(dirname "$(dirname "$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)")")}   # the folder that holds this repo (its main checkout)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../.." && pwd)
T=${SHADERTOOLS:-$WS/ext/shadertools/usr}
export LD_LIBRARY_PATH="$T/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
GLSLANG=${GLSLANG:-$T/bin/glslangValidator}
SPIRV_CROSS=${SPIRV_CROSS:-$T/bin/spirv-cross}
SRC=$PORT/src/b3gfx/shaders
OUT=$PORT/build/shaders
mkdir -p "$OUT"
for s in mesh.vert mesh.frag fullscreen.vert specular.frag present.frag car.vert car.frag sun.vert sun.frag blur.frag post.frag post_lean.frag dof.frag light.frag ao.frag night.frag shadow.frag mesh_shadow.frag beam.frag emissive.frag motion.frag; do
    # mesh_shadow.frag: mesh.frag with SUN_SHADOW defined (the track draws that receive the sun shadow)
    if [ "$s" = mesh_shadow.frag ]; then
        "$GLSLANG" -V --target-env vulkan1.0 -S frag -DSUN_SHADOW -o "$OUT/$s.spv" "$SRC/mesh.frag" > /dev/null
    else
        "$GLSLANG" -V --target-env vulkan1.0 -o "$OUT/$s.spv" "$SRC/$s" > /dev/null
    fi
    "$SPIRV_CROSS" "$OUT/$s.spv" --msl --msl-version 20100 --msl-decoration-binding --output "$OUT/$s.metal"
done
python3 -I "$HERE/embed_shaders.py" "$OUT" "$PORT/src/b3gfx/shaders_gen.h" "$SRC" mesh.vert mesh.frag fullscreen.vert specular.frag present.frag car.vert car.frag sun.vert sun.frag blur.frag post.frag post_lean.frag dof.frag light.frag ao.frag night.frag shadow.frag mesh_shadow.frag beam.frag emissive.frag motion.frag
echo "wrote src/b3gfx/shaders_gen.h"
