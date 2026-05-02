const uint LIGHT_TYPE_INVALID     = 0;
const uint LIGHT_TYPE_DIRECTIONAL = 1;
const uint LIGHT_TYPE_POINT       = 2;
const uint LIGHT_TYPE_SPOT        = 3;

struct Light {
    vec3 position;
    uint type;

    vec3 direction;
    float range;

    vec3 color;
    float intensity;

    float inner_cone_cos;
    float outer_cone_cos;
};

layout(buffer_reference, std430) readonly buffer LightBuffer {
    vec3 ambient_color;
    uint count;
    Light lights[];
};

float light_attenuation(float dist, float range)
{
    if(range <= 0.0)
    {
        return 1.0;
    }
    float t = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    return t * t / max(dist * dist, 1e-4);
}

vec3 compute_ambient(LightBuffer light_buffer, vec3 normal, vec3 base_color)
{
    return light_buffer.ambient_color * base_color;
}

vec3 evaluate_lighting(LightBuffer light_buffer, vec3 world_pos, vec3 normal, vec3 base_color)
{
    vec3 lit = compute_ambient(light_buffer, normal, base_color);

    for(uint i = 0; i < light_buffer.count; i++)
    {
        Light L = light_buffer.lights[i];
        if(L.type == LIGHT_TYPE_INVALID)
        {
            continue;
        }

        vec3 to_light;
        float attenuation = 1.0;

        if(L.type == LIGHT_TYPE_DIRECTIONAL)
        {
            to_light = normalize(-L.direction);
        }
        else
        {
            vec3 delta  = L.position - world_pos;
            float dist  = length(delta);
            to_light    = delta / max(dist, 1e-4);
            attenuation = light_attenuation(dist, L.range);

            if(L.type == LIGHT_TYPE_SPOT)
            {
                float cos_angle = dot(normalize(-L.direction), to_light);
                float spot      = smoothstep(L.outer_cone_cos, L.inner_cone_cos, cos_angle);
                attenuation *= spot;
            }
        }

        float ndotl = max(dot(normal, to_light), 0.0);
        lit += base_color * L.color * (L.intensity * ndotl * attenuation);
    }

    return lit;
}
