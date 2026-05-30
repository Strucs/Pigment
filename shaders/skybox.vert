#version 450
#include "types.glsl"

layout(push_constant) uniform constants {
    CameraBuffer camera_buffer;
    uint cubemap_id;
    uint sampler_id;
} push;

layout(location = 0) out vec3 viewDir;

void main()
{
    vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vec2 ndc = uv * 2.0 - 1.0;

    mat4 inv_proj = inverse(push.camera_buffer.proj);
    vec4 view_pos = inv_proj * vec4(ndc, 1.0, 1.0);
    view_pos /= view_pos.w;

    mat3 view_rot = mat3(push.camera_buffer.view);
    viewDir = transpose(view_rot) * view_pos.xyz;

    gl_Position = vec4(ndc, 0.0, 1.0);
}
