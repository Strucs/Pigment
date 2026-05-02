#version 450
#include "types.glsl"
#include "lights.glsl"

layout(push_constant) uniform constants {
    VertexBuffer vertex_buffer;
    CameraBuffer camera_buffer;
    LightBuffer light_buffer;
    float scale;
} push;

layout(location = 0) flat out uint fragLightIndex;

void main()
{
    Vertex v = push.vertex_buffer.vertices[gl_VertexIndex];
    Light L  = push.light_buffer.lights[gl_InstanceIndex];

    vec3 origin = (L.type == LIGHT_TYPE_DIRECTIONAL) ? -L.direction * 100.0 : L.position;
    vec3 world  = origin + v.pos * push.scale;

    gl_Position    = push.camera_buffer.proj * push.camera_buffer.view * vec4(world, 1.0);
    fragLightIndex = uint(gl_InstanceIndex);
}
