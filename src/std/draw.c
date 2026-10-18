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

void pigment_draw(Pigment* pigment, PCommandBuffer* cmd, PStdBindless* bindless, PInstances* instances, PMaterials* materials, PLights* lights, PCamera* camera, PPipeline* pipeline, PDrawCall* draws, uint32_t draw_count)
{
    if(pigment == NULL || cmd == NULL || bindless == NULL || instances == NULL || instances->instance_size != sizeof(PInstanceData) || pipeline == NULL || draws == NULL || draw_count == 0)
    {
        return;
    }

    PDescriptorSet* bindless_set = pigment_std_bindless_set(pigment, bindless, cmd);
    if(bindless_set == NULL)
    {
        return;
    }
    pigment_cmd_bind_descriptor_sets(pigment, cmd, pipeline, 0, &bindless_set, 1, NULL, 0);

    uint64_t material_buffer_address = pigment_std_materials_address(pigment, materials, cmd);
    uint64_t light_buffer_address    = pigment_std_lights_address(pigment, lights, cmd);
    uint64_t camera_buffer_address   = pigment_std_camera_address(pigment, camera, cmd);
    if((materials != NULL && material_buffer_address == 0)
       || (lights != NULL && light_buffer_address == 0)
       || (camera != NULL && camera_buffer_address == 0))
    {
        return;
    }

    uint64_t instance_count = 0;
    for(uint32_t i = 0; i < draw_count; i++)
    {
        if(draws[i].mesh != NULL && draws[i].instances != NULL)
        {
            instance_count += draws[i].instance_count;
            if(instance_count > UINT32_MAX)
            {
                return;
            }
        }
    }
    if(instance_count == 0)
    {
        return;
    }

    pigment_std_instances_reset(instances);
    PInstanceData* instance_data = pigment_std_instances_alloc(pigment, instances, (uint32_t) instance_count);
    if(instance_data == NULL)
    {
        return;
    }

    uint32_t first_instance = 0;
    for(uint32_t i = 0; i < draw_count; i++)
    {
        PDrawCall* draw_call = &draws[i];
        if(draw_call->mesh == NULL || draw_call->instance_count == 0 || draw_call->instances == NULL)
        {
            continue;
        }

        memcpy(instance_data + first_instance, draw_call->instances, (size_t) draw_call->instance_count * sizeof(*instance_data));
        first_instance += draw_call->instance_count;
    }

    uint64_t instance_buffer_address = pigment_std_instances_address(pigment, instances, cmd);
    if(instance_buffer_address == 0)
    {
        return;
    }

    first_instance = 0;
    for(uint32_t i = 0; i < draw_count; i++)
    {
        PDrawCall* draw_call = &draws[i];
        if(draw_call->mesh == NULL || draw_call->instance_count == 0 || draw_call->instances == NULL)
        {
            continue;
        }

        pigment_std_mesh_use(pigment, cmd, draw_call->mesh);

        PStdPushConstants push = {
            .vertex_buffer   = pigment_buffer_address(draw_call->mesh->vertex_buffer),
            .instance_buffer = instance_buffer_address,
            .camera_buffer   = camera_buffer_address,
            .material_buffer = material_buffer_address,
            .light_buffer    = light_buffer_address,
        };

        pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);
        pigment_cmd_draw_indexed(pigment, cmd, draw_call->mesh->index_buffer, draw_call->mesh->index_type, 0, draw_call->first_index, draw_call->index_count, 0, draw_call->instance_count, first_instance);
        first_instance += draw_call->instance_count;
    }
}

void pigment_std_draw_skybox(Pigment* pigment, PCommandBuffer* cmd, PStdBindless* bindless, PPipeline* pipeline, PCamera* camera, uint32_t cubemap_slot, uint32_t sampler_slot)
{
    if(pigment == NULL || cmd == NULL || bindless == NULL || pipeline == NULL || camera == NULL)
    {
        return;
    }

    PDescriptorSet* bindless_set = pigment_std_bindless_set(pigment, bindless, cmd);
    if(bindless_set == NULL)
    {
        return;
    }

    pigment_cmd_bind_descriptor_sets(pigment, cmd, pipeline, 0, &bindless_set, 1, NULL, 0);

    uint64_t camera_address = pigment_std_camera_address(pigment, camera, cmd);
    if(camera_address == 0)
    {
        return;
    }

    PStdSkyboxPushConstants push = {
        .camera_buffer = camera_address,
        .cubemap_id    = cubemap_slot,
        .sampler_id    = sampler_slot,
    };
    pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);

    pigment_cmd_set_cull(pigment, cmd, P_CULL_MODE_FRONT, P_FRONT_FACE_COUNTER_CLOCKWISE);
    pigment_cmd_set_depth(pigment, cmd, P_TRUE, P_FALSE, P_COMPARE_OP_GREATER_OR_EQUAL);
    pigment_cmd_draw(pigment, cmd, 3, 1, 0, 0);
}

void pigment_std_draw_crt(Pigment* pigment, PCommandBuffer* cmd, PWindowRenderer* renderer, PStdBindless* bindless, PPipeline* pipeline, uint32_t texture_slot, uint32_t sampler_slot, float time)
{
    if(pigment == NULL || cmd == NULL || renderer == NULL || bindless == NULL || pipeline == NULL)
    {
        return;
    }

    uint32_t w = 0;
    uint32_t h = 0;
    pigment_get_swapchain_size(renderer, &w, &h);

    PDescriptorSet* bindless_set = pigment_std_bindless_set(pigment, bindless, cmd);
    if(bindless_set == NULL)
    {
        return;
    }

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
