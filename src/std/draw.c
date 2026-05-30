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

#include "bindless.h"
#include "camera.h"
#include "lights.h"
#include "material.h"
#include "std_internal.h"

#include "pigment/pigment.h"

#include <string.h>

struct PInstanceRing {
    PBuffer* buffer;

    uint32_t instance_size;
    uint32_t per_frame_capacity;
    uint32_t cursor;
    uint32_t last_seen_frame;
    PBool overflow_warned;    // anti-spam for logging when the ring is full and instances are dropped
};

PInstanceRing* pigment_std_create_instance_ring(Pigment* pigment, uint32_t instance_size, uint32_t max_instances_per_frame)
{
    if(pigment == NULL || instance_size == 0 || max_instances_per_frame == 0)
    {
        return NULL;
    }

    PInstanceRing* ring = P_NEW_FOR_OBJECT(pigment, ring);
    if(ring == NULL)
    {
        return NULL;
    }

    uint32_t frame_count = pigment_max_frames_in_flight(pigment);
    PBufferDesc desc     = {
        .size   = (uint64_t) instance_size * max_instances_per_frame * frame_count,
        .usage  = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS,
        .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT | P_MEMORY_DEVICE_LOCAL_BIT},
    };
    ring->buffer = pigment_create_buffer(pigment, &desc);
    if(ring->buffer == NULL)
    {
        PLOG_ERROR(pigment, "Failed to create instance ring buffer (size=%llu)", (unsigned long long) desc.size);
        P_FREE(pigment, ring);
        return NULL;
    }

    ring->instance_size      = instance_size;
    ring->per_frame_capacity = max_instances_per_frame;
    ring->cursor             = 0;
    ring->last_seen_frame    = UINT32_MAX;
    ring->overflow_warned    = P_FALSE;

    return ring;
}

void pigment_std_destroy_instance_ring(Pigment* pigment, PInstanceRing* ring)
{
    if(pigment == NULL || ring == NULL)
    {
        return;
    }

    pigment_destroy_buffer(pigment, ring->buffer);
    P_FREE(pigment, ring);
}

void pigment_std_instance_ring_sync_frame(PInstanceRing* ring, uint32_t current_frame)
{
    if(ring == NULL)
    {
        return;
    }

    if(current_frame != ring->last_seen_frame)
    {
        ring->cursor          = 0;
        ring->last_seen_frame = current_frame;
        ring->overflow_warned = P_FALSE;
    }
}

void pigment_std_instance_ring_use(Pigment* pigment, PCommandBuffer* cmd, PInstanceRing* ring)
{
    if(pigment == NULL || cmd == NULL || ring == NULL || ring->buffer == NULL)
    {
        return;
    }
    pigment_cmd_use_buffer(pigment, cmd, ring->buffer);
}

uint32_t pigment_std_instance_ring_cursor(PInstanceRing* ring)
{
    return (ring != NULL) ? ring->cursor : 0;
}

void* pigment_std_instance_ring_alloc(Pigment* pigment, PInstanceRing* ring, uint32_t instance_count)
{
    if(pigment == NULL || ring == NULL || instance_count == 0)
    {
        return NULL;
    }

    if(ring->cursor + instance_count > ring->per_frame_capacity)
    {
        if(!ring->overflow_warned)
        {
            PLOG_WARN(pigment, "Instance ring full: %u + %u exceeds per-frame capacity %u. Dropping instances this frame (increase max_instances_per_frame).", ring->cursor, instance_count, ring->per_frame_capacity);
            ring->overflow_warned = P_TRUE;
        }
        return NULL;
    }

    uint8_t* mapped = (uint8_t*) pigment_buffer_mapped(ring->buffer);
    if(mapped == NULL)
    {
        return NULL;
    }

    uint64_t slot_offset = (uint64_t) ring->last_seen_frame * ring->per_frame_capacity * ring->instance_size;
    uint64_t offset      = slot_offset + (uint64_t) ring->cursor * ring->instance_size;

    ring->cursor += instance_count;

    return mapped + offset;
}

uint64_t pigment_std_instance_ring_frame_address(PInstanceRing* ring)
{
    if(ring == NULL)
    {
        return 0;
    }

    uint64_t base_address = pigment_buffer_address(ring->buffer);
    uint64_t slot_offset  = (uint64_t) ring->last_seen_frame * ring->per_frame_capacity * ring->instance_size;

    return base_address + slot_offset;
}

void pigment_std_instance_ring_flush_range(Pigment* pigment, PInstanceRing* ring, uint32_t first_instance, uint32_t instance_count)
{
    if(pigment == NULL || ring == NULL || instance_count == 0)
    {
        return;
    }

    uint64_t slot_offset = (uint64_t) ring->last_seen_frame * ring->per_frame_capacity * ring->instance_size;
    uint64_t offset      = slot_offset + (uint64_t) first_instance * ring->instance_size;
    uint64_t size        = (uint64_t) instance_count * ring->instance_size;
    pigment_buffer_flush(pigment, ring->buffer, offset, size);
}

void pigment_draw(Pigment* pigment, PWindowRenderer* renderer, PStdBindless* bindless, PInstanceRing* ring, PMaterials* materials, PLights* lights, PCamera* camera, PPipeline* pipeline, PDrawCall* draws, uint32_t draw_count)
{
    if(pigment == NULL || renderer == NULL || bindless == NULL || ring == NULL || pipeline == NULL || draws == NULL || draw_count == 0)
    {
        return;
    }

    PCommandBuffer* cmd    = pigment_renderer_frame_cmd(renderer);
    uint32_t current_frame = pigment_renderer_current_frame(renderer);

    PDescriptorSet* bindless_set = pigment_std_bindless_set(pigment, bindless, cmd, current_frame);
    pigment_cmd_bind_descriptor_sets(pigment, cmd, pipeline, 0, &bindless_set, 1, NULL, 0);

    pigment_std_instance_ring_sync_frame(ring, current_frame);
    pigment_std_instance_ring_use(pigment, cmd, ring);
    pigment_std_materials_use(pigment, cmd, materials);
    pigment_std_lights_use(pigment, cmd, lights);

    uint64_t camera_slot_address = 0;
    if(camera != NULL)
    {
        pigment_std_camera_upload(pigment, camera, current_frame);
        pigment_std_camera_use(pigment, cmd, camera);
        camera_slot_address = (uint64_t) pigment_std_camera_frame_address(camera, current_frame);
    }

    uint64_t material_buffer_address = (uint64_t) pigment_std_material_address(materials);
    uint64_t light_buffer_address    = (uint64_t) pigment_std_light_address(lights);
    uint64_t instance_buffer_address = pigment_std_instance_ring_frame_address(ring);
    uint32_t initial_cursor          = pigment_std_instance_ring_cursor(ring);

    for(uint32_t i = 0; i < draw_count; i++)
    {
        PDrawCall* draw_call = &draws[i];
        if(draw_call->mesh == NULL || draw_call->instance_count == 0 || draw_call->instances == NULL)
        {
            continue;
        }

        uint32_t first_instance = pigment_std_instance_ring_cursor(ring);
        void* dst               = pigment_std_instance_ring_alloc(pigment, ring, draw_call->instance_count);
        if(dst == NULL)
        {
            continue;
        }
        memcpy(dst, draw_call->instances, (size_t) draw_call->instance_count * sizeof(PInstanceData));

        pigment_std_mesh_use(pigment, cmd, draw_call->mesh);

        PStdPushConstants push = {
            .vertex_buffer   = pigment_buffer_address(draw_call->mesh->vertex_buffer),
            .instance_buffer = instance_buffer_address,
            .camera_buffer   = camera_slot_address,
            .material_buffer = material_buffer_address,
            .light_buffer    = light_buffer_address,
        };

        pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);
        pigment_cmd_draw_indexed(pigment, cmd, draw_call->mesh->index_buffer, draw_call->mesh->index_type, 0, draw_call->first_index, draw_call->index_count, 0, draw_call->instance_count, first_instance);
    }

    uint32_t final_cursor = pigment_std_instance_ring_cursor(ring);
    if(final_cursor > initial_cursor)
    {
        pigment_std_instance_ring_flush_range(pigment, ring, initial_cursor, final_cursor - initial_cursor);
    }
}

void pigment_std_draw_skybox(Pigment* pigment, PWindowRenderer* renderer, PStdBindless* bindless, PPipeline* pipeline, PCamera* camera, uint32_t cubemap_slot, uint32_t sampler_slot)
{
    if(pigment == NULL || renderer == NULL || bindless == NULL || pipeline == NULL || camera == NULL)
    {
        return;
    }

    PCommandBuffer* cmd    = pigment_renderer_frame_cmd(renderer);
    uint32_t current_frame = pigment_renderer_current_frame(renderer);

    PDescriptorSet* bindless_set = pigment_std_bindless_set(pigment, bindless, cmd, current_frame);
    pigment_cmd_bind_descriptor_sets(pigment, cmd, pipeline, 0, &bindless_set, 1, NULL, 0);

    pigment_std_camera_upload(pigment, camera, current_frame);
    pigment_std_camera_use(pigment, cmd, camera);

    PStdSkyboxPushConstants push = {
        .camera_buffer = (uint64_t) pigment_std_camera_frame_address(camera, current_frame),
        .cubemap_id    = cubemap_slot,
        .sampler_id    = sampler_slot,
    };
    pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);

    pigment_cmd_set_cull(pigment, cmd, P_CULL_MODE_FRONT, P_FRONT_FACE_COUNTER_CLOCKWISE);
    pigment_cmd_set_depth(pigment, cmd, P_TRUE, P_FALSE, P_COMPARE_OP_GREATER_OR_EQUAL);
    pigment_cmd_draw(pigment, cmd, 3, 1, 0, 0);
}

void pigment_std_draw_crt(Pigment* pigment, PWindowRenderer* renderer, PStdBindless* bindless, PPipeline* pipeline, uint32_t texture_slot, uint32_t sampler_slot, float time)
{
    if(pigment == NULL || renderer == NULL || bindless == NULL || pipeline == NULL)
    {
        return;
    }

    PCommandBuffer* cmd    = pigment_renderer_frame_cmd(renderer);
    uint32_t current_frame = pigment_renderer_current_frame(renderer);

    uint32_t w = 0;
    uint32_t h = 0;
    pigment_get_swapchain_size(renderer, &w, &h);

    PDescriptorSet* bindless_set = pigment_std_bindless_set(pigment, bindless, cmd, current_frame);
    pigment_cmd_bind_descriptor_sets(pigment, cmd, pipeline, 0, &bindless_set, 1, NULL, 0);

    PStdCrtPushConstants push = {
        .texture_id   = texture_slot,
        .sampler_id   = sampler_slot,
        .time         = time,
        .aspect       = (h == 0) ? 1.0f : (float) w / (float) h,
        .resolution_x = (float) w,
        .resolution_y = (float) h,
    };
    pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);

    pigment_cmd_set_cull(pigment, cmd, P_CULL_MODE_NONE, P_FRONT_FACE_COUNTER_CLOCKWISE);
    pigment_cmd_set_depth(pigment, cmd, P_FALSE, P_FALSE, P_COMPARE_OP_ALWAYS);
    pigment_cmd_draw(pigment, cmd, 3, 1, 0, 0);
}
