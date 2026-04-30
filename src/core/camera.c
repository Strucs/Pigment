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

#include "camera.h"
#include "internal.h"
#include "log_internal.h"

PCamera* pigment_create_camera(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return NULL;
    }

    PCamera* camera = calloc(1, sizeof(*camera));
    if(camera == NULL)
    {
        return NULL;
    }

    camera->data = (PCameraData) {.view = MAT4_IDENTITY, .projection = MAT4_IDENTITY};

    VkDeviceSize size           = (VkDeviceSize) pigment->config.max_frames_in_flight * sizeof(PCameraData);
    VkBufferUsageFlags usage    = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if(create_buffer(pigment, &camera->buffer, &camera->allocation, size, usage, props) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create camera buffer (size=%llu)", (unsigned long long) size);
        goto ERROR;
    }

    PVkAllocator* alloc = pigment->allocator;
    VkResult result     = alloc->map(alloc->user_data, camera->allocation, &camera->mapped);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to map camera buffer (result: %d)", result);
        goto ERROR;
    }

    VkBufferDeviceAddressInfo addr_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = camera->buffer,
    };
    camera->address = vkGetBufferDeviceAddress(pigment->device->logical_device, &addr_info);

    return camera;

ERROR:
    pigment->allocator->destroy_buffer(pigment->allocator->user_data, camera->buffer, camera->allocation);
    free(camera);
    return NULL;
}

void pigment_destroy_camera(Pigment* pigment, PCamera* camera)
{
    if(pigment == NULL || camera == NULL)
    {
        return;
    }

    PVkAllocator* alloc = pigment->allocator;
    if(camera->mapped != NULL)
    {
        alloc->unmap(alloc->user_data, camera->allocation);
    }
    alloc->destroy_buffer(alloc->user_data, camera->buffer, camera->allocation);

    free(camera);
}

void pigment_camera_set_view(PCamera* camera, mat4 view)
{
    if(camera == NULL)
    {
        return;
    }
    memcpy(camera->data.view, view, sizeof(mat4));
}

void pigment_camera_set_projection(PCamera* camera, mat4 projection)
{
    if(camera == NULL)
    {
        return;
    }
    memcpy(camera->data.projection, projection, sizeof(mat4));
}
