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
#include "internal.h"

PMeshBuffers* pigment_upload_mesh(Pigment* pigment, const void* vertices, size_t vertices_size, const uint32_t* indices, uint32_t index_count)
{
    if(pigment == NULL || vertices == NULL || indices == NULL)
    {
        return NULL;
    }

    PCommandPool* command_pool = pigment_default_pool(pigment);
    if(command_pool == NULL)
    {
        return NULL;
    }

    PMeshBuffers* mesh = calloc(1, sizeof(*mesh));
    if(mesh == NULL)
    {
        return NULL;
    }

    VkCommandPool pool = command_pool->pool;

    if(create_vertex_buffer(pigment, &mesh->vertex_buffer, &mesh->vertex_buffer_allocation, &mesh->vertex_buffer_address, vertices, (VkDeviceSize) vertices_size, pool) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    if(create_index_buffer(pigment, &mesh->index_buffer, &mesh->index_buffer_allocation, indices, index_count, pool) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    return mesh;

ERROR:
    pigment_destroy_mesh(pigment, mesh);
    return NULL;
}

void pigment_destroy_mesh(Pigment* pigment, PMeshBuffers* mesh)
{
    if(pigment == NULL || mesh == NULL)
    {
        return;
    }

    PVkAllocator* alloc = pigment->allocator;
    alloc->destroy_buffer(alloc->user_data, mesh->vertex_buffer, mesh->vertex_buffer_allocation);
    alloc->destroy_buffer(alloc->user_data, mesh->index_buffer, mesh->index_buffer_allocation);
    free(mesh);
}
