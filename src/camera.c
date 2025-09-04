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

#include "defines.h"
#include "structs.h"
#include <cglm/quat.h>

void get_view_matrix(PCamera* camera, UniformBufferObject* ubo);

PCamera* create_camera(void)
{
    PCamera* camera = malloc(sizeof(*camera));
    if(camera == NULL)
    {
        perror("create_camera");
        return NULL;
    }

    vec3 position = {0.f, 0.0f, 0.5f};
    vec3 front    = {-1.0f, 0.0f, 0.0f};
    vec3 up       = {0.0f, 0.0f, 1.0f};

    memcpy(camera->position, position, sizeof(position));
    memcpy(camera->front, front, sizeof(front));
    memcpy(camera->up, up, sizeof(up));

    camera->speed = 10.0f;

    camera->roll  = 0.0f;
    camera->pitch = 0.0f;
    camera->yaw   = 0.0f;

    return camera;
}

void destroy_camera(PCamera* camera)
{
    free(camera);
}

void add_camera_to_window(PCamera* camera, PWindow* window)
{
    window->camera = camera;
}

void get_view_matrix(PCamera* camera, UniformBufferObject* ubo)
{
    vec3 target;
    glm_vec3_add(camera->position, camera->front, target);

    glm_lookat(camera->position, target, camera->up, ubo->view);
}
