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
#include "internal.h"
#include "log_internal.h"

static PCommandPool* create_command_pool_internal(Pigment* pigment, const PCommandPoolDesc* desc);
static VkCommandPool create_vk_command_pool(Pigment* pigment, uint32_t queue_family_index, VkCommandPoolCreateFlags flags);
static uint32_t resolve_queue_family_index(Pigment* pigment, PQueueFamily family);
static VkCommandPoolCreateFlags pigment_flags_to_vk(PCommandPoolFlags flags);
static int command_pools_append(PCommandPoolList* pools, PCommandPool* pool);
static void command_pools_destroy(Pigment* pigment, PCommandPoolList* pools, PCommandPool* pool);
static VkCommandBuffer* allocate_command_buffers(Pigment* pigment, VkCommandPool command_pool, const uint32_t command_buffers_numbers);

#define PIGMENT_COMMAND_POOLS_INITIAL_CAPACITY 4

PCommandPoolList* create_command_pools(Pigment* pigment)
{
    PDevice* device            = pigment->device;
    PCommandPoolList* pools    = NULL;
    PCommandPool* default_pool = NULL;

    pools = malloc(sizeof(*pools));
    if(pools == NULL)
    {
        goto ERROR;
    }

    pools->capacity = PIGMENT_COMMAND_POOLS_INITIAL_CAPACITY;
    pools->count    = 0;
    pools->pools    = malloc(pools->capacity * sizeof(*pools->pools));
    if(pools->pools == NULL)
    {
        goto ERROR;
    }

    PCommandPoolDesc default_desc = {
        .queue_family = P_QUEUE_FAMILY_GRAPHICS,
        .flags        = P_COMMAND_POOL_FLAG_RESET_BUFFER,
    };

    default_pool = create_command_pool_internal(pigment, &default_desc);
    if(default_pool == NULL)
    {
        goto ERROR;
    }

    if(command_pools_append(pools, default_pool) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    return pools;

ERROR:
    PLOG_ERROR(pigment, "Failed to create command pools!");
    if(default_pool != NULL)
    {
        vkDestroyCommandPool(device->logical_device, default_pool->pool, NULL);
        free(default_pool);
    }
    if(pools != NULL)
    {
        free(pools->pools);
        free(pools);
    }
    return NULL;
}

void destroy_command_pools(Pigment* pigment, PCommandPoolList* pools)
{
    if(pools == NULL)
    {
        return;
    }

    while(pools->count > 0)
    {
        command_pools_destroy(pigment, pools, pools->pools[0]);
    }

    free(pools->pools);
    free(pools);
}

PCommandPool* pigment_create_command_pool(Pigment* pigment, PCommandPoolDesc* desc)
{
    if(pigment == NULL || desc == NULL)
    {
        return NULL;
    }

    PCommandPool* pool = create_command_pool_internal(pigment, desc);
    if(pool == NULL)
    {
        return NULL;
    }

    if(command_pools_append(pigment->command_pools, pool) != PIGMENT_SUCCESS)
    {
        vkDestroyCommandPool(pigment->device->logical_device, pool->pool, NULL);
        free(pool);
        return NULL;
    }

    return pool;
}

void pigment_destroy_command_pool(Pigment* pigment, PCommandPool* pool)
{
    if(pigment == NULL || pool == NULL)
    {
        return;
    }

    if(pool == pigment_default_pool(pigment))
    {
        PLOG_ERROR(pigment, "pigment_destroy_command_pool: cannot destroy the default pool.");
        return;
    }

    vkDeviceWaitIdle(pigment->device->logical_device);

    command_pools_destroy(pigment, pigment->command_pools, pool);
}

PCommandPool* pigment_default_pool(Pigment* pigment)
{
    if(pigment == NULL || pigment->command_pools == NULL || pigment->command_pools->count == 0)
    {
        return NULL;
    }

    return pigment->command_pools->pools[0];
}

PCommandBuffers* create_command_buffers(Pigment* pigment, PCommandPool* pool, uint32_t count)
{
    if(pool == NULL)
    {
        return NULL;
    }

    PCommandBuffers* command_buffers = malloc(sizeof(*command_buffers));
    if(command_buffers == NULL)
    {
        goto ERROR;
    }

    VkCommandBuffer* buffers = allocate_command_buffers(pigment, pool->pool, count);
    if(buffers == NULL)
    {
        goto ERROR;
    }

    command_buffers->buffers     = buffers;
    command_buffers->source_pool = pool->pool;

    return command_buffers;

ERROR:
    free(command_buffers);
    return NULL;
}

void destroy_command_buffers(Pigment* pigment, PCommandBuffers* command_buffers, uint32_t count)
{
    if(command_buffers == NULL)
    {
        return;
    }
    vkFreeCommandBuffers(pigment->device->logical_device, command_buffers->source_pool, count, command_buffers->buffers);
    free(command_buffers->buffers);
    free(command_buffers);
}

void cmd_begin_rendering(Pigment* pigment, VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index, bool transparent)
{
    VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if(swapchain->depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT
        || swapchain->depth_format == VK_FORMAT_D24_UNORM_S8_UINT
        || swapchain->depth_format == VK_FORMAT_D16_UNORM_S8_UINT)
    {
        depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    VkImageMemoryBarrier2 barriers_to_render[2] = {
        {.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
         .srcAccessMask       = VK_ACCESS_2_NONE,
         .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
         .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image               = swapchain->images[image_index],
         .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}},
        {.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .srcStageMask        = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
         .srcAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
         .dstStageMask        = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
         .dstAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
         .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image               = swapchain->depth_image,
         .subresourceRange    = {depth_aspect, 0, 1, 0, 1}}
    };

    VkDependencyInfo dep_to_render = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers    = barriers_to_render
    };

    vkCmdPipelineBarrier2(command_buffer, &dep_to_render);

    VkClearColorValue clear_color_value = {
        {0.0f, 0.0f, 0.0f, transparent ? 0.0f : 1.0f}
    };
    VkClearDepthStencilValue clear_depth_stencil_value = {pigment->config.depth_clear_value, 0};

    VkRenderingAttachmentInfoKHR color_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView   = swapchain->image_views[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = {.color = clear_color_value}
    };

    VkRenderingAttachmentInfoKHR depth_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView   = swapchain->depth_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue  = {.depthStencil = clear_depth_stencil_value}
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

static PCommandPool* create_command_pool_internal(Pigment* pigment, const PCommandPoolDesc* desc)
{
    PDevice* device             = pigment->device;
    PCommandPool* command_pool  = NULL;
    VkCommandPool pool          = NULL;
    uint32_t queue_family_index = P_QUEUE_FAMILY_MAX_ENUM;

    queue_family_index = resolve_queue_family_index(pigment, desc->queue_family);
    if(queue_family_index == P_QUEUE_FAMILY_MAX_ENUM)
    {
        goto ERROR;
    }

    pool = create_vk_command_pool(pigment, queue_family_index, pigment_flags_to_vk(desc->flags));
    if(pool == NULL)
    {
        goto ERROR;
    }

    command_pool = malloc(sizeof(*command_pool));
    if(command_pool == NULL)
    {
        goto ERROR;
    }

    command_pool->pool               = pool;
    command_pool->queue_family       = desc->queue_family;
    command_pool->queue_family_index = queue_family_index;
    command_pool->flags              = desc->flags;

    return command_pool;

ERROR:
    if(pool != NULL)
    {
        vkDestroyCommandPool(device->logical_device, pool, NULL);
    }
    return NULL;
}

static VkCommandPool create_vk_command_pool(Pigment* pigment, uint32_t queue_family_index, VkCommandPoolCreateFlags flags)
{
    VkCommandPool command_pool = NULL;

    VkCommandPoolCreateInfo create_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = flags,
        .queueFamilyIndex = queue_family_index,
    };

    VkResult result = vkCreateCommandPool(pigment->device->logical_device, &create_info, NULL, &command_pool);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create command pool! (result: %d)", result);
        return NULL;
    }

    return command_pool;
}

static uint32_t resolve_queue_family_index(Pigment* pigment, PQueueFamily family)
{
    if(family != P_QUEUE_FAMILY_GRAPHICS)
    {
        PLOG_ERROR(pigment, "resolve_queue_family_index: only P_QUEUE_FAMILY_GRAPHICS is supported (got %d).", family);
        return P_QUEUE_FAMILY_MAX_ENUM;
    }

    return pigment->device->graphics_family_index;
}

static VkCommandPoolCreateFlags pigment_flags_to_vk(PCommandPoolFlags flags)
{
    VkCommandPoolCreateFlags result = 0;
    if(flags & P_COMMAND_POOL_FLAG_TRANSIENT)
    {
        result |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    }
    if(flags & P_COMMAND_POOL_FLAG_RESET_BUFFER)
    {
        result |= VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    }
    return result;
}

static int command_pools_append(PCommandPoolList* pools, PCommandPool* pool)
{
    if(pools->count >= pools->capacity)
    {
        uint32_t new_capacity  = pools->capacity * 2;
        PCommandPool** new_ptr = realloc(pools->pools, new_capacity * sizeof(*new_ptr));

        if(new_ptr == NULL)
        {
            return PIGMENT_ERROR;
        }

        pools->pools    = new_ptr;
        pools->capacity = new_capacity;
    }

    pools->pools[pools->count] = pool;
    pools->count++;

    return PIGMENT_SUCCESS;
}

static void command_pools_destroy(Pigment* pigment, PCommandPoolList* pools, PCommandPool* pool)
{
    for(uint32_t i = 0; i < pools->count; i++)
    {
        if(pools->pools[i] == pool)
        {
            pools->pools[i] = pools->pools[pools->count - 1];
            pools->count--;
            vkDestroyCommandPool(pigment->device->logical_device, pool->pool, NULL);
            free(pool);
            return;
        }
    }
}

static VkCommandBuffer* allocate_command_buffers(Pigment* pigment, VkCommandPool command_pool, const uint32_t command_buffers_numbers)
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
    if((result = vkAllocateCommandBuffers(pigment->device->logical_device, &command_buffer_allocate_info, command_buffers)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to allocate command buffers! (result: %d)", result);
        goto ERROR;
    }

    return command_buffers;

ERROR:
    free(command_buffers);
    return NULL;
}
