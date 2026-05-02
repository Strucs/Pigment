#version 450
#include "types.glsl"
#include "lights.glsl"

layout(push_constant) uniform constants {
    VertexBuffer vertex_buffer;
    CameraBuffer camera_buffer;
    LightBuffer light_buffer;
    float scale;
} push;

layout(location = 0) flat in uint fragLightIndex;
layout(location = 0) out vec4 outColor;

void main()
{
    Light L = push.light_buffer.lights[fragLightIndex];
    if(L.type == LIGHT_TYPE_INVALID)
    {
        discard;
    }
    outColor = vec4(L.color * L.intensity, 1.0);
}
