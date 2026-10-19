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

#include "camera.h"

#include "std_internal.h"

#include "pigment/pigment.h"

#include <string.h>

typedef struct PCameraData {
    PMat4 view;
    PMat4 projection;
} PCameraData;

struct PCamera {
    PCameraData data;
    PStdBufferVersions buffers;
    PBool dirty;
};

PCamera* pigment_std_create_camera(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return NULL;
    }

    PCamera* camera = P_NEW_FOR_OBJECT(pigment, camera);
    if(camera == NULL)
    {
        return NULL;
    }

    camera->data  = (PCameraData) {.view = P_MAT4_IDENTITY, .projection = P_MAT4_IDENTITY};
    camera->dirty = P_TRUE;

    return camera;
}

void pigment_std_destroy_camera(Pigment* pigment, PCamera* camera)
{
    if(pigment == NULL || camera == NULL)
    {
        return;
    }

    destroy_buffer_versions(pigment, &camera->buffers);
    P_FREE(pigment, camera);
}

void pigment_std_camera_set_view(PCamera* camera, PMat4 view)
{
    if(camera == NULL)
    {
        return;
    }
    memcpy(camera->data.view, view, sizeof(PMat4));
    camera->dirty = P_TRUE;
}

void pigment_std_camera_set_projection(PCamera* camera, PMat4 projection)
{
    if(camera == NULL)
    {
        return;
    }
    memcpy(camera->data.projection, projection, sizeof(PMat4));
    camera->dirty = P_TRUE;
}

PStdBufferVersion* pigment_std_camera_prepare(Pigment* pigment, PCamera* camera)
{
    if(pigment == NULL || camera == NULL)
    {
        return NULL;
    }

    if(camera->dirty)
    {
        PBuffer* buffer = acquire_buffer_version(pigment, &camera->buffers, sizeof(camera->data), "pigment_std_camera_version");
        if(buffer == NULL)
        {
            return NULL;
        }

        memcpy(pigment_buffer_mapped(buffer), &camera->data, sizeof(camera->data));
        pigment_buffer_flush(pigment, buffer, 0, sizeof(camera->data));

        camera->buffers.current->count = 1;
        camera->dirty                  = P_FALSE;
    }

    return retain_buffer_version(pigment, &camera->buffers);
}
