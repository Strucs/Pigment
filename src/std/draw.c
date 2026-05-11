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
#include "pigment.h"
#include "material.h"
#include "lights.h"
#include "bindless.h"
#include "camera.h"
#include "std_internal.h"
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
        .memory = P_MEMORY_HOST_UPLOAD,
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

void pigment_draw(Pigment* pigment, PWindowRenderer* renderer, PStdBindless* bindless, PInstanceRing* ring, PMaterials* materials, PLights* lights, PCamera* camera, PPipeline* pipeline, PDrawCall* draws, uint32_t draw_count)
{
    if(pigment == NULL || renderer == NULL || bindless == NULL || ring == NULL || pipeline == NULL || draws == NULL || draw_count == 0)
    {
        return;
    }

    PCommandBuffer* cmd    = pigment_renderer_frame_cmd(renderer);
    uint32_t current_frame = pigment_renderer_current_frame(renderer);

    PDescriptorSet* bindless_set = pigment_std_bindless_set(pigment, bindless, current_frame);
    pigment_cmd_bind_descriptor_sets(pigment, cmd, pipeline, 0, &bindless_set, 1, NULL, 0);

    if(current_frame != ring->last_seen_frame)
    {
        ring->cursor          = 0;
        ring->last_seen_frame = current_frame;
    }

    uint32_t slot_base              = current_frame * ring->per_frame_capacity;
    uint32_t initial_cursor         = ring->cursor;
    PInstanceData* instances_mapped = (PInstanceData*) pigment_buffer_mapped(ring->buffer);

    uint64_t camera_slot_address = 0;
    if(camera != NULL)
    {
        pigment_std_camera_upload(pigment, camera, current_frame);
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

        pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);
        pigment_cmd_draw_indexed(pigment, cmd, draw_call->mesh->index_buffer, draw_call->mesh->index_type, 0, draw_call->first_index, draw_call->index_count, 0, draw_call->instance_count, first_instance);
    }

    if(ring->cursor > initial_cursor)
    {
        uint64_t flush_offset = (uint64_t) (slot_base + initial_cursor) * sizeof(PInstanceData);
        uint64_t flush_size   = (uint64_t) (ring->cursor - initial_cursor) * sizeof(PInstanceData);
        pigment_buffer_flush(pigment, ring->buffer, flush_offset, flush_size);
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

    PDescriptorSet* bindless_set = pigment_std_bindless_set(pigment, bindless, current_frame);
    pigment_cmd_bind_descriptor_sets(pigment, cmd, pipeline, 0, &bindless_set, 1, NULL, 0);

    pigment_std_camera_upload(pigment, camera, current_frame);

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

    PDescriptorSet* bindless_set = pigment_std_bindless_set(pigment, bindless, current_frame);
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
