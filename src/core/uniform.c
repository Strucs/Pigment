/**
 * Copyright 2025-2026 Angel-Leduc TA
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

#include "uniform.h"
#include "structs.h"

extern void get_view_matrix(PCamera* camera, UniformBufferObject* ubo);

void update_uniform_buffer(PUniformBuffers* buffers, PSwapchain* swapchain, PCamera* camera)
{
    float fov    = glm_rad(45.0f);
    float aspect = (float) swapchain->extent.width / (float) swapchain->extent.height;
    float near   = 0.1f;
    float f      = 1.0f / tanf(fov / 2.0f);

    // Infinite reverse Z projection (near=1, far=0, no far plane clipping)
    mat4 projection = {
        { f / aspect, 0.0f,  0.0f,  0.0f },
        { 0.0f,       f,     0.0f,  0.0f },
        { 0.0f,       0.0f,  0.0f, -1.0f },
        { 0.0f,       0.0f,  near,  0.0f }
    };

    UniformBufferObject ubo;
    get_view_matrix(camera, &ubo);
    glm_mat4_copy(projection, ubo.projection);

    ubo.projection[1][1] *= -1;

    memcpy(buffers->uniform_buffers_mapped[swapchain->current_frame], &ubo, sizeof(ubo));
}
