#version 450
// PROP SHADOWS (propshadow1, remaster-features §20): the sun-shadow caster pass's fragment stage for track draws (the
// props and the units' instances: lamp posts, signs, barriers), after mesh.vert's simple path with the map's
// orthographic view-projection. As shadow.frag (the light depth + 2 x the gate), with the texture's alpha cut for
// cut-out props (fences, foliage cards): a texel the main pass would drop or nearly clear casts nothing.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(set = 3, binding = 0) uniform ShadowCasterUniforms {
    vec4 gate;   // x: the gate, an integer 1..255; y: 1 = cut by alpha (the draw is alpha-tested or blended), 0 = solid
};

void main() {
    if (gate.y > 0.5) {
        // the GS alpha / 128 of the textured track path (mesh.frag: t.a x 128 x the vertex alpha, here in_color.a x 255 / 128)
        float a = texture(tex, v_uv).a * v_color.a * (255.0 / 128.0);
        if (a < 0.5) discard;
    }
    out_color = vec4(2.0 * gate.x + clamp(gl_FragCoord.z, 0.0, 0.99999), 0.0, 0.0, 0.0);
}
