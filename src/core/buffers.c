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
#include "structs.h"
#include "log_internal.h"

int create_buffer(Pigment* pigment, VkBuffer* buffer, VkDeviceMemory* buffer_memory, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
uint32_t find_memory_type(Pigment* pigment, uint32_t type_filter, VkMemoryPropertyFlags properties);
void copy_buffer(Pigment* pigment, VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size, VkCommandPool command_pool);
int create_vertex_buffer(Pigment* pigment, VkBuffer* buffer, VkDeviceMemory* memory, VkDeviceAddress* address, const void* data, VkDeviceSize size, VkCommandPool command_pool);
int create_index_buffer(Pigment* pigment, VkBuffer* buffer, VkDeviceMemory* memory, const uint32_t* indices, uint32_t index_count, VkCommandPool command_pool);
VkCommandBuffer start_single_usage_commands(Pigment* pigment, VkCommandPool command_pool);
void end_single_usage_commands(Pigment* pigment, VkCommandBuffer* command_buffer, VkCommandPool command_pool);

uint32_t find_memory_type(Pigment* pigment, uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(pigment->device->physical_device, &memory_properties);

    for(uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
    {
        if((type_filter & (1 << i)) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    PLOG_ERROR(pigment, "Failed to find suitable memory type!");
    return 0;
}

int create_vertex_buffer(Pigment* pigment, VkBuffer* buffer, VkDeviceMemory* memory, VkDeviceAddress* address, const void* data, VkDeviceSize size, VkCommandPool command_pool)
{
    PDevice* device                      = pigment->device;
    VkBuffer staging_buffer              = VK_NULL_HANDLE;
    VkDeviceMemory staging_buffer_memory = VK_NULL_HANDLE;

    if(create_buffer(pigment, &staging_buffer, &staging_buffer_memory, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    void* mapped;

    VkResult result;
    if((result = vkMapMemory(device->logical_device, staging_buffer_memory, 0, size, 0, &mapped)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to map vertex staging buffer memory! (result: %d)", result);
        goto ERROR;
    }

    memcpy(mapped, data, (size_t) size);
    vkUnmapMemory(device->logical_device, staging_buffer_memory);

    if(create_buffer(pigment, buffer, memory, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    copy_buffer(pigment, staging_buffer, *buffer, size, command_pool);

    vkDestroyBuffer(device->logical_device, staging_buffer, NULL);
    vkFreeMemory(device->logical_device, staging_buffer_memory, NULL);

    VkBufferDeviceAddressInfo addr_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = *buffer
    };

    *address = vkGetBufferDeviceAddress(device->logical_device, &addr_info);

    return PIGMENT_SUCCESS;

ERROR:
    vkDestroyBuffer(device->logical_device, staging_buffer, NULL);
    vkFreeMemory(device->logical_device, staging_buffer_memory, NULL);
    return PIGMENT_ERROR;
}

int create_index_buffer(Pigment* pigment, VkBuffer* buffer, VkDeviceMemory* memory, const uint32_t* indices, uint32_t index_count, VkCommandPool command_pool)
{
    PDevice* device          = pigment->device;
    VkDeviceSize buffer_size = sizeof(*indices) * index_count;

    VkBuffer staging_buffer              = VK_NULL_HANDLE;
    VkDeviceMemory staging_buffer_memory = VK_NULL_HANDLE;

    if(create_buffer(pigment, &staging_buffer, &staging_buffer_memory, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    void* mapped;

    VkResult result;
    if((result = vkMapMemory(device->logical_device, staging_buffer_memory, 0, buffer_size, 0, &mapped)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to map index staging buffer memory! (result: %d)", result);
        goto ERROR;
    }

    memcpy(mapped, indices, (size_t) buffer_size);
    vkUnmapMemory(device->logical_device, staging_buffer_memory);

    if(create_buffer(pigment, buffer, memory, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    copy_buffer(pigment, staging_buffer, *buffer, buffer_size, command_pool);

    vkDestroyBuffer(device->logical_device, staging_buffer, NULL);
    vkFreeMemory(device->logical_device, staging_buffer_memory, NULL);

    return PIGMENT_SUCCESS;

ERROR:
    vkDestroyBuffer(device->logical_device, staging_buffer, NULL);
    vkFreeMemory(device->logical_device, staging_buffer_memory, NULL);
    return PIGMENT_ERROR;
}

PUniformBuffers* create_uniform_buffers(Pigment* pigment, uint32_t uniform_buffers_count)
{
    PDevice* device          = pigment->device;
    VkDeviceSize buffer_size = sizeof(UniformBufferObject);

    PUniformBuffers* buffers = calloc(1, sizeof(*buffers));
    if(buffers == NULL)
    {
        return NULL;
    }

    buffers->uniform_buffers        = NULL;
    buffers->uniform_buffers_memory = NULL;
    buffers->uniform_buffers_mapped = NULL;

    buffers->uniform_buffers = calloc(uniform_buffers_count, sizeof(*buffers->uniform_buffers));
    if(buffers->uniform_buffers == NULL)
    {
        goto ERROR;
    }

    buffers->uniform_buffers_memory = calloc(uniform_buffers_count, sizeof(*buffers->uniform_buffers_memory));
    if(buffers->uniform_buffers_memory == NULL)
    {
        goto ERROR;
    }

    buffers->uniform_buffers_mapped = malloc(uniform_buffers_count * sizeof(*buffers->uniform_buffers_mapped));
    if(buffers->uniform_buffers_mapped == NULL)
    {
        goto ERROR;
    }

    for(size_t i = 0; i < uniform_buffers_count; i++)
    {
        if(create_buffer(pigment, &buffers->uniform_buffers[i], &buffers->uniform_buffers_memory[i], buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != PIGMENT_SUCCESS)
        {
            goto ERROR;
        }

        VkResult result = vkMapMemory(device->logical_device, buffers->uniform_buffers_memory[i], 0, buffer_size, 0, &buffers->uniform_buffers_mapped[i]);

        if(result != VK_SUCCESS)
        {
            PLOG_ERROR(pigment, "Failed to map uniform buffer memory! (result: %d)", result);
            goto ERROR;
        }
    }

    return buffers;

ERROR:
    if(buffers != NULL)
    {
        if(buffers->uniform_buffers != NULL)
        {
            for(size_t i = 0; i < uniform_buffers_count; i++)
            {
                if(buffers->uniform_buffers[i] != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(device->logical_device, buffers->uniform_buffers[i], NULL);
                }

                if(buffers->uniform_buffers_memory != NULL && buffers->uniform_buffers_memory[i] != VK_NULL_HANDLE)
                {
                    vkFreeMemory(device->logical_device, buffers->uniform_buffers_memory[i], NULL);
                }
            }
        }

        free(buffers->uniform_buffers_mapped);
        free(buffers->uniform_buffers_memory);
        free(buffers->uniform_buffers);
        free(buffers);
    }

    return NULL;
}

void destroy_uniform_buffers(Pigment* pigment, PUniformBuffers* buffers, const uint32_t uniform_buffers_count)
{
    PDevice* device = pigment->device;
    if(buffers != NULL)
    {
        if(buffers->uniform_buffers != NULL)
        {
            for(size_t i = 0; i < uniform_buffers_count; i++)
            {
                vkDestroyBuffer(device->logical_device, buffers->uniform_buffers[i], NULL);
            }
        }
        if(buffers->uniform_buffers_memory != NULL)
        {
            for(size_t i = 0; i < uniform_buffers_count; i++)
            {
                vkFreeMemory(device->logical_device, buffers->uniform_buffers_memory[i], NULL);
            }
        }

        free(buffers->uniform_buffers_mapped);
        buffers->uniform_buffers_mapped = NULL;
        free(buffers->uniform_buffers_memory);
        buffers->uniform_buffers_memory = NULL;
        free(buffers->uniform_buffers);
        buffers->uniform_buffers = NULL;

        free(buffers);
    }
}

int create_buffer(Pigment* pigment, VkBuffer* buffer, VkDeviceMemory* buffer_memory, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    PDevice* device = pigment->device;

    VkBufferCreateInfo buffer_create_info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VkResult result;
    if((result = vkCreateBuffer(device->logical_device, &buffer_create_info, NULL, buffer)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create buffer! (result: %d)", result);
        goto ERROR;
    }

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device->logical_device, *buffer, &memory_requirements);

    VkMemoryAllocateFlagsInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };

    VkMemoryAllocateInfo allocate_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memory_requirements.size,
        .memoryTypeIndex = find_memory_type(pigment, memory_requirements.memoryTypeBits, properties),
        .pNext           = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &flags_info : NULL
    };

    if((result = vkAllocateMemory(device->logical_device, &allocate_info, NULL, buffer_memory)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to allocate buffer memory! (result: %d)", result);
        goto ERROR;
    }

    if((result = vkBindBufferMemory(device->logical_device, *buffer, *buffer_memory, 0)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to bind buffer memory! (result: %d)", result);
        goto ERROR;
    }

    return PIGMENT_SUCCESS;

ERROR:
    vkFreeMemory(device->logical_device, *buffer_memory, NULL);
    vkDestroyBuffer(device->logical_device, *buffer, NULL);
    return PIGMENT_ERROR;
}

void copy_buffer(Pigment* pigment, VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size, VkCommandPool command_pool)
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
