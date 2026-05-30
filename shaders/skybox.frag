#version 450
#extension GL_EXT_nonuniform_qualifier : require
#include "types.glsl"

layout(push_constant) uniform constants {
    CameraBuffer camera_buffer;
    uint cubemap_id;
    uint sampler_id;
} push;

layout(binding = 0) uniform sampler _sampler[];
layout(binding = 1) uniform textureCube _cubemap[];

layout(location = 0) in vec3 viewDir;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(samplerCube(_cubemap[push.cubemap_id], _sampler[push.sampler_id]), normalize(viewDir));
}
