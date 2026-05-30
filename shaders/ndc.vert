#version 450
#extension GL_EXT_buffer_reference : require

struct CanvasInstance {
    mat4 transform;
    vec4 color;
    vec4 uv_rect;
    int image_idx;
    int sampler_idx;
};

layout(buffer_reference, std430) readonly buffer CanvasInstanceBuffer {
    CanvasInstance instances[];
};

layout(push_constant) uniform constants {
    CanvasInstanceBuffer instance_buffer;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;
layout(location = 2) flat out int fragImageIdx;
layout(location = 3) flat out int fragSamplerIdx;

const vec2 QUAD_CORNERS[6] = vec2[](
    vec2(0.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 0.0),
    vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0)
);

void main()
{
    CanvasInstance inst = push.instance_buffer.instances[gl_InstanceIndex];
    vec2 corner         = QUAD_CORNERS[gl_VertexIndex];

    fragColor      = inst.color;
    fragUV         = mix(inst.uv_rect.xy, inst.uv_rect.zw, corner);
    fragImageIdx   = inst.image_idx;
    fragSamplerIdx = inst.sampler_idx;

    gl_Position = inst.transform * vec4(corner, 0.0, 1.0);
}
