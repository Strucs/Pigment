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
        perror("calloc");
        return NULL;
    }

    PDevice* device    = pigment->device;
    VkCommandPool pool = command_pool->pool;

    if(create_vertex_buffer(&mesh->vertex_buffer, &mesh->vertex_buffer_memory, &mesh->vertex_buffer_address, vertices, (VkDeviceSize) vertices_size, device, pool) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    if(create_index_buffer(&mesh->index_buffer, &mesh->index_buffer_memory, indices, index_count, device, pool) != PIGMENT_SUCCESS)
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

    VkDevice dev = pigment->device->logical_device;
    vkDestroyBuffer(dev, mesh->vertex_buffer, NULL);
    vkFreeMemory(dev, mesh->vertex_buffer_memory, NULL);
    vkDestroyBuffer(dev, mesh->index_buffer, NULL);
    vkFreeMemory(dev, mesh->index_buffer_memory, NULL);
    free(mesh);
}
