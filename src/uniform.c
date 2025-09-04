/**
 * Copyright 2025 Angel-Leduc TA
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

void update_uniform_buffer(PBuffers* buffers, PSwapchain* swapchain, PCamera* camera)
{
    mat4 model = GLM_MAT4_IDENTITY_INIT;
    vec3 rotation_axis = {0.0f, 1.0f, 0.0f};
    vec3 pivot = {0.0f, 0.0f, 0.0f};

    glm_rotate_at(model, pivot, glm_rad(90.0f), rotation_axis);

    vec3 rotation_axis2 = {0.0f, 0.0f, 1.0f};
    glm_rotate_at(model, pivot, glm_rad(90.0f), rotation_axis2);

    mat4 projection;
    glm_perspective(glm_rad(45.0f), (float) swapchain->extent.width / (float) swapchain->extent.height, 0.1f, 1000.0f, projection);

    UniformBufferObject ubo;
    glm_mat4_copy(model, ubo.model);
    get_view_matrix(camera, &ubo);
    glm_mat4_copy(projection, ubo.projection);

    ubo.projection[1][1] *= -1;

    memcpy(buffers->uniform_buffers_mapped[swapchain->current_frame], &ubo, sizeof(ubo));
}
