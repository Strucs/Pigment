#version 450

layout (binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inTexCoord;
layout (location = 3) in int inTextureIndex;
layout (location = 4) in int inSamplerIndex;

layout (location = 0) out vec3 fragColor;
layout (location = 1) out vec2 fragTexCoord;
layout (location = 2) flat out int fragTexIndex;
layout (location = 3) flat out int fragSamplerIndex;

void main()
{
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragTexIndex = inTextureIndex;
    fragSamplerIndex = inSamplerIndex;
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
}