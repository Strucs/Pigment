/**
 * Copyright 2025 Angel-Leduc TA
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

int create_buffer(VkBuffer* buffer, VkDeviceMemory* buffer_memory, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device);
uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);
void copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size, VkCommandPool command_pool, PDevice* device);
int create_vertex_buffer(PBuffers* buffers, Vertex* vertices, uint32_t vertices_size, PDevice* device, VkCommandPool command_pool);
int create_index_buffer(PBuffers* buffers, const uint32_t* indices, uint32_t indices_size, PDevice* device, VkCommandPool command_pool);
int create_uniform_buffers(PBuffers* buffers, PDevice* device, const uint32_t uniform_buffers_numbers);
VkCommandBuffer start_single_usage_commands(VkCommandPool command_pool, PDevice* device);
void end_single_usage_commands(VkCommandBuffer* command_buffer, VkCommandPool command_pool, PDevice* device);

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for(uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
    {
        if((type_filter & (1 << i)) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    fprintf(stderr, "Failed to find suitable memory type!\n");
    return 0;
}

int create_vertex_buffer(PBuffers* buffers, Vertex* vertices, uint32_t vertices_size, PDevice* device, VkCommandPool command_pool)
{
    VkDeviceSize buffer_size = sizeof(vertices[0]) * vertices_size;

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;

    if(create_buffer(&staging_buffer, &staging_buffer_memory, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    void* data;
    vkMapMemory(device->logical_device, staging_buffer_memory, 0, buffer_size, 0, &data);
    memcpy(data, vertices, (size_t) buffer_size);
    vkUnmapMemory(device->logical_device, staging_buffer_memory);

    if(create_buffer(&buffers->vertex_buffer, &buffers->vertex_buffer_memory, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    copy_buffer(staging_buffer, buffers->vertex_buffer, buffer_size, command_pool, device);

    vkDestroyBuffer(device->logical_device, staging_buffer, NULL);
    vkFreeMemory(device->logical_device, staging_buffer_memory, NULL);

    return PIGMENT_SUCCESS;

ERROR:
    return PIGMENT_ERROR;
}

int create_index_buffer(PBuffers* buffers, const uint32_t* indices, uint32_t indices_size, PDevice* device, VkCommandPool command_pool)
{
    VkDeviceSize buffer_size = sizeof(indices[0]) * indices_size;

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;

    if(create_buffer(&staging_buffer, &staging_buffer_memory, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    void* data;
    vkMapMemory(device->logical_device, staging_buffer_memory, 0, buffer_size, 0, &data);
    memcpy(data, indices, (size_t) buffer_size);
    vkUnmapMemory(device->logical_device, staging_buffer_memory);

    if(create_buffer(&buffers->index_buffer, &buffers->index_buffer_memory, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    copy_buffer(staging_buffer, buffers->index_buffer, buffer_size, command_pool, device);

    vkDestroyBuffer(device->logical_device, staging_buffer, NULL);
    vkFreeMemory(device->logical_device, staging_buffer_memory, NULL);

    return PIGMENT_SUCCESS;

ERROR:
    return PIGMENT_ERROR;
}

int create_uniform_buffers(PBuffers* buffers, PDevice* device, const uint32_t uniform_buffers_numbers)
{
    VkDeviceSize buffer_size = sizeof(UniformBufferObject);

    buffers->uniform_buffers = NULL;
    buffers->uniform_buffers_memory = NULL;
    buffers->uniform_buffers_mapped = NULL;

    buffers->uniform_buffers = malloc(uniform_buffers_numbers * sizeof(*buffers->uniform_buffers));
    if(buffers->uniform_buffers == NULL)
    {
        goto ERROR;
    }

    buffers->uniform_buffers_memory = malloc(uniform_buffers_numbers * sizeof(*buffers->uniform_buffers_memory));
    if(buffers->uniform_buffers_memory == NULL)
    {
        goto ERROR;
    }

    buffers->uniform_buffers_mapped = malloc(uniform_buffers_numbers * sizeof(*buffers->uniform_buffers_mapped));
    if(buffers->uniform_buffers_mapped == NULL)
    {
        goto ERROR;
    }

    for(size_t i = 0; i < uniform_buffers_numbers; i++)
    {
        if(create_buffer(&buffers->uniform_buffers[i], &buffers->uniform_buffers_memory[i], buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, device) != PIGMENT_SUCCESS)
        {
            goto ERROR;
        }

        vkMapMemory(device->logical_device, buffers->uniform_buffers_memory[i], 0, buffer_size, 0, &buffers->uniform_buffers_mapped[i]);
    }

    return PIGMENT_SUCCESS;

ERROR:
    free(buffers->uniform_buffers_mapped);
    buffers->uniform_buffers_mapped = NULL;
    free(buffers->uniform_buffers_memory);
    buffers->uniform_buffers_memory = NULL;
    free(buffers->uniform_buffers);
    buffers->uniform_buffers = NULL;
    return PIGMENT_ERROR;
}

PBuffers* create_buffers(PModel* model, PDevice* device, PCommands* commands, const uint32_t uniform_buffers_numbers)
{
    PBuffers* buffers = calloc(1, sizeof(*buffers));
    if(buffers == NULL)
    {
        goto ERROR;
    }

    buffers->vertices_size = model->vertices_number;
    buffers->indices_size  = model->indices_number;

    if(create_vertex_buffer(buffers, model->vertices, model->vertices_number, device, commands->command_pool) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    if(create_index_buffer(buffers, model->indices, model->indices_number, device, commands->command_pool) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    if(create_uniform_buffers(buffers, device, uniform_buffers_numbers) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    return buffers;

ERROR:
    perror("create_buffers");
    destroy_buffers(buffers, device, uniform_buffers_numbers);
    return NULL;
}

void destroy_buffers(PBuffers* buffers, PDevice* device, const uint32_t uniform_buffers_numbers)
{
    if(buffers != NULL)
    {
        if(buffers->uniform_buffers != NULL)
        {
            for(size_t i = 0; i < uniform_buffers_numbers; i++)
            {
                vkDestroyBuffer(device->logical_device, buffers->uniform_buffers[i], NULL);
            }
        }
        if(buffers->uniform_buffers_memory != NULL)
        {
            for(size_t i = 0; i < uniform_buffers_numbers; i++)
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

        vkDestroyBuffer(device->logical_device, buffers->vertex_buffer, NULL);
        vkFreeMemory(device->logical_device, buffers->vertex_buffer_memory, NULL);

        vkDestroyBuffer(device->logical_device, buffers->index_buffer, NULL);
        vkFreeMemory(device->logical_device, buffers->index_buffer_memory, NULL);

        free(buffers);
    }
}

int create_buffer(VkBuffer* buffer, VkDeviceMemory* buffer_memory, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device)
{
    VkBufferCreateInfo buffer_create_info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if(vkCreateBuffer(device->logical_device, &buffer_create_info, NULL, buffer) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create buffer!");
        goto ERROR;
    }

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device->logical_device, *buffer, &memory_requirements);

    VkMemoryAllocateInfo allocate_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memory_requirements.size,
        .memoryTypeIndex = find_memory_type(device->physical_device, memory_requirements.memoryTypeBits, properties)
    };

    if(vkAllocateMemory(device->logical_device, &allocate_info, NULL, buffer_memory) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to allocate buffer memory!");
        goto ERROR;
    }

    vkBindBufferMemory(device->logical_device, *buffer, *buffer_memory, 0);

    return PIGMENT_SUCCESS;

ERROR:
    vkDestroyBuffer(device->logical_device, *buffer, NULL);
    return PIGMENT_ERROR;
}

void copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size, VkCommandPool command_pool, PDevice* device)
{
    VkCommandBuffer command_buffer = start_single_usage_commands(command_pool, device);

    VkBufferCopy copy_region = {.size = size};
    vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);

    end_single_usage_commands(&command_buffer, command_pool, device);
}

VkCommandBuffer start_single_usage_commands(VkCommandPool command_pool, PDevice* device)
{
    VkCommandBufferAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool        = command_pool,
        .commandBufferCount = 1
    };

    VkCommandBuffer command_buffer;
    vkAllocateCommandBuffers(device->logical_device, &alloc_info, &command_buffer);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    vkBeginCommandBuffer(command_buffer, &begin_info);

    return command_buffer;
}

void end_single_usage_commands(VkCommandBuffer* command_buffer, VkCommandPool command_pool, PDevice* device)
{
    vkEndCommandBuffer(*command_buffer);

    VkSubmitInfo submit_info = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = command_buffer
    };

    vkQueueSubmit(device->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->graphics_queue);

    vkFreeCommandBuffers(device->logical_device, command_pool, 1, command_buffer);
}
