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

module types;

namespace pigment.std {
    public struct Vertex {
        public float3 pos;
        public float uv_x;
        public float3 normal;
        public float uv_y;
        public float4 color;
    };

    public struct Instance {
        public float4x4 transform;
        public uint material_id;
    };

    public struct Camera {
        public float4x4 view;
        public float4x4 proj;
    };
}
