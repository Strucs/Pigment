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

#include "camera.h"

#include "pigment/pigment.h"

#include "internal.h"
#include "std_internal.h"

#include <stddef.h>

typedef struct PLightsHeader {
    vec3 ambient_color;
    uint32_t count;
} __attribute__((aligned(16))) PLightsHeader;

struct PLights {
    PBuffer* buffer;

    uint32_t capacity;
    uint32_t count;

    uint32_t* free_slots;
    uint32_t free_count;
};

static PLightsHeader* lights_header(PLights* lights)
{
    return (PLightsHeader*) pigment_buffer_mapped(lights->buffer);
}

static PLightDesc* lights_data(PLights* lights)
{
    return (PLightDesc*) ((unsigned char*) pigment_buffer_mapped(lights->buffer) + sizeof(PLightsHeader));
}

PLights* pigment_std_create_lights(Pigment* pigment, uint32_t max_lights)
{
    if(pigment == NULL || max_lights == 0)
    {
        return NULL;
    }

    PLights* lights = P_NEW_FOR_OBJECT(pigment, lights);
    if(lights == NULL)
    {
        return NULL;
    }

    lights->free_slots = P_NEW_ARRAY_FOR_OBJECT(pigment, lights->free_slots, max_lights);
    if(lights->free_slots == NULL)
    {
        goto ERROR;
    }

    PBufferDesc desc = {
        .size   = (uint64_t) sizeof(PLightsHeader) + (uint64_t) max_lights * sizeof(PLightDesc),
        .usage  = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS,
        .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT | P_MEMORY_DEVICE_LOCAL_BIT},
    };
    lights->buffer = pigment_create_buffer(pigment, &desc);
    if(lights->buffer == NULL)
    {
        PLOG_ERROR(pigment, "Failed to create light buffer (size=%llu)", (unsigned long long) desc.size);
        goto ERROR;
    }

    lights->capacity   = max_lights;
    lights->count      = 0;
    lights->free_count = 0;

    PLightsHeader* header    = lights_header(lights);
    header->count            = 0;
    header->ambient_color[0] = 0.05f;
    header->ambient_color[1] = 0.05f;
    header->ambient_color[2] = 0.05f;
    pigment_buffer_flush(pigment, lights->buffer, 0, sizeof(PLightsHeader));

    return lights;

ERROR:
    pigment_destroy_buffer(pigment, lights->buffer);
    P_FREE(pigment, lights->free_slots);
    P_FREE(pigment, lights);
    return NULL;
}

void pigment_std_set_ambient(Pigment* pigment, PLights* lights, vec3 color)
{
    if(pigment == NULL || lights == NULL)
    {
        return;
    }

    PLightsHeader* header    = lights_header(lights);
    header->ambient_color[0] = color[0];
    header->ambient_color[1] = color[1];
    header->ambient_color[2] = color[2];
    pigment_buffer_flush(pigment, lights->buffer, offsetof(PLightsHeader, ambient_color), sizeof(header->ambient_color));
}

void pigment_std_destroy_lights(Pigment* pigment, PLights* lights)
{
    if(pigment == NULL || lights == NULL)
    {
        return;
    }

    pigment_destroy_buffer(pigment, lights->buffer);
    P_FREE(pigment, lights->free_slots);
    P_FREE(pigment, lights);
}

uint32_t pigment_std_light_create(Pigment* pigment, PLights* lights, const PLightDesc* desc)
{
    if(pigment == NULL || lights == NULL || desc == NULL)
    {
        return UINT32_MAX;
    }

    uint32_t id;
    PBool count_changed = P_FALSE;
    if(lights->free_count > 0)
    {
        id = lights->free_slots[--lights->free_count];
    }
    else if(lights->count < lights->capacity)
    {
        id                           = lights->count++;
        lights_header(lights)->count = lights->count;
        count_changed                = P_TRUE;
    }
    else
    {
        PLOG_ERROR(pigment, "PLights capacity (%u) reached, cannot create more lights.", lights->capacity);
        return UINT32_MAX;
    }

    lights_data(lights)[id] = *desc;

    if(count_changed)
    {
        pigment_buffer_flush(pigment, lights->buffer, offsetof(PLightsHeader, count), sizeof(lights_header(lights)->count));
    }
    pigment_buffer_flush(pigment, lights->buffer, sizeof(PLightsHeader) + (uint64_t) id * sizeof(PLightDesc), sizeof(PLightDesc));

    return id;
}

void pigment_std_light_update(Pigment* pigment, PLights* lights, uint32_t id, const PLightDesc* desc)
{
    if(pigment == NULL || lights == NULL || desc == NULL || id >= lights->capacity)
    {
        return;
    }

    lights_data(lights)[id] = *desc;
    pigment_buffer_flush(pigment, lights->buffer, sizeof(PLightsHeader) + (uint64_t) id * sizeof(PLightDesc), sizeof(PLightDesc));
}

void pigment_std_light_destroy(Pigment* pigment, PLights* lights, uint32_t id)
{
    if(pigment == NULL || lights == NULL || id >= lights->capacity || lights->free_count >= lights->capacity)
    {
        return;
    }

    lights_data(lights)[id].type             = P_LIGHT_TYPE_INVALID;
    lights->free_slots[lights->free_count++] = id;
    pigment_buffer_flush(pigment, lights->buffer, sizeof(PLightsHeader) + (uint64_t) id * sizeof(PLightDesc) + offsetof(PLightDesc, type), sizeof(lights_data(lights)[id].type));
}

uint64_t pigment_std_light_address(PLights* lights)
{
    if(lights == NULL)
    {
        return 0;
    }
    return (uint64_t) pigment_buffer_address(lights->buffer);
}

void pigment_std_draw_light_gizmos(Pigment* pigment, PWindowRenderer* renderer, PPipeline* pipeline, PCamera* camera, PLights* lights, PMeshBuffers* sphere_mesh, uint32_t sphere_index_count, float scale)
{
    if(pigment == NULL || renderer == NULL || pipeline == NULL || lights == NULL || sphere_mesh == NULL || lights->count == 0)
    {
        return;
    }

    PCommandBuffer* cmd    = pigment_renderer_frame_cmd(renderer);
    uint32_t current_frame = pigment_renderer_current_frame(renderer);

    uint64_t camera_slot_address = 0;
    if(camera != NULL)
    {
        pigment_std_camera_upload(pigment, camera, current_frame);
        camera_slot_address = (uint64_t) pigment_std_camera_frame_address(camera, current_frame);
    }

    PStdGizmoPushConstants push = {
        .vertex_buffer = pigment_buffer_address(sphere_mesh->vertex_buffer),
        .camera_buffer = camera_slot_address,
        .light_buffer  = pigment_buffer_address(lights->buffer),
        .scale         = scale,
    };

    pigment_cmd_push_constants(pigment, cmd, pipeline, 0, sizeof(push), &push);
    pigment_cmd_draw_indexed(pigment, cmd, sphere_mesh->index_buffer, sphere_mesh->index_type, 0, 0, sphere_index_count, 0, lights->count, 0);
}
