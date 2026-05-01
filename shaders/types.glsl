#extension GL_EXT_buffer_reference : require

struct Vertex {
    vec3 pos;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

struct Instance {
    mat4 transform;
    uint material_id;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer InstanceBuffer {
    Instance instances[];
};

layout(buffer_reference, std430) readonly buffer CameraBuffer {
    mat4 view;
    mat4 proj;
};
