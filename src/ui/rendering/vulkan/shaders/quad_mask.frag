// quad_mask.frag — text draws. The uploaded texture is a white-background
// coverage mask rasterized by GDI (black glyphs on white), so glyph alpha is
// 1 - luminance; the tint supplies the text color.
#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main() {
    const float coverage = 1.0 - texture(tex, v_uv).r;
    out_color = vec4(v_color.rgb, v_color.a * coverage);
}
