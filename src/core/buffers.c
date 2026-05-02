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
#include "internal.h"
#include "log_internal.h"

#include <stdlib.h>

static void copy_buffer(Pigment* pigment, VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize src_offset, VkDeviceSize dst_offset, VkDeviceSize size, VkCommandPool command_pool);
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

    VkBufferUsageFlags vk_usage    = translate_usage(desc->usage);
    VkMemoryPropertyFlags vk_props = (desc->memory == P_MEMORY_HOST_VISIBLE) ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                                                             : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkBufferCreateInfo buffer_create_info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = (VkDeviceSize) desc->size,
        .usage       = vk_usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    PVkAllocator* alloc = pigment->allocator;
    VkResult result     = alloc->create_buffer(alloc->user_data, &buffer_create_info, vk_props, &buffer->buffer, &buffer->allocation);
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

void pigment_buffer_upload(Pigment* pigment, PBuffer* dst, const void* data, uint64_t size, uint64_t offset)
{
    if(pigment == NULL || dst == NULL || data == NULL || size == 0)
    {
        return;
    }

    if(offset + size > dst->size)
    {
        PLOG_ERROR(pigment, "Buffer upload out of range (offset=%llu, size=%llu, buffer size=%llu)", (unsigned long long) offset, (unsigned long long) size, (unsigned long long) dst->size);
        return;
    }

    if(dst->mapped != NULL)
    {
        memcpy((unsigned char*) dst->mapped + offset, data, (size_t) size);
        return;
    }

    PCommandPool* pool = pigment_default_pool(pigment);
    if(pool == NULL)
    {
        return;
    }

    PBufferDesc staging_desc = {
        .size   = size,
        .usage  = P_BUFFER_USAGE_TRANSFER_SRC,
        .memory = P_MEMORY_HOST_VISIBLE,
    };

    PBuffer* staging = pigment_create_buffer(pigment, &staging_desc);
    if(staging == NULL)
    {
        return;
    }

    memcpy(staging->mapped, data, (size_t) size);

    copy_buffer(pigment, staging->buffer, dst->buffer, 0, (VkDeviceSize) offset, (VkDeviceSize) size, pool->pool);

    pigment_destroy_buffer(pigment, staging);
}

VkCommandBuffer start_single_usage_commands(Pigment* pigment, VkCommandPool command_pool)
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

void end_single_usage_commands(Pigment* pigment, VkCommandBuffer* command_buffer, VkCommandPool command_pool)
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

    if((result = vkQueueSubmit(device->graphics_queue, 1, &submit_info, VK_NULL_HANDLE)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to submit single usage command buffer! (result: %d)", result);
    }

    vkQueueWaitIdle(device->graphics_queue);

    vkFreeCommandBuffers(device->logical_device, command_pool, 1, command_buffer);
}

static void copy_buffer(Pigment* pigment, VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize src_offset, VkDeviceSize dst_offset, VkDeviceSize size, VkCommandPool command_pool)
{
    VkCommandBuffer command_buffer = start_single_usage_commands(pigment, command_pool);

    VkBufferCopy copy_region = {.srcOffset = src_offset, .dstOffset = dst_offset, .size = size};
    vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);

    end_single_usage_commands(pigment, &command_buffer, command_pool);
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
    if(usage & P_BUFFER_USAGE_TRANSFER_DST)
    {
        out |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    return out;
}
