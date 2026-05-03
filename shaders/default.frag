#version 450
#extension GL_EXT_nonuniform_qualifier : require
#include "types.glsl"
#include "material.glsl"
#include "lights.glsl"

layout(push_constant) uniform constants {
    VertexBuffer vertex_buffer;
    InstanceBuffer instance_buffer;
    CameraBuffer camera_buffer;
    MaterialBuffer material_buffer;
    LightBuffer light_buffer;
} push;

layout(binding = 0) uniform sampler _sampler[];
layout(binding = 2) uniform texture2D _image[];

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragMaterialId;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragWorldNormal;

layout(location = 0) out vec4 outColor;

void main()
{
    Material mat = push.material_buffer.materials[fragMaterialId];

    vec4 albedo;
    if(mat.albedo_image == -1)
    {
        vec2 c        = floor(fragTexCoord * 8.0);
        float checker = mod(c.x + c.y, 2.0);
        albedo        = mix(vec4(0.0, 0.0, 0.0, 1.0), vec4(1.0, 0.0, 1.0, 1.0), checker);
    }
    else
    {
        int s  = max(mat.albedo_sampler, 0);
        albedo = texture(sampler2D(_image[nonuniformEXT(mat.albedo_image)], _sampler[s]), fragTexCoord) * mat.base_color_factor * fragColor;
    }

    if(albedo.w < 0.8)
    {
        discard;
    }

    vec3 normal = normalize(fragWorldNormal);
    vec3 lit    = evaluate_lighting(push.light_buffer, fragWorldPos, normal, albedo.rgb);

    vec3 emissive = mat.emissive_factor.rgb;
    if(mat.emissive_image != -1)
    {
        int es   = max(mat.emissive_sampler, 0);
        emissive *= texture(sampler2D(_image[nonuniformEXT(mat.emissive_image)], _sampler[es]), fragTexCoord).rgb;
    }
    lit += emissive;

    outColor = vec4(lit, albedo.w);
}
