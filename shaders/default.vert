#version 450
#include "types.glsl"

layout(push_constant) uniform constants
{
    VertexBuffer vertex_buffer;
    InstanceBuffer instance_buffer;
    CameraBuffer camera_buffer;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint fragMaterialId;

void main()
{
    Vertex v      = push.vertex_buffer.vertices[gl_VertexIndex];
    Instance inst = push.instance_buffer.instances[gl_InstanceIndex];

    fragColor      = v.color;
    fragTexCoord   = vec2(v.uv_x, v.uv_y);
    fragMaterialId = inst.material_id;

    gl_Position = push.camera_buffer.proj * push.camera_buffer.view * inst.transform * vec4(v.pos, 1.0);
}

