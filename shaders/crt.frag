#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(push_constant) uniform constants {
    uint texture_id;
    uint sampler_id;
    float time;
    float aspect;
    float resolution_x;
    float resolution_y;
} push;

layout(binding = 0) uniform sampler _sampler[];
layout(binding = 2) uniform texture2D _render_target[];

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

vec3 sample_scene(vec2 coord)
{
    return texture(sampler2D(_render_target[push.texture_id], _sampler[push.sampler_id]), coord).rgb;
}

float hash(vec2 coord)
{
    coord = fract(coord * vec2(123.34, 456.21));
    coord += dot(coord, coord + 45.32);

    return fract(coord.x * coord.y);
}

void main()
{
    vec2 center_offset = uv - 0.5;
    float ca = 0.0008 * length(center_offset);
    vec3 color;
    color.r = sample_scene(uv + center_offset * ca).r;
    color.g = sample_scene(uv).g;
    color.b = sample_scene(uv - center_offset * ca).b;

    color = color / (color + vec3(1.0));

    float scan = 0.5 + 0.5 * sin(uv.y * push.resolution_y * 3.14159);
    float scan_strength = 0.25;
    color *= mix(1.0 - scan_strength, 1.0, scan);

    float roll = sin(uv.y * 2.0 + push.time * 1.5) * 0.5 + 0.5;
    color *= 0.97 + 0.03 * roll;

    float px = floor(uv.x * push.resolution_x);
    int phosphor = int(mod(px, 3.0));
    vec3 mask = vec3(1.0);
    if(phosphor == 0)
    {
        mask = vec3(1.05, 0.92, 0.92);
    }
    else if(phosphor == 1)
    {
        mask = vec3(0.92, 1.05, 0.92);
    }
    else
    {
        mask = vec3(0.92, 0.92, 1.05);
    }
    color *= mask;

    vec2 vig = uv - 0.5;
    float v = 1.0 - dot(vig, vig) * 1.4;
    color *= clamp(v, 0.0, 1.0);

    float grain = hash(uv * vec2(push.resolution_x, push.resolution_y) + push.time * 100.0);
    color += (grain - 0.5) * 0.08;

    color = pow(color, vec3(0.95));

    outColor = vec4(color, 1.0);
}
