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

#include "bindless.h"
#include "internal.h"
#include "pigment.h"
#include "std_internal.h"

#include <math.h>
#include <string.h>

typedef struct PImageList {
    PImage** images;
    uint32_t count;
    uint32_t capacity;
} PImageList;

typedef struct PSamplerList {
    PSampler** samplers;
    PSamplerDesc* descs;
    uint32_t count;
    uint32_t capacity;
} PSamplerList;

typedef struct PTrackedRT {
    PRenderTarget* rt;
    uint32_t first_slot;
    uint32_t color_count;
    uint32_t last_seen_generation;
} PTrackedRT;

typedef struct PTrackedRTList {
    PTrackedRT* targets;
    uint32_t count;
    uint32_t capacity;
} PTrackedRTList;

struct PStdBindless {
    PDescriptorSetLayout* layout;
    PDescriptorPool* pool;
    PCommandPool* upload_pool;
    PDescriptorSet** sets;
    uint32_t set_count;

    PImageList images;
    PImageList cubemaps;
    PImageList render_targets;
    PSamplerList samplers;
    PTrackedRTList tracked_rts;

    PStdPipelineLayouts* pipeline_layouts;
};

#define PIGMENT_BINDLESS_BINDING_SAMPLERS 0
#define PIGMENT_BINDLESS_BINDING_CUBEMAPS 1
#define PIGMENT_BINDLESS_BINDING_RENDER_TARGETS 2
#define PIGMENT_BINDLESS_BINDING_IMAGES 3

static PResult image_list_append(Pigment* pigment, PImageList* image_list, PImage* image);
static PResult prepare_layered_image_upload(Pigment* pigment, PImage** out_image, PBuffer** out_staging, const unsigned char* const* layer_data, uint32_t width, uint32_t height, uint32_t layer_count, PFormat format, PImageType type);
static void record_image_upload(Pigment* pigment, PCommandBuffer* cmd, PImage* image, PBuffer* staging, uint32_t width, uint32_t height, uint32_t layer_count, uint64_t layer_size_bytes);
static PResult batch_record_uploads(Pigment* pigment, PCommandBuffer* cmd, PImage** out_images, PBuffer** stagings, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count);
static uint32_t batch_append_images(Pigment* pigment, PImageList* list, PImage** images, uint32_t count);
static PResult add_image_from_pixels(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format);
static PResult add_default_image(Pigment* pigment, PStdBindless* bindless);
static PResult sampler_list_init(Pigment* pigment, PSamplerList* sampler_list, uint32_t max_samplers);
static void sampler_list_destroy(Pigment* pigment, PSamplerList* sampler_list);
static void image_list_destroy(Pigment* pigment, PImageList* image_list, PBool owns_images);
static void write_sampler_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PSampler* sampler);
static void write_cubemap_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PImage* image);
static void write_image_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PImage* image);
static void write_render_target_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PImage* image);
static void batch_write_descriptors(Pigment* pigment, PStdBindless* bindless, uint32_t start_slot, uint32_t count);
static PResult tracked_rt_list_append(Pigment* pigment, PTrackedRTList* list, PTrackedRT entry);
static void sync_tracked_rts(Pigment* pigment, PStdBindless* bindless);

static inline int imax(int a, int b)
{
    return a > b ? a : b;
}

PStdBindless* pigment_std_create_bindless(Pigment* pigment, uint32_t max_images, uint32_t max_samplers, uint32_t max_cubemaps, uint32_t max_render_targets)
{
    if(pigment == NULL || max_images == 0 || max_samplers == 0)
    {
        return NULL;
    }

    PStdBindless* bindless = P_NEW_FOR_OBJECT(pigment, bindless);
    if(bindless == NULL)
    {
        return NULL;
    }

    PDescriptorBinding bindings[] = {
        {
         .binding = PIGMENT_BINDLESS_BINDING_SAMPLERS,
         .type    = P_DESCRIPTOR_TYPE_SAMPLER,
         .count   = max_samplers,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
         },
        {
         .binding = PIGMENT_BINDLESS_BINDING_CUBEMAPS,
         .type    = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count   = (max_cubemaps == 0) ? 1 : max_cubemaps,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
         },
        {
         .binding = PIGMENT_BINDLESS_BINDING_RENDER_TARGETS,
         .type    = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count   = (max_render_targets == 0) ? 1 : max_render_targets,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
         },
        {
         .binding = PIGMENT_BINDLESS_BINDING_IMAGES,
         .type    = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count   = max_images,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | P_DESCRIPTOR_BINDING_VARIABLE_COUNT_BIT,
         },
    };

    PDescriptorSetLayoutDesc layout_desc = {
        .bindings      = bindings,
        .binding_count = sizeof(bindings) / sizeof(bindings[0]),
    };

    bindless->layout = pigment_create_descriptor_set_layout(pigment, &layout_desc);
    if(bindless->layout == NULL)
    {
        goto ERROR;
    }

    PCommandPoolDesc upload_pool_desc = {
        .queue_flags = P_QUEUE_GRAPHICS_BIT,
        .flags       = P_COMMAND_POOL_FLAG_TRANSIENT,
        .name        = "pigment_std_bindless_upload_pool",
    };
    bindless->upload_pool = pigment_create_command_pool(pigment, &upload_pool_desc);
    if(bindless->upload_pool == NULL)
    {
        goto ERROR;
    }

    uint32_t frames = pigment->config.max_frames_in_flight;

    PDescriptorPoolSize pool_sizes[] = {
        {      .type  = P_DESCRIPTOR_TYPE_SAMPLER,
         .count = frames * max_samplers                                    },
        {.type  = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count = frames * (max_images + max_cubemaps + max_render_targets)},
    };

    PDescriptorPoolDesc pool_desc = {
        .pool_sizes      = pool_sizes,
        .pool_size_count = sizeof(pool_sizes) / sizeof(pool_sizes[0]),
        .max_sets        = frames,
    };

    bindless->pool = pigment_create_descriptor_pool(pigment, &pool_desc);
    if(bindless->pool == NULL)
    {
        goto ERROR;
    }

    bindless->sets = P_NEW_ARRAY_FOR_OBJECT(pigment, bindless->sets, frames);
    if(bindless->sets == NULL)
    {
        goto ERROR;
    }

    bindless->set_count = frames;

    PDescriptorSetAllocate* set_allocs = P_NEW_ARRAY_FOR_OBJECT(pigment, set_allocs, frames);
    if(set_allocs == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < frames; i++)
    {
        set_allocs[i] = (PDescriptorSetAllocate) {
            .layout         = bindless->layout,
            .variable_count = max_images,
            .name           = "pigment_bindless_set",
        };
    }

    PResult result = pigment_create_descriptor_sets(pigment, bindless->pool, set_allocs, frames, bindless->sets);
    P_FREE(pigment, set_allocs);
    if(result != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    if(sampler_list_init(pigment, &bindless->samplers, max_samplers) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    if(add_default_image(pigment, bindless) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    PDescriptorImageInfo* sampler_infos = P_NEW_ARRAY_FOR_OBJECT(pigment, sampler_infos, bindless->samplers.count);
    if(sampler_infos == NULL)
    {
        goto ERROR;
    }

    PDescriptorImageInfo image_info = {
        .image = bindless->images.images[0]
    };

    for(uint32_t i = 0; i < bindless->samplers.count; i++)
    {
        sampler_infos[i].sampler = bindless->samplers.samplers[i];
    }

    for(uint32_t f = 0; f < bindless->set_count; f++)
    {
        PDescriptorWrite writes[2] = {
            {
             .set           = bindless->sets[f],
             .binding       = PIGMENT_BINDLESS_BINDING_SAMPLERS,
             .array_element = 0,
             .type          = P_DESCRIPTOR_TYPE_SAMPLER,
             .count         = bindless->samplers.count,
             .image_infos   = sampler_infos,
             },
            {
             .set           = bindless->sets[f],
             .binding       = PIGMENT_BINDLESS_BINDING_IMAGES,
             .array_element = 0,
             .type          = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
             .count         = 1,
             .image_infos   = &image_info,
             },
        };
        pigment_update_descriptors(pigment, writes, sizeof(writes) / sizeof(writes[0]), NULL, 0);
    }
    P_FREE(pigment, sampler_infos);

    return bindless;

ERROR:
    pigment_std_destroy_bindless(pigment, bindless);
    return NULL;
}

void pigment_std_destroy_bindless(Pigment* pigment, PStdBindless* bindless)
{
    if(bindless == NULL)
    {
        return;
    }
    image_list_destroy(pigment, &bindless->images, P_TRUE);
    image_list_destroy(pigment, &bindless->cubemaps, P_TRUE);
    image_list_destroy(pigment, &bindless->render_targets, P_FALSE);
    sampler_list_destroy(pigment, &bindless->samplers);
    P_FREE(pigment, bindless->tracked_rts.targets);
    P_FREE(pigment, bindless->sets);
    pigment_destroy_descriptor_pool(pigment, bindless->pool);
    pigment_destroy_descriptor_set_layout(pigment, bindless->layout);
    pigment_destroy_command_pool(pigment, bindless->upload_pool);
    if(bindless->pipeline_layouts != NULL)
    {
        pigment_destroy_layout(pigment, bindless->pipeline_layouts->default_layout);
        pigment_destroy_layout(pigment, bindless->pipeline_layouts->gizmo_layout);
        pigment_destroy_layout(pigment, bindless->pipeline_layouts->skybox_layout);
        pigment_destroy_layout(pigment, bindless->pipeline_layouts->crt_layout);
        P_FREE(pigment, bindless->pipeline_layouts);
    }
    P_FREE(pigment, bindless);
}

PStdPipelineLayouts** pigment_std_bindless_pipeline_layouts_slot(PStdBindless* bindless)
{
    return (bindless != NULL) ? &bindless->pipeline_layouts : NULL;
}

uint32_t pigment_std_upload_image(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format)
{
    if(pigment == NULL || bindless == NULL || pixels == NULL || width == 0 || height == 0)
    {
        return UINT32_MAX;
    }

    uint32_t slot = bindless->images.count;

    if(add_image_from_pixels(pigment, bindless, pixels, width, height, format) != PIGMENT_SUCCESS)
    {
        return UINT32_MAX;
    }

    write_image_descriptor(pigment, bindless, slot, bindless->images.images[slot]);

    return slot;
}

uint32_t pigment_std_upload_image_batch(Pigment* pigment, PStdBindless* bindless, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count)
{
    if(pigment == NULL || bindless == NULL || pixels == NULL || count == 0)
    {
        return UINT32_MAX;
    }

    PBuffer** stagings  = P_NEW_ARRAY_FOR_OBJECT(pigment, stagings, count);
    PImage** new_images = P_NEW_ARRAY_FOR_OBJECT(pigment, new_images, count);
    uint32_t start_slot = UINT32_MAX;

    if(stagings == NULL || new_images == NULL)
    {
        goto FREE;
    }

    PCommandBuffer* cmd = NULL;
    pigment_create_command_buffers(pigment, bindless->upload_pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd);
    pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
    PResult result = batch_record_uploads(pigment, cmd, new_images, stagings, pixels, widths, heights, formats, count);
    pigment_end_recording(pigment, cmd);
    pigment_queue_submit(pigment, &(PSubmit) {.cmds = &cmd, .cmd_count = 1}, 1);
    pigment_destroy_command_buffers(pigment, &cmd, 1);

    if(result == PIGMENT_SUCCESS)
    {
        start_slot = batch_append_images(pigment, &bindless->images, new_images, count);
        batch_write_descriptors(pigment, bindless, start_slot, count);
    }
    else
    {
        for(uint32_t i = 0; i < count; i++)
        {
            pigment_destroy_image(pigment, new_images[i]);
        }
    }

FREE:
    if(stagings != NULL)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            pigment_destroy_buffer(pigment, stagings[i]);
        }
    }
    P_FREE(pigment, stagings);
    P_FREE(pigment, new_images);
    return start_slot;
}

uint32_t pigment_std_add_sampler(Pigment* pigment, PStdBindless* bindless, const PSamplerDesc* desc)
{
    if(pigment == NULL || bindless == NULL || desc == NULL)
    {
        return UINT32_MAX;
    }

    PSamplerList* samplers = &bindless->samplers;

    for(uint32_t i = 0; i < samplers->count; i++)
    {
        if(memcmp(&samplers->descs[i], desc, sizeof(*desc)) == 0)
        {
            return i;
        }
    }

    uint32_t slot = samplers->count;
    if(slot >= samplers->capacity)
    {
        PLOG_ERROR(pigment, "Max samplers reached (%u)", samplers->capacity);
        return UINT32_MAX;
    }

    PSampler* sampler = pigment_create_sampler(pigment, desc);
    if(sampler == NULL)
    {
        return UINT32_MAX;
    }
    samplers->samplers[slot] = sampler;
    samplers->descs[slot]    = *desc;
    samplers->count++;

    write_sampler_descriptor(pigment, bindless, slot, sampler);
    return slot;
}

uint32_t pigment_std_upload_cubemap(Pigment* pigment, PStdBindless* bindless, const unsigned char* faces[6], uint32_t face_width, uint32_t face_height, PFormat format)
{
    if(pigment == NULL || bindless == NULL || faces == NULL || face_width == 0 || face_height == 0)
    {
        return UINT32_MAX;
    }

    PBuffer* staging = NULL;
    PImage* image    = NULL;
    if(prepare_layered_image_upload(pigment, &image, &staging, faces, face_width, face_height, 6, format, P_IMAGE_TYPE_CUBE) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    uint64_t face_size = (uint64_t) face_width * face_height * pigment_format_pixel_size(format);

    PCommandBuffer* cmd = NULL;
    pigment_create_command_buffers(pigment, bindless->upload_pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd);
    pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
    record_image_upload(pigment, cmd, image, staging, face_width, face_height, 6, face_size);
    pigment_end_recording(pigment, cmd);
    pigment_queue_submit(pigment, &(PSubmit) {.cmds = &cmd, .cmd_count = 1}, 1);
    pigment_destroy_command_buffers(pigment, &cmd, 1);
    pigment_destroy_buffer(pigment, staging);

    uint32_t slot = bindless->cubemaps.count;
    if(image_list_append(pigment, &bindless->cubemaps, image) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    write_cubemap_descriptor(pigment, bindless, slot, image);
    return slot;

ERROR:
    pigment_destroy_buffer(pigment, staging);
    pigment_destroy_image(pigment, image);
    return UINT32_MAX;
}

uint32_t pigment_std_register_render_target(Pigment* pigment, PStdBindless* bindless, PRenderTarget* rt)
{
    if(pigment == NULL || bindless == NULL || rt == NULL)
    {
        return UINT32_MAX;
    }

    uint32_t color_count = pigment_std_render_target_color_count(rt);
    if(color_count == 0)
    {
        return UINT32_MAX;
    }

    uint32_t first_slot = bindless->render_targets.count;

    for(uint32_t i = 0; i < color_count; i++)
    {
        PImage* sampled = pigment_std_render_target_color_sampled(rt, i);
        if(sampled == NULL)
        {
            return UINT32_MAX;
        }
        if(image_list_append(pigment, &bindless->render_targets, sampled) != PIGMENT_SUCCESS)
        {
            return UINT32_MAX;
        }
        write_render_target_descriptor(pigment, bindless, first_slot + i, sampled);
    }

    PTrackedRT entry = {
        .rt                   = rt,
        .first_slot           = first_slot,
        .color_count          = color_count,
        .last_seen_generation = pigment_std_render_target_generation(rt),
    };

    if(tracked_rt_list_append(pigment, &bindless->tracked_rts, entry) != PIGMENT_SUCCESS)
    {
        return UINT32_MAX;
    }

    return first_slot;
}

uint32_t pigment_std_register_render_target_depth(Pigment* pigment, PStdBindless* bindless, PRenderTarget* rt)
{
    if(pigment == NULL || bindless == NULL || rt == NULL)
    {
        return UINT32_MAX;
    }

    PImage* sampled = pigment_std_render_target_depth_sampled(rt);
    if(sampled == NULL)
    {
        return UINT32_MAX;
    }

    uint32_t slot = bindless->render_targets.count;
    if(image_list_append(pigment, &bindless->render_targets, sampled) != PIGMENT_SUCCESS)
    {
        return UINT32_MAX;
    }
    write_render_target_descriptor(pigment, bindless, slot, sampled);

    return slot;
}

PDescriptorSetLayout* pigment_std_bindless_layout(PStdBindless* bindless)
{
    return (bindless != NULL) ? bindless->layout : NULL;
}

PDescriptorSet* pigment_std_bindless_set(Pigment* pigment, PStdBindless* bindless, uint32_t current_frame)
{
    if(bindless == NULL)
    {
        return NULL;
    }

    if(current_frame >= bindless->set_count)
    {
        return NULL;
    }

    sync_tracked_rts(pigment, bindless);

    return bindless->sets[current_frame];
}

static void image_list_destroy(Pigment* pigment, PImageList* image_list, PBool owns_images)
{
    if(owns_images)
    {
        for(uint32_t i = 0; i < image_list->count; i++)
        {
            pigment_destroy_image(pigment, image_list->images[i]);
        }
    }

    P_FREE(pigment, image_list->images);
}

static PResult image_list_append(Pigment* pigment, PImageList* image_list, PImage* image)
{
    PResult res = P_ARRAY_RESERVE_OBJECT(pigment, image_list->images, image_list->count, image_list->capacity, 1, 1);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
    }
    image_list->images[image_list->count++] = image;

    return PIGMENT_SUCCESS;
}

static PResult sampler_list_init(Pigment* pigment, PSamplerList* sampler_list, uint32_t max_samplers)
{
    sampler_list->samplers = P_NEW_ARRAY_FOR_OBJECT(pigment, sampler_list->samplers, max_samplers);
    sampler_list->descs    = P_NEW_ARRAY_FOR_OBJECT(pigment, sampler_list->descs, max_samplers);
    sampler_list->capacity = max_samplers;
    sampler_list->count    = 0;

    if(sampler_list->samplers == NULL || sampler_list->descs == NULL)
    {
        P_FREE(pigment, sampler_list->samplers);
        P_FREE(pigment, sampler_list->descs);
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    PSamplerDesc nearest_desc = {
        .mag_filter     = P_FILTERING_MODE_NEAREST,
        .min_filter     = P_FILTERING_MODE_NEAREST,
        .mipmap_mode    = P_FILTERING_MODE_NEAREST,
        .address_mode   = P_ADDRESS_MODE_REPEAT,
        .max_anisotropy = 16.0f,
    };
    sampler_list->samplers[0] = pigment_create_sampler(pigment, &nearest_desc);
    sampler_list->descs[0]    = nearest_desc;
    sampler_list->count++;

    PSamplerDesc linear_desc = {
        .mag_filter     = P_FILTERING_MODE_LINEAR,
        .min_filter     = P_FILTERING_MODE_LINEAR,
        .mipmap_mode    = P_FILTERING_MODE_LINEAR,
        .address_mode   = P_ADDRESS_MODE_REPEAT,
        .max_anisotropy = 16.0f,
    };
    sampler_list->samplers[1] = pigment_create_sampler(pigment, &linear_desc);
    sampler_list->descs[1]    = linear_desc;
    sampler_list->count++;

    return PIGMENT_SUCCESS;
}

static void sampler_list_destroy(Pigment* pigment, PSamplerList* sampler_list)
{
    for(uint32_t i = 0; i < sampler_list->count; i++)
    {
        pigment_destroy_sampler(pigment, sampler_list->samplers[i]);
    }

    P_FREE(pigment, sampler_list->samplers);
    P_FREE(pigment, sampler_list->descs);
}

static PResult add_image_from_pixels(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format)
{
    PBuffer* staging = NULL;
    PImage* image    = NULL;

    if(prepare_layered_image_upload(pigment, &image, &staging, &pixels, width, height, 1, format, P_IMAGE_TYPE_2D) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    PCommandBuffer* cmd = NULL;
    pigment_create_command_buffers(pigment, bindless->upload_pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd);
    pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
    record_image_upload(pigment, cmd, image, staging, width, height, 1, 0);
    pigment_end_recording(pigment, cmd);
    pigment_queue_submit(pigment, &(PSubmit) {.cmds = &cmd, .cmd_count = 1}, 1);
    pigment_destroy_command_buffers(pigment, &cmd, 1);

    pigment_destroy_buffer(pigment, staging);

    if(image_list_append(pigment, &bindless->images, image) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    return PIGMENT_SUCCESS;

ERROR:
    pigment_destroy_buffer(pigment, staging);
    pigment_destroy_image(pigment, image);
    PLOG_ERROR(pigment, "Failed to add image from pixels.");
    return PIGMENT_ERROR;
}

static PResult add_default_image(Pigment* pigment, PStdBindless* bindless)
{
    unsigned char white[] = {255, 255, 255, 255};
    return add_image_from_pixels(pigment, bindless, white, 1, 1, P_FORMAT_R8G8B8A8_UNORM);
}

static void record_image_upload(Pigment* pigment, PCommandBuffer* cmd, PImage* image, PBuffer* staging, uint32_t width, uint32_t height, uint32_t layer_count, uint64_t layer_size_bytes)
{
    PImageBarrier to_dst = {
        .image       = image,
        .old_layout  = P_IMAGE_LAYOUT_UNDEFINED,
        .new_layout  = P_IMAGE_LAYOUT_TRANSFER_DST,
        .src         = {        P_PIPELINE_STAGE_NONE,               P_MEMORY_ACCESS_NONE},
        .dst         = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
        .layer_count = layer_count,
    };
    pigment_cmd_image_barriers(pigment, cmd, &to_dst, 1);

    PBufferImageCopy* regions = P_NEW_ARRAY_FOR_OBJECT(pigment, regions, layer_count);
    if(regions == NULL)
    {
        return;
    }
    for(uint32_t i = 0; i < layer_count; i++)
    {
        regions[i] = (PBufferImageCopy) {
            .buffer_offset    = (uint64_t) i * layer_size_bytes,
            .base_array_layer = i,
            .layer_count      = 1,
            .extent_w         = width,
            .extent_h         = height,
            .extent_d         = 1,
        };
    }
    pigment_cmd_copy_buffer_to_image(pigment, cmd, staging, image, P_IMAGE_LAYOUT_TRANSFER_DST, regions, layer_count);
    P_FREE(pigment, regions);

    pigment_cmd_generate_mipmaps(pigment, cmd, image, 0, layer_count, P_IMAGE_LAYOUT_SHADER_READ_ONLY);
}

static PResult prepare_layered_image_upload(Pigment* pigment, PImage** out_image, PBuffer** out_staging, const unsigned char* const* layer_data, uint32_t width, uint32_t height, uint32_t layer_count, PFormat format, PImageType type)
{
    uint32_t pixel_size = pigment_format_pixel_size(format);
    if(pixel_size == 0)
    {
        PLOG_ERROR(pigment, "Unsupported PFormat (%d)!", format);
        return PIGMENT_ERROR;
    }

    if(!pigment_format_supports_linear_blit(pigment, format))
    {
        PLOG_ERROR(pigment, "Image format does not support linear blitting!");
        return PIGMENT_ERROR;
    }

    uint64_t layer_size = (uint64_t) width * height * pixel_size;
    uint64_t total_size = layer_size * layer_count;
    uint32_t mip_levels = (uint32_t) (floor(log2(imax(width, height)))) + 1;

    PBufferDesc staging_desc = {
        .size   = total_size,
        .usage  = P_BUFFER_USAGE_TRANSFER_SRC,
        .memory = P_MEMORY_HOST_UPLOAD,
    };

    *out_staging = pigment_create_buffer(pigment, &staging_desc);
    if(*out_staging == NULL)
    {
        return PIGMENT_ERROR_VULKAN;
    }

    unsigned char* mapped = (unsigned char*) pigment_buffer_mapped(*out_staging);
    for(uint32_t i = 0; i < layer_count; i++)
    {
        memcpy(mapped + i * layer_size, layer_data[i], (size_t) layer_size);
    }
    pigment_buffer_flush(pigment, *out_staging, 0, total_size);

    PImageDesc image_desc = {
        .width        = width,
        .height       = height,
        .array_layers = layer_count,
        .format       = format,
        .usage        = P_IMAGE_USAGE_TRANSFER_SRC | P_IMAGE_USAGE_TRANSFER_DST | P_IMAGE_USAGE_SAMPLED,
        .mip_levels   = mip_levels,
        .type         = type,
    };

    *out_image = pigment_create_image(pigment, &image_desc);
    if(*out_image == NULL)
    {
        return PIGMENT_ERROR_VULKAN;
    }

    return PIGMENT_SUCCESS;
}

static PResult batch_record_uploads(Pigment* pigment, PCommandBuffer* cmd, PImage** out_images, PBuffer** stagings, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count)
{
    for(uint32_t i = 0; i < count; i++)
    {
        PResult r = prepare_layered_image_upload(pigment, &out_images[i], &stagings[i], &pixels[i], widths[i], heights[i], 1, formats[i], P_IMAGE_TYPE_2D);
        if(r != PIGMENT_SUCCESS)
        {
            return r;
        }
        record_image_upload(pigment, cmd, out_images[i], stagings[i], widths[i], heights[i], 1, 0);
    }
    return PIGMENT_SUCCESS;
}

static uint32_t batch_append_images(Pigment* pigment, PImageList* list, PImage** images, uint32_t count)
{
    uint32_t start_slot = list->count;
    for(uint32_t i = 0; i < count; i++)
    {
        image_list_append(pigment, list, images[i]);
    }
    return start_slot;
}

static void write_sampler_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PSampler* sampler)
{
    PDescriptorImageInfo info = {.sampler = sampler};
    for(uint32_t i = 0; i < bindless->set_count; i++)
    {
        PDescriptorWrite write = {
            .set           = bindless->sets[i],
            .binding       = PIGMENT_BINDLESS_BINDING_SAMPLERS,
            .array_element = slot,
            .type          = P_DESCRIPTOR_TYPE_SAMPLER,
            .count         = 1,
            .image_infos   = &info,
        };
        pigment_update_descriptors(pigment, &write, 1, NULL, 0);
    }
}

static void write_sampled_image_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t binding, uint32_t slot, PImage* image)
{
    PDescriptorImageInfo info = {.image = image};
    for(uint32_t i = 0; i < bindless->set_count; i++)
    {
        PDescriptorWrite write = {
            .set           = bindless->sets[i],
            .binding       = binding,
            .array_element = slot,
            .type          = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .count         = 1,
            .image_infos   = &info,
        };
        pigment_update_descriptors(pigment, &write, 1, NULL, 0);
    }
}

static void write_image_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PImage* image)
{
    write_sampled_image_descriptor(pigment, bindless, PIGMENT_BINDLESS_BINDING_IMAGES, slot, image);
}

static void write_cubemap_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PImage* image)
{
    write_sampled_image_descriptor(pigment, bindless, PIGMENT_BINDLESS_BINDING_CUBEMAPS, slot, image);
}

static void write_render_target_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PImage* image)
{
    write_sampled_image_descriptor(pigment, bindless, PIGMENT_BINDLESS_BINDING_RENDER_TARGETS, slot, image);
}

static void batch_write_descriptors(Pigment* pigment, PStdBindless* bindless, uint32_t start_slot, uint32_t count)
{
    PDescriptorImageInfo* infos = P_NEW_ARRAY_FOR_OBJECT(pigment, infos, count);
    if(infos == NULL)
    {
        return;
    }
    for(uint32_t i = 0; i < count; i++)
    {
        infos[i].sampler = NULL;
        infos[i].image   = bindless->images.images[start_slot + i];
        infos[i].layout  = P_IMAGE_DESCRIPTOR_LAYOUT_AUTO;
    }
    for(uint32_t f = 0; f < bindless->set_count; f++)
    {
        PDescriptorWrite write = {
            .set           = bindless->sets[f],
            .binding       = PIGMENT_BINDLESS_BINDING_IMAGES,
            .array_element = start_slot,
            .type          = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .count         = count,
            .image_infos   = infos,
        };

        pigment_update_descriptors(pigment, &write, 1, NULL, 0);
    }
    P_FREE(pigment, infos);
}

static PResult tracked_rt_list_append(Pigment* pigment, PTrackedRTList* list, PTrackedRT entry)
{
    PResult res = P_ARRAY_RESERVE_OBJECT(pigment, list->targets, list->count, list->capacity, 1, 4);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
    }

    list->targets[list->count++] = entry;
    return PIGMENT_SUCCESS;
}

static void sync_tracked_rts(Pigment* pigment, PStdBindless* bindless)
{
    for(uint32_t i = 0; i < bindless->tracked_rts.count; i++)
    {
        PTrackedRT* tracked = &bindless->tracked_rts.targets[i];
        uint32_t current    = pigment_std_render_target_generation(tracked->rt);
        if(current == tracked->last_seen_generation)
        {
            continue;
        }

        for(uint32_t s = 0; s < tracked->color_count; s++)
        {
            PImage* sampled                                          = pigment_std_render_target_color_sampled(tracked->rt, s);
            bindless->render_targets.images[tracked->first_slot + s] = sampled;
            write_render_target_descriptor(pigment, bindless, tracked->first_slot + s, sampled);
        }
        tracked->last_seen_generation = current;
    }
}
