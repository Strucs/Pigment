#version 450

#extension GL_EXT_nonuniform_qualifier : require
#define MAX_SAMPLERS 16

layout (binding = 1) uniform sampler _sampler[MAX_SAMPLERS];
layout (binding = 2) uniform texture2D _image[];

layout (location = 0) in vec4 fragColor;
layout (location = 1) in vec2 fragTexCoord;
layout (location = 2) flat in int inImageIndex;
layout (location = 3) flat in int inSamplerIndex;

layout (location = 0) out vec4 outColor;

void main()
{
    // -1 (missing texture) -> magenta/black checker
    if (inImageIndex == -1)
    {
        vec2 c        = floor(fragTexCoord * 8.0);
        float checker = mod(c.x + c.y, 2.0);
        outColor      = mix(vec4(0.0, 0.0, 0.0, 1.0), vec4(1.0, 0.0, 1.0, 1.0), checker);
        return;
    }

    outColor = texture(sampler2D(_image[nonuniformEXT(inImageIndex)], _sampler[inSamplerIndex]), fragTexCoord) * fragColor;
    if (outColor.w < 0.8)
    {
        discard;
    }
}
