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

#include "commands.h"
#include "structs.h"

extern QueueFamilyIndices* find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);


VkCommandPool create_command_pool(PDevice* device, PSurface* surface);
VkCommandBuffer* create_command_buffers(VkCommandPool command_pool, PDevice* device, const uint32_t command_buffers_numbers);
void record_commands(VkCommandBuffer command_buffer, PPipeline* pipeline, PSwapchain* swapchain, PRenderPass* render_pass, uint32_t image_index, PBuffers* buffers, PDescriptor* descriptor);

PCommands* create_commands(PDevice* device, PSurface* surface)
{
    PCommands* commands = malloc(sizeof(*commands));
    if(commands == NULL)
    {
        perror("malloc");
        return NULL;
    }

    VkCommandPool command_pool = create_command_pool(device, surface);

    commands->command_pool = command_pool;

    return commands;
}

void update_commands(PCommands* commands, PDevice* device, const uint32_t command_buffers_numbers)
{
    VkCommandBuffer* command_buffers = create_command_buffers(commands->command_pool, device, command_buffers_numbers);
    commands->command_buffers        = command_buffers;
}

void destroy_commands(PCommands* commands, PDevice* device, const uint32_t command_buffers_numbers)
{
    if(commands == NULL)
    {
        return;
    }
    vkFreeCommandBuffers(device->logical_device, commands->command_pool, command_buffers_numbers, commands->command_buffers);
    free(commands->command_buffers);
    vkDestroyCommandPool(device->logical_device, commands->command_pool, NULL);
    free(commands);
}

void record_commands(VkCommandBuffer command_buffer, PPipeline* pipeline, PSwapchain* swapchain, PRenderPass* render_pass, uint32_t image_index, PBuffers* buffers, PDescriptor* descriptor)
{
    VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };

    if(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to begin recording command buffer!\n");
        return;
    }

    VkRect2D render_area = {
        {0, 0},
        swapchain->extent
    };

    VkClearColorValue clear_color_value = {
        {0.0f, 0.0f, 0.0f, 1.0f}
    };
    VkClearDepthStencilValue clear_depth_stencil_value = {1.0f, 0};

    VkClearValue clear_values[2] = {0};
    clear_values[0].color        = clear_color_value;
    clear_values[1].depthStencil = clear_depth_stencil_value;

    VkRenderPassBeginInfo render_pass_begin_info = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = render_pass->render_pass,
        .framebuffer     = swapchain->framebuffers[image_index],
        .renderArea      = render_area,
        .clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
        .pClearValues    = clear_values
    };

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->graphic_pipeline);

    VkViewport viewport = {
        viewport.x        = 0.0f,
        viewport.y        = 0.0f,
        viewport.width    = (float) swapchain->extent.width,
        viewport.height   = (float) swapchain->extent.height,
        viewport.minDepth = 0.0f,
        viewport.maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        {0, 0},
        swapchain->extent
    };

    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    VkBuffer vertex_buffers[] = {buffers->vertex_buffer};
    VkDeviceSize offsets[]    = {0};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffer, buffers->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline_layout, 0, 1, &descriptor->descriptor_sets[swapchain->current_frame], 0, NULL);
    vkCmdDrawIndexed(command_buffer, buffers->indices_size, 1, 0, 0, 0);

    vkCmdEndRenderPass(command_buffer);

    if(vkEndCommandBuffer(command_buffer) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to record command buffer!\n");
    }
}

VkCommandPool create_command_pool(PDevice* device, PSurface* surface)
{
    VkCommandPool command_pool = NULL;
    QueueFamilyIndices* indices = NULL;

    indices = find_queue_families(device->physical_device, surface->surface);
    if(indices == NULL)
    {
        goto FREE;
    }


    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = indices->graphics_family.value
    };

    if(vkCreateCommandPool(device->logical_device, &command_pool_create_info, NULL, &command_pool) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create command pool!\n");
        command_pool = NULL;
        goto FREE;
    }

FREE:
    free(indices);
    return command_pool;
}

VkCommandBuffer* create_command_buffers(VkCommandPool command_pool, PDevice* device, const uint32_t command_buffers_numbers)
{
    VkCommandBuffer* command_buffers = malloc(command_buffers_numbers * sizeof(*command_buffers));
    if(command_buffers == NULL)
    {
        perror("malloc");
        return NULL;
    }

    VkCommandBufferAllocateInfo command_buffer_allocate_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = command_buffers_numbers
    };

    if(vkAllocateCommandBuffers(device->logical_device, &command_buffer_allocate_info, command_buffers) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to allocate command buffers!\n");
        free(command_buffers);
        return NULL;
    }

    return command_buffers;
}
