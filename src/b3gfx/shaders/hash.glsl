// d3dhash: the shaders' random values (the night's lit windows, the stars, the rain's puddles, the metallic paint's
// flakes) from integer hashes of integer cells, exact on every backend and GPU.
//
// The float hashes they replace ("hash without sine": fract() of products near a few thousand) moved with the last bit
// of a multiply-add, and whether a compiler fuses one is its own choice: dxc (D3D12) and the Vulkan drivers fused
// differently, so a night shop front's windows all came out lit and blue-white on D3D12 and warm on Vulkan. uint
// arithmetic wraps modulo 2^32 the same in SPIR-V, DXIL and MSL, and the top 24 bits convert to float exactly.
//
// `cell` holds integer values (a floor()): its int conversion is exact, negative cells included (two's complement).

// PCG3D [Jarzynski & Olano 2020, "Hash Functions for GPU Rendering"]
uvec3 hash_pcg3d(uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

// Three values in [0, 1) for an integer cell
vec3 hash3i(vec3 cell) {
    uvec3 h = hash_pcg3d(uvec3(ivec3(cell)));
    return vec3(h >> 8u) * (1.0 / 16777216.0);
}

// The same, stream `stream` of a 3D cell (independent values for one cell: a second draw, a frame): the cell moved by
// the stream times large odd constants before the hash, not by small diagonal offsets (cell + 17 is a neighbour's cell)
vec3 hash3i_s(vec3 cell, uint stream) {
    uvec3 h = hash_pcg3d(uvec3(ivec3(cell)) + stream * uvec3(0x9E3779B9u, 0x7F4A7C15u, 0x85EBCA6Bu));
    return vec3(h >> 8u) * (1.0 / 16777216.0);
}
