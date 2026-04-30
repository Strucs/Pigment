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

#include "draw.h"
#include "frame.h"
#include "internal.h"
#include "log_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct PInstanceRing {
    VkBuffer buffer;
    PVkAllocation* allocation;
    VkDeviceAddress address;
    void* mapped;
} PInstanceRing;

struct PStdDrawState {
    PInstanceRing transforms;

    uint32_t per_frame_capacity;
    uint32_t cursor;
    uint32_t last_seen_frame;
};

static int init_instance_ring(Pigment* pigment, PInstanceRing* ring, VkDeviceSize size)
{
    VkBufferUsageFlags usage    = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if(create_buffer(pigment, &ring->buffer, &ring->allocation, size, usage, props) != PIGMENT_SUCCESS)
    {
        return PIGMENT_ERROR;
    }

    PVkAllocator* alloc = pigment->allocator;
    VkResult result     = alloc->map(alloc->user_data, ring->allocation, &ring->mapped);
    if(result != VK_SUCCESS)
    {
        return PIGMENT_ERROR;
    }

    VkBufferDeviceAddressInfo addr_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = ring->buffer,
    };
    ring->address = vkGetBufferDeviceAddress(pigment->device->logical_device, &addr_info);

    return PIGMENT_SUCCESS;
}

static void destroy_instance_ring(Pigment* pigment, PInstanceRing* ring)
{
    PVkAllocator* alloc = pigment->allocator;
    if(ring->mapped != NULL)
    {
        alloc->unmap(alloc->user_data, ring->allocation);
    }
    alloc->destroy_buffer(alloc->user_data, ring->buffer, ring->allocation);
}

PStdDrawState* pigment_std_draw_init(Pigment* pigment, uint32_t max_transforms_per_frame)
{
    if(pigment == NULL || max_transforms_per_frame == 0)
    {
        return NULL;
    }

    PStdDrawState* state = calloc(1, sizeof(*state));
    if(state == NULL)
    {
        return NULL;
    }

    uint32_t frame_count = pigment->config.max_frames_in_flight;
    VkDeviceSize size    = (VkDeviceSize) max_transforms_per_frame * frame_count * sizeof(mat4);

    if(init_instance_ring(pigment, &state->transforms, size) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to init transform ring (size=%llu)", (unsigned long long) size);
        goto ERROR;
    }

    state->per_frame_capacity = max_transforms_per_frame;
    state->cursor             = 0;
    state->last_seen_frame    = UINT32_MAX;

    return state;

ERROR:
    destroy_instance_ring(pigment, &state->transforms);
    free(state);
    return NULL;
}

void pigment_std_draw_shutdown(Pigment* pigment, PStdDrawState* state)
{
    if(pigment == NULL || state == NULL)
    {
        return;
    }

    destroy_instance_ring(pigment, &state->transforms);
    free(state);
}

void pigment_draw(Pigment* pigment, PStdDrawState* state, uint32_t window_index, PPipeline* pipeline, PDrawCall* draws, uint32_t draw_count)
{
    if(pigment == NULL || state == NULL || pipeline == NULL || draws == NULL || draw_count == 0 || window_index >= pigment->window_count)
    {
        return;
    }

    PWindowRenderer* renderer = pigment->renderers[window_index];
    uint32_t current_frame    = renderer->swapchain->current_frame;

    if(current_frame != state->last_seen_frame)
    {
        state->cursor          = 0;
        state->last_seen_frame = current_frame;
    }

    uint32_t slot_base      = current_frame * state->per_frame_capacity;
    mat4* transforms_mapped = (mat4*) state->transforms.mapped;

    VkDeviceAddress camera_slot_address = 0;
    if(renderer->current_camera != NULL)
    {
        camera_slot_address = renderer->current_camera->address + (VkDeviceSize) current_frame * 2 * sizeof(mat4);
    }

    for(uint32_t i = 0; i < draw_count; i++)
    {
        PDrawCall* draw_call = &draws[i];
        if(draw_call->mesh == NULL || draw_call->instance_count == 0 || draw_call->transforms == NULL)
        {
            continue;
        }
        if(state->cursor + draw_call->instance_count > state->per_frame_capacity)
        {
            PLOG_ERROR(pigment, "Transform ring buffer overflow (cursor=%u + instance_count=%u > capacity=%u)", state->cursor, draw_call->instance_count, state->per_frame_capacity);
            continue;
        }

        uint32_t first_instance = slot_base + state->cursor;
        memcpy(&transforms_mapped[first_instance], draw_call->transforms, draw_call->instance_count * sizeof(mat4));
        state->cursor += draw_call->instance_count;

        PDrawPushConstants push = {
            .vertex_buffer    = draw_call->mesh->vertex_buffer_address,
            .transform_buffer = state->transforms.address,
            .camera_buffer    = camera_slot_address,
            .image_index      = draw_call->image_index,
            .sampler_index    = draw_call->sampler_index,
        };

        pigment_cmd_push_constants(pigment, window_index, pipeline, 0, sizeof(push), &push);
        pigment_cmd_draw_indexed(pigment, window_index, draw_call->mesh, 0, draw_call->first_index, draw_call->index_count, 0, draw_call->instance_count, first_instance);
    }
}
