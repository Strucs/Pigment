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

#include "std_internal.h"

#include "pigment/pigment.h"

PBuffer* acquire_buffer_version(Pigment* pigment, PStdBufferVersions* buffers, uint64_t size, const char* name)
{
    PStdBufferVersion* version = buffers->versions;
    while(version != NULL)
    {
        if(atomic_load_explicit(&version->references, memory_order_acquire) == 1
           && pigment_buffer_size(version->buffer) >= size && pigment_buffer_idle(pigment, version->buffer))
        {
            break;
        }
        version = version->next;
    }

    if(version == NULL)
    {
        version = P_NEW_FOR_OBJECT(pigment, version);
        if(version == NULL)
        {
            return NULL;
        }

        PBufferDesc desc = {
            .size   = size,
            .usage  = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS,
            .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT | P_MEMORY_DEVICE_LOCAL_BIT},
            .name   = name,
        };

        version->buffer = pigment_create_buffer(pigment, &desc);
        if(version->buffer == NULL)
        {
            P_FREE(pigment, version);
            return NULL;
        }

        atomic_init(&version->references, 1);
        version->next     = buffers->versions;
        buffers->versions = version;
    }

    buffers->current = version;
    return version->buffer;
}

PStdBufferVersion* retain_buffer_version(Pigment* pigment, PStdBufferVersions* buffers)
{
    // Keep the current version and release older buffers once their uses finish.
    PStdBufferVersion** link = &buffers->versions;
    while(*link != NULL)
    {
        PStdBufferVersion* version = *link;
        if(version != buffers->current
           && atomic_load_explicit(&version->references, memory_order_acquire) == 1
           && pigment_buffer_idle(pigment, version->buffer))
        {
            *link = version->next;
            pigment_std_release_buffer_version(pigment, version);
        }
        else
        {
            link = &version->next;
        }
    }

    atomic_fetch_add_explicit(&buffers->current->references, 1, memory_order_relaxed);
    return buffers->current;
}

void destroy_buffer_versions(Pigment* pigment, PStdBufferVersions* buffers)
{
    PStdBufferVersion* version = buffers->versions;
    while(version != NULL)
    {
        PStdBufferVersion* next = version->next;
        pigment_std_release_buffer_version(pigment, version);
        version = next;
    }
    *buffers = (PStdBufferVersions) {0};
}

uint64_t pigment_std_buffer_version_address(Pigment* pigment, const PStdBufferVersion* version, PCommandBuffer* cmd)
{
    if(pigment == NULL || version == NULL || cmd == NULL)
    {
        return 0;
    }

    pigment_cmd_use_buffer(pigment, cmd, version->buffer);
    return pigment_buffer_address(version->buffer);
}

uint32_t pigment_std_buffer_version_count(const PStdBufferVersion* version)
{
    return version != NULL ? version->count : 0;
}

void pigment_std_release_buffer_version(Pigment* pigment, PStdBufferVersion* version)
{
    if(pigment != NULL && version != NULL
       && atomic_fetch_sub_explicit(&version->references, 1, memory_order_acq_rel) == 1)
    {
        pigment_destroy_buffer(pigment, version->buffer);
        P_FREE(pigment, version);
    }
}
