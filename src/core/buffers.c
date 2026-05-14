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

static void destroy_buffer_immediate(Pigment* pigment, void* resource);
static VkBufferUsageFlags translate_usage(PBufferUsage usage);

PBuffer* pigment_create_buffer(Pigment* pigment, const PBufferDesc* desc)
{
    if(pigment == NULL || desc == NULL || desc->size == 0)
    {
        return NULL;
    }

    PBuffer* buffer = P_NEW_FOR_OBJECT(pigment, buffer);
    if(buffer == NULL)
    {
        return NULL;
    }

    if(pigment_resource_tracker_init(pigment, &buffer->tracker) != PIGMENT_SUCCESS)
    {
        P_FREE(pigment, buffer);
        return NULL;
    }

    VkBufferUsageFlags vk_usage = translate_usage(desc->usage);

    VkBufferCreateInfo buffer_create_info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = (VkDeviceSize) desc->size,
        .usage       = vk_usage,
        .sharingMode = (VkSharingMode) desc->sharing_mode,
    };

    PVkAllocationFlags vk_alloc_flags = 0;
    if(desc->memory.required & P_MEMORY_HOST_VISIBLE_BIT)
    {
        vk_alloc_flags |= P_VK_ALLOCATION_PERSISTENT_MAP_BIT;
    }
    if(desc->flags & P_BUFFER_DEDICATED_BIT)
    {
        vk_alloc_flags |= P_VK_ALLOCATION_DEDICATED_BIT;
    }

    PVkAllocationCreateInfo alloc_info = {
        .debug_name      = desc->name,
        .required_flags  = (VkMemoryPropertyFlags) desc->memory.required,
        .preferred_flags = (VkMemoryPropertyFlags) desc->memory.preferred,
        .flags           = vk_alloc_flags,
    };

    PVkAllocator* alloc = pigment->gpu_allocator;
    VkResult result     = alloc->create_buffer(alloc->user_data, &buffer_create_info, &alloc_info, &buffer->buffer, &buffer->allocation);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create buffer (size=%llu, result=%d)", (unsigned long long) desc->size, result);
        pigment_resource_tracker_destroy(pigment, &buffer->tracker);
        P_FREE(pigment, buffer);
        return NULL;
    }

    buffer->size         = desc->size;
    buffer->memory_flags = alloc->get_memory_flags(alloc->user_data, buffer->allocation);

    if(buffer->memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        result = alloc->map(alloc->user_data, buffer->allocation, &buffer->mapped);
        if(result != VK_SUCCESS)
        {
            PLOG_ERROR(pigment, "Failed to map buffer (result: %d)", result);
            alloc->destroy_buffer(alloc->user_data, buffer->buffer, buffer->allocation);
            pigment_resource_tracker_destroy(pigment, &buffer->tracker);
            P_FREE(pigment, buffer);
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
    PVkAllocator* alloc = pigment->gpu_allocator;
    if(buffer->mapped != NULL)
    {
        alloc->unmap(alloc->user_data, buffer->allocation);
    }
    alloc->destroy_buffer(alloc->user_data, buffer->buffer, buffer->allocation);
    pigment_resource_tracker_destroy(pigment, &buffer->tracker);
    P_FREE(pigment, buffer);
}

void* pigment_buffer_mapped(PBuffer* buffer)
{
    return (buffer != NULL) ? buffer->mapped : NULL;
}

uint64_t pigment_buffer_address(PBuffer* buffer)
{
    return (buffer != NULL) ? (uint64_t) buffer->address : 0;
}

uint64_t pigment_buffer_size(PBuffer* buffer)
{
    return (buffer != NULL) ? buffer->size : 0;
}

void pigment_buffer_flush(Pigment* pigment, PBuffer* buffer, uint64_t offset, uint64_t size)
{
    if(pigment == NULL || buffer == NULL || buffer->mapped == NULL)
    {
        return;
    }

    if(buffer->memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    {
        return;
    }

    PVkAllocator* alloc = pigment->gpu_allocator;
    alloc->flush(alloc->user_data, buffer->allocation, (VkDeviceSize) offset, (size == 0) ? VK_WHOLE_SIZE : (VkDeviceSize) size);
}

void pigment_buffer_invalidate(Pigment* pigment, PBuffer* buffer, uint64_t offset, uint64_t size)
{
    if(pigment == NULL || buffer == NULL || buffer->mapped == NULL)
    {
        return;
    }

    if(buffer->memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    {
        return;
    }

    PVkAllocator* alloc = pigment->gpu_allocator;
    alloc->invalidate(alloc->user_data, buffer->allocation, (VkDeviceSize) offset, (size == 0) ? VK_WHOLE_SIZE : (VkDeviceSize) size);
}

VkBuffer pigment_vk_buffer(PBuffer* buffer)
{
    return (buffer != NULL) ? buffer->buffer : VK_NULL_HANDLE;
}

VkDeviceAddress pigment_vk_buffer_address(PBuffer* buffer)
{
    return (buffer != NULL) ? buffer->address : 0;
}

void pigment_cmd_copy_buffer(Pigment* pigment, PCommandBuffer* cmd, PBuffer* src, PBuffer* dst, const PBufferCopy* regions, uint32_t region_count)
{
    if(pigment == NULL || cmd == NULL || src == NULL || dst == NULL || regions == NULL || region_count == 0)
    {
        return;
    }

    pigment_cmd_use_buffer(pigment, cmd, src);
    pigment_cmd_use_buffer(pigment, cmd, dst);

    P_STACK_OR_HEAP(VkBufferCopy, vk_regions, region_count);
    if(vk_regions == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < region_count; i++)
    {
        vk_regions[i] = (VkBufferCopy) {
            .srcOffset = (VkDeviceSize) regions[i].src_offset,
            .dstOffset = (VkDeviceSize) regions[i].dst_offset,
            .size      = (VkDeviceSize) regions[i].size,
        };
    }

    vkCmdCopyBuffer(cmd->buffer, src->buffer, dst->buffer, region_count, vk_regions);
    P_STACK_OR_HEAP_FREE(pigment, vk_regions);
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
