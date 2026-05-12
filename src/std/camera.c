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
#include "internal.h"
#include "pigment.h"
#include "std_internal.h"

#include <string.h>

typedef struct PCameraData {
    mat4 view;
    mat4 projection;
} PCameraData;

struct PCamera {
    PCameraData data;
    PBuffer* buffer;
    uint32_t frame_count;
    uint32_t last_uploaded_frame;
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

    camera->data                = (PCameraData) {.view = GLM_MAT4_IDENTITY_INIT, .projection = GLM_MAT4_IDENTITY_INIT};
    camera->frame_count         = pigment->config.max_frames_in_flight;
    camera->last_uploaded_frame = UINT32_MAX;
    camera->dirty               = P_TRUE;

    PBufferDesc desc = {
        .size   = (uint64_t) camera->frame_count * sizeof(PCameraData),
        .usage  = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS,
        .memory = P_MEMORY_HOST_UPLOAD,
    };
    camera->buffer = pigment_create_buffer(pigment, &desc);
    if(camera->buffer == NULL)
    {
        PLOG_ERROR(pigment, "Failed to create camera buffer (size=%llu)", (unsigned long long) desc.size);
        P_FREE(pigment, camera);
        return NULL;
    }

    return camera;
}

void pigment_std_destroy_camera(Pigment* pigment, PCamera* camera)
{
    if(pigment == NULL || camera == NULL)
    {
        return;
    }

    pigment_destroy_buffer(pigment, camera->buffer);
    P_FREE(pigment, camera);
}

void pigment_std_camera_set_view(PCamera* camera, mat4 view)
{
    if(camera == NULL)
    {
        return;
    }
    memcpy(camera->data.view, view, sizeof(mat4));
    camera->dirty = P_TRUE;
}

void pigment_std_camera_set_projection(PCamera* camera, mat4 projection)
{
    if(camera == NULL)
    {
        return;
    }
    memcpy(camera->data.projection, projection, sizeof(mat4));
    camera->dirty = P_TRUE;
}

void pigment_std_camera_upload(Pigment* pigment, PCamera* camera, uint32_t current_frame)
{
    if(pigment == NULL || camera == NULL || current_frame >= camera->frame_count)
    {
        return;
    }
    if(camera->last_uploaded_frame == current_frame && !camera->dirty)
    {
        return;
    }
    PCameraData* slot = (PCameraData*) pigment_buffer_mapped(camera->buffer) + current_frame;
    *slot             = camera->data;
    pigment_buffer_flush(pigment, camera->buffer, (uint64_t) current_frame * sizeof(PCameraData), sizeof(PCameraData));

    camera->last_uploaded_frame = current_frame;
    camera->dirty               = P_FALSE;
}

uint64_t pigment_std_camera_frame_address(PCamera* camera, uint32_t current_frame)
{
    if(camera == NULL)
    {
        return 0;
    }
    uint64_t base = pigment_buffer_address(camera->buffer);
    return base + (uint64_t) current_frame * sizeof(PCameraData);
}
