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
static VkCommandPool create_vk_command_pool(Pigment* pigment, uint32_t queue_family_index, VkCommandPoolCreateFlags flags);
static VkCommandPoolCreateFlags pigment_flags_to_vk(PCommandPoolFlags flags);
static PResult cmd_track_use(Pigment* pigment, PCommandBuffer* cmd, PResourceTracker* tracker);
static void cmd_release_uses(Pigment* pigment, PCommandBuffer* cmd);
static void destroy_resource_tracker_immediate(Pigment* pigment, void* resource);
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

    PDevice* device             = pigment->device;
    PCommandPool* command_pool  = NULL;
    VkCommandPool pool          = NULL;
    uint32_t queue_family_index = UINT32_MAX;

    queue_family_index = desc->queue_family;

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
        cmd_release_uses(pigment, pool->buffers[i]);
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
        cmd_release_uses(pigment, pool->buffers[i]);
    }
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
    else
    {
        cmd_release_uses(pigment, cmd);
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
        if(secondaries[i]->tracking_failed)
        {
            primary->tracking_failed = P_TRUE;
        }
        for(uint32_t j = 0; j < secondaries[i]->use_count; j++)
        {
            pigment_cmd_use(pigment, primary, secondaries[i]->uses[j]);
        }
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

PResult pigment_queue_submit(Pigment* pigment, const PSubmit* submits, uint32_t submit_count, PSubmitHandle* handles_out)
{
    if(handles_out != NULL)
    {
        memset(handles_out, 0, sizeof(*handles_out) * submit_count);
    }

    if(pigment == NULL || submits == NULL || submit_count == 0)
    {
        return PIGMENT_ERROR;
    }

    PDeviceQueue* graphics_default = device_find_queue(pigment->device, P_QUEUE_GRAPHICS_BIT);

    uint32_t total_cmd_count  = 0;
    uint32_t total_wait_count = 0;
    for(uint32_t i = 0; i < submit_count; i++)
    {
        const PSubmit* submit = &submits[i];
        if((submit->cmd_count > 0 && submit->cmds == NULL) || (submit->wait_count > 0 && submit->waits == NULL))
        {
            PLOG_ERROR(pigment, "pigment_queue_submit: submits[%u] has a missing array.", i);
            return PIGMENT_ERROR;
        }

        PDeviceQueue* queue = submits[i].queue != NULL ? submits[i].queue : graphics_default;
        if(queue == NULL || queue->timeline == VK_NULL_HANDLE)
        {
            PLOG_ERROR(pigment, "pigment_queue_submit: submits[%u] target queue unavailable.", i);
            return PIGMENT_ERROR;
        }

        for(uint32_t j = 0; j < submits[i].cmd_count; j++)
        {
            PCommandBuffer* cmd = submits[i].cmds[j];
            if(cmd == NULL || cmd->source_pool == NULL)
            {
                PLOG_ERROR(pigment, "pigment_queue_submit: submits[%u].cmds[%u] is invalid.", i, j);
                return PIGMENT_ERROR;
            }
            if(cmd->tracking_failed)
            {
                PLOG_ERROR(pigment, "pigment_queue_submit: submits[%u].cmds[%u] has incomplete resource tracking.", i, j);
                return PIGMENT_ERROR;
            }
            if(cmd->source_pool->queue_family_index != queue->family_index)
            {
                PLOG_ERROR(pigment, "pigment_queue_submit: submits[%u].cmds[%u] was allocated from a pool tied to queue family %u but is being submitted to a queue of family %u. Submit aborted.", i, j, cmd->source_pool->queue_family_index, queue->family_index);
                return PIGMENT_ERROR;
            }
        }

        for(uint32_t j = 0; j < submit->wait_count; j++)
        {
            PSubmitHandle handle = submit->waits[j].handle;
            if(handle.queue == NULL || handle.queue->timeline == VK_NULL_HANDLE || handle.value == 0)
            {
                PLOG_ERROR(pigment, "pigment_queue_submit: submits[%u].waits[%u] is invalid.", i, j);
                return PIGMENT_ERROR;
            }
        }

        PFrame* frame = submit->frame;
        if(frame != NULL)
        {
            if(!frame->active || graphics_default == NULL || queue->family_index != graphics_default->family_index)
            {
                PLOG_ERROR(pigment, "pigment_queue_submit: submits[%u] requires an active frame and its graphics family.", i);
                return PIGMENT_ERROR;
            }

            PBool acquire_waited = frame->acquire_waited;
            PBool present_ready  = frame->present_ready;
            for(uint32_t j = i; j > 0; j--)
            {
                if(submits[j - 1].frame == frame)
                {
                    acquire_waited = P_TRUE;
                    present_ready  = submits[j - 1].signal_present;
                    break;
                }
            }

            if(present_ready || (submit->wait_acquire && acquire_waited) || (!submit->wait_acquire && !acquire_waited))
            {
                PLOG_ERROR(pigment, "pigment_queue_submit: submits[%u] must wait acquire once and signal present last.", i);
                return PIGMENT_ERROR;
            }

            total_wait_count += submit->wait_acquire ? 1 : 0;
        }
        else if(submit->wait_acquire || submit->signal_present || submit->acquire_stage != P_PIPELINE_STAGE_NONE)
        {
            PLOG_ERROR(pigment, "pigment_queue_submit: submits[%u] has frame settings without a frame.", i);
            return PIGMENT_ERROR;
        }

        total_cmd_count += submits[i].cmd_count;
        total_wait_count += submits[i].wait_count;
    }

    P_STACK_OR_HEAP(VkSubmitInfo2, submit_infos, submit_count);
    P_STACK_OR_HEAP(VkCommandBufferSubmitInfo, cmd_infos, total_cmd_count);
    P_STACK_OR_HEAP(VkSemaphoreSubmitInfo, signals, submit_count * 2);
    P_STACK_OR_HEAP(VkSemaphoreSubmitInfo, waits, total_wait_count == 0 ? 1 : total_wait_count);
    P_STACK_OR_HEAP(PSubmitHandle, handles, submit_count);
    if(submit_infos == NULL || cmd_infos == NULL || signals == NULL || waits == NULL || handles == NULL)
    {
        P_STACK_OR_HEAP_FREE(pigment, submit_infos);
        P_STACK_OR_HEAP_FREE(pigment, cmd_infos);
        P_STACK_OR_HEAP_FREE(pigment, signals);
        P_STACK_OR_HEAP_FREE(pigment, waits);
        P_STACK_OR_HEAP_FREE(pigment, handles);
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    uint32_t cmd_offset  = 0;
    uint32_t wait_offset = 0;
    for(uint32_t i = 0; i < submit_count; i++)
    {
        PDeviceQueue* queue = submits[i].queue != NULL ? submits[i].queue : graphics_default;
        uint64_t this_value = device_queue_acquire_value(queue);
        handles[i]          = (PSubmitHandle) {.queue = queue, .value = this_value};

        for(uint32_t j = 0; j < submits[i].cmd_count; j++)
        {
            cmd_infos[cmd_offset + j] = (VkCommandBufferSubmitInfo) {
                .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = submits[i].cmds[j]->buffer,
            };
        }

        for(uint32_t j = 0; j < submits[i].wait_count; j++)
        {
            const PSubmitWait* wait = &submits[i].waits[j];
            PPipelineStage stage    = (wait->stage != P_PIPELINE_STAGE_NONE) ? wait->stage : P_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            waits[wait_offset + j]  = (VkSemaphoreSubmitInfo) {
                .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = wait->handle.queue->timeline,
                .value     = wait->handle.value,
                .stageMask = pipeline_stage_to_vk(stage),
            };
        }

        uint32_t wait_count = submits[i].wait_count;
        PFrame* frame       = submits[i].frame;
        if(submits[i].wait_acquire)
        {
            PPipelineStage stage              = submits[i].acquire_stage;
            waits[wait_offset + wait_count++] = (VkSemaphoreSubmitInfo) {
                .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = frame->sync->image_available_semaphores[frame->slot],
                .stageMask = stage != P_PIPELINE_STAGE_NONE ? pipeline_stage_to_vk(stage) : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            };
        }

        signals[i * 2] = (VkSemaphoreSubmitInfo) {
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = queue->timeline,
            .value     = this_value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };

        uint32_t signal_count = 1;
        if(submits[i].signal_present)
        {
            signals[i * 2 + signal_count++] = (VkSemaphoreSubmitInfo) {
                .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = frame->sync->render_finished_semaphores[frame->image_index],
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            };
        }

        submit_infos[i] = (VkSubmitInfo2) {
            .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount   = wait_count,
            .pWaitSemaphoreInfos      = wait_count > 0 ? &waits[wait_offset] : NULL,
            .commandBufferInfoCount   = submits[i].cmd_count,
            .pCommandBufferInfos      = &cmd_infos[cmd_offset],
            .signalSemaphoreInfoCount = signal_count,
            .pSignalSemaphoreInfos    = &signals[i * 2],
        };

        cmd_offset += submits[i].cmd_count;
        wait_offset += wait_count;
    }

    // Batch submits by queue to minimize while keeping the order of submits intact to
    // reduce the number of vkQueueSubmit2 calls, which can be expensive.
    VkResult result = VK_SUCCESS;
    uint32_t i      = 0;
    while(i < submit_count)
    {
        PDeviceQueue* queue = handles[i].queue;
        uint32_t end        = i + 1;
        while(end < submit_count && handles[end].queue == queue)
        {
            end++;
        }

        result = vkQueueSubmit2(queue->queue, end - i, &submit_infos[i], VK_NULL_HANDLE);
        if(result != VK_SUCCESS)
        {
            break;
        }

        for(uint32_t j = i; j < end; j++)
        {
            PSubmitHandle handle = handles[j];
            stamp_uses_submit(submits[j].cmds, submits[j].cmd_count, queue->slot, handle.value);
            if(handles_out != NULL)
            {
                handles_out[j] = handle;
            }

            PFrame* frame = submits[j].frame;
            if(frame != NULL)
            {
                frame->acquire_waited = P_TRUE;
                frame->present_ready  = submits[j].signal_present;
                atomic_store_explicit(&frame->sync->per_slot_trackers[frame->slot].last_used[queue->slot], handle.value, memory_order_relaxed);
                atomic_store_explicit(&frame->renderer->tracker.last_used[queue->slot], handle.value, memory_order_relaxed);
            }
        }

        i = end;
    }

    P_STACK_OR_HEAP_FREE(pigment, submit_infos);
    P_STACK_OR_HEAP_FREE(pigment, cmd_infos);
    P_STACK_OR_HEAP_FREE(pigment, signals);
    P_STACK_OR_HEAP_FREE(pigment, waits);
    P_STACK_OR_HEAP_FREE(pigment, handles);

    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to batch submit command buffers (result: %d)", result);
        return PIGMENT_ERROR_VULKAN;
    }

    return PIGMENT_SUCCESS;
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
        cmd_release_uses(pigment, batch->wrappers[i]);
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
        cmd->tracking_failed = P_TRUE;
        PLOG_ERROR(pigment, "pigment_cmd_use: failed to retain resource, recording cannot be submitted.");
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
    for(uint32_t i = 0; i < cmd->use_count; i++)
    {
        if(cmd->uses[i] == tracker)
        {
            return PIGMENT_SUCCESS;
        }
    }

    PResult res = P_ARRAY_RESERVE_OBJECT(pigment, cmd->uses, cmd->use_count, cmd->use_capacity, 1, PIGMENT_CMD_USES_INITIAL_CAPACITY);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
    }

    resource_tracker_retain(tracker);
    cmd->uses[cmd->use_count++] = tracker;
    return PIGMENT_SUCCESS;
}

static void cmd_release_uses(Pigment* pigment, PCommandBuffer* cmd)
{
    for(uint32_t i = 0; i < cmd->use_count; i++)
    {
        resource_tracker_release(pigment, cmd->uses[i]);
    }

    cmd->use_count       = 0;
    cmd->tracking_failed = P_FALSE;
}

void stamp_uses_submit(PCommandBuffer** cmds, uint32_t count, uint32_t queue_slot, uint64_t value)
{
    for(uint32_t i = 0; i < count; i++)
    {
        PCommandBuffer* cmd = cmds[i];
        for(uint32_t j = 0; j < cmd->use_count; j++)
        {
            _Atomic uint64_t* table = cmd->uses[j]->last_used;
            if(table == NULL)
            {
                continue;
            }
            _Atomic uint64_t* slot = &table[queue_slot];
            uint64_t old           = atomic_load_explicit(slot, memory_order_relaxed);
            while(old < value && !atomic_compare_exchange_weak_explicit(slot, &old, value, memory_order_release, memory_order_relaxed))
            {
                // retry
            }
        }
    }
}

PResult pigment_resource_tracker_init(Pigment* pigment, PResourceTracker* tracker)
{
    if(pigment == NULL || tracker == NULL || pigment->device == NULL || pigment->device->queue_count == 0)
    {
        return PIGMENT_ERROR;
    }

    if(tracker->last_used != NULL)
    {
        return PIGMENT_SUCCESS;
    }

    tracker->last_used = P_NEW_ARRAY_FOR_OBJECT(pigment, tracker->last_used, pigment->device->queue_count);
    if(tracker->last_used == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    atomic_init(&tracker->references, 1);
    return PIGMENT_SUCCESS;
}

void pigment_resource_tracker_destroy(Pigment* pigment, PResourceTracker* tracker)
{
    if(pigment == NULL || tracker == NULL || tracker->last_used == NULL)
    {
        return;
    }

    P_FREE(pigment, (void*) tracker->last_used);
    tracker->last_used = NULL;
}

void pigment_resource_tracker_wait(Pigment* pigment, const PResourceTracker* tracker)
{
    if(pigment == NULL || tracker == NULL || pigment->device == NULL || tracker->last_used == NULL)
    {
        return;
    }

    PDevice* device = pigment->device;

    P_STACK_OR_HEAP(VkSemaphore, semaphores, device->queue_count);
    P_STACK_OR_HEAP(uint64_t, values, device->queue_count);
    if(semaphores == NULL || values == NULL)
    {
        P_STACK_OR_HEAP_FREE(pigment, semaphores);
        P_STACK_OR_HEAP_FREE(pigment, values);
        return;
    }

    uint32_t count = 0;
    for(uint32_t q = 0; q < device->queue_count; q++)
    {
        uint64_t value = atomic_load_explicit(&tracker->last_used[q], memory_order_relaxed);
        if(value == 0 || device->queues[q].timeline == VK_NULL_HANDLE)
        {
            continue;
        }

        semaphores[count] = device->queues[q].timeline;
        values[count]     = value;
        count++;
    }

    if(count > 0)
    {
        VkSemaphoreWaitInfo wait_info = {
            .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = count,
            .pSemaphores    = semaphores,
            .pValues        = values,
        };
        vkWaitSemaphores(device->logical_device, &wait_info, UINT64_MAX);
    }

    P_STACK_OR_HEAP_FREE(pigment, semaphores);
    P_STACK_OR_HEAP_FREE(pigment, values);
}

PResourceTracker* pigment_create_resource_tracker(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return NULL;
    }

    PResourceTracker* tracker = P_NEW_FOR_OBJECT(pigment, tracker);
    if(tracker == NULL)
    {
        return NULL;
    }
    tracker->last_used = NULL;

    if(pigment_resource_tracker_init(pigment, tracker) != PIGMENT_SUCCESS)
    {
        P_FREE(pigment, tracker);
        return NULL;
    }

    return tracker;
}

void pigment_destroy_resource_tracker(Pigment* pigment, PResourceTracker* tracker)
{
    if(pigment == NULL || tracker == NULL)
    {
        return;
    }

    if(atomic_load_explicit(&tracker->references, memory_order_acquire) == 0)
    {
        destroy_resource_tracker_immediate(pigment, tracker);
        return;
    }

    pigment_defer_destroy_tracked(pigment, destroy_resource_tracker_immediate, tracker, tracker);
}

static void destroy_resource_tracker_immediate(Pigment* pigment, void* resource)
{
    PResourceTracker* tracker = resource;
    pigment_resource_tracker_destroy(pigment, tracker);
    P_FREE(pigment, tracker);
}

VkCommandBuffer pigment_vk_command_buffer(PCommandBuffer* cmd)
{
    return (cmd != NULL) ? cmd->buffer : VK_NULL_HANDLE;
}

VkCommandPool pigment_vk_command_pool(PCommandPool* pool)
{
    return (pool != NULL) ? pool->pool : VK_NULL_HANDLE;
}
