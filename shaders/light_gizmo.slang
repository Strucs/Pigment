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

module light_gizmo;

import buffer;
import vertex_input;
import types;
import lights;

namespace pigment.std.light_gizmo {
    struct PushConstants {
        Buffer<Vertex> vertex_buffer;
        Buffer<Camera> camera_buffer;
        Buffer<Light> light_buffer;
        float scale;
    };

    struct VertexOutput {
        float4 position                  : SV_Position;
        nointerpolation uint light_index : TEXCOORD0;
    };

    [shader("vertex")]
    VertexOutput vertexMain(VertexInput input, uniform PushConstants push)
    {
        uint light_index = input.instance_index;
        Vertex v         = push.vertex_buffer[input.vertex_index];
        Light L          = push.light_buffer[light_index];
        float3 origin    = L.type == LIGHT_TYPE_DIRECTIONAL ? -L.direction * 100.0 : L.position;
        float3 world     = origin + v.pos * push.scale;

        Camera camera = push.camera_buffer[0];
        VertexOutput output;
        output.position    = mul(camera.proj, mul(camera.view, float4(world, 1.0)));
        output.light_index = light_index;

        return output;
    }

    [shader("fragment")]
    float4 fragmentMain(VertexOutput input, uniform PushConstants push):
        SV_Target0
    {
        Light L = push.light_buffer[input.light_index];
        if(L.type == LIGHT_TYPE_INVALID)
        {
            discard;
        }
        return float4(L.color * L.intensity, 1.0);
    }
}
