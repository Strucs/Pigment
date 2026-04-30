#version 450
#extension GL_EXT_buffer_reference : require

struct Vertex {
    vec3  pos;
    float uv_x;
    vec3  normal;
    float uv_y;
    vec4  color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer TransformBuffer {
    mat4 transforms[];
};

layout(buffer_reference, std430) readonly buffer CameraBuffer {
    mat4 view;
    mat4 proj;
};

layout(push_constant) uniform constants {
    VertexBuffer    vertex_buffer;
    TransformBuffer transform_buffer;
    CameraBuffer    camera_buffer;
    int             image_index;
    int             sampler_index;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out int fragImageIndex;
layout(location = 3) flat out int fragSamplerIndex;

void main()
{
    Vertex v          = push.vertex_buffer.vertices[gl_VertexIndex];
    mat4 world_matrix = push.transform_buffer.transforms[gl_InstanceIndex];

    fragColor        = v.color;
    fragTexCoord     = vec2(v.uv_x, v.uv_y);
    fragImageIndex   = push.image_index;
    fragSamplerIndex = push.sampler_index;

    gl_Position = push.camera_buffer.proj * push.camera_buffer.view * world_matrix * vec4(v.pos, 1.0);
}
