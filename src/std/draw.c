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
#include "material.h"
#include "lights.h"
#include "bindless.h"
#include "buffers.h"
#include "camera.h"
#include "descriptor.h"
#include "frame.h"
#include "std_internal.h"
#include "internal.h"
#include "log_internal.h"

#include <stdlib.h>
#include <string.h>

struct PInstanceRing {
    PBuffer* buffer;

    uint32_t per_frame_capacity;
    uint32_t cursor;
    uint32_t last_seen_frame;
};

PInstanceRing* pigment_std_create_instance_ring(Pigment* pigment, uint32_t max_instances_per_frame)
{
    if(pigment == NULL || max_instances_per_frame == 0)
    {
        return NULL;
    }

    PInstanceRing* ring = calloc(1, sizeof(*ring));
    if(ring == NULL)
    {
        return NULL;
    }

    uint32_t frame_count = pigment->config.max_frames_in_flight;
    PBufferDesc desc     = {
        .size   = (uint64_t) max_instances_per_frame * frame_count * sizeof(PInstanceData),
        .usage  = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS,
        .memory = P_MEMORY_HOST_VISIBLE,
    };
    ring->buffer = pigment_create_buffer(pigment, &desc);
    if(ring->buffer == NULL)
    {
        PLOG_ERROR(pigment, "Failed to create instance ring buffer (size=%llu)", (unsigned long long) desc.size);
        free(ring);
        return NULL;
    }

    ring->per_frame_capacity = max_instances_per_frame;
    ring->cursor             = 0;
    ring->last_seen_frame    = UINT32_MAX;

    return ring;
}

void pigment_std_destroy_instance_ring(Pigment* pigment, PInstanceRing* ring)
{
    if(pigment == NULL || ring == NULL)
    {
        return;
    }

    pigment_destroy_buffer(pigment, ring->buffer);
    free(ring);
}

void pigment_draw(Pigment* pigment, PStdBindless* bindless, PInstanceRing* ring, PMaterials* materials, PLights* lights, PCamera* camera, uint32_t window_index, PPipeline* pipeline, PDrawCall* draws, uint32_t draw_count)
{
    if(pigment == NULL || bindless == NULL || ring == NULL || pipeline == NULL || draws == NULL || draw_count == 0 || window_index >= pigment->window_count)
    {
        return;
    }

    PWindowRenderer* renderer = pigment->renderers[window_index];
    uint32_t current_frame    = renderer->swapchain->current_frame;

    pigment_cmd_bind_descriptor_set(pigment, window_index, pipeline, 0, pigment_std_bindless_set(pigment, bindless, window_index));

    if(current_frame != ring->last_seen_frame)
    {
        ring->cursor          = 0;
        ring->last_seen_frame = current_frame;
    }

    uint32_t slot_base              = current_frame * ring->per_frame_capacity;
    PInstanceData* instances_mapped = (PInstanceData*) pigment_buffer_mapped(ring->buffer);

    uint64_t camera_slot_address = 0;
    if(camera != NULL)
    {
        pigment_std_camera_upload(camera, current_frame);
        camera_slot_address = (uint64_t) pigment_std_camera_frame_address(camera, current_frame);
    }

    uint64_t material_buffer_address = (uint64_t) pigment_std_material_address(materials);
    uint64_t light_buffer_address    = (uint64_t) pigment_std_light_address(lights);

    for(uint32_t i = 0; i < draw_count; i++)
    {
        PDrawCall* draw_call = &draws[i];
        if(draw_call->mesh == NULL || draw_call->instance_count == 0 || draw_call->instances == NULL)
        {
            continue;
        }
        if(ring->cursor + draw_call->instance_count > ring->per_frame_capacity)
        {
            PLOG_ERROR(pigment, "Instance ring buffer overflow (cursor=%u + instance_count=%u > capacity=%u)", ring->cursor, draw_call->instance_count, ring->per_frame_capacity);
            continue;
        }

        uint32_t first_instance = slot_base + ring->cursor;
        memcpy(&instances_mapped[first_instance], draw_call->instances, draw_call->instance_count * sizeof(PInstanceData));
        ring->cursor += draw_call->instance_count;

        PStdPushConstants push = {
            .vertex_buffer   = pigment_buffer_address(draw_call->mesh->vertex_buffer),
            .instance_buffer = pigment_buffer_address(ring->buffer),
            .camera_buffer   = camera_slot_address,
            .material_buffer = material_buffer_address,
            .light_buffer    = light_buffer_address,
        };

        pigment_cmd_push_constants(pigment, window_index, pipeline, 0, sizeof(push), &push);
        pigment_cmd_draw_indexed(pigment, window_index, draw_call->mesh->index_buffer, draw_call->mesh->index_type, 0, draw_call->first_index, draw_call->index_count, 0, draw_call->instance_count, first_instance);
    }
}
