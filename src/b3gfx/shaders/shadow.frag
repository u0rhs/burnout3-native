#version 450
// SUN SHADOWS (remaster-features §20): the caster pass's fragment stage, after car.vert with the map's orthographic
// view-projection. Writes the caster's light depth + 2 x its gate (shaders/sun_shadow.glsl reads it back); the
// depth test (LESS_OR_EQUAL, cleared to 1) keeps the caster nearest to the light per texel.
layout(location = 0) out vec4 out_color;

layout(set = 3, binding = 0) uniform ShadowCasterUniforms {
    vec4 gate;   // x: the gate, an integer 1..255 (the caster's sky_open, and its traffic fade-in)
};

void main() {
    out_color = vec4(2.0 * gate.x + clamp(gl_FragCoord.z, 0.0, 0.99999), 0.0, 0.0, 0.0);
}
