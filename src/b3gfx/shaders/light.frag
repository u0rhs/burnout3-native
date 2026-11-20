#version 450
// b3gfx headlight beam (night mode, a remaster feature off by default; docs/remaster-features.md §17). Drawn with
// mesh.vert's simple model over a quad on the ground ahead of a car: v_uv = (lateral metres from the car's centre line,
// metres ahead of the lamps), v_color = the lamp colour, v_normal.x = the car's half width at the lamps (m). The presets
// without the projected beams (nightlights, §17.2: beam.frag) draw these. The beam does not add light: the pre-grade
// colour (`src`, the copy night.frag grades) x the lamp's colour is blended over the frame with the alpha 1 - exp(-L)
// (SRC_ALPHA / ONE_MINUS_SRC_ALPHA), so a dark road lights up to at most its daytime colour, and overlapping pools
// combine as 1 - prod(1 - a_i). The sky is drawn over them afterwards.
// Two parts: the pool just ahead of the car (wide, bright, falling off quickly) and the throw (the beam's long reach,
// narrower, brightest along the centre line, fading out towards the range), so a beam shows past a chase camera's car.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_normal;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 3, binding = 0) uniform LightUniforms {
    vec4 shape;   // x: tan of the cone's half angle, y: range (m), z: the pool's gain, w: the throw's gain
};
void main() {
    float d = v_uv.y;
    if (d <= 0.0) discard;
    float w = max(v_normal.x, 0.1) + d * shape.x;
    float spread = abs(v_uv.x) / w;
    float cone = 1.0 - smoothstep(0.4, 1.0, spread);
    float hot = 1.0 - smoothstep(0.0, 0.7, spread);   // the beam's centre
    float r = d / shape.y;
    float pool = smoothstep(0.0, 0.03, r) / (1.0 + 60.0 * r * r);
    float th = smoothstep(0.02, 0.12, r) * (1.0 - smoothstep(0.45, 1.0, r)) / (1.0 + 3.0 * r);
    float L = cone * (shape.z * pool * (0.55 + 0.45 * hot) + shape.w * th * hot);
    float f = max(v_color.r, max(v_color.g, v_color.b));   // the car's fade (the colour's brightest channel is 1 at full)
    float a = (1.0 - exp(-L)) * f;
    if (a <= 1e-4) discard;
    out_color = vec4(texelFetch(src, ivec2(gl_FragCoord.xy), 0).rgb * v_color.rgb / max(f, 1e-3), a);
}
