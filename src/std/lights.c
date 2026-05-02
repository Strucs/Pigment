/**
 * Copyright 2026 Angel-Leduc TA
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

#include "lights.h"
#include "frame.h"
#include "std_internal.h"
#include "internal.h"
#include "log_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct PLightsHeader {
    vec3 ambient_color;
    uint32_t count;
} __attribute__((aligned(16))) PLightsHeader;

struct PLights {
    VkBuffer buffer;
    PVkAllocation* allocation;
    VkDeviceAddress address;
    void* mapped;

    uint32_t capacity;
    uint32_t count;

    uint32_t* free_slots;
    uint32_t free_count;
};

static PLightsHeader* lights_header(PLights* lights)
{
    return (PLightsHeader*) lights->mapped;
}

static PLightDesc* lights_data(PLights* lights)
{
    return (PLightDesc*) ((unsigned char*) lights->mapped + sizeof(PLightsHeader));
}

PLights* pigment_std_create_lights(Pigment* pigment, uint32_t max_lights)
{
    if(pigment == NULL || max_lights == 0)
    {
        return NULL;
    }

    PLights* lights = calloc(1, sizeof(*lights));
    if(lights == NULL)
    {
        return NULL;
    }

    lights->free_slots = calloc(max_lights, sizeof(*lights->free_slots));
    if(lights->free_slots == NULL)
    {
        goto ERROR;
    }

    VkDeviceSize size           = (VkDeviceSize) sizeof(PLightsHeader) + (VkDeviceSize) max_lights * sizeof(PLightDesc);
    VkBufferUsageFlags usage    = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if(create_buffer(pigment, &lights->buffer, &lights->allocation, size, usage, props) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create light buffer (size=%llu)", (unsigned long long) size);
        goto ERROR;
    }

    PVkAllocator* alloc = pigment->allocator;
    VkResult result     = alloc->map(alloc->user_data, lights->allocation, &lights->mapped);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to map light buffer (result: %d)", result);
        goto ERROR;
    }

    VkBufferDeviceAddressInfo addr_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = lights->buffer,
    };
    lights->address = vkGetBufferDeviceAddress(pigment->device->logical_device, &addr_info);

    lights->capacity   = max_lights;
    lights->count      = 0;
    lights->free_count = 0;

    PLightsHeader* header    = lights_header(lights);
    header->count            = 0;
    header->ambient_color[0] = 0.05f;
    header->ambient_color[1] = 0.05f;
    header->ambient_color[2] = 0.05f;

    return lights;

ERROR:
    pigment->allocator->destroy_buffer(pigment->allocator->user_data, lights->buffer, lights->allocation);
    free(lights->free_slots);
    free(lights);
    return NULL;
}

void pigment_std_set_ambient(PLights* lights, vec3 color)
{
    if(lights == NULL)
    {
        return;
    }

    PLightsHeader* header    = lights_header(lights);
    header->ambient_color[0] = color[0];
    header->ambient_color[1] = color[1];
    header->ambient_color[2] = color[2];
}

void pigment_std_destroy_lights(Pigment* pigment, PLights* lights)
{
    if(pigment == NULL || lights == NULL)
    {
        return;
    }

    PVkAllocator* alloc = pigment->allocator;
    if(lights->mapped != NULL)
    {
        alloc->unmap(alloc->user_data, lights->allocation);
    }
    alloc->destroy_buffer(alloc->user_data, lights->buffer, lights->allocation);

    free(lights->free_slots);
    free(lights);
}

uint32_t pigment_std_light_create(Pigment* pigment, PLights* lights, const PLightDesc* desc)
{
    if(pigment == NULL || lights == NULL || desc == NULL)
    {
        return UINT32_MAX;
    }

    uint32_t id;
    if(lights->free_count > 0)
    {
        id = lights->free_slots[--lights->free_count];
    }
    else if(lights->count < lights->capacity)
    {
        id                           = lights->count++;
        lights_header(lights)->count = lights->count;
    }
    else
    {
        PLOG_ERROR(pigment, "PLights capacity (%u) reached, cannot create more lights.", lights->capacity);
        return UINT32_MAX;
    }

    lights_data(lights)[id] = *desc;

    return id;
}

void pigment_std_light_update(PLights* lights, uint32_t id, const PLightDesc* desc)
{
    if(lights == NULL || desc == NULL || id >= lights->capacity)
    {
        return;
    }

    lights_data(lights)[id] = *desc;
}

void pigment_std_light_destroy(PLights* lights, uint32_t id)
{
    if(lights == NULL || id >= lights->capacity || lights->free_count >= lights->capacity)
    {
        return;
    }

    lights_data(lights)[id].type             = P_LIGHT_TYPE_INVALID;
    lights->free_slots[lights->free_count++] = id;
}

uint64_t pigment_std_light_address(PLights* lights)
{
    if(lights == NULL)
    {
        return 0;
    }
    return (uint64_t) lights->address;
}

void pigment_std_draw_light_gizmos(Pigment* pigment, uint32_t window_index, PPipeline* pipeline, PLights* lights, PMeshBuffers* sphere_mesh, uint32_t sphere_index_count, float scale)
{
    if(pigment == NULL || pipeline == NULL || lights == NULL || sphere_mesh == NULL || lights->count == 0 || window_index >= pigment->window_count)
    {
        return;
    }

    PWindowRenderer* renderer = pigment->renderers[window_index];
    uint32_t current_frame    = renderer->swapchain->current_frame;

    VkDeviceAddress camera_slot_address = 0;
    if(renderer->current_camera != NULL)
    {
        camera_slot_address = renderer->current_camera->address + (VkDeviceSize) current_frame * 2 * sizeof(mat4);
    }

    PStdGizmoPushConstants push = {
        .vertex_buffer = sphere_mesh->vertex_buffer_address,
        .camera_buffer = camera_slot_address,
        .light_buffer  = lights->address,
        .scale         = scale,
    };

    pigment_cmd_push_constants(pigment, window_index, pipeline, 0, sizeof(push), &push);
    pigment_cmd_draw_indexed(pigment, window_index, sphere_mesh, 0, 0, sphere_index_count, 0, lights->count, 0);
}
