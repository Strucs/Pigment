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

module ndc;

import buffer;
import bindless;
import vertex_input;
import types;

namespace pigment.std.ndc {
    using pigment.std.bindless;

    struct CanvasInstance {
        float4x4 transform;
        float4 color;
        float4 uv_rect;
        int image_idx;
        int sampler_idx;
    };

    struct PushConstants {
        Buffer<CanvasInstance> instance_buffer;
    };

    struct VertexOutput {
        float4 position                 : SV_Position;
        float4 color                    : COLOR0;
        float2 uv                       : TEXCOORD0;
        nointerpolation int image_idx   : TEXCOORD1;
        nointerpolation int sampler_idx : TEXCOORD2;
    };

    static const float2 QUAD_CORNERS[6] = {
        float2(0.0, 0.0),
        float2(0.0, 1.0),
        float2(1.0, 0.0),

        float2(1.0, 0.0),
        float2(0.0, 1.0),
        float2(1.0, 1.0)
    };

    [shader("vertex")]
    VertexOutput vertexMain(VertexInput input, uniform PushConstants push)
    {
        CanvasInstance inst = push.instance_buffer[input.instance_index];
        float2 corner       = QUAD_CORNERS[input.vertex_index];

        VertexOutput output;
        output.position    = mul(inst.transform, float4(corner, 0.0, 1.0));
        output.color       = inst.color;
        output.uv          = lerp(inst.uv_rect.xy, inst.uv_rect.zw, corner);
        output.image_idx   = inst.image_idx;
        output.sampler_idx = inst.sampler_idx;

        return output;
    }

    [shader("fragment")]
    float4 fragmentMain(VertexOutput input):
        SV_Target0
    {
        float4 tex = float4(1.0);
        if(input.image_idx != -1)
        {
            uint s = uint(max(input.sampler_idx, 0));
            tex    = get_image(uint(input.image_idx)).Sample(get_sampler(s), input.uv);
        }

        return tex * input.color;
    }
}
