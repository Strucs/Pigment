#version 450
#extension GL_EXT_buffer_reference : require

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

struct Vertex {
    vec3  pos;
    float uv_x;
    vec3  normal;
    float uv_y;
    vec4  color;
    int   texture_index;
    int   sampler_index;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant) uniform constants {
    mat4         world_matrix;
    VertexBuffer vertex_buffer;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out int fragTexIndex;
layout(location = 3) flat out int fragSamplerIndex;

void main()
{
    Vertex v = push.vertex_buffer.vertices[gl_VertexIndex];

    fragColor        = v.color;
    fragTexCoord     = vec2(v.uv_x, v.uv_y);
    fragTexIndex     = v.texture_index;
    fragSamplerIndex = v.sampler_index;

    gl_Position = ubo.proj * ubo.view * push.world_matrix * vec4(v.pos, 1.0);
}