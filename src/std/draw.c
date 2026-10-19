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

typedef struct PPreparedDraw {
    PMeshBuffers mesh;
    PResourceTracker* vertex_reference;
    PResourceTracker* index_reference;
    uint32_t first_instance;
    uint32_t instance_count;
    uint32_t first_index;
    uint32_t index_count;
} PPreparedDraw;

typedef enum PStdDrawType {
    P_STD_DRAW_MESHES,
    P_STD_DRAW_SKYBOX,
    P_STD_DRAW_CRT,
    P_STD_DRAW_LIGHT_GIZMOS,
} PStdDrawType;

struct PStdDrawBatch {
    PStdDrawType type;
    PStdBindlessVersion* bindless;
    PStdBufferVersion* instances;
    PStdBufferVersion* materials;
    PStdBufferVersion* lights;
    PStdBufferVersion* camera;
    PPreparedDraw* draws;
    uint32_t draw_count;

    union {
        PStdSkyboxPushConstants skybox;
        PStdCrtPushConstants crt;
        float gizmo_scale;
    };
};

static void draw_meshes(Pigment* pigment, PCommandBuffer* cmd, const PStdDrawBatch* batch, PPipeline* pipeline);
static void draw_skybox(Pigment* pigment, PCommandBuffer* cmd, const PStdDrawBatch* batch, PPipeline* pipeline);
static void draw_crt(Pigment* pigment, PCommandBuffer* cmd, const PStdDrawBatch* batch, PPipeline* pipeline);
static void draw_light_gizmos(Pigment* pigment, PCommandBuffer* cmd, const PStdDrawBatch* batch, PPipeline* pipeline);

PStdDrawBatch* pigment_std_prepare_draws(Pigment* pigment, PStdBindless* bindless, PInstances* instances, PMaterials* materials, PLights* lights, PCamera* camera, const PDrawCall* draws, uint32_t draw_count)
{
    if(pigment == NULL || bindless == NULL || instances == NULL || instances->instance_size != sizeof(PInstanceData) || draws == NULL || draw_count == 0)
    {
        return NULL;
    }

    uint64_t count = 0;
    for(uint32_t i = 0; i < draw_count; i++)
    {
        if(draws[i].mesh != NULL && draws[i].instances != NULL)
        {
            count += draws[i].instance_count;
            if(count > UINT32_MAX)
            {
                return NULL;
            }
        }
    }

    if(count == 0)
    {
        return NULL;
    }

    PStdDrawBatch* batch = P_NEW_FOR_OBJECT(pigment, batch);
    if(batch == NULL)
    {
        return NULL;
    }

    batch->type  = P_STD_DRAW_MESHES;
    batch->draws = P_NEW_ARRAY_FOR_OBJECT(pigment, batch->draws, draw_count);
    if(batch->draws == NULL)
    {
        goto ERROR;
    }

    pigment_std_instances_reset(instances);
    PInstanceData* data = pigment_std_instances_alloc(pigment, instances, (uint32_t) count);
    if(data == NULL)
    {
        goto ERROR;
    }
    uint32_t first_instance = 0;
    for(uint32_t i = 0; i < draw_count; i++)
    {
        const PDrawCall* draw = &draws[i];
        if(draw->mesh == NULL || draw->instances == NULL || draw->instance_count == 0)
        {
            continue;
        }

        memcpy(data + first_instance, draw->instances, (size_t) draw->instance_count * sizeof(*data));
        batch->draws[batch->draw_count++] = (PPreparedDraw) {
            .mesh             = *draw->mesh,
            .vertex_reference = pigment_retain_resource((PResource) {.type = P_RESOURCE_TYPE_BUFFER, .buffer = draw->mesh->vertex_buffer}),
            .index_reference  = pigment_retain_resource((PResource) {.type = P_RESOURCE_TYPE_BUFFER, .buffer = draw->mesh->index_buffer}),
            .first_instance   = first_instance,
            .instance_count   = draw->instance_count,
            .first_index      = draw->first_index,
            .index_count      = draw->index_count,
        };
        first_instance += draw->instance_count;
    }

    batch->bindless  = pigment_std_bindless_prepare(pigment, bindless);
    batch->instances = pigment_std_instances_prepare(pigment, instances);
    batch->materials = pigment_std_materials_prepare(pigment, materials);
    batch->lights    = pigment_std_lights_prepare(pigment, lights);
    batch->camera    = pigment_std_camera_prepare(pigment, camera);

    if(batch->bindless == NULL || batch->instances == NULL
       || (materials != NULL && batch->materials == NULL)
       || (lights != NULL && batch->lights == NULL)
       || (camera != NULL && batch->camera == NULL))
    {
        goto ERROR;
    }

    return batch;

ERROR:
    pigment_std_release_draws(pigment, batch);
    return NULL;
}

PStdDrawBatch* pigment_std_prepare_skybox(Pigment* pigment, PStdBindless* bindless, PCamera* camera, uint32_t cubemap_slot, uint32_t sampler_slot)
{
    if(pigment == NULL || bindless == NULL || camera == NULL)
    {
        return NULL;
    }

    PStdDrawBatch* batch = P_NEW_FOR_OBJECT(pigment, batch);
    if(batch == NULL)
    {
        return NULL;
    }

    batch->type     = P_STD_DRAW_SKYBOX;
    batch->bindless = pigment_std_bindless_prepare(pigment, bindless);
    batch->camera   = pigment_std_camera_prepare(pigment, camera);
    batch->skybox   = (PStdSkyboxPushConstants) {
        .cubemap_id = cubemap_slot,
        .sampler_id = sampler_slot,
    };

    if(batch->bindless == NULL || batch->camera == NULL)
    {
        goto ERROR;
    }

    return batch;

ERROR:
    pigment_std_release_draws(pigment, batch);
    return NULL;
}

PStdDrawBatch* pigment_std_prepare_crt(Pigment* pigment, PStdBindless* bindless, uint32_t width, uint32_t height, uint32_t texture_slot, uint32_t sampler_slot, float time)
{
    if(pigment == NULL || bindless == NULL)
    {
        return NULL;
    }

    PStdDrawBatch* batch = P_NEW_FOR_OBJECT(pigment, batch);
    if(batch == NULL)
    {
        return NULL;
    }

    batch->type     = P_STD_DRAW_CRT;
    batch->bindless = pigment_std_bindless_prepare(pigment, bindless);
    batch->crt      = (PStdCrtPushConstants) {
        .texture_id   = texture_slot,
        .sampler_id   = sampler_slot,
        .time         = time,
        .aspect       = height == 0 ? 1.0f : (float) width / (float) height,
        .resolution_x = (float) width,
        .resolution_y = (float) height,
    };

    if(batch->bindless == NULL)
    {
        goto ERROR;
    }

    return batch;

ERROR:
    pigment_std_release_draws(pigment, batch);
    return NULL;
}

PStdDrawBatch* pigment_std_prepare_light_gizmos(Pigment* pigment, PCamera* camera, PLights* lights, const PMeshBuffers* sphere_mesh, uint32_t sphere_index_count, float scale)
{
    if(pigment == NULL
       || lights == NULL || sphere_mesh == NULL || sphere_mesh->vertex_buffer == NULL
       || sphere_mesh->index_buffer == NULL || sphere_index_count == 0)
    {
        return NULL;
    }

    PStdDrawBatch* batch = P_NEW_FOR_OBJECT(pigment, batch);
    if(batch == NULL)
    {
        return NULL;
    }

    batch->type        = P_STD_DRAW_LIGHT_GIZMOS;
    batch->lights      = pigment_std_lights_prepare(pigment, lights);
    batch->camera      = pigment_std_camera_prepare(pigment, camera);
    batch->gizmo_scale = scale;

    if(batch->lights == NULL || pigment_std_buffer_version_count(batch->lights) == 0
       || (camera != NULL && batch->camera == NULL))
    {
        goto ERROR;
    }

    batch->draws = P_NEW_ARRAY_FOR_OBJECT(pigment, batch->draws, 1);
    if(batch->draws == NULL)
    {
        goto ERROR;
    }

    batch->draws[0] = (PPreparedDraw) {
        .mesh             = *sphere_mesh,
        .vertex_reference = pigment_retain_resource((PResource) {.type = P_RESOURCE_TYPE_BUFFER, .buffer = sphere_mesh->vertex_buffer}),
        .index_reference  = pigment_retain_resource((PResource) {.type = P_RESOURCE_TYPE_BUFFER, .buffer = sphere_mesh->index_buffer}),
        .instance_count   = pigment_std_buffer_version_count(batch->lights),
        .index_count      = sphere_index_count,
    };
    batch->draw_count = 1;

    return batch;

ERROR:
    pigment_std_release_draws(pigment, batch);
    return NULL;
}

void pigment_std_release_draws(Pigment* pigment, PStdDrawBatch* batch)
{
    if(pigment == NULL || batch == NULL)
    {
        return;
    }

    pigment_std_release_bindless_version(pigment, batch->bindless);
    pigment_std_release_buffer_version(pigment, batch->instances);
    pigment_std_release_buffer_version(pigment, batch->materials);
    pigment_std_release_buffer_version(pigment, batch->lights);
    pigment_std_release_buffer_version(pigment, batch->camera);

    for(uint32_t i = 0; i < batch->draw_count; i++)
    {
        pigment_release_resource(pigment, batch->draws[i].vertex_reference);
        pigment_release_resource(pigment, batch->draws[i].index_reference);
    }

    P_FREE(pigment, batch->draws);
    P_FREE(pigment, batch);
}

void pigment_std_draw(Pigment* pigment, PCommandBuffer* cmd, const PStdDrawBatch* batch, PPipeline* pipeline)
{
    if(pigment == NULL || cmd == NULL || batch == NULL || pipeline == NULL)
    {
        return;
    }

    if(batch->bindless != NULL)
    {
        PDescriptorSet* set = pigment_std_bindless_version_set(pigment, batch->bindless, cmd);
        pigment_cmd_bind_descriptor_sets(pigment, cmd, pipeline, 0, &set, 1, NULL, 0);
    }

    switch(batch->type)
    {
        case P_STD_DRAW_MESHES:
            draw_meshes(pigment, cmd, batch, pipeline);
            break;
        case P_STD_DRAW_SKYBOX:
            draw_skybox(pigment, cmd, batch, pipeline);
            break;
        case P_STD_DRAW_CRT:
            draw_crt(pigment, cmd, batch, pipeline);
            break;
        case P_STD_DRAW_LIGHT_GIZMOS:
            draw_light_gizmos(pigment, cmd, batch, pipeline);
            break;
    }
}

static void draw_meshes(Pigment* pigment, PCommandBuffer* cmd, const PStdDrawBatch* batch, PPipeline* pipeline)
{
    PStdPushConstants push = {
        .instance_buffer = pigment_std_buffer_version_address(pigment, batch->instances, cmd),
        .camera_buffer   = pigment_std_buffer_version_address(pigment, batch->camera, cmd),
        .material_buffer = pigment_std_buffer_version_address(pigment, batch->materials, cmd),
        .light_buffer    = pigment_std_buffer_version_address(pigment, batch->lights, cmd),
    };

    for(uint32_t i = 0; i < batch->draw_count; i++)
    {
        const PPreparedDraw* draw = &batch->draws[i];
        pigment_cmd_use(pigment, cmd, draw->vertex_reference);
        pigment_cmd_use(pigment, cmd, draw->index_reference);
        push.vertex_buffer = pigment_buffer_address(draw->mesh.vertex_buffer);

        pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);
        pigment_cmd_draw_indexed(pigment, cmd, draw->mesh.index_buffer, draw->mesh.index_type, 0, draw->first_index, draw->index_count, 0, draw->instance_count, draw->first_instance);
    }
}

static void draw_skybox(Pigment* pigment, PCommandBuffer* cmd, const PStdDrawBatch* batch, PPipeline* pipeline)
{
    PStdSkyboxPushConstants push = batch->skybox;
    push.camera_buffer           = pigment_std_buffer_version_address(pigment, batch->camera, cmd);

    pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);
    pigment_cmd_set_cull(pigment, cmd, P_CULL_MODE_FRONT, P_FRONT_FACE_COUNTER_CLOCKWISE);
    pigment_cmd_set_depth(pigment, cmd, P_TRUE, P_FALSE, P_COMPARE_OP_GREATER_OR_EQUAL);
    pigment_cmd_draw(pigment, cmd, 3, 1, 0, 0);
}

static void draw_crt(Pigment* pigment, PCommandBuffer* cmd, const PStdDrawBatch* batch, PPipeline* pipeline)
{
    pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(batch->crt), &batch->crt);
    pigment_cmd_set_cull(pigment, cmd, P_CULL_MODE_NONE, P_FRONT_FACE_COUNTER_CLOCKWISE);
    pigment_cmd_set_depth(pigment, cmd, P_FALSE, P_FALSE, P_COMPARE_OP_ALWAYS);
    pigment_cmd_draw(pigment, cmd, 3, 1, 0, 0);
}

static void draw_light_gizmos(Pigment* pigment, PCommandBuffer* cmd, const PStdDrawBatch* batch, PPipeline* pipeline)
{
    const PPreparedDraw* draw = &batch->draws[0];
    pigment_cmd_use(pigment, cmd, draw->vertex_reference);
    pigment_cmd_use(pigment, cmd, draw->index_reference);

    PStdGizmoPushConstants push = {
        .vertex_buffer = pigment_buffer_address(draw->mesh.vertex_buffer),
        .camera_buffer = pigment_std_buffer_version_address(pigment, batch->camera, cmd),
        .light_buffer  = pigment_std_buffer_version_address(pigment, batch->lights, cmd),
        .scale         = batch->gizmo_scale,
    };

    pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);
    pigment_cmd_draw_indexed(pigment, cmd, draw->mesh.index_buffer, draw->mesh.index_type, 0, 0, draw->index_count, 0, draw->instance_count, 0);
}
