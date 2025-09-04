#version 450

#extension GL_EXT_nonuniform_qualifier : require
#define MAX_SAMPLERS 2

layout (binding = 1) uniform sampler _sampler[MAX_SAMPLERS];
layout (binding = 2) uniform texture2D _texture[];

layout (location = 0) in vec3 fragColor;
layout (location = 1) in vec2 fragTexCoord;
layout (location = 2) flat in int inTexIndex;
layout (location = 3) flat in int inSamplerIndex;

layout (location = 0) out vec4 outColor;

void main()
{
    int samplerIndex;
    if (inTexIndex <= 0)
    {
        samplerIndex = 0;
    }
    else
    {
        samplerIndex = inSamplerIndex;
    }
    outColor = texture(sampler2D(_texture[nonuniformEXT(inTexIndex)], _sampler[samplerIndex]), fragTexCoord);
    if (outColor.w < 0.8)
    {
        discard;
    }
}