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

module skybox;

import buffer;
import bindless;
import vertex_input;
import types;

namespace pigment.std.skybox {
    using pigment.std.bindless;

    struct PushConstants {
        Buffer<Camera> camera_buffer;
        uint cubemap_id;
        uint sampler_id;
    };

    struct VertexOutput {
        float4 position : SV_Position;
        float3 view_dir : TEXCOORD0;
    };

    float4 cofactor_row(float4 a, float4 b, float4 c)
    {
        return float4(
            determinant(float3x3(a.yzw, b.yzw, c.yzw)),
            -determinant(float3x3(a.xzw, b.xzw, c.xzw)),
            determinant(float3x3(a.xyw, b.xyw, c.xyw)),
            -determinant(float3x3(a.xyz, b.xyz, c.xyz))
        );
    }

    bool try_inverse_matrix(float4x4 m, out float4x4 inverse)
    {
        float4 c0 = cofactor_row(m[1], m[2], m[3]);
        float det = dot(m[0], c0);
        if(det == 0.0)
        {
            inverse = float4x4(0.0);
            return false;
        }

        float4 c1 = -cofactor_row(m[0], m[2], m[3]);
        float4 c2 = cofactor_row(m[0], m[1], m[3]);
        float4 c3 = -cofactor_row(m[0], m[1], m[2]);
        inverse   = transpose(float4x4(c0, c1, c2, c3)) / det;
        return true;
    }

    [shader("vertex")]
    VertexOutput vertexMain(VertexInput input, uniform PushConstants push)
    {
        float2 uv  = float2(float((input.vertex_index << 1) & 2), float(input.vertex_index & 2));
        float2 ndc = uv * 2.0 - 1.0;

        Camera camera = push.camera_buffer[0];

        VertexOutput output;

        float4x4 inv_proj;
        if(!try_inverse_matrix(camera.proj, inv_proj))
        {
            // Collapse the triangle when the projection has no inverse.
            output.position = float4(0.0, 0.0, 0.0, 1.0);
            output.view_dir = float3(0.0);

            return output;
        }

        float4 view_pos = mul(inv_proj, float4(ndc, 1.0, 1.0));
        view_pos /= view_pos.w;

        output.position = float4(ndc, 0.0, 1.0);
        output.view_dir = mul(transpose((float3x3) camera.view), view_pos.xyz);

        return output;
    }

    [shader("fragment")]
    float4 fragmentMain(VertexOutput input, uniform PushConstants push):
        SV_Target0
    {
        return get_cubemap(push.cubemap_id).Sample(get_sampler(push.sampler_id), normalize(input.view_dir));
    }
}
