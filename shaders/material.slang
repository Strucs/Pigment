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

module material;

namespace pigment.std {
    public struct Material {
        public float4 base_color_factor;
        public float4 emissive_factor;
        public float metallic_factor;
        public float roughness_factor;
        public float normal_scale;
        public float occlusion_scale;
        public int albedo_image;
        public int albedo_sampler;
        public int metallic_roughness_image;
        public int metallic_roughness_sampler;
        public int normal_image;
        public int normal_sampler;
        public int emissive_image;
        public int emissive_sampler;
        public int occlusion_image;
        public int occlusion_sampler;
    };
}
