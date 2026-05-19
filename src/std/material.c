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

#include "internal_alloc.h"
#include "log_internal.h"

#include <string.h>

struct PMaterials {
    PBuffer* buffer;

    uint32_t capacity;
    uint32_t count;

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

    PBufferDesc desc = {
        .size   = (uint64_t) initial_size * sizeof(PMaterialDesc),
        .usage  = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS,
        .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT | P_MEMORY_DEVICE_LOCAL_BIT},
    };
    materials->buffer = pigment_create_buffer(pigment, &desc);
    if(materials->buffer == NULL)
    {
        PLOG_ERROR(pigment, "Failed to create material buffer (size=%llu)", (unsigned long long) desc.size);
        goto ERROR;
    }

    materials->capacity   = initial_size;
    materials->count      = 0;
    materials->free_count = 0;

    return materials;

ERROR:
    pigment_destroy_buffer(pigment, materials->buffer);
    P_FREE(pigment, materials->free_slots);
    P_FREE(pigment, materials);
    return NULL;
}

void pigment_std_destroy_materials(Pigment* pigment, PMaterials* materials)
{
    if(pigment == NULL || materials == NULL)
    {
        return;
    }

    pigment_destroy_buffer(pigment, materials->buffer);
    P_FREE(pigment, materials->free_slots);
    P_FREE(pigment, materials);
}

static PResult materials_grow(Pigment* pigment, PMaterials* materials)
{
    uint32_t new_capacity = materials->capacity * 2;

    PBufferDesc desc = {
        .size   = (uint64_t) new_capacity * sizeof(PMaterialDesc),
        .usage  = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS,
        .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT | P_MEMORY_DEVICE_LOCAL_BIT},
    };
    PBuffer* new_buffer = pigment_create_buffer(pigment, &desc);
    if(new_buffer == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    uint64_t used = (uint64_t) materials->count * sizeof(PMaterialDesc);
    memcpy(pigment_buffer_mapped(new_buffer), pigment_buffer_mapped(materials->buffer), (size_t) used);
    pigment_buffer_flush(pigment, new_buffer, 0, used);

    pigment_destroy_buffer(pigment, materials->buffer);

    materials->buffer   = new_buffer;
    materials->capacity = new_capacity;
    return PIGMENT_SUCCESS;
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
    else if(materials_grow(pigment, materials) == PIGMENT_SUCCESS)
    {
        id = materials->count++;
    }
    else
    {
        PLOG_ERROR(pigment, "PMaterials capacity (%u) reached and grow failed.", materials->capacity);
        return UINT32_MAX;
    }

    PMaterialDesc* slot = (PMaterialDesc*) pigment_buffer_mapped(materials->buffer) + id;
    *slot               = *desc;
    pigment_buffer_flush(pigment, materials->buffer, (uint64_t) id * sizeof(PMaterialDesc), sizeof(PMaterialDesc));

    return id;
}

void pigment_std_material_update(Pigment* pigment, PMaterials* materials, uint32_t id, const PMaterialDesc* desc)
{
    if(pigment == NULL || materials == NULL || desc == NULL || id >= materials->capacity)
    {
        return;
    }

    PMaterialDesc* slot = (PMaterialDesc*) pigment_buffer_mapped(materials->buffer) + id;
    *slot               = *desc;
    pigment_buffer_flush(pigment, materials->buffer, (uint64_t) id * sizeof(PMaterialDesc), sizeof(PMaterialDesc));
}

void pigment_std_material_destroy(Pigment* pigment, PMaterials* materials, uint32_t id)
{
    if(pigment == NULL || materials == NULL || id >= materials->capacity)
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

uint64_t pigment_std_material_address(PMaterials* materials)
{
    if(materials == NULL)
    {
        return 0;
    }
    return (uint64_t) pigment_buffer_address(materials->buffer);
}
