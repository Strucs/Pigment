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

struct PStdBufferVersion {
    PBuffer* buffer;
    PStdBufferVersion* next;
};

PBuffer* acquire_buffer_version(Pigment* pigment, PStdBufferVersions* buffers, uint64_t size, const char* name)
{
    PStdBufferVersion* version = buffers->versions;
    while(version != NULL)
    {
        if(pigment_buffer_size(version->buffer) >= size && pigment_buffer_idle(pigment, version->buffer))
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

        version->next     = buffers->versions;
        buffers->versions = version;
    }

    buffers->current = version;
    return version->buffer;
}

uint64_t buffer_version_address(Pigment* pigment, PStdBufferVersions* buffers, PCommandBuffer* cmd)
{
    // Keep the current version and release older buffers once their uses finish.
    PStdBufferVersion** link = &buffers->versions;
    while(*link != NULL)
    {
        PStdBufferVersion* version = *link;
        if(version != buffers->current && pigment_buffer_idle(pigment, version->buffer))
        {
            *link = version->next;
            pigment_destroy_buffer(pigment, version->buffer);
            P_FREE(pigment, version);
        }
        else
        {
            link = &version->next;
        }
    }

    pigment_cmd_use_buffer(pigment, cmd, buffers->current->buffer);
    return pigment_buffer_address(buffers->current->buffer);
}

void destroy_buffer_versions(Pigment* pigment, PStdBufferVersions* buffers)
{
    PStdBufferVersion* version = buffers->versions;
    while(version != NULL)
    {
        PStdBufferVersion* next = version->next;
        pigment_destroy_buffer(pigment, version->buffer);
        P_FREE(pigment, version);
        version = next;
    }
    *buffers = (PStdBufferVersions) {0};
}
