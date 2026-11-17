#version 450
// Sun corona and lens flare sprites (water1, docs/native-renderer.md §21): 6 vertices per sprite from a uniform array
// (CB3SunRenderer::RenderCorona 0x24BF60's GS sprites; positions already in NDC, SDL_GPU +y up).
layout(set = 1, binding = 0) uniform SunVertexUniforms {
    vec4 rect[13];   // NDC x0, y0, x1, y1
    vec4 uv[13];     // u0, v0, u1, v1
    vec4 rgb[13];    // vertex colour, GS bytes / 255
};
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_rgb;
void main() {
    const vec2 corner[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1), vec2(0, 1));
    int s = gl_VertexIndex / 6;
    vec2 t = corner[gl_VertexIndex % 6];
    gl_Position = vec4(mix(rect[s].x, rect[s].z, t.x), mix(rect[s].y, rect[s].w, t.y), 0.0, 1.0);
    v_uv = vec2(mix(uv[s].x, uv[s].z, t.x), mix(uv[s].y, uv[s].w, t.y));
    v_rgb = rgb[s].rgb;
}
