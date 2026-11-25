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

module bindless;

namespace pigment.std.bindless {
    // Runtime-sized arrays preserve the capacities selected by PStdBindlessDesc.

    public SamplerState samplers[];
    public TextureCube<float4> cubemaps[];
    public Texture2D<float4> render_targets[];
    public Texture2D<float4> images[];    // Keep last: variable descriptor count.

    public SamplerState get_sampler(uint index)
    {
        return samplers[NonUniformResourceIndex(index)];
    }

    public TextureCube<float4> get_cubemap(uint index)
    {
        return cubemaps[NonUniformResourceIndex(index)];
    }

    public Texture2D<float4> get_render_target(uint index)
    {
        return render_targets[NonUniformResourceIndex(index)];
    }

    public Texture2D<float4> get_image(uint index)
    {
        return images[NonUniformResourceIndex(index)];
    }
}
