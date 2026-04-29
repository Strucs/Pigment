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

static void copy_buffer(Pigment* pigment, VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size, VkCommandPool command_pool);

int create_buffer(Pigment* pigment, VkBuffer* buffer, PVkAllocation** allocation, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    VkBufferCreateInfo buffer_create_info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VkResult result = pigment->allocator->create_buffer(pigment->allocator->user_data, &buffer_create_info, properties, buffer, allocation);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create buffer (size=%llu, result=%d)", (unsigned long long) size, result);
        return PIGMENT_ERROR;
    }
    return PIGMENT_SUCCESS;
}

int create_vertex_buffer(Pigment* pigment, VkBuffer* buffer, PVkAllocation** allocation, VkDeviceAddress* address, const void* data, VkDeviceSize size, VkCommandPool command_pool)
{
    PVkAllocator* alloc               = pigment->allocator;
    VkBuffer staging_buffer           = VK_NULL_HANDLE;
    PVkAllocation* staging_allocation = NULL;

    if(create_buffer(pigment, &staging_buffer, &staging_allocation, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    void* mapped = NULL;
    VkResult result;
    if((result = alloc->map(alloc->user_data, staging_allocation, &mapped)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to map vertex staging buffer memory! (result: %d)", result);
        goto ERROR;
    }

    memcpy(mapped, data, (size_t) size);
    alloc->unmap(alloc->user_data, staging_allocation);

    if(create_buffer(pigment, buffer, allocation, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    copy_buffer(pigment, staging_buffer, *buffer, size, command_pool);

    alloc->destroy_buffer(alloc->user_data, staging_buffer, staging_allocation);

    VkBufferDeviceAddressInfo addr_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = *buffer
    };

    *address = vkGetBufferDeviceAddress(pigment->device->logical_device, &addr_info);

    return PIGMENT_SUCCESS;

ERROR:
    alloc->destroy_buffer(alloc->user_data, staging_buffer, staging_allocation);
    return PIGMENT_ERROR;
}

int create_index_buffer(Pigment* pigment, VkBuffer* buffer, PVkAllocation** allocation, const uint32_t* indices, uint32_t index_count, VkCommandPool command_pool)
{
    PVkAllocator* alloc               = pigment->allocator;
    VkDeviceSize buffer_size          = sizeof(*indices) * index_count;
    VkBuffer staging_buffer           = VK_NULL_HANDLE;
    PVkAllocation* staging_allocation = NULL;

    if(create_buffer(pigment, &staging_buffer, &staging_allocation, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    void* mapped = NULL;
    VkResult result;
    if((result = alloc->map(alloc->user_data, staging_allocation, &mapped)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to map index staging buffer memory! (result: %d)", result);
        goto ERROR;
    }

    memcpy(mapped, indices, (size_t) buffer_size);
    alloc->unmap(alloc->user_data, staging_allocation);

    if(create_buffer(pigment, buffer, allocation, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    copy_buffer(pigment, staging_buffer, *buffer, buffer_size, command_pool);

    alloc->destroy_buffer(alloc->user_data, staging_buffer, staging_allocation);

    return PIGMENT_SUCCESS;

ERROR:
    alloc->destroy_buffer(alloc->user_data, staging_buffer, staging_allocation);
    return PIGMENT_ERROR;
}

PUniformBuffers* create_uniform_buffers(Pigment* pigment, uint32_t uniform_buffers_count)
{
    PVkAllocator* alloc      = pigment->allocator;
    VkDeviceSize buffer_size = sizeof(UniformBufferObject);

    PUniformBuffers* buffers = calloc(1, sizeof(*buffers));
    if(buffers == NULL)
    {
        return NULL;
    }

    buffers->uniform_buffers             = calloc(uniform_buffers_count, sizeof(*buffers->uniform_buffers));
    buffers->uniform_buffers_allocations = calloc(uniform_buffers_count, sizeof(*buffers->uniform_buffers_allocations));
    buffers->uniform_buffers_mapped      = calloc(uniform_buffers_count, sizeof(*buffers->uniform_buffers_mapped));

    if(buffers->uniform_buffers == NULL || buffers->uniform_buffers_allocations == NULL || buffers->uniform_buffers_mapped == NULL)
    {
        goto ERROR;
    }

    for(size_t i = 0; i < uniform_buffers_count; i++)
    {
        if(create_buffer(pigment, &buffers->uniform_buffers[i], &buffers->uniform_buffers_allocations[i], buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != PIGMENT_SUCCESS)
        {
            goto ERROR;
        }

        VkResult result = alloc->map(alloc->user_data, buffers->uniform_buffers_allocations[i], &buffers->uniform_buffers_mapped[i]);
        if(result != VK_SUCCESS)
        {
            PLOG_ERROR(pigment, "Failed to map uniform buffer memory! (result: %d)", result);
            goto ERROR;
        }
    }

    return buffers;

ERROR:
    destroy_uniform_buffers(pigment, buffers, uniform_buffers_count);
    return NULL;
}

void destroy_uniform_buffers(Pigment* pigment, PUniformBuffers* buffers, const uint32_t uniform_buffers_count)
{
    if(buffers == NULL)
    {
        return;
    }

    PVkAllocator* alloc = pigment->allocator;

    if(buffers->uniform_buffers != NULL && buffers->uniform_buffers_allocations != NULL)
    {
        for(size_t i = 0; i < uniform_buffers_count; i++)
        {
            if(buffers->uniform_buffers_allocations[i] != NULL)
            {
                alloc->unmap(alloc->user_data, buffers->uniform_buffers_allocations[i]);
            }
            alloc->destroy_buffer(alloc->user_data, buffers->uniform_buffers[i], buffers->uniform_buffers_allocations[i]);
        }
    }

    free(buffers->uniform_buffers_mapped);
    free(buffers->uniform_buffers_allocations);
    free(buffers->uniform_buffers);
    free(buffers);
}

static void copy_buffer(Pigment* pigment, VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size, VkCommandPool command_pool)
{
    VkCommandBuffer command_buffer = start_single_usage_commands(pigment, command_pool);

    VkBufferCopy copy_region = {.size = size};
    vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);

    end_single_usage_commands(pigment, &command_buffer, command_pool);
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
