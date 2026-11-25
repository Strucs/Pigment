/**
 * Copyright 2026 Angel-Leduc TA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

module lights;

import buffer;

namespace pigment.std {
    public static const uint LIGHT_TYPE_INVALID     = 0;
    public static const uint LIGHT_TYPE_DIRECTIONAL = 1;
    public static const uint LIGHT_TYPE_POINT       = 2;
    public static const uint LIGHT_TYPE_SPOT        = 3;

    public struct Light {
        public float3 position;
        public uint type;

        public float3 direction;
        public float range;

        public float3 color;
        public float intensity;

        public float inner_cone_cos;
        public float outer_cone_cos;
    };

    public struct LightsHeader {
        public float3 ambient_color;
        public uint count;
    };

    public struct Lights {
        public Buffer<LightsHeader> header;
        public Buffer<Light> items;
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

    public float3 evaluate_lighting(Lights lights, float3 world_pos, float3 normal, float3 base_color)
    {
        LightsHeader header = lights.header[0];
        float3 lit          = header.ambient_color * base_color;

        for(uint i = 0; i < header.count; i++)
        {
            Light L = lights.items[i];
            if(L.type == LIGHT_TYPE_INVALID)
            {
                continue;
            }

            float3 to_light;
            float attenuation = 1.0;

            if(L.type == LIGHT_TYPE_DIRECTIONAL)
            {
                to_light = normalize(-L.direction);
            }
            else
            {
                float3 delta = L.position - world_pos;
                float dist   = length(delta);
                to_light     = delta / max(dist, 1e-4);
                attenuation  = light_attenuation(dist, L.range);

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
}
