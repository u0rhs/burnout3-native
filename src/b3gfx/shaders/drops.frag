#version 450
// b3gfx weather (remaster-features §38): rain drops on the lens, over the 3D picture before the 2D stage. Each drop a
// small dome of water: the scene behind it seen flipped and magnified through it (the lens of a drop), a darker rim,
// a soft highlight; blended by its own alpha (its age's fade). `src`: the frame copied at the rain's grade split (the
// night pass's copy: before the grade), graded here as night.frag grades the world (so a drop shows the scene as the
// frame does, without the lights and effects drawn after the grade). Frame units.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 3, binding = 0) uniform DropUniforms {
    vec4 a;          // x: the drops' count (0..24), y: the view's aspect (width / height), z: 1 at night (the copy lacks
                     // the night's lights: the drops mostly clear, their highlight kept)
    vec4 g;          // the grade: x saturation kept, y gamma, z pivot
    vec4 tint;       // the grade's world multiplier (rgb)
    vec4 d[24];      // xy: the centre (uv), z: the radius (uv of the height), w: the alpha (0..1)
};

void main() {
    int n = int(a.x + 0.5);
    float asp = a.y;
    for (int i = 0; i < 24; ++i) {
        if (i >= n) break;
        vec4 q = d[i];
        vec2 p = (v_uv - q.xy) * vec2(asp, 1.0);
        float l = length(p) / max(q.z, 1e-4);
        if (l >= 1.0) continue;
        float h = sqrt(max(1.0 - l * l, 0.0));                 // the dome's height
        vec2 ruv = q.xy - (v_uv - q.xy) * (1.4 + 1.2 * h);     // the scene round the drop, flipped and magnified
        // (a third of the scene right behind the drop, unrefracted: a drop over a dark road at night is not a black ball)
        vec3 c = mix(texture(src, clamp(ruv, vec2(0.002), vec2(0.998))).rgb, texture(src, v_uv).rgb, 0.35);
        c = mix(vec3(dot(c, vec3(0.299, 0.587, 0.114))), c, g.x) * tint.rgb;
        c = pow(max(c, vec3(0.0)) / g.z, vec3(max(g.y, 0.1))) * g.z * 0.9;
        c *= mix(0.7, 1.0, smoothstep(0.0, 0.55, h));           // the rim darker (light bent away)
        vec3 nrm = normalize(vec3(p / max(q.z, 1e-4), h + 0.15));
        float spec = pow(max(dot(nrm, normalize(vec3(-0.45, 0.6, 0.66))), 0.0), 24.0);
        float alpha = q.w * (1.0 - smoothstep(0.82, 1.0, l));
        float k = a.z > 0.5 ? 0.3 : 1.0;
        c += vec3(0.32) * spec / k;   // (so the highlight keeps its strength through the lower alpha)
        alpha *= k;
        out_color = vec4(c, alpha);
        return;   // (the first drop listed wins where two overlap)
    }
    discard;
}
