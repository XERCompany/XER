// quad.vert — fullscreen-quad primitive used for every VulkanRenderer draw.
//
// Convex quad corners arrive as push constants (NDC, computed CPU-side from
// the widget-space rect + active 2x3 transform + viewport). The quad is
// emitted as two triangles: (0,1,2) and (1,3,2) with corner order
// 0=TL, 1=TR, 2=BL, 3=BR. UVs map corner-wise so textured draws can pass
// sub-rects of a texture atlas.
//
// Regenerate the embedded blobs after editing:
//   glslc quad.vert        -o quad.vert.spv
//   glslc quad_color.frag  -o quad_color.frag.spv
//   glslc quad_tex.frag    -o quad_tex.frag.spv
//   glslc quad_mask.frag   -o quad_mask.frag.spv
// then xxd -i / embed the bytes in vulkan_renderer.cc (kQuad*Spv arrays).
#version 450

layout(push_constant) uniform PushConstants {
    vec4 corners01;  // c0.xy = top-left, c1.xy = top-right (NDC)
    vec4 corners23;  // c2.xy = bottom-left, c3.xy = bottom-right (NDC)
    vec4 color;      // straight-alpha RGBA tint
    vec4 uv01;       // u0,v0,u1,v1
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main() {
    const vec2 corners[4] = vec2[](
        pc.corners01.xy,
        pc.corners01.zw,
        pc.corners23.xy,
        pc.corners23.zw);
    const vec2 uvs[4] = vec2[](
        vec2(pc.uv01.x, pc.uv01.y),
        vec2(pc.uv01.z, pc.uv01.y),
        vec2(pc.uv01.x, pc.uv01.w),
        vec2(pc.uv01.z, pc.uv01.w));
    const int indices[6] = int[](0, 1, 2, 1, 3, 2);
    const int i = indices[gl_VertexIndex];
    gl_Position = vec4(corners[i], 0.0, 1.0);
    v_uv = uvs[i];
    v_color = pc.color;
}
