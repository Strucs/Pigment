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
#include "buffers.h"
#include "log_internal.h"

#include <stdlib.h>
#include <string.h>

struct PMaterials {
    PBuffer* buffer;

    uint32_t capacity;
    uint32_t count;

    uint32_t* free_slots;
    uint32_t free_count;
};

PMaterials* pigment_std_create_materials(Pigment* pigment, uint32_t max_materials)
{
    if(pigment == NULL || max_materials == 0)
    {
        return NULL;
    }

    PMaterials* materials = calloc(1, sizeof(*materials));
    if(materials == NULL)
    {
        return NULL;
    }

    materials->free_slots = calloc(max_materials, sizeof(*materials->free_slots));
    if(materials->free_slots == NULL)
    {
        goto ERROR;
    }

    PBufferDesc desc = {
        .size   = (uint64_t) max_materials * sizeof(PMaterialDesc),
        .usage  = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS,
        .memory = P_MEMORY_HOST_VISIBLE,
    };
    materials->buffer = pigment_create_buffer(pigment, &desc);
    if(materials->buffer == NULL)
    {
        PLOG_ERROR(pigment, "Failed to create material buffer (size=%llu)", (unsigned long long) desc.size);
        goto ERROR;
    }

    materials->capacity   = max_materials;
    materials->count      = 0;
    materials->free_count = 0;

    return materials;

ERROR:
    pigment_destroy_buffer(pigment, materials->buffer);
    free(materials->free_slots);
    free(materials);
    return NULL;
}

void pigment_std_destroy_materials(Pigment* pigment, PMaterials* materials)
{
    if(pigment == NULL || materials == NULL)
    {
        return;
    }

    pigment_destroy_buffer(pigment, materials->buffer);
    free(materials->free_slots);
    free(materials);
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
    else
    {
        PLOG_ERROR(pigment, "PMaterials capacity (%u) reached, cannot create more materials.", materials->capacity);
        return UINT32_MAX;
    }

    PMaterialDesc* slot = (PMaterialDesc*) pigment_buffer_mapped(materials->buffer) + id;
    *slot               = *desc;

    return id;
}

void pigment_std_material_update(PMaterials* materials, uint32_t id, const PMaterialDesc* desc)
{
    if(materials == NULL || desc == NULL || id >= materials->capacity)
    {
        return;
    }

    PMaterialDesc* slot = (PMaterialDesc*) pigment_buffer_mapped(materials->buffer) + id;
    *slot               = *desc;
}

void pigment_std_material_destroy(PMaterials* materials, uint32_t id)
{
    if(materials == NULL || id >= materials->capacity || materials->free_count >= materials->capacity)
    {
        return;
    }

    materials->free_slots[materials->free_count++] = id;
}

uint64_t pigment_std_material_address(PMaterials* materials)
{
    if(materials == NULL)
    {
        return 0;
    }
    return (uint64_t) pigment_buffer_address(materials->buffer);
}
