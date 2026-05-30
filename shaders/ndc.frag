#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(binding = 0) uniform sampler _sampler[];
layout(binding = 3) uniform texture2D _image[];

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) flat in int fragImageIdx;
layout(location = 3) flat in int fragSamplerIdx;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 tex = vec4(1.0);
    if(fragImageIdx != -1)
    {
        int s = max(fragSamplerIdx, 0);
        tex   = texture(sampler2D(_image[nonuniformEXT(fragImageIdx)], _sampler[s]), fragUV);
    }

    outColor = tex * fragColor;
}
