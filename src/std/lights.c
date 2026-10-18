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

#include "lights.h"

#include "camera.h"
#include "std_internal.h"

#include "pigment/pigment.h"

#include <string.h>

typedef struct PIGMENT_ALIGN(16) PLightsHeader {
    PVec3 ambient_color;
    uint32_t count;
} PLightsHeader;

struct PLights {
    PLightsHeader header;
    PLightDesc* data;
    uint32_t capacity;
    PBool dirty;
    PStdBufferVersions buffers;

    uint32_t* free_slots;
    uint32_t free_count;
    uint32_t free_capacity;
};

PLights* pigment_std_create_lights(Pigment* pigment, uint32_t initial_size)
{
    if(pigment == NULL || initial_size == 0)
    {
        return NULL;
    }

    PLights* lights = P_NEW_FOR_OBJECT(pigment, lights);
    if(lights == NULL)
    {
        return NULL;
    }

    lights->data = P_NEW_ARRAY_FOR_OBJECT(pigment, lights->data, initial_size);
    if(lights->data == NULL)
    {
        P_FREE(pigment, lights);
        return NULL;
    }

    lights->capacity = initial_size;
    lights->dirty    = P_TRUE;
    lights->header   = (PLightsHeader) {
        .ambient_color = {0.05f, 0.05f, 0.05f}
    };

    return lights;
}

void pigment_std_set_ambient(Pigment* pigment, PLights* lights, PVec3 color)
{
    if(pigment == NULL || lights == NULL || color == NULL)
    {
        return;
    }

    memcpy(lights->header.ambient_color, color, sizeof(lights->header.ambient_color));
    lights->dirty = P_TRUE;
}

void pigment_std_destroy_lights(Pigment* pigment, PLights* lights)
{
    if(pigment == NULL || lights == NULL)
    {
        return;
    }

    destroy_buffer_versions(pigment, &lights->buffers);
    P_FREE(pigment, lights->data);
    P_FREE(pigment, lights->free_slots);
    P_FREE(pigment, lights);
}

uint32_t pigment_std_light_create(Pigment* pigment, PLights* lights, const PLightDesc* desc)
{
    if(pigment == NULL || lights == NULL || desc == NULL)
    {
        return UINT32_MAX;
    }

    uint32_t id;
    if(lights->free_count > 0)
    {
        id = lights->free_slots[--lights->free_count];
    }
    else if(lights->header.count < lights->capacity)
    {
        id = lights->header.count++;
    }
    else if(lights->capacity <= UINT32_MAX / 2 && P_ARRAY_RESERVE_OBJECT(pigment, lights->data, lights->header.count, lights->capacity, 1, lights->capacity) == PIGMENT_SUCCESS)
    {
        id = lights->header.count++;
    }
    else
    {
        PLOG_ERROR(pigment, "PLights capacity (%u) reached and grow failed.", lights->capacity);
        return UINT32_MAX;
    }

    lights->data[id] = *desc;
    lights->dirty    = P_TRUE;
    return id;
}

void pigment_std_light_update(Pigment* pigment, PLights* lights, uint32_t id, const PLightDesc* desc)
{
    if(pigment == NULL || lights == NULL || desc == NULL || id >= lights->header.count)
    {
        return;
    }

    lights->data[id] = *desc;
    lights->dirty    = P_TRUE;
}

void pigment_std_light_destroy(Pigment* pigment, PLights* lights, uint32_t id)
{
    if(pigment == NULL || lights == NULL || id >= lights->header.count)
    {
        return;
    }

    if(P_ARRAY_RESERVE_OBJECT(pigment, lights->free_slots, lights->free_count, lights->free_capacity, 1, PIGMENT_STD_FREE_LIST_INITIAL_CAPACITY) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to record freed light slot %u.", id);
        return;
    }

    lights->data[id].type                    = P_LIGHT_TYPE_INVALID;
    lights->free_slots[lights->free_count++] = id;
    lights->dirty                            = P_TRUE;
}

uint64_t pigment_std_lights_address(Pigment* pigment, PLights* lights, PCommandBuffer* cmd)
{
    if(pigment == NULL || lights == NULL || cmd == NULL)
    {
        return 0;
    }

    if(lights->dirty)
    {
        uint64_t size   = sizeof(lights->header) + (uint64_t) lights->capacity * sizeof(*lights->data);
        PBuffer* buffer = acquire_buffer_version(pigment, &lights->buffers, size, "pigment_std_lights_version");
        if(buffer == NULL)
        {
            return 0;
        }

        uint64_t used = (uint64_t) lights->header.count * sizeof(*lights->data);

        unsigned char* mapped = pigment_buffer_mapped(buffer);
        memcpy(mapped, &lights->header, sizeof(lights->header));
        memcpy(mapped + sizeof(lights->header), lights->data, (size_t) used);
        pigment_buffer_flush(pigment, buffer, 0, sizeof(lights->header) + used);

        lights->dirty = P_FALSE;
    }

    return buffer_version_address(pigment, &lights->buffers, cmd);
}

void pigment_std_draw_light_gizmos(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline, PCamera* camera, PLights* lights, PMeshBuffers* sphere_mesh, uint32_t sphere_index_count, float scale)
{
    if(pigment == NULL || cmd == NULL || pipeline == NULL || lights == NULL || sphere_mesh == NULL || lights->header.count == 0)
    {
        return;
    }

    uint64_t light_address  = pigment_std_lights_address(pigment, lights, cmd);
    uint64_t camera_address = pigment_std_camera_address(pigment, camera, cmd);
    if(light_address == 0 || (camera != NULL && camera_address == 0))
    {
        return;
    }

    pigment_std_mesh_use(pigment, cmd, sphere_mesh);

    PStdGizmoPushConstants push = {
        .vertex_buffer = pigment_buffer_address(sphere_mesh->vertex_buffer),
        .camera_buffer = camera_address,
        .light_buffer  = light_address,
        .scale         = scale,
    };

    pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);
    pigment_cmd_draw_indexed(pigment, cmd, sphere_mesh->index_buffer, sphere_mesh->index_type, 0, 0, sphere_index_count, 0, lights->header.count, 0);
}
