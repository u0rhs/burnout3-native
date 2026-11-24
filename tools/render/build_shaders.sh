#!/bin/bash
# Compiles the b3gfx shaders (src/b3gfx/shaders/*.vert|frag, GLSL 450) to SPIR-V (Vulkan: Windows, Linux) and MSL
# (Metal: macOS) and writes src/b3gfx/shaders_gen.h (committed, so normal builds need no shader tools).
#   glslangValidator 16.2.0 and spirv-cross (Vulkan SDK 1.4.335) from Ubuntu 26.04 packages, unpacked without root into
#   ../ext/shadertools (apt-get download + dpkg -x; hashes in ../ext/debs/SHA256SUMS). Override with GLSLANG= / SPIRV_CROSS=.
# d3d12: and DXIL (Direct3D 12: Windows' GRAPHICS API D3D12 or Auto's fallback): the SPIR-V as HLSL by the same spirv-cross
# (shader model 6.0; its registers are SDL_GPU's D3D12 layout as they come: vertex uniforms b# space1, fragment textures
# and samplers t# / s# space2, fragment uniforms b# space3, vertex inputs TEXCOORD<location>), then Microsoft's dxc
# v1.9.2609 for Linux (linux_dxc_2026_09_28.x86_x64.tar.gz, SHA-256 96faadc7...aa39a1, the release's published digest),
# unpacked into ../ext/dxc/v1.9.2609 (its libdxil.so signs the containers). Override with DXC=.
WS=${B3_WORKSPACE:-$(dirname "$(dirname "$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)")")}   # the folder that holds this repo (its main checkout)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../.." && pwd)
T=${SHADERTOOLS:-$WS/ext/shadertools/usr}
export LD_LIBRARY_PATH="$T/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
GLSLANG=${GLSLANG:-$T/bin/glslangValidator}
SPIRV_CROSS=${SPIRV_CROSS:-$T/bin/spirv-cross}
DXC=${DXC:-$WS/ext/dxc/v1.9.2609/bin/dxc}
export LD_LIBRARY_PATH="$(dirname "$(dirname "$DXC")")/lib:$LD_LIBRARY_PATH"
SRC=$PORT/src/b3gfx/shaders
OUT=$PORT/build/shaders
mkdir -p "$OUT"
# every shader, in the header's order (d3d12: one list for the SPIR-V / MSL / HLSL pass, the DXIL pass and the header)
SHADERS="mesh.vert mesh.frag fullscreen.vert specular.frag present.frag car.vert car.frag sun.vert sun.frag blur.frag post.frag post_lean.frag dof.frag light.frag ao.frag night.frag shadow.frag shadow_mesh.frag mesh_shadow.frag beam.frag emissive.frag motion.frag drops.frag mesh_depth.frag mesh_shadow_depth.frag car_depth.frag sdfix.frag"
for s in $SHADERS; do
    # mesh_shadow.frag: mesh.frag with SUN_SHADOW defined (the track draws that receive the sun shadow)
    # scenedepth: mesh_depth.frag / mesh_shadow_depth.frag / car_depth.frag with SCENE_DEPTH (the scene depth target)
    if [ "$s" = mesh_shadow.frag ]; then
        "$GLSLANG" -V --target-env vulkan1.0 -S frag -DSUN_SHADOW -o "$OUT/$s.spv" "$SRC/mesh.frag" > /dev/null
    elif [ "$s" = mesh_depth.frag ]; then
        "$GLSLANG" -V --target-env vulkan1.0 -S frag -DSCENE_DEPTH -o "$OUT/$s.spv" "$SRC/mesh.frag" > /dev/null
    elif [ "$s" = mesh_shadow_depth.frag ]; then
        "$GLSLANG" -V --target-env vulkan1.0 -S frag -DSUN_SHADOW -DSCENE_DEPTH -o "$OUT/$s.spv" "$SRC/mesh.frag" > /dev/null
    elif [ "$s" = car_depth.frag ]; then
        "$GLSLANG" -V --target-env vulkan1.0 -S frag -DSCENE_DEPTH -o "$OUT/$s.spv" "$SRC/car.frag" > /dev/null
    else
        "$GLSLANG" -V --target-env vulkan1.0 -o "$OUT/$s.spv" "$SRC/$s" > /dev/null
    fi
    "$SPIRV_CROSS" "$OUT/$s.spv" --msl --msl-version 20100 --msl-decoration-binding --output "$OUT/$s.metal"
    "$SPIRV_CROSS" "$OUT/$s.spv" --hlsl --shader-model 60 --output "$OUT/$s.hlsl"
done
# d3d12: D3D12 links the stages by their packed signatures, so each fragment shader takes its vertex shader's outputs
# as its inputs (hlsl_link.py; the pairs b3gfx_sdl.cpp's pipelines make), then dxc
partner() {
    case "$1" in
        mesh.frag|mesh_shadow.frag|light.frag|emissive.frag|shadow_mesh.frag|mesh_depth.frag|mesh_shadow_depth.frag) echo mesh.vert ;;
        car.frag|shadow.frag|car_depth.frag) echo car.vert ;;
        sun.frag) echo sun.vert ;;
        specular.frag|present.frag|blur.frag|post.frag|post_lean.frag|dof.frag|ao.frag|night.frag|beam.frag|motion.frag|drops.frag|sdfix.frag) echo fullscreen.vert ;;
        *) echo "build_shaders.sh: $1 has no vertex shader in partner() (add its pipeline's pair)" >&2; return 1 ;;
    esac
}
for s in $SHADERS; do
    case "$s" in
        *.vert) prof=vs_6_0 ;;
        *) prof=ps_6_0; vert=$(partner "$s"); python3 -I "$HERE/hlsl_link.py" "$OUT/$s.hlsl" "$OUT/$vert.hlsl" ;;
    esac
    "$DXC" -T "$prof" -E main -Fo "$OUT/$s.dxil" "$OUT/$s.hlsl" > /dev/null
done
python3 -I "$HERE/embed_shaders.py" "$OUT" "$PORT/src/b3gfx/shaders_gen.h" "$SRC" $SHADERS
echo "wrote src/b3gfx/shaders_gen.h"
