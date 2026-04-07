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
void cmd_begin_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index);
void cmd_end_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index);
void record_commands(VkCommandBuffer command_buffer, PPipeline* pipeline, PSwapchain* swapchain, uint32_t image_index, PBuffers* buffers, PDescriptor* descriptor);

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

void cmd_begin_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index)
{
    VkImageMemoryBarrier2 barriers_to_render[2] = {
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask       = VK_ACCESS_2_NONE,
            .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = swapchain->images[image_index],
            .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        },
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask        = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = swapchain->depth_image,
            .subresourceRange    = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
        }
    };

    VkDependencyInfo dep_to_render = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers    = barriers_to_render
    };

    vkCmdPipelineBarrier2(command_buffer, &dep_to_render);

    VkClearColorValue clear_color_value = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkClearDepthStencilValue clear_depth_stencil_value = {1.0f, 0};

    VkRenderingAttachmentInfoKHR color_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView   = swapchain->image_views[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = { .color = clear_color_value }
    };

    VkRenderingAttachmentInfoKHR depth_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView   = swapchain->depth_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue  = { .depthStencil = clear_depth_stencil_value }
    };

    VkRenderingInfoKHR rendering_info = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .renderArea           = {{0, 0}, swapchain->extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_attachment,
        .pDepthAttachment     = &depth_attachment
    };

    vkCmdBeginRendering(command_buffer, &rendering_info);
}

void cmd_end_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index)
{
    vkCmdEndRendering(command_buffer);

    VkImageMemoryBarrier2 barrier_to_present = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask       = VK_ACCESS_2_NONE,
        .oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = swapchain->images[image_index],
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };

    VkDependencyInfo dep_to_present = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier_to_present
    };

    vkCmdPipelineBarrier2(command_buffer, &dep_to_present);
}

void record_commands(VkCommandBuffer command_buffer, PPipeline* pipeline, PSwapchain* swapchain, uint32_t image_index, PBuffers* buffers, PDescriptor* descriptor)
{
    VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };

    if(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to begin recording command buffer!\n");
        return;
    }

    cmd_begin_rendering(command_buffer, swapchain, image_index);

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

    PDrawPushConstants push = {0};
    glm_mat4_identity(push.world_matrix);
    push.vertex_buffer = buffers->vertex_buffer_address;

    vkCmdPushConstants(command_buffer, pipeline->pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(PDrawPushConstants), &push);

    vkCmdBindIndexBuffer(command_buffer, buffers->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline_layout, 0, 1, &descriptor->descriptor_sets[swapchain->current_frame], 0, NULL);
    vkCmdDrawIndexed(command_buffer, buffers->indices_size, 1, 0, 0, 0);

    cmd_end_rendering(command_buffer, swapchain, image_index);

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
