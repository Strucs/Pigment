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
#include "deletion.h"
#include "internal.h"
#include "log_internal.h"

static void destroy_command_pool_immediate(Pigment* pigment, void* resource);
static void destroy_command_buffer_immediate(Pigment* pigment, void* resource);
static PCommandPool* create_command_pool_internal(Pigment* pigment, const PCommandPoolDesc* desc);
static VkCommandPool create_vk_command_pool(Pigment* pigment, uint32_t queue_family_index, VkCommandPoolCreateFlags flags);
static uint32_t resolve_queue_family_index(Pigment* pigment, PQueueFlags flags);
static VkCommandPoolCreateFlags pigment_flags_to_vk(PCommandPoolFlags flags);
static PResult command_pools_append(PCommandPoolList* pools, PCommandPool* pool);
static void command_pools_destroy(Pigment* pigment, PCommandPoolList* pools, PCommandPool* pool);
static PResult cmd_track_use(PCommandBuffer* cmd, PResourceTracker* tracker);
static PResult pool_append_buffer(PCommandPool* pool, PCommandBuffer* cmd);
static void pool_remove_buffer(PCommandPool* pool, PCommandBuffer* cmd);

#define PIGMENT_COMMAND_POOLS_INITIAL_CAPACITY 4
#define PIGMENT_POOL_BUFFERS_INITIAL_CAPACITY 4
#define PIGMENT_CMD_USES_INITIAL_CAPACITY 16

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

    pigment_defer_destroy(pigment, destroy_command_pool_immediate, pool);
}

static void destroy_command_pool_immediate(Pigment* pigment, void* resource)
{
    command_pools_destroy(pigment, pigment->command_pools, (PCommandPool*) resource);
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

    PCommandBuffer** command_buffers = calloc(count, sizeof(*command_buffers));
    if(command_buffers == NULL)
    {
        return NULL;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        command_buffers[i] = pigment_create_command_buffer(pigment, pool);
        if(command_buffers[i] == NULL)
        {
            for(uint32_t j = 0; j < i; j++)
            {
                vkFreeCommandBuffers(pigment->device->logical_device, pool->pool, 1, &command_buffers[j]->buffer);
                pool_remove_buffer(pool, command_buffers[j]);
                free(command_buffers[j]->uses);
                free(command_buffers[j]);
            }
            free(command_buffers);
            return NULL;
        }
    }

    return command_buffers;
}

void destroy_command_buffers(Pigment* pigment, PCommandBuffer** command_buffers, uint32_t count)
{
    if(command_buffers == NULL || count == 0)
    {
        return;
    }

    PCommandPool* pool = command_buffers[0]->source_pool;

    VkCommandBuffer* vk_cmd_buffer = malloc(count * sizeof(*vk_cmd_buffer));
    if(vk_cmd_buffer != NULL)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            vk_cmd_buffer[i] = command_buffers[i]->buffer;
        }
        vkFreeCommandBuffers(pigment->device->logical_device, pool->pool, count, vk_cmd_buffer);
        free(vk_cmd_buffer);
    }

    for(uint32_t i = 0; i < count; i++)
    {
        pool_remove_buffer(pool, command_buffers[i]);
        free(command_buffers[i]->uses);
        free(command_buffers[i]);
    }
    free(command_buffers);
}

PCommandBuffer* pigment_create_command_buffer(Pigment* pigment, PCommandPool* pool)
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

    VkCommandBufferAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool        = pool->pool,
        .commandBufferCount = 1,
    };

    VkCommandBuffer vk_cmd = VK_NULL_HANDLE;
    VkResult result        = vkAllocateCommandBuffers(pigment->device->logical_device, &alloc_info, &vk_cmd);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to allocate command buffer (result: %d)", result);
        return NULL;
    }

    PCommandBuffer* cmd = calloc(1, sizeof(*cmd));
    if(cmd == NULL)
    {
        vkFreeCommandBuffers(pigment->device->logical_device, pool->pool, 1, &vk_cmd);
        return NULL;
    }
    cmd->uses = malloc(PIGMENT_CMD_USES_INITIAL_CAPACITY * sizeof(*cmd->uses));
    if(cmd->uses == NULL)
    {
        vkFreeCommandBuffers(pigment->device->logical_device, pool->pool, 1, &vk_cmd);
        free(cmd);
        return NULL;
    }
    cmd->buffer       = vk_cmd;
    cmd->source_pool  = pool;
    cmd->use_capacity = PIGMENT_CMD_USES_INITIAL_CAPACITY;

    if(pool_append_buffer(pool, cmd) != PIGMENT_SUCCESS)
    {
        vkFreeCommandBuffers(pigment->device->logical_device, pool->pool, 1, &vk_cmd);
        free(cmd->uses);
        free(cmd);
        return NULL;
    }
    return cmd;
}

void pigment_begin_recording(Pigment* pigment, PCommandBuffer* cmd, PCommandBufferUsage flags)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }

    cmd->use_count = 0;

    VkCommandBufferUsageFlags vk_flags = 0;
    if(flags & P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
    {
        vk_flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    if(flags & P_CMD_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
    {
        vk_flags |= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = vk_flags,
    };

    VkResult result = vkBeginCommandBuffer(cmd->buffer, &begin_info);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to begin command buffer recording (result: %d)", result);
    }
}

void pigment_end_recording(Pigment* pigment, PCommandBuffer* cmd)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    VkResult result = vkEndCommandBuffer(cmd->buffer);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to end command buffer recording (result: %d)", result);
    }
}

PSubmitHandle pigment_queue_submit(Pigment* pigment, PCommandBuffer** cmds, uint32_t count)
{
    if(pigment == NULL || cmds == NULL || count == 0)
    {
        return (PSubmitHandle) {0};
    }

    PDeviceQueue* graphics = device_find_queue(pigment->device, P_QUEUE_GRAPHICS_BIT, 0);
    if(graphics == NULL || graphics->timeline == VK_NULL_HANDLE)
    {
        PLOG_ERROR(pigment, "Graphics queue timeline unavailable; submit aborted.");
        return (PSubmitHandle) {0};
    }

    uint64_t value = device_queue_acquire_value(graphics);
    stamp_uses_submit(cmds, count, value);

    VkCommandBufferSubmitInfo* cmd_infos = malloc(count * sizeof(*cmd_infos));
    if(cmd_infos == NULL)
    {
        PLOG_ERROR(pigment, "Failed to allocate command info array for queue submit.");
        return (PSubmitHandle) {0};
    }

    for(uint32_t i = 0; i < count; i++)
    {
        cmd_infos[i] = (VkCommandBufferSubmitInfo) {
            .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cmds[i]->buffer,
        };
    }

    VkSemaphoreSubmitInfo signal = {
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = graphics->timeline,
        .value     = value,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    VkSubmitInfo2 submit_info = {
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount   = count,
        .pCommandBufferInfos      = cmd_infos,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &signal,
    };

    VkResult result = vkQueueSubmit2(graphics->queue, 1, &submit_info, VK_NULL_HANDLE);
    free(cmd_infos);

    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to submit command buffers (result: %d)", result);
        return (PSubmitHandle) {0};
    }

    return (PSubmitHandle) {.queue = graphics, .value = value};
}

PBool pigment_submit_complete(Pigment* pigment, PSubmitHandle handle)
{
    if(pigment == NULL || handle.queue == NULL || handle.value == 0)
    {
        return P_TRUE;
    }

    if(handle.queue->timeline == VK_NULL_HANDLE)
    {
        return P_TRUE;
    }

    uint64_t current = 0;
    if(vkGetSemaphoreCounterValue(pigment->device->logical_device, handle.queue->timeline, &current) != VK_SUCCESS)
    {
        return P_FALSE;
    }

    return current >= handle.value;
}

void pigment_submit_wait(Pigment* pigment, PSubmitHandle handle)
{
    if(pigment == NULL || handle.queue == NULL || handle.value == 0)
    {
        return;
    }

    if(handle.queue->timeline == VK_NULL_HANDLE)
    {
        return;
    }

    VkSemaphoreWaitInfo wait_info = {
        .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores    = &handle.queue->timeline,
        .pValues        = &handle.value,
    };
    vkWaitSemaphores(pigment->device->logical_device, &wait_info, UINT64_MAX);
}

static void destroy_command_buffer_immediate(Pigment* pigment, void* resource)
{
    PCommandBuffer* cmd = (PCommandBuffer*) resource;
    vkFreeCommandBuffers(pigment->device->logical_device, cmd->source_pool->pool, 1, &cmd->buffer);
    free(cmd->uses);
    free(cmd);
}

void pigment_destroy_command_buffer(Pigment* pigment, PCommandBuffer* cmd)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }

    pool_remove_buffer(cmd->source_pool, cmd);
    pigment_defer_destroy(pigment, destroy_command_buffer_immediate, cmd);
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

    command_pool = calloc(1, sizeof(*command_pool));
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
            for(uint32_t j = 0; j < pool->buffer_count; j++)
            {
                free(pool->buffers[j]->uses);
                free(pool->buffers[j]);
            }
            free(pool->buffers);
            free(pool);
            return;
        }
    }
}

static PResult pool_append_buffer(PCommandPool* pool, PCommandBuffer* cmd)
{
    if(pool->buffer_count >= pool->buffer_capacity)
    {
        uint32_t new_capacity    = (pool->buffer_capacity == 0) ? PIGMENT_POOL_BUFFERS_INITIAL_CAPACITY : pool->buffer_capacity * 2;
        PCommandBuffer** new_ptr = realloc(pool->buffers, new_capacity * sizeof(*new_ptr));
        if(new_ptr == NULL)
        {
            return PIGMENT_ERROR_OUT_OF_MEMORY;
        }
        pool->buffers         = new_ptr;
        pool->buffer_capacity = new_capacity;
    }
    pool->buffers[pool->buffer_count++] = cmd;
    return PIGMENT_SUCCESS;
}

static void pool_remove_buffer(PCommandPool* pool, PCommandBuffer* cmd)
{
    for(uint32_t i = 0; i < pool->buffer_count; i++)
    {
        if(pool->buffers[i] == cmd)
        {
            pool->buffers[i] = pool->buffers[--pool->buffer_count];
            return;
        }
    }
}

void pigment_cmd_use(Pigment* pigment, PCommandBuffer* cmd, PResourceTracker* tracker)
{
    if(pigment == NULL || cmd == NULL || tracker == NULL)
    {
        return;
    }
    if(cmd_track_use(cmd, tracker) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "pigment_cmd_use: failed to grow uses array, tracking dropped for this entry.");
    }
}

void pigment_cmd_use_buffer(Pigment* pigment, PCommandBuffer* cmd, PBuffer* buffer)
{
    if(buffer == NULL)
    {
        return;
    }
    pigment_cmd_use(pigment, cmd, &buffer->tracker);
}

void pigment_cmd_use_image(Pigment* pigment, PCommandBuffer* cmd, PImage* image)
{
    if(image == NULL)
    {
        return;
    }
    pigment_cmd_use(pigment, cmd, &image->tracker);
}

void pigment_cmd_use_sampler(Pigment* pigment, PCommandBuffer* cmd, PSampler* sampler)
{
    if(sampler == NULL)
    {
        return;
    }
    pigment_cmd_use(pigment, cmd, &sampler->tracker);
}

static PResult cmd_track_use(PCommandBuffer* cmd, PResourceTracker* tracker)
{
    if(cmd->use_count >= cmd->use_capacity)
    {
        uint32_t new_capacity       = (cmd->use_capacity == 0) ? PIGMENT_CMD_USES_INITIAL_CAPACITY : cmd->use_capacity * 2;
        PResourceTracker** new_uses = realloc(cmd->uses, new_capacity * sizeof(*new_uses));
        if(new_uses == NULL)
        {
            return PIGMENT_ERROR_OUT_OF_MEMORY;
        }
        cmd->uses         = new_uses;
        cmd->use_capacity = new_capacity;
    }

    cmd->uses[cmd->use_count++] = tracker;
    return PIGMENT_SUCCESS;
}

void stamp_uses_submit(PCommandBuffer** cmds, uint32_t count, uint64_t value)
{
    for(uint32_t i = 0; i < count; i++)
    {
        PCommandBuffer* cmd = cmds[i];
        for(uint32_t j = 0; j < cmd->use_count; j++)
        {
            _Atomic uint64_t* slot = &cmd->uses[j]->last_used_submit;
            uint64_t old           = atomic_load_explicit(slot, memory_order_relaxed);
            while(old < value && !atomic_compare_exchange_weak_explicit(slot, &old, value, memory_order_release, memory_order_relaxed))
            {
                // retry
            }
        }
    }
}
