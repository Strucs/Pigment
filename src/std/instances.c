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

#include "instances.h"

#include "std_internal.h"

#include "pigment/pigment.h"

#include <string.h>

PInstances* pigment_std_create_instances(Pigment* pigment, uint32_t instance_size, uint32_t initial_capacity)
{
    if(pigment == NULL || instance_size == 0 || initial_capacity == 0)
    {
        return NULL;
    }

    uint64_t size   = (uint64_t) instance_size * initial_capacity;
    uint64_t blocks = size / sizeof(PInstanceBlock) + (size % sizeof(PInstanceBlock) != 0);
    if(blocks > UINT32_MAX)
    {
        return NULL;
    }

    PInstances* instances = P_NEW_FOR_OBJECT(pigment, instances);
    if(instances == NULL)
    {
        return NULL;
    }

    instances->data = P_NEW_ARRAY_FOR_OBJECT(pigment, instances->data, (uint32_t) blocks);
    if(instances->data == NULL)
    {
        P_FREE(pigment, instances);
        return NULL;
    }

    instances->block_capacity = (uint32_t) blocks;
    instances->instance_size  = instance_size;
    return instances;
}

void pigment_std_destroy_instances(Pigment* pigment, PInstances* instances)
{
    if(pigment == NULL || instances == NULL)
    {
        return;
    }

    destroy_buffer_versions(pigment, &instances->buffers);
    P_FREE(pigment, instances->data);
    P_FREE(pigment, instances);
}

void pigment_std_instances_reset(PInstances* instances)
{
    if(instances != NULL)
    {
        instances->count = 0;
    }
}

uint32_t pigment_std_instances_count(PInstances* instances)
{
    return instances != NULL ? instances->count : 0;
}

void* pigment_std_instances_alloc(Pigment* pigment, PInstances* instances, uint32_t instance_count)
{
    if(pigment == NULL || instances == NULL || instance_count == 0 || instance_count > UINT32_MAX - instances->count)
    {
        return NULL;
    }

    uint64_t offset = (uint64_t) instances->count * instances->instance_size;
    uint64_t size   = offset + (uint64_t) instance_count * instances->instance_size;
    uint64_t blocks = size / sizeof(*instances->data) + (size % sizeof(*instances->data) != 0);
    if(blocks > instances->block_capacity && blocks > UINT32_MAX / 2)
    {
        return NULL;
    }

    if(P_ARRAY_RESERVE_OBJECT(pigment, instances->data, 0, instances->block_capacity, (uint32_t) blocks, 1) != PIGMENT_SUCCESS)
    {
        return NULL;
    }

    instances->count += instance_count;
    return (unsigned char*) instances->data + offset;
}

uint64_t pigment_std_instances_address(Pigment* pigment, PInstances* instances, PCommandBuffer* cmd)
{
    if(pigment == NULL || instances == NULL || cmd == NULL || instances->count == 0)
    {
        return 0;
    }

    uint64_t size   = (uint64_t) instances->block_capacity * sizeof(*instances->data);
    PBuffer* buffer = acquire_buffer_version(pigment, &instances->buffers, size, "pigment_std_instances_version");
    if(buffer == NULL)
    {
        return 0;
    }

    // The caller writes through alloc's pointer, so each publication copies the data.
    uint64_t used = (uint64_t) instances->count * instances->instance_size;

    memcpy(pigment_buffer_mapped(buffer), instances->data, (size_t) used);
    pigment_buffer_flush(pigment, buffer, 0, used);

    return buffer_version_address(pigment, &instances->buffers, cmd);
}
