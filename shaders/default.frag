#version 450
#extension GL_EXT_nonuniform_qualifier : require
#include "types.glsl"
#include "material.glsl"

layout(push_constant) uniform constants {
    VertexBuffer vertex_buffer;
    InstanceBuffer instance_buffer;
    CameraBuffer camera_buffer;
    MaterialBuffer material_buffer;
} push;

layout(binding = 1) uniform sampler _sampler[];
layout(binding = 2) uniform texture2D _image[];

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragMaterialId;

layout(location = 0) out vec4 outColor;

void main()
{
    Material mat = push.material_buffer.materials[fragMaterialId];

    if(mat.albedo_image == -1)
    {
        vec2 c        = floor(fragTexCoord * 8.0);
        float checker = mod(c.x + c.y, 2.0);
        outColor      = mix(vec4(0.0, 0.0, 0.0, 1.0), vec4(1.0, 0.0, 1.0, 1.0), checker);
        return;
    }

    int s    = max(mat.albedo_sampler, 0);
    outColor = texture(sampler2D(_image[nonuniformEXT(mat.albedo_image)], _sampler[s]), fragTexCoord) * mat.base_color_factor * fragColor;
    if(outColor.w < 0.8)
    {
        discard;
    }
}
