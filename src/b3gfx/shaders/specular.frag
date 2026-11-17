#version 450
// Full-screen specular [C CB3GraphicsManager::RenderFullScreenSpecular 0x2274B0]: a sprite of colour
// min(sun specular colour * 127.5 * intensity, 255) blended with ALPHA 0x58 = Cs * Ad + Cd, i.e. every pixel gains
// colour * (its frame-buffer alpha / 128). The pipeline blends with DST_ALPHA / ONE; alpha is left alone.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 3, binding = 0) uniform SpecularUniforms { vec4 colour; };  // GS bytes / 255
void main() { out_color = vec4(colour.rgb, 0.0); }
