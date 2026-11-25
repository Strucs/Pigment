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

module default_shader;

import buffer;
import bindless;
import vertex_input;
import types;
import material;
import lights;

namespace pigment.std.default_shader {
    using pigment.std.bindless;

    struct PushConstants {
        Buffer<Vertex> vertex_buffer;
        Buffer<Instance> instance_buffer;
        Buffer<Camera> camera_buffer;
        Buffer<Material> material_buffer;
        Lights lights;
    };

    struct VertexOutput {
        float4 position                  : SV_Position;
        float4 color                     : COLOR0;
        float2 uv                        : TEXCOORD0;
        nointerpolation uint material_id : TEXCOORD1;
        float3 world_pos                 : TEXCOORD2;
        float3 world_normal              : TEXCOORD3;
    };

    [shader("vertex")]
    VertexOutput vertexMain(VertexInput input, uniform PushConstants push)
    {
        Vertex v      = push.vertex_buffer[input.vertex_index];
        Instance inst = push.instance_buffer[input.instance_index];

        float4 world_pos = mul(inst.transform, float4(v.pos, 1.0));

        Camera camera = push.camera_buffer[0];
        VertexOutput output;
        output.position     = mul(camera.proj, mul(camera.view, world_pos));
        output.color        = v.color;
        output.uv           = float2(v.uv_x, v.uv_y);
        output.material_id  = inst.material_id;
        output.world_pos    = world_pos.xyz;
        output.world_normal = normalize(mul((float3x3) inst.transform, v.normal));

        return output;
    }

    [shader("fragment")]
    float4 fragmentMain(VertexOutput input, uniform PushConstants push):
        SV_Target0
    {
        Material mat = push.material_buffer[input.material_id];
        float4 albedo;
        if(mat.albedo_image == -1)
        {
            float2 c      = floor(input.uv * 8.0);
            float checker = c.x + c.y - 2.0 * floor((c.x + c.y) / 2.0);
            albedo        = lerp(float4(0.0, 0.0, 0.0, 1.0), float4(1.0, 0.0, 1.0, 1.0), checker);
        }
        else
        {
            uint s = uint(max(mat.albedo_sampler, 0));
            albedo = get_image(uint(mat.albedo_image)).Sample(get_sampler(s), input.uv)
                     * mat.base_color_factor * input.color;
        }

        if(albedo.w < 0.8)
        {
            discard;
        }

        float3 normal = normalize(input.world_normal);
        float3 lit    = evaluate_lighting(push.lights, input.world_pos, normal, albedo.rgb);

        float3 emissive = mat.emissive_factor.rgb;
        if(mat.emissive_image != -1)
        {
            uint s = uint(max(mat.emissive_sampler, 0));
            emissive *= get_image(uint(mat.emissive_image)).Sample(get_sampler(s), input.uv).rgb;
        }

        return float4(lit + emissive, albedo.w);
    }
}
