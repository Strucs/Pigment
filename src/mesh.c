/**
 * Copyright 2025 Angel-Leduc TA
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
#include "structs.h"

extern int create_vertex_buffer(VkBuffer* buffer, VkDeviceMemory* memory, VkDeviceAddress* address, const void* data, VkDeviceSize size, PDevice* device, VkCommandPool command_pool);
extern int create_index_buffer(VkBuffer* buffer, VkDeviceMemory* memory, const uint32_t* indices, uint32_t index_count, PDevice* device, VkCommandPool command_pool);

PMeshBuffers* pigment_upload_mesh(Pigment* pigment, const void* vertices, size_t vertices_size, const uint32_t* indices, uint32_t index_count)
{
    if(pigment == NULL || vertices == NULL || indices == NULL)
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
    VkCommandPool pool = pigment->commands->command_pool;

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

void pigment_draw(Pigment* pigment, PDrawCall* draw_cmds, uint32_t draw_cmd_count)
{
    if(pigment == NULL || draw_cmds == NULL || draw_cmd_count == 0)
    {
        return;
    }

    uint32_t current_frame = pigment->swapchain->current_frame;
    VkCommandBuffer cmd    = pigment->commands->command_buffers[current_frame];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pigment->pipeline->graphic_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pigment->pipeline->pipeline_layout, 0, 1, &pigment->descriptor->descriptor_sets[current_frame], 0, NULL);

    for(uint32_t i = 0; i < draw_cmd_count; i++)
    {
        PDrawCall* draw_call = &draw_cmds[i];
        if(draw_call->mesh == NULL)
        {
            continue;
        }

        PDrawPushConstants push = {0};
        glm_mat4_copy(draw_call->transform, push.world_matrix);
        push.vertex_buffer = draw_call->mesh->vertex_buffer_address;

        vkCmdPushConstants(cmd, pigment->pipeline->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PDrawPushConstants), &push);

        vkCmdBindIndexBuffer(cmd, draw_call->mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, draw_call->index_count, 1, draw_call->first_index, 0, 0);
    }
}
