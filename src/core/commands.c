/**
 * Copyright 2025-2026 Angel-Leduc TA
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


static VkCommandPool create_command_pool(PDevice* device, PSurface* surface);
static VkCommandBuffer* allocate_command_buffers(VkCommandPool command_pool, PDevice* device, const uint32_t command_buffers_numbers);
void cmd_begin_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index);
void cmd_end_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index);

#define PIGMENT_COMMAND_POOLS_INITIAL_CAPACITY 4

PCommandPools* create_command_pools(PDevice* device, PSurface* surface)
{
    PCommandPools* pools = NULL;
    VkCommandPool main_pool = NULL;

    pools = malloc(sizeof(*pools));
    if(pools == NULL)
    {
        goto ERROR;
    }

    pools->pool_capacity = PIGMENT_COMMAND_POOLS_INITIAL_CAPACITY;
    pools->pool_count    = 0;
    pools->pools         = malloc(pools->pool_capacity * sizeof(*pools->pools));
    if(pools->pools == NULL)
    {
        goto ERROR;
    }

    main_pool = create_command_pool(device, surface);
    if(main_pool == VK_NULL_HANDLE)
    {
        goto ERROR;
    }

    pools->pools[0]   = main_pool;
    pools->pool_count++;

    return pools;

ERROR:
    fprintf(stderr, "Failed to create command pools!\n");
    if(pools == NULL)
    {
        return NULL;
    }
    free(pools->pools);
    free(pools);
    return NULL;
}

void destroy_command_pools(PCommandPools* pools, PDevice* device)
{
    if(pools == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < pools->pool_count; i++)
    {
        vkDestroyCommandPool(device->logical_device, pools->pools[i], NULL);
    }
    free(pools->pools);
    free(pools);
}

PCommandBuffers* create_command_buffers(PCommandPools* pools, uint32_t pool_index, PDevice* device, uint32_t count)
{
    if(pools == NULL || pool_index >= pools->pool_count)
    {
        return NULL;
    }

    PCommandBuffers* command_buffers = malloc(sizeof(*command_buffers));
    if(command_buffers == NULL)
    {
        perror("create_command_buffers");
        goto ERROR;
    }

    VkCommandPool pool       = pools->pools[pool_index];
    VkCommandBuffer* buffers = allocate_command_buffers(pool, device, count);
    if(buffers == NULL)
    {
        goto ERROR;
    }

    command_buffers->buffers     = buffers;
    command_buffers->source_pool = pool;

    return command_buffers;

ERROR:
    free(command_buffers);
    return NULL;
}

void destroy_command_buffers(PCommandBuffers* command_buffers, PDevice* device, uint32_t count)
{
    if(command_buffers == NULL)
    {
        return;
    }
    vkFreeCommandBuffers(device->logical_device, command_buffers->source_pool, count, command_buffers->buffers);
    free(command_buffers->buffers);
    free(command_buffers);
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
    VkClearDepthStencilValue clear_depth_stencil_value = {0.0f, 0};

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

static VkCommandPool create_command_pool(PDevice* device, PSurface* surface)
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

    VkResult result;
    if((result = vkCreateCommandPool(device->logical_device, &command_pool_create_info, NULL, &command_pool)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create command pool! (result: %d)\n", result);
        command_pool = NULL;
    }

FREE:
    free(indices);
    return command_pool;
}

static VkCommandBuffer* allocate_command_buffers(VkCommandPool command_pool, PDevice* device, const uint32_t command_buffers_numbers)
{
    VkCommandBuffer* command_buffers = malloc(command_buffers_numbers * sizeof(*command_buffers));
    if(command_buffers == NULL)
    {
        goto ERROR;
    }

    VkCommandBufferAllocateInfo command_buffer_allocate_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = command_buffers_numbers
    };

    VkResult result;
    if((result = vkAllocateCommandBuffers(device->logical_device, &command_buffer_allocate_info, command_buffers)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to allocate command buffers! (result: %d)\n", result);
        goto ERROR;
    }

    return command_buffers;

ERROR:
    free(command_buffers);
    return NULL;
}
