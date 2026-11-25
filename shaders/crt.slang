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

module crt;

import bindless;
import vertex_input;

namespace pigment.std.crt {
    using pigment.std.bindless;

    struct PushConstants {
        uint texture_id;
        uint sampler_id;
        float time;
        float aspect;
        float resolution_x;
        float resolution_y;
    };

    struct VertexOutput {
        float4 position : SV_Position;
        float2 uv       : TEXCOORD0;
    };

    [shader("vertex")]
    VertexOutput vertexMain(VertexInput input)
    {
        VertexOutput output;
        output.uv       = float2(float((input.vertex_index << 1) & 2), float(input.vertex_index & 2));
        output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
        return output;
    }

    float3 sample_scene(PushConstants push, float2 coord)
    {
        return get_render_target(push.texture_id).Sample(get_sampler(push.sampler_id), coord).rgb;
    }

    float hash(float2 coord)
    {
        coord = frac(coord * float2(123.34, 456.21));
        coord += dot(coord, coord + 45.32);

        return frac(coord.x * coord.y);
    }

    [shader("fragment")]
    float4 fragmentMain(VertexOutput input, uniform PushConstants push):
        SV_Target0
    {
        float2 uv            = input.uv;
        float2 center_offset = uv - 0.5;
        float ca             = 0.0008 * length(center_offset);
        float3 color;
        color.r = sample_scene(push, uv + center_offset * ca).r;
        color.g = sample_scene(push, uv).g;
        color.b = sample_scene(push, uv - center_offset * ca).b;

        color = color / (color + 1.0);

        float scan          = 0.5 + 0.5 * sin(uv.y * push.resolution_y * 3.14159);
        float scan_strength = 0.25;
        color *= lerp(1.0 - scan_strength, 1.0, scan);

        float roll = sin(uv.y * 2.0 + push.time * 1.5) * 0.5 + 0.5;
        color *= 0.97 + 0.03 * roll;

        float px     = floor(uv.x * push.resolution_x);
        int phosphor = int(px - 3.0 * floor(px / 3.0));
        float3 mask  = float3(1.0);
        if(phosphor == 0)
        {
            mask = float3(1.05, 0.92, 0.92);
        }
        else if(phosphor == 1)
        {
            mask = float3(0.92, 1.05, 0.92);
        }
        else
        {
            mask = float3(0.92, 0.92, 1.05);
        }
        color *= mask;

        float2 vig = uv - 0.5;
        float v    = 1.0 - dot(vig, vig) * 1.4;
        color *= clamp(v, 0.0, 1.0);

        float grain = hash(uv * float2(push.resolution_x, push.resolution_y) + push.time * 100.0);
        color += (grain - 0.5) * 0.08;

        color = pow(color, float3(0.95));

        return float4(color, 1.0);
    }
}
