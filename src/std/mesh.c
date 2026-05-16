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

#include "mesh.h"

#include "transfert.h"

#include "internal.h"

PMeshBuffers* pigment_std_upload_mesh(Pigment* pigment, PCommandPool* pool, const void* vertices, size_t vertices_size, const uint32_t* indices, uint32_t index_count)
{
    if(pigment == NULL || pool == NULL || vertices == NULL || vertices_size == 0 || indices == NULL || index_count == 0)
    {
        return NULL;
    }

    PMeshBuffers* mesh = P_NEW_FOR_OBJECT(pigment, mesh);
    if(mesh == NULL)
    {
        return NULL;
    }

    PBufferDesc vertex_buffer_desc = {
        .size   = (uint64_t) vertices_size,
        .usage  = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS | P_BUFFER_USAGE_TRANSFER_DST,
        .memory = {.required = P_MEMORY_DEVICE_LOCAL_BIT},
    };
    mesh->vertex_buffer = pigment_create_buffer(pigment, &vertex_buffer_desc);
    if(mesh->vertex_buffer == NULL)
    {
        goto ERROR;
    }

    PBufferDesc index_buffer_desc = {
        .size   = (uint64_t) index_count * sizeof(uint32_t),
        .usage  = P_BUFFER_USAGE_INDEX | P_BUFFER_USAGE_TRANSFER_DST,
        .memory = {.required = P_MEMORY_DEVICE_LOCAL_BIT},
    };
    mesh->index_buffer = pigment_create_buffer(pigment, &index_buffer_desc);
    if(mesh->index_buffer == NULL)
    {
        goto ERROR;
    }

    PBufferUploadDesc uploads[] = {
        {.dst = mesh->vertex_buffer, .data = vertices, .size = vertices_size,          .offset = 0},
        { .dst = mesh->index_buffer, .data = indices,  .size = index_buffer_desc.size, .offset = 0},
    };
    pigment_std_buffer_upload(pigment, pool, uploads, 2, NULL);

    mesh->index_count = index_count;
    mesh->index_type  = P_INDEX_TYPE_UINT32;

    return mesh;

ERROR:
    pigment_std_destroy_mesh(pigment, mesh);
    return NULL;
}

void pigment_std_destroy_mesh(Pigment* pigment, PMeshBuffers* mesh)
{
    if(pigment == NULL || mesh == NULL)
    {
        return;
    }
    pigment_destroy_buffer(pigment, mesh->vertex_buffer);
    pigment_destroy_buffer(pigment, mesh->index_buffer);
    P_FREE(pigment, mesh);
}

uint64_t pigment_std_mesh_vertex_address(PMeshBuffers* mesh)
{
    if(mesh == NULL)
    {
        return 0;
    }
    return (uint64_t) pigment_buffer_address(mesh->vertex_buffer);
}
