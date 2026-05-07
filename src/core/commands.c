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
static uint32_t resolve_queue_family_index(Pigment* pigment, PQueueFlags flags);
static VkCommandPoolCreateFlags pigment_flags_to_vk(PCommandPoolFlags flags);
static PResult command_pools_append(PCommandPoolList* pools, PCommandPool* pool);
static void command_pools_destroy(Pigment* pigment, PCommandPoolList* pools, PCommandPool* pool);
static VkCommandBuffer* allocate_command_buffers(Pigment* pigment, VkCommandPool command_pool, const uint32_t command_buffers_numbers);
static VkCommandBuffer start_single_usage_commands(Pigment* pigment, VkCommandPool command_pool);
static void end_single_usage_commands(Pigment* pigment, VkCommandBuffer* command_buffer, VkCommandPool command_pool);

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
        .queue_flags = P_QUEUE_GRAPHICS_BIT,
        .flags       = P_COMMAND_POOL_FLAG_RESET_BUFFER,
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

PCommandBuffer** create_command_buffers(Pigment* pigment, PCommandPool* pool, uint32_t count)
{
    if(pool == NULL || count == 0)
    {
        return NULL;
    }

    VkCommandBuffer* vk_buffers      = NULL;
    PCommandBuffer** command_buffers = calloc(count, sizeof(*command_buffers));
    if(command_buffers == NULL)
    {
        goto ERROR;
    }

    vk_buffers = allocate_command_buffers(pigment, pool->pool, count);
    if(vk_buffers == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        command_buffers[i] = calloc(1, sizeof(**command_buffers));
        if(command_buffers[i] == NULL)
        {
            goto ERROR;
        }
        command_buffers[i]->buffer      = vk_buffers[i];
        command_buffers[i]->source_pool = pool->pool;
    }

    free(vk_buffers);
    return command_buffers;

ERROR:
    free(vk_buffers);
    if(command_buffers != NULL)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            free(command_buffers[i]);
        }
        free(command_buffers);
    }
    return NULL;
}

void destroy_command_buffers(Pigment* pigment, PCommandBuffer** command_buffers, uint32_t count)
{
    if(command_buffers == NULL || count == 0)
    {
        return;
    }

    VkCommandBuffer* vk_cmd_buffer = malloc(count * sizeof(*vk_cmd_buffer));
    if(vk_cmd_buffer != NULL)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            vk_cmd_buffer[i] = command_buffers[i]->buffer;
        }
        vkFreeCommandBuffers(pigment->device->logical_device, command_buffers[0]->source_pool, count, vk_cmd_buffer);
        free(vk_cmd_buffer);
    }

    for(uint32_t i = 0; i < count; i++)
    {
        free(command_buffers[i]);
    }
    free(command_buffers);
}

PCommandBuffer* pigment_begin_single_use_cmd(Pigment* pigment, PCommandPool* pool)
{
    if(pigment == NULL)
    {
        return NULL;
    }

    if(pool == NULL)
    {
        pool = pigment_default_pool(pigment);
        if(pool == NULL)
        {
            return NULL;
        }
    }

    VkCommandBuffer vk_cmd = start_single_usage_commands(pigment, pool->pool);
    if(vk_cmd == VK_NULL_HANDLE)
    {
        return NULL;
    }

    PCommandBuffer* cmd = calloc(1, sizeof(*cmd));
    if(cmd == NULL)
    {
        vkFreeCommandBuffers(pigment->device->logical_device, pool->pool, 1, &vk_cmd);
        return NULL;
    }
    cmd->buffer      = vk_cmd;
    cmd->source_pool = pool->pool;
    return cmd;
}

void pigment_end_single_use_cmd(Pigment* pigment, PCommandBuffer* cmd)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    end_single_usage_commands(pigment, &cmd->buffer, cmd->source_pool);
    free(cmd);
}

void pigment_cmd_begin_label(Pigment* pigment, PCommandBuffer* cmd, const char* name)
{
    (void) pigment;
    if(cmd == NULL || name == NULL || vkCmdBeginDebugUtilsLabelEXT == NULL)
    {
        return;
    }
    VkDebugUtilsLabelEXT label = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = name,
    };
    vkCmdBeginDebugUtilsLabelEXT(cmd->buffer, &label);
}

void pigment_cmd_end_label(Pigment* pigment, PCommandBuffer* cmd)
{
    (void) pigment;
    if(cmd == NULL || vkCmdEndDebugUtilsLabelEXT == NULL)
    {
        return;
    }
    vkCmdEndDebugUtilsLabelEXT(cmd->buffer);
}

void pigment_cmd_insert_label(Pigment* pigment, PCommandBuffer* cmd, const char* name)
{
    (void) pigment;
    if(cmd == NULL || name == NULL || vkCmdInsertDebugUtilsLabelEXT == NULL)
    {
        return;
    }
    VkDebugUtilsLabelEXT label = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = name,
    };
    vkCmdInsertDebugUtilsLabelEXT(cmd->buffer, &label);
}

static PCommandPool* create_command_pool_internal(Pigment* pigment, const PCommandPoolDesc* desc)
{
    PDevice* device             = pigment->device;
    PCommandPool* command_pool  = NULL;
    VkCommandPool pool          = NULL;
    uint32_t queue_family_index = UINT32_MAX;

    queue_family_index = resolve_queue_family_index(pigment, desc->queue_flags);
    if(queue_family_index == UINT32_MAX)
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
    command_pool->queue_flags        = desc->queue_flags;
    command_pool->queue_family_index = queue_family_index;
    command_pool->flags              = desc->flags;

    set_object_name(device->logical_device, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t) pool, desc->name);

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

static uint32_t resolve_queue_family_index(Pigment* pigment, PQueueFlags flags)
{
    PDeviceQueue* found = device_find_queue(pigment->device, flags, 0);
    if(found == NULL)
    {
        PLOG_ERROR(pigment, "resolve_queue_family_index: no queue family on the picked GPU has flags %d.", (unsigned) flags);
        return UINT32_MAX;
    }

    return found->family_index;
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

static PResult command_pools_append(PCommandPoolList* pools, PCommandPool* pool)
{
    if(pools->count >= pools->capacity)
    {
        uint32_t new_capacity  = pools->capacity * 2;
        PCommandPool** new_ptr = realloc(pools->pools, new_capacity * sizeof(*new_ptr));

        if(new_ptr == NULL)
        {
            return PIGMENT_ERROR_OUT_OF_MEMORY;
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

static VkCommandBuffer start_single_usage_commands(Pigment* pigment, VkCommandPool command_pool)
{
    PDevice* device                        = pigment->device;
    VkCommandBufferAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool        = command_pool,
        .commandBufferCount = 1
    };

    VkCommandBuffer command_buffer;

    VkResult result;
    if((result = vkAllocateCommandBuffers(device->logical_device, &alloc_info, &command_buffer)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to allocate single usage command buffer! (result: %d)", result);
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    if((result = vkBeginCommandBuffer(command_buffer, &begin_info)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to begin single usage command buffer! (result: %d)", result);
        vkFreeCommandBuffers(device->logical_device, command_pool, 1, &command_buffer);
        return VK_NULL_HANDLE;
    }

    return command_buffer;
}

static void end_single_usage_commands(Pigment* pigment, VkCommandBuffer* command_buffer, VkCommandPool command_pool)
{
    PDevice* device = pigment->device;
    VkResult result;
    if((result = vkEndCommandBuffer(*command_buffer)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to end single usage command buffer! (result: %d)", result);
    }

    VkSubmitInfo submit_info = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = command_buffer
    };

    VkQueue graphics_queue = device_find_queue(device, P_QUEUE_GRAPHICS_BIT, 0)->queue;
    if((result = vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to submit single usage command buffer! (result: %d)", result);
    }

    vkQueueWaitIdle(graphics_queue);

    vkFreeCommandBuffers(device->logical_device, command_pool, 1, command_buffer);
}
