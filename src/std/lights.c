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

PStdBufferVersion* pigment_std_lights_prepare(Pigment* pigment, PLights* lights)
{
    if(pigment == NULL || lights == NULL)
    {
        return NULL;
    }

    if(lights->dirty)
    {
        uint64_t size   = sizeof(lights->header) + (uint64_t) lights->capacity * sizeof(*lights->data);
        PBuffer* buffer = acquire_buffer_version(pigment, &lights->buffers, size, "pigment_std_lights_version");
        if(buffer == NULL)
        {
            return NULL;
        }

        uint64_t used = (uint64_t) lights->header.count * sizeof(*lights->data);

        unsigned char* mapped = pigment_buffer_mapped(buffer);
        memcpy(mapped, &lights->header, sizeof(lights->header));
        memcpy(mapped + sizeof(lights->header), lights->data, (size_t) used);
        pigment_buffer_flush(pigment, buffer, 0, sizeof(lights->header) + used);

        lights->buffers.current->count = lights->header.count;
        lights->dirty                  = P_FALSE;
    }

    return retain_buffer_version(pigment, &lights->buffers);
}
