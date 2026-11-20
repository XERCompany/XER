// quad_tex.frag — BGRA texture draws (images, video frames, resources).
// v_color is a straight-alpha tint; its alpha carries the draw opacity.
#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(tex, v_uv) * v_color;
}
