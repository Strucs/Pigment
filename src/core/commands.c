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

static void destroy_command_pool_immediate(Pigment* pigment, void* resource);
static void destroy_command_buffers_immediate(Pigment* pigment, void* resource);
static PCommandPool* create_command_pool_internal(Pigment* pigment, const PCommandPoolDesc* desc);
static VkCommandPool create_vk_command_pool(Pigment* pigment, uint32_t queue_family_index, VkCommandPoolCreateFlags flags);
static uint32_t resolve_queue_family_index(Pigment* pigment, PQueueFlags flags);
static VkCommandPoolCreateFlags pigment_flags_to_vk(PCommandPoolFlags flags);
static PResult cmd_track_use(Pigment* pigment, PCommandBuffer* cmd, PResourceTracker* tracker);
static PResult pool_append_buffer(Pigment* pigment, PCommandPool* pool, PCommandBuffer* cmd);
static void pool_remove_buffer(PCommandPool* pool, PCommandBuffer* cmd);

#define PIGMENT_POOL_BUFFERS_INITIAL_CAPACITY 4
#define PIGMENT_CMD_USES_INITIAL_CAPACITY 16

typedef struct PCommandBufferBatch {
    PCommandPool* pool;
    VkCommandBuffer* vk_buffers;
    PCommandBuffer** wrappers;
    uint32_t count;
} PCommandBufferBatch;

PCommandPool* pigment_create_command_pool(Pigment* pigment, PCommandPoolDesc* desc)
{
    if(pigment == NULL || desc == NULL)
    {
        return NULL;
    }

    return create_command_pool_internal(pigment, desc);
}

void pigment_destroy_command_pool(Pigment* pigment, PCommandPool* pool)
{
    if(pigment == NULL || pool == NULL)
    {
        return;
    }

    pigment_defer_destroy(pigment, destroy_command_pool_immediate, pool);
}

static void destroy_command_pool_immediate(Pigment* pigment, void* resource)
{
    PCommandPool* pool = (PCommandPool*) resource;
    vkDestroyCommandPool(pigment->device->logical_device, pool->pool, &pigment->vk_alloc);
    for(uint32_t i = 0; i < pool->buffer_count; i++)
    {
        P_FREE(pigment, pool->buffers[i]->uses);
        P_FREE(pigment, pool->buffers[i]);
    }
    P_FREE(pigment, pool->buffers);
    P_FREE(pigment, pool);
}

void pigment_reset_command_pool(Pigment* pigment, PCommandPool* pool)
{
    if(pigment == NULL || pool == NULL)
    {
        return;
    }

    VkResult result = vkResetCommandPool(pigment->device->logical_device, pool->pool, 0);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to reset command pool (result: %d)", result);
        return;
    }

    for(uint32_t i = 0; i < pool->buffer_count; i++)
    {
        pool->buffers[i]->use_count = 0;
    }
}

PCommandBuffer** create_command_buffers(Pigment* pigment, PCommandPool* pool, uint32_t count)
{
    if(pool == NULL || count == 0)
    {
        return NULL;
    }

    PCommandBuffer** command_buffers = P_NEW_ARRAY_FOR_COMMAND(pigment, command_buffers, count);
    if(command_buffers == NULL)
    {
        return NULL;
    }

    if(pigment_create_command_buffers(pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, count, command_buffers) != PIGMENT_SUCCESS)
    {
        P_FREE(pigment, command_buffers);
        return NULL;
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

    P_STACK_OR_HEAP(VkCommandBuffer, vk_cmd_buffer, count);
    if(vk_cmd_buffer != NULL)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            vk_cmd_buffer[i] = command_buffers[i]->buffer;
        }
        vkFreeCommandBuffers(pigment->device->logical_device, pool->pool, count, vk_cmd_buffer);
        P_STACK_OR_HEAP_FREE(pigment, vk_cmd_buffer);
    }

    for(uint32_t i = 0; i < count; i++)
    {
        pool_remove_buffer(pool, command_buffers[i]);
        P_FREE(pigment, command_buffers[i]->uses);
        P_FREE(pigment, command_buffers[i]);
    }
    P_FREE(pigment, command_buffers);
}

PResult pigment_create_command_buffers(Pigment* pigment, PCommandPool* pool, PCommandBufferLevel level, uint32_t count, PCommandBuffer** out_cmds)
{
    if(pigment == NULL || pool == NULL || count == 0 || out_cmds == NULL)
    {
        return PIGMENT_ERROR;
    }

    PResult status         = PIGMENT_ERROR;
    uint32_t wrappers_done = 0;
    uint32_t appended      = 0;
    PBool vk_allocated     = P_FALSE;

    P_STACK_OR_HEAP(VkCommandBuffer, vk_cmds, count);
    P_STACK_OR_HEAP(PCommandBuffer*, wrappers, count);
    if(vk_cmds == NULL || wrappers == NULL)
    {
        status = PIGMENT_ERROR_OUT_OF_MEMORY;
        goto FREE;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level              = (VkCommandBufferLevel) level,
        .commandPool        = pool->pool,
        .commandBufferCount = count,
    };

    VkResult result = vkAllocateCommandBuffers(pigment->device->logical_device, &alloc_info, vk_cmds);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to allocate command buffers (count=%u, result: %d)", count, result);
        status = PIGMENT_ERROR_VULKAN;
        goto FREE;
    }
    vk_allocated = P_TRUE;

    for(uint32_t i = 0; i < count; i++)
    {
        wrappers[i] = P_NEW_FOR_OBJECT(pigment, wrappers[i]);
        if(wrappers[i] == NULL)
        {
            status = PIGMENT_ERROR_OUT_OF_MEMORY;
            goto FREE;
        }
        wrappers[i]->uses = P_NEW_ARRAY_FOR_OBJECT(pigment, wrappers[i]->uses, PIGMENT_CMD_USES_INITIAL_CAPACITY);
        if(wrappers[i]->uses == NULL)
        {
            status = PIGMENT_ERROR_OUT_OF_MEMORY;
            goto FREE;
        }
        wrappers[i]->buffer       = vk_cmds[i];
        wrappers[i]->source_pool  = pool;
        wrappers[i]->use_capacity = PIGMENT_CMD_USES_INITIAL_CAPACITY;
        wrappers_done++;

        if(pool_append_buffer(pigment, pool, wrappers[i]) != PIGMENT_SUCCESS)
        {
            status = PIGMENT_ERROR_OUT_OF_MEMORY;
            goto FREE;
        }
        appended++;

        out_cmds[i] = wrappers[i];
    }

    P_STACK_OR_HEAP_FREE(pigment, vk_cmds);
    P_STACK_OR_HEAP_FREE(pigment, wrappers);
    return PIGMENT_SUCCESS;

FREE:
    for(uint32_t i = 0; i < appended; i++)
    {
        pool_remove_buffer(pool, wrappers[i]);
    }
    for(uint32_t i = 0; i < wrappers_done; i++)
    {
        P_FREE(pigment, wrappers[i]->uses);
        P_FREE(pigment, wrappers[i]);
    }
    if(vk_allocated)
    {
        vkFreeCommandBuffers(pigment->device->logical_device, pool->pool, count, vk_cmds);
    }
    P_STACK_OR_HEAP_FREE(pigment, vk_cmds);
    P_STACK_OR_HEAP_FREE(pigment, wrappers);
    return status;
}

void pigment_begin_recording(Pigment* pigment, PCommandBuffer* cmd, PCommandBufferUsage flags, const PCommandBufferInheritance* inheritance)
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
    if(flags & P_CMD_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)
    {
        vk_flags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    }

    VkCommandBufferInheritanceRenderingInfo rendering_inheritance = {0};
    VkCommandBufferInheritanceInfo inheritance_info               = {0};
    uint32_t color_format_count                                   = (inheritance != NULL) ? inheritance->color_format_count : 0;
    P_STACK_OR_HEAP(VkFormat, color_formats, color_format_count);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = vk_flags,
    };

    if(inheritance != NULL)
    {
        if(color_format_count > 0)
        {
            if(color_formats == NULL)
            {
                PLOG_ERROR(pigment, "Failed to allocate color formats for inheritance.");
                return;
            }
            for(uint32_t i = 0; i < color_format_count; i++)
            {
                color_formats[i] = (VkFormat) inheritance->color_formats[i];
            }
        }

        rendering_inheritance = (VkCommandBufferInheritanceRenderingInfo) {
            .sType                   = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
            .viewMask                = inheritance->view_mask,
            .colorAttachmentCount    = inheritance->color_format_count,
            .pColorAttachmentFormats = color_formats,
            .depthAttachmentFormat   = (VkFormat) inheritance->depth_format,
            .stencilAttachmentFormat = (VkFormat) inheritance->stencil_format,
            .rasterizationSamples    = (inheritance->samples == 0) ? VK_SAMPLE_COUNT_1_BIT : (VkSampleCountFlagBits) inheritance->samples,
        };

        inheritance_info = (VkCommandBufferInheritanceInfo) {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
            .pNext = &rendering_inheritance,
        };

        begin_info.pInheritanceInfo = &inheritance_info;
    }

    VkResult result = vkBeginCommandBuffer(cmd->buffer, &begin_info);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to begin command buffer recording (result: %d)", result);
    }

    P_STACK_OR_HEAP_FREE(pigment, color_formats);
}

void pigment_cmd_execute_commands(Pigment* pigment, PCommandBuffer* primary, PCommandBuffer** secondaries, uint32_t count)
{
    if(pigment == NULL || primary == NULL || secondaries == NULL || count == 0)
    {
        return;
    }

    P_STACK_OR_HEAP(VkCommandBuffer, vk_secondaries, count);
    if(vk_secondaries == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        if(secondaries[i] == NULL)
        {
            P_STACK_OR_HEAP_FREE(pigment, vk_secondaries);
            return;
        }
        vk_secondaries[i] = secondaries[i]->buffer;
    }

    vkCmdExecuteCommands(primary->buffer, count, vk_secondaries);
    P_STACK_OR_HEAP_FREE(pigment, vk_secondaries);
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

PSubmitHandle pigment_queue_submit(Pigment* pigment, const PSubmit* submits, uint32_t submit_count)
{
    if(pigment == NULL || submits == NULL || submit_count == 0)
    {
        return (PSubmitHandle) {0};
    }

    PDeviceQueue* graphics = device_find_queue(pigment->device, P_QUEUE_GRAPHICS_BIT, 0);
    if(graphics == NULL || graphics->timeline == VK_NULL_HANDLE)
    {
        PLOG_ERROR(pigment, "Graphics queue timeline unavailable, submit aborted.");
        return (PSubmitHandle) {0};
    }

    uint32_t total_cmd_count = 0;
    for(uint32_t i = 0; i < submit_count; i++)
    {
        if(submits[i].cmds == NULL || submits[i].cmd_count == 0)
        {
            PLOG_ERROR(pigment, "pigment_queue_submit: submits[%u] has no command buffers.", i);
            return (PSubmitHandle) {0};
        }
        total_cmd_count += submits[i].cmd_count;
    }

    P_STACK_OR_HEAP(VkSubmitInfo2, submit_infos, submit_count);
    P_STACK_OR_HEAP(VkCommandBufferSubmitInfo, cmd_infos, total_cmd_count);
    P_STACK_OR_HEAP(VkSemaphoreSubmitInfo, signals, submit_count);
    if(submit_infos == NULL || cmd_infos == NULL || signals == NULL)
    {
        P_STACK_OR_HEAP_FREE(pigment, submit_infos);
        P_STACK_OR_HEAP_FREE(pigment, cmd_infos);
        P_STACK_OR_HEAP_FREE(pigment, signals);
        return (PSubmitHandle) {0};
    }

    uint64_t last_value = 0;
    uint32_t cmd_offset = 0;
    for(uint32_t i = 0; i < submit_count; i++)
    {
        last_value = device_queue_acquire_value(graphics);
        stamp_uses_submit(submits[i].cmds, submits[i].cmd_count, last_value);

        for(uint32_t j = 0; j < submits[i].cmd_count; j++)
        {
            cmd_infos[cmd_offset + j] = (VkCommandBufferSubmitInfo) {
                .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = submits[i].cmds[j]->buffer,
            };
        }

        signals[i] = (VkSemaphoreSubmitInfo) {
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = graphics->timeline,
            .value     = last_value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };

        submit_infos[i] = (VkSubmitInfo2) {
            .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount   = submits[i].cmd_count,
            .pCommandBufferInfos      = &cmd_infos[cmd_offset],
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos    = &signals[i],
        };

        cmd_offset += submits[i].cmd_count;
    }

    VkResult result = vkQueueSubmit2(graphics->queue, submit_count, submit_infos, VK_NULL_HANDLE);

    P_STACK_OR_HEAP_FREE(pigment, submit_infos);
    P_STACK_OR_HEAP_FREE(pigment, cmd_infos);
    P_STACK_OR_HEAP_FREE(pigment, signals);

    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to batch submit command buffers (result: %d)", result);
        return (PSubmitHandle) {0};
    }

    return (PSubmitHandle) {.queue = graphics, .value = last_value};
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

static void destroy_command_buffers_immediate(Pigment* pigment, void* resource)
{
    PCommandBufferBatch* batch = (PCommandBufferBatch*) resource;
    vkFreeCommandBuffers(pigment->device->logical_device, batch->pool->pool, batch->count, batch->vk_buffers);
    for(uint32_t i = 0; i < batch->count; i++)
    {
        P_FREE(pigment, batch->wrappers[i]->uses);
        P_FREE(pigment, batch->wrappers[i]);
    }
    P_FREE(pigment, batch->vk_buffers);
    P_FREE(pigment, batch->wrappers);
    P_FREE(pigment, batch);
}

void pigment_destroy_command_buffers(Pigment* pigment, PCommandBuffer** cmds, uint32_t count)
{
    if(pigment == NULL || cmds == NULL || count == 0)
    {
        return;
    }

    PCommandPool* pool = cmds[0]->source_pool;
    for(uint32_t i = 0; i < count; i++)
    {
        if(cmds[i] == NULL || cmds[i]->source_pool != pool)
        {
            PLOG_ERROR(pigment, "pigment_destroy_command_buffers: all command buffers must come from the same pool.");
            return;
        }
    }

    PCommandBufferBatch* batch = P_NEW_FOR_OBJECT(pigment, batch);
    if(batch == NULL)
    {
        return;
    }

    batch->vk_buffers = P_NEW_ARRAY_FOR_OBJECT(pigment, batch->vk_buffers, count);
    batch->wrappers   = P_NEW_ARRAY_FOR_OBJECT(pigment, batch->wrappers, count);
    if(batch->vk_buffers == NULL || batch->wrappers == NULL)
    {
        P_FREE(pigment, batch->vk_buffers);
        P_FREE(pigment, batch->wrappers);
        P_FREE(pigment, batch);
        return;
    }

    batch->pool  = pool;
    batch->count = count;

    for(uint32_t i = 0; i < count; i++)
    {
        batch->vk_buffers[i] = cmds[i]->buffer;
        batch->wrappers[i]   = cmds[i];
        pool_remove_buffer(pool, cmds[i]);
    }

    pigment_defer_destroy(pigment, destroy_command_buffers_immediate, batch);
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

    command_pool = P_NEW_FOR_OBJECT(pigment, command_pool);
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
        vkDestroyCommandPool(device->logical_device, pool, &pigment->vk_alloc);
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

    VkResult result = vkCreateCommandPool(pigment->device->logical_device, &create_info, &pigment->vk_alloc, &command_pool);
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

static PResult pool_append_buffer(Pigment* pigment, PCommandPool* pool, PCommandBuffer* cmd)
{
    PResult res = P_ARRAY_RESERVE_OBJECT(pigment, pool->buffers, pool->buffer_count, pool->buffer_capacity, 1, PIGMENT_POOL_BUFFERS_INITIAL_CAPACITY);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
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
    if(cmd_track_use(pigment, cmd, tracker) != PIGMENT_SUCCESS)
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

static PResult cmd_track_use(Pigment* pigment, PCommandBuffer* cmd, PResourceTracker* tracker)
{
    PResult res = P_ARRAY_RESERVE_OBJECT(pigment, cmd->uses, cmd->use_count, cmd->use_capacity, 1, PIGMENT_CMD_USES_INITIAL_CAPACITY);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
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
