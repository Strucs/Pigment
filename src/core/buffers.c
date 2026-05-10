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

#include "buffers.h"
#include "commands.h"
#include "deletion.h"
#include "internal.h"
#include "log_internal.h"

#include <stdlib.h>

static void destroy_buffer_immediate(Pigment* pigment, void* resource);
static VkBufferUsageFlags translate_usage(PBufferUsage usage);

PBuffer* pigment_create_buffer(Pigment* pigment, const PBufferDesc* desc)
{
    if(pigment == NULL || desc == NULL || desc->size == 0)
    {
        return NULL;
    }

    PBuffer* buffer = calloc(1, sizeof(*buffer));
    if(buffer == NULL)
    {
        return NULL;
    }

    VkBufferUsageFlags vk_usage = translate_usage(desc->usage);

    VkBufferCreateInfo buffer_create_info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = (VkDeviceSize) desc->size,
        .usage       = vk_usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    PVkAllocationCreateInfo alloc_info = {
        .debug_name = desc->name,
    };

    if(desc->memory == P_MEMORY_HOST_VISIBLE)
    {
        alloc_info.required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        alloc_info.flags          = P_VK_ALLOCATION_PERSISTENT_MAP_BIT;
    }
    else
    {
        alloc_info.required_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    PVkAllocator* alloc = pigment->allocator;
    VkResult result     = alloc->create_buffer(alloc->user_data, &buffer_create_info, &alloc_info, &buffer->buffer, &buffer->allocation);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create buffer (size=%llu, result=%d)", (unsigned long long) desc->size, result);
        free(buffer);
        return NULL;
    }

    buffer->size = desc->size;

    if(desc->memory == P_MEMORY_HOST_VISIBLE)
    {
        result = alloc->map(alloc->user_data, buffer->allocation, &buffer->mapped);
        if(result != VK_SUCCESS)
        {
            PLOG_ERROR(pigment, "Failed to map buffer (result: %d)", result);
            alloc->destroy_buffer(alloc->user_data, buffer->buffer, buffer->allocation);
            free(buffer);
            return NULL;
        }
    }

    if(desc->usage & P_BUFFER_USAGE_SHADER_ADDRESS)
    {
        VkBufferDeviceAddressInfo addr_info = {
            .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer->buffer,
        };
        buffer->address = vkGetBufferDeviceAddress(pigment->device->logical_device, &addr_info);
    }

    return buffer;
}

void pigment_destroy_buffer(Pigment* pigment, PBuffer* buffer)
{
    if(pigment == NULL || buffer == NULL)
    {
        return;
    }
    pigment_defer_destroy_tracked(pigment, destroy_buffer_immediate, buffer, &buffer->tracker);
}

static void destroy_buffer_immediate(Pigment* pigment, void* resource)
{
    PBuffer* buffer     = (PBuffer*) resource;
    PVkAllocator* alloc = pigment->allocator;
    if(buffer->mapped != NULL)
    {
        alloc->unmap(alloc->user_data, buffer->allocation);
    }
    alloc->destroy_buffer(alloc->user_data, buffer->buffer, buffer->allocation);
    free(buffer);
}

void* pigment_buffer_mapped(PBuffer* buffer)
{
    return (buffer != NULL) ? buffer->mapped : NULL;
}

uint64_t pigment_buffer_address(PBuffer* buffer)
{
    return (buffer != NULL) ? (uint64_t) buffer->address : 0;
}

VkBuffer pigment_vk_buffer(PBuffer* buffer)
{
    return (buffer != NULL) ? buffer->buffer : VK_NULL_HANDLE;
}

PSubmitHandle pigment_buffer_upload(Pigment* pigment, PBuffer* dst, const void* data, uint64_t size, uint64_t offset)
{
    if(pigment == NULL || dst == NULL || data == NULL || size == 0)
    {
        return (PSubmitHandle) {0};
    }

    if(offset + size > dst->size)
    {
        PLOG_ERROR(pigment, "Buffer upload out of range (offset=%llu, size=%llu, buffer size=%llu)", (unsigned long long) offset, (unsigned long long) size, (unsigned long long) dst->size);
        return (PSubmitHandle) {0};
    }

    if(dst->mapped != NULL)
    {
        memcpy((unsigned char*) dst->mapped + offset, data, (size_t) size);
        return (PSubmitHandle) {0};
    }

    PBufferDesc staging_desc = {
        .size   = size,
        .usage  = P_BUFFER_USAGE_TRANSFER_SRC,
        .memory = P_MEMORY_HOST_VISIBLE,
    };

    PBuffer* staging = pigment_create_buffer(pigment, &staging_desc);
    if(staging == NULL)
    {
        return (PSubmitHandle) {0};
    }

    memcpy(staging->mapped, data, (size_t) size);

    PCommandBuffer* cmd = pigment_create_command_buffer(pigment, NULL);
    if(cmd == NULL)
    {
        pigment_destroy_buffer(pigment, staging);
        return (PSubmitHandle) {0};
    }
    pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    pigment_cmd_use_buffer(pigment, cmd, staging);
    pigment_cmd_use_buffer(pigment, cmd, dst);

    VkBufferCopy copy_region = {.srcOffset = 0, .dstOffset = (VkDeviceSize) offset, .size = (VkDeviceSize) size};
    vkCmdCopyBuffer(cmd->buffer, staging->buffer, dst->buffer, 1, &copy_region);
    pigment_end_recording(pigment, cmd);

    PSubmitHandle handle = pigment_queue_submit(pigment, &cmd, 1);

    pigment_destroy_command_buffer(pigment, cmd);
    pigment_destroy_buffer(pigment, staging);

    return handle;
}

static VkBufferUsageFlags translate_usage(PBufferUsage usage)
{
    VkBufferUsageFlags out = 0;
    if(usage & P_BUFFER_USAGE_VERTEX)
    {
        out |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if(usage & P_BUFFER_USAGE_INDEX)
    {
        out |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if(usage & P_BUFFER_USAGE_STORAGE)
    {
        out |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if(usage & P_BUFFER_USAGE_UNIFORM)
    {
        out |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if(usage & P_BUFFER_USAGE_SHADER_ADDRESS)
    {
        out |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    if(usage & P_BUFFER_USAGE_TRANSFER_SRC)
    {
        out |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if(usage & P_BUFFER_USAGE_INDIRECT)
    {
        out |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    if(usage & P_BUFFER_USAGE_TRANSFER_DST)
    {
        out |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    return out;
}
