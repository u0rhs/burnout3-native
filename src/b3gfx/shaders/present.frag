#version 450
// Present [C CB3GraphicsManager::PresentDrawBuffer 0x2270B0 -> CBlit::BuildPacket 0x22B540]: the render buffer is
// drawn into the display buffer as a texture with TFX = MODULATE and vertex colour luBrightness = min(255,
// 256 * brightness), brightness 1.0 by default: every channel is multiplied by 255/128 and clamped.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D scene;
layout(set = 3, binding = 0) uniform PresentUniforms { vec4 scale; };  // x: brightness factor, y: dither (perf1)
// perf1 (docs/native-renderer.md §33.8): with scale.y > 0 an ordered 4 x 4 Bayer dither of +-scale.y / 2 output steps
// (8-bit) breaks up the one-step bands of shallow gradients; scale.y = 0 is the plain present, as before.
void main() {
    vec3 c = min(texture(scene, v_uv).rgb * scale.x, vec3(1.0));
    if (scale.y > 0.0) {
        const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        ivec2 p = ivec2(gl_FragCoord.xy) & 3;
        c = clamp(c + ((bayer[p.y * 4 + p.x] + 0.5) / 16.0 - 0.5) * scale.y / 255.0, 0.0, 1.0);
    }
    out_color = vec4(c, 1.0);
}
