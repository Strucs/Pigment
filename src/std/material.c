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

#include "material.h"

#include "std_internal.h"

#include "pigment/pigment.h"

#include <string.h>

struct PMaterials {
    PMaterialDesc* data;
    uint32_t capacity;
    uint32_t count;
    PBool dirty;

    PStdBufferVersions buffers;

    uint32_t* free_slots;
    uint32_t free_count;
    uint32_t free_capacity;
};

PMaterials* pigment_std_create_materials(Pigment* pigment, uint32_t initial_size)
{
    if(pigment == NULL || initial_size == 0)
    {
        return NULL;
    }

    PMaterials* materials = P_NEW_FOR_OBJECT(pigment, materials);
    if(materials == NULL)
    {
        return NULL;
    }

    materials->data = P_NEW_ARRAY_FOR_OBJECT(pigment, materials->data, initial_size);
    if(materials->data == NULL)
    {
        P_FREE(pigment, materials);
        return NULL;
    }

    materials->capacity = initial_size;
    materials->dirty    = P_TRUE;
    return materials;
}

void pigment_std_destroy_materials(Pigment* pigment, PMaterials* materials)
{
    if(pigment == NULL || materials == NULL)
    {
        return;
    }

    destroy_buffer_versions(pigment, &materials->buffers);

    P_FREE(pigment, materials->data);
    P_FREE(pigment, materials->free_slots);
    P_FREE(pigment, materials);
}

uint32_t pigment_std_material_create(Pigment* pigment, PMaterials* materials, const PMaterialDesc* desc)
{
    if(pigment == NULL || materials == NULL || desc == NULL)
    {
        return UINT32_MAX;
    }

    uint32_t id;
    if(materials->free_count > 0)
    {
        id = materials->free_slots[--materials->free_count];
    }
    else if(materials->count < materials->capacity)
    {
        id = materials->count++;
    }
    else if(materials->capacity <= UINT32_MAX / 2 && P_ARRAY_RESERVE_OBJECT(pigment, materials->data, materials->count, materials->capacity, 1, materials->capacity) == PIGMENT_SUCCESS)
    {
        id = materials->count++;
    }
    else
    {
        PLOG_ERROR(pigment, "PMaterials capacity (%u) reached and grow failed.", materials->capacity);
        return UINT32_MAX;
    }

    materials->data[id] = *desc;
    materials->dirty    = P_TRUE;

    return id;
}

void pigment_std_material_update(Pigment* pigment, PMaterials* materials, uint32_t id, const PMaterialDesc* desc)
{
    if(pigment == NULL || materials == NULL || desc == NULL || id >= materials->count)
    {
        return;
    }

    materials->data[id] = *desc;
    materials->dirty    = P_TRUE;
}

void pigment_std_material_destroy(Pigment* pigment, PMaterials* materials, uint32_t id)
{
    if(pigment == NULL || materials == NULL || id >= materials->count)
    {
        return;
    }

    if(P_ARRAY_RESERVE_OBJECT(pigment, materials->free_slots, materials->free_count, materials->free_capacity, 1, PIGMENT_STD_FREE_LIST_INITIAL_CAPACITY) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to record freed material slot %u.", id);
        return;
    }
    materials->free_slots[materials->free_count++] = id;
}

uint64_t pigment_std_materials_address(Pigment* pigment, PMaterials* materials, PCommandBuffer* cmd)
{
    if(pigment == NULL || materials == NULL || cmd == NULL)
    {
        return 0;
    }

    if(materials->dirty)
    {
        uint64_t size   = (uint64_t) materials->capacity * sizeof(*materials->data);
        PBuffer* buffer = acquire_buffer_version(pigment, &materials->buffers, size, "pigment_std_materials_version");
        if(buffer == NULL)
        {
            return 0;
        }

        uint64_t used = (uint64_t) materials->count * sizeof(*materials->data);

        memcpy(pigment_buffer_mapped(buffer), materials->data, (size_t) used);
        pigment_buffer_flush(pigment, buffer, 0, used);

        materials->dirty = P_FALSE;
    }

    return buffer_version_address(pigment, &materials->buffers, cmd);
}
