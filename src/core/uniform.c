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
#include "internal.h"

void update_uniform_buffer(PUniformBuffers* buffers, PSwapchain* swapchain, PCamera* camera)
{
    UniformBufferObject ubo;
    glm_mat4_copy(camera->view, ubo.view);
    glm_mat4_copy(camera->projection, ubo.projection);

    memcpy(buffers->uniform_buffers_mapped[swapchain->current_frame], &ubo, sizeof(ubo));
}
