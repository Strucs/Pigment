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

#include "transfert.h"
#include "std_internal.h"

#include "pigment/pigment.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdatomic.h>

typedef struct PImageList {
    PImage** images;
    uint32_t count;
    uint32_t capacity;
    uint32_t limit;
    uint32_t* free_slots;
    uint32_t free_count;
    uint32_t free_capacity;
} PImageList;

typedef struct PBindlessImageBinding {
    PImageList* list;
    uint32_t binding;
} PBindlessImageBinding;

typedef struct PSamplerList {
    PSampler** samplers;
    PSamplerDesc* descs;
    uint32_t count;
    uint32_t capacity;
} PSamplerList;

typedef struct PTrackedRT {
    PRenderTarget* rt;
    uint32_t* slots;
    uint32_t color_count;
    uint32_t last_seen_generation;
    PBool has_depth;
} PTrackedRT;

typedef struct PTrackedRTList {
    PTrackedRT* targets;
    uint32_t count;
    uint32_t capacity;
} PTrackedRTList;

struct PStdBindlessVersion {
    PDescriptorPool* pool;
    PDescriptorSet* set;
    PResourceTracker* tracker;
    // Keeps the allocation alive for the cache and the active publication.
    _Atomic uint32_t allocation_refs;

    // Keeps the publication alive for the current version and CPU preparations.
    _Atomic uint32_t publication_refs;
    PResourceTracker** resources;
    uint32_t resource_count;

    struct PStdBindlessVersion* next;
};

struct PStdBindless {
    PDescriptorSetLayout* layout;
    PCommandPool* upload_pool;
    PStdBindlessVersion* version;
    PStdBindlessVersion* versions;
    uint64_t next_version_id;
    PDescriptorImageInfo* infos;
    uint32_t info_capacity;
    PDescriptorWrite* writes;
    uint32_t write_capacity;
    PBool dirty;

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

static uint32_t image_list_put(Pigment* pigment, PImageList* image_list, PImage* image);
static void image_list_release_slot(Pigment* pigment, PImageList* image_list, uint32_t slot);
static uint32_t batch_append_images(Pigment* pigment, PImageList* list, PImage** images, uint32_t count);
static PImage* add_image_from_pixels(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format);
static PResult add_default_image(Pigment* pigment, PStdBindless* bindless);
static PResult sampler_list_init(Pigment* pigment, PSamplerList* sampler_list, uint32_t max_samplers);
static void sampler_list_destroy(Pigment* pigment, PSamplerList* sampler_list);
static void image_list_destroy(Pigment* pigment, PImageList* image_list, PBool owns_images);
static PResult tracked_rt_list_append(Pigment* pigment, PTrackedRTList* list, PTrackedRT entry);
static void sync_tracked_rts(Pigment* pigment, PStdBindless* bindless);
static PResult publish_bindless(Pigment* pigment, PStdBindless* bindless);
static void release_bindless_allocation(Pigment* pigment, PStdBindlessVersion* version);
static void recycle_bindless_version(Pigment* pigment, void* resource);

/**
 * @brief Describe the same four bindings for queries and creation.
 */
static void bindless_bindings(PDescriptorBinding bindings[4], uint32_t max_images, uint32_t max_samplers, uint32_t max_cubemaps, uint32_t max_render_targets)
{
    const PDescriptorBinding values[] = {
        {
         .binding = PIGMENT_BINDLESS_BINDING_SAMPLERS,
         .type    = P_DESCRIPTOR_TYPE_SAMPLER,
         .count   = max_samplers,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | P_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
         },
        {
         .binding = PIGMENT_BINDLESS_BINDING_CUBEMAPS,
         .type    = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count   = (max_cubemaps == 0) ? 1 : max_cubemaps,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | P_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
         },
        {
         .binding = PIGMENT_BINDLESS_BINDING_RENDER_TARGETS,
         .type    = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count   = (max_render_targets == 0) ? 1 : max_render_targets,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | P_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
         },
        {
         .binding = PIGMENT_BINDLESS_BINDING_IMAGES,
         .type    = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count   = max_images,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | P_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | P_DESCRIPTOR_BINDING_VARIABLE_COUNT_BIT,
         },
    };

    memcpy(bindings, values, sizeof(values));
}

PBool pigment_std_bindless_supported(Pigment* pigment, uint32_t max_images, uint32_t max_samplers, uint32_t max_cubemaps, uint32_t max_render_targets)
{
    if(pigment == NULL || max_images == 0 || max_samplers < 2)
    {
        return P_FALSE;
    }

    const uint64_t cubemaps = max_cubemaps ? max_cubemaps : 1;
    const uint64_t targets  = max_render_targets ? max_render_targets : 1;
    const uint64_t sampled  = (uint64_t) max_images + cubemaps + targets;
    const uint64_t total    = sampled + max_samplers;

    PDeviceLimits limits = pigment_device_limits(pigment);

    if(sampled > limits.max_sampled_images
       || max_samplers > limits.max_samplers
       || sampled > UINT32_MAX)
    {
        return P_FALSE;
    }

    if(sampled > limits.max_per_stage_resources
       || total > limits.max_update_after_bind_descriptors)
    {
        return P_FALSE;
    }

    PDescriptorBinding bindings[4];
    bindless_bindings(bindings, max_images, max_samplers, max_cubemaps, max_render_targets);

    PDescriptorSetLayoutDesc desc = {
        .bindings      = bindings,
        .binding_count = sizeof(bindings) / sizeof(bindings[0]),
    };

    return pigment_descriptor_set_layout_supported(pigment, &desc);
}

PStdBindless* pigment_std_create_bindless(Pigment* pigment, uint32_t max_images, uint32_t max_samplers, uint32_t max_cubemaps, uint32_t max_render_targets)
{
    if(pigment == NULL || max_images == 0 || max_samplers < 2)
    {
        return NULL;
    }

    uint32_t cubemaps      = (max_cubemaps == 0) ? 1 : max_cubemaps;
    uint32_t targets       = (max_render_targets == 0) ? 1 : max_render_targets;
    uint64_t sampled_total = (uint64_t) max_images + cubemaps + targets;

    PDeviceLimits limits = pigment_device_limits(pigment);
    if(sampled_total > limits.max_sampled_images)
    {
        PLOG_ERROR(pigment, "Bindless needs %llu sampled images (%u images + %u cubemaps + %u render targets) but the device caps at %u.", (unsigned long long) sampled_total, max_images, cubemaps, targets, limits.max_sampled_images);
        return NULL;
    }

    if(max_samplers > limits.max_samplers)
    {
        PLOG_ERROR(pigment, "Bindless needs %u samplers but the device caps at %u.", max_samplers, limits.max_samplers);
        return NULL;
    }

    PStdBindless* bindless = P_NEW_FOR_OBJECT(pigment, bindless);
    if(bindless == NULL)
    {
        return NULL;
    }

    PDescriptorBinding bindings[4];
    bindless_bindings(bindings, max_images, max_samplers, max_cubemaps, max_render_targets);

    PDescriptorSetLayoutDesc layout_desc = {
        .bindings      = bindings,
        .binding_count = sizeof(bindings) / sizeof(bindings[0]),
        .name          = "pigment_bindless_layout"
    };

    bindless->layout = pigment_create_descriptor_set_layout(pigment, &layout_desc);
    if(bindless->layout == NULL)
    {
        goto ERROR;
    }

    PCommandPoolDesc upload_pool_desc = {
        .queue_family = pigment_queue_family(pigment_get_queue(pigment, P_QUEUE_GRAPHICS_BIT)),
        .flags        = P_COMMAND_POOL_FLAG_TRANSIENT,
        .name         = "pigment_std_bindless_upload_pool",
    };
    bindless->upload_pool = pigment_create_command_pool(pigment, &upload_pool_desc);
    if(bindless->upload_pool == NULL)
    {
        goto ERROR;
    }

    bindless->images.limit         = max_images;
    bindless->cubemaps.limit       = cubemaps;
    bindless->render_targets.limit = targets;
    bindless->dirty                = P_TRUE;

    if(sampler_list_init(pigment, &bindless->samplers, max_samplers) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    if(add_default_image(pigment, bindless) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

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
    for(uint32_t i = 0; i < bindless->tracked_rts.count; i++)
    {
        P_FREE(pigment, bindless->tracked_rts.targets[i].slots);
    }
    P_FREE(pigment, bindless->tracked_rts.targets);
    pigment_std_release_bindless_version(pigment, bindless->version);

    PStdBindlessVersion* version = bindless->versions;
    while(version != NULL)
    {
        PStdBindlessVersion* next = version->next;
        release_bindless_allocation(pigment, version);
        version = next;
    }

    P_FREE(pigment, bindless->infos);
    P_FREE(pigment, bindless->writes);
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

uint32_t pigment_std_add_image(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format)
{
    if(pigment == NULL || bindless == NULL || pixels == NULL || width == 0 || height == 0)
    {
        return UINT32_MAX;
    }

    PImage* image = add_image_from_pixels(pigment, bindless, pixels, width, height, format);
    if(image == NULL)
    {
        return UINT32_MAX;
    }

    uint32_t slot = image_list_put(pigment, &bindless->images, image);
    if(slot == UINT32_MAX)
    {
        pigment_destroy_image(pigment, image);
        return UINT32_MAX;
    }

    bindless->dirty = P_TRUE;
    return slot;
}

uint32_t pigment_std_register_image(Pigment* pigment, PStdBindless* bindless, PImage* image)
{
    if(pigment == NULL || bindless == NULL || image == NULL)
    {
        return UINT32_MAX;
    }

    uint32_t slot = image_list_put(pigment, &bindless->images, image);
    if(slot == UINT32_MAX)
    {
        return UINT32_MAX;
    }

    bindless->dirty = P_TRUE;
    return slot;
}

void pigment_std_unregister_image(Pigment* pigment, PStdBindless* bindless, uint32_t slot)
{
    if(pigment == NULL || bindless == NULL)
    {
        return;
    }

    if(slot == 0 || slot >= bindless->images.count || bindless->images.images[slot] == NULL)
    {
        PLOG_ERROR(pigment, "Cannot unregister bindless image slot %u: out of range, already free, or the default slot.", slot);
        return;
    }

    pigment_destroy_image(pigment, bindless->images.images[slot]);

    bindless->dirty               = P_TRUE;
    bindless->images.images[slot] = NULL;
    image_list_release_slot(pigment, &bindless->images, slot);
}

uint32_t pigment_std_add_image_batch(Pigment* pigment, PStdBindless* bindless, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count)
{
    if(pigment == NULL || bindless == NULL || pixels == NULL || count == 0)
    {
        return UINT32_MAX;
    }

    PImageUploadDesc* descs = P_NEW_ARRAY_FOR_OBJECT(pigment, descs, count);
    PImage** new_images     = P_NEW_ARRAY_FOR_OBJECT(pigment, new_images, count);
    uint32_t start_slot     = UINT32_MAX;

    if(descs == NULL || new_images == NULL)
    {
        goto FREE;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        descs[i] = (PImageUploadDesc) {.layers = &pixels[i], .width = widths[i], .height = heights[i], .format = formats[i], .flags = P_IMAGE_GENERATE_MIPMAPS};
    }

    if(pigment_std_image_upload(pigment, bindless->upload_pool, NULL, descs, count, new_images, NULL) != PIGMENT_SUCCESS)
    {
        goto FREE;
    }

    if(pigment_std_image_finalize(pigment, bindless->upload_pool, NULL, new_images, descs, count, NULL) != PIGMENT_SUCCESS)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            pigment_destroy_image(pigment, new_images[i]);
        }
        goto FREE;
    }

    start_slot = batch_append_images(pigment, &bindless->images, new_images, count);
    if(start_slot == UINT32_MAX)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            pigment_destroy_image(pigment, new_images[i]);
        }
        goto FREE;
    }
    bindless->dirty = P_TRUE;

FREE:
    P_FREE(pigment, descs);
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

    bindless->dirty = P_TRUE;
    return slot;
}

uint32_t pigment_std_add_cubemap(Pigment* pigment, PStdBindless* bindless, const unsigned char* faces[6], uint32_t face_width, uint32_t face_height, PFormat format)
{
    if(pigment == NULL || bindless == NULL || faces == NULL || face_width == 0 || face_height == 0)
    {
        return UINT32_MAX;
    }

    PImageUploadDesc desc = {
        .layers      = faces,
        .layer_count = 6,
        .width       = face_width,
        .height      = face_height,
        .format      = format,
        .type        = P_IMAGE_TYPE_CUBE,
        .flags       = P_IMAGE_GENERATE_MIPMAPS,
    };
    PImage* image = NULL;
    if(pigment_std_image_upload(pigment, bindless->upload_pool, NULL, &desc, 1, &image, NULL) != PIGMENT_SUCCESS)
    {
        return UINT32_MAX;
    }

    if(pigment_std_image_finalize(pigment, bindless->upload_pool, NULL, &image, &desc, 1, NULL) != PIGMENT_SUCCESS)
    {
        pigment_destroy_image(pigment, image);
        return UINT32_MAX;
    }

    uint32_t slot = image_list_put(pigment, &bindless->cubemaps, image);
    if(slot == UINT32_MAX)
    {
        pigment_destroy_image(pigment, image);
        return UINT32_MAX;
    }

    bindless->dirty = P_TRUE;
    return slot;
}

void pigment_std_unregister_cubemap(Pigment* pigment, PStdBindless* bindless, uint32_t slot)
{
    if(pigment == NULL || bindless == NULL)
    {
        return;
    }

    if(slot >= bindless->cubemaps.count || bindless->cubemaps.images[slot] == NULL)
    {
        PLOG_ERROR(pigment, "Cannot unregister bindless cubemap slot %u: out of range or already free.", slot);
        return;
    }

    pigment_destroy_image(pigment, bindless->cubemaps.images[slot]);

    bindless->dirty                 = P_TRUE;
    bindless->cubemaps.images[slot] = NULL;
    image_list_release_slot(pigment, &bindless->cubemaps, slot);
}

PResult pigment_std_register_render_target(Pigment* pigment, PStdBindless* bindless, PRenderTarget* rt, uint32_t* slots, uint32_t* slot_count)
{
    if(pigment == NULL || bindless == NULL || rt == NULL || slot_count == NULL)
    {
        return PIGMENT_ERROR;
    }

    uint32_t color_count  = pigment_std_render_target_color_count(rt);
    PImage* depth_sampled = pigment_std_render_target_depth_sampled(rt);
    if(depth_sampled != NULL && !(pigment_image_usage(depth_sampled) & P_IMAGE_USAGE_SAMPLED))
    {
        depth_sampled = NULL;
    }
    uint32_t total = color_count + (depth_sampled != NULL ? 1 : 0);

    if(slots == NULL)
    {
        *slot_count = total;
        return PIGMENT_SUCCESS;
    }

    if(total == 0)
    {
        PLOG_WARN(pigment, "Cannot register render target: missing color or depth attachments.");
        return PIGMENT_ERROR;
    }

    if(*slot_count < total)
    {
        PLOG_WARN(pigment, "Cannot register render target: slots capacity %u is below the %u slots required.", *slot_count, total);
        return PIGMENT_ERROR;
    }

    PTrackedRT entry = {
        .rt                   = rt,
        .color_count          = color_count,
        .last_seen_generation = pigment_std_render_target_generation(rt),
        .has_depth            = (depth_sampled != NULL),
    };

    entry.slots = P_NEW_ARRAY_FOR_OBJECT(pigment, entry.slots, total);
    if(entry.slots == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    PResult result    = PIGMENT_ERROR;
    uint32_t assigned = 0;
    for(uint32_t i = 0; i < color_count; i++)
    {
        PImage* sampled = pigment_std_render_target_color_sampled(rt, i);
        if(sampled == NULL)
        {
            goto ERROR;
        }

        uint32_t slot = image_list_put(pigment, &bindless->render_targets, sampled);
        if(slot == UINT32_MAX)
        {
            result = PIGMENT_ERROR_OUT_OF_MEMORY;
            goto ERROR;
        }
        entry.slots[assigned++] = slot;
        bindless->dirty         = P_TRUE;
    }

    if(depth_sampled != NULL)
    {
        uint32_t slot = image_list_put(pigment, &bindless->render_targets, depth_sampled);
        if(slot == UINT32_MAX)
        {
            result = PIGMENT_ERROR_OUT_OF_MEMORY;
            goto ERROR;
        }
        entry.slots[assigned++] = slot;
        bindless->dirty         = P_TRUE;
    }

    if(tracked_rt_list_append(pigment, &bindless->tracked_rts, entry) != PIGMENT_SUCCESS)
    {
        result = PIGMENT_ERROR_OUT_OF_MEMORY;
        goto ERROR;
    }

    for(uint32_t i = 0; i < assigned; i++)
    {
        slots[i] = entry.slots[i];
    }
    *slot_count = assigned;

    return PIGMENT_SUCCESS;

ERROR:
    for(uint32_t i = 0; i < assigned; i++)
    {
        bindless->render_targets.images[entry.slots[i]] = NULL;
        image_list_release_slot(pigment, &bindless->render_targets, entry.slots[i]);
    }
    P_FREE(pigment, entry.slots);

    return result;
}

void pigment_std_unregister_render_target(Pigment* pigment, PStdBindless* bindless, PRenderTarget* rt)
{
    if(pigment == NULL || bindless == NULL || rt == NULL)
    {
        return;
    }

    uint32_t i = 0;
    while(i < bindless->tracked_rts.count)
    {
        PTrackedRT* tracked = &bindless->tracked_rts.targets[i];
        if(tracked->rt != rt)
        {
            i++;
            continue;
        }

        uint32_t slot_count = tracked->color_count + (tracked->has_depth ? 1 : 0);
        for(uint32_t s = 0; s < slot_count; s++)
        {
            bindless->dirty                                    = P_TRUE;
            bindless->render_targets.images[tracked->slots[s]] = NULL;
            image_list_release_slot(pigment, &bindless->render_targets, tracked->slots[s]);
        }
        P_FREE(pigment, tracked->slots);

        bindless->tracked_rts.targets[i] = bindless->tracked_rts.targets[--bindless->tracked_rts.count];
    }
}

PDescriptorSetLayout* pigment_std_bindless_layout(PStdBindless* bindless)
{
    return (bindless != NULL) ? bindless->layout : NULL;
}

PImage* pigment_std_bindless_image(PStdBindless* bindless, uint32_t image_slot)
{
    if(bindless == NULL || image_slot >= bindless->images.count)
    {
        return NULL;
    }
    return bindless->images.images[image_slot];
}

PStdBindlessVersion* pigment_std_bindless_prepare(Pigment* pigment, PStdBindless* bindless)
{
    if(pigment == NULL || bindless == NULL)
    {
        return NULL;
    }

    sync_tracked_rts(pigment, bindless);

    if(bindless->dirty && publish_bindless(pigment, bindless) != PIGMENT_SUCCESS)
    {
        return NULL;
    }

    atomic_fetch_add_explicit(&bindless->version->publication_refs, 1, memory_order_relaxed);
    return bindless->version;
}

PDescriptorSet* pigment_std_bindless_version_set(Pigment* pigment, const PStdBindlessVersion* version, PCommandBuffer* cmd)
{
    if(pigment == NULL || version == NULL || cmd == NULL)
    {
        return NULL;
    }

    pigment_cmd_use(pigment, cmd, version->tracker);
    for(uint32_t i = 0; i < version->resource_count; i++)
    {
        pigment_cmd_use(pigment, cmd, version->resources[i]);
    }

    return version->set;
}

void pigment_std_release_bindless_version(Pigment* pigment, PStdBindlessVersion* version)
{
    if(pigment != NULL && version != NULL
       && atomic_fetch_sub_explicit(&version->publication_refs, 1, memory_order_acq_rel) == 1)
    {
        pigment_defer_destroy_tracked(pigment, recycle_bindless_version, version, version->tracker);
    }
}

static void image_list_destroy(Pigment* pigment, PImageList* image_list, PBool owns_images)
{
    if(owns_images)
    {
        for(uint32_t i = 0; i < image_list->count; i++)
        {
            if(image_list->images[i] != NULL)
            {
                pigment_destroy_image(pigment, image_list->images[i]);
            }
        }
    }

    P_FREE(pigment, image_list->images);
    P_FREE(pigment, image_list->free_slots);
}

static uint32_t image_list_put(Pigment* pigment, PImageList* image_list, PImage* image)
{
    if(image_list->free_count > 0)
    {
        uint32_t slot            = image_list->free_slots[--image_list->free_count];
        image_list->images[slot] = image;
        return slot;
    }

    if(image_list->count >= image_list->limit)
    {
        return UINT32_MAX;
    }

    if(P_ARRAY_RESERVE_OBJECT(pigment, image_list->images, image_list->count, image_list->capacity, 1, 1) != PIGMENT_SUCCESS)
    {
        return UINT32_MAX;
    }

    uint32_t slot            = image_list->count++;
    image_list->images[slot] = image;
    return slot;
}

static void image_list_release_slot(Pigment* pigment, PImageList* image_list, uint32_t slot)
{
    if(P_ARRAY_RESERVE_OBJECT(pigment, image_list->free_slots, image_list->free_count, image_list->free_capacity, 1, PIGMENT_STD_FREE_LIST_INITIAL_CAPACITY) == PIGMENT_SUCCESS)
    {
        image_list->free_slots[image_list->free_count++] = slot;
    }
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

static PImage* add_image_from_pixels(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format)
{
    PImageUploadDesc desc = {.layers = &pixels, .width = width, .height = height, .format = format, .flags = P_IMAGE_GENERATE_MIPMAPS};
    PImage* image         = NULL;

    if(pigment_std_image_upload(pigment, bindless->upload_pool, NULL, &desc, 1, &image, NULL) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to add image from pixels.");
        return NULL;
    }

    if(pigment_std_image_finalize(pigment, bindless->upload_pool, NULL, &image, &desc, 1, NULL) != PIGMENT_SUCCESS)
    {
        pigment_destroy_image(pigment, image);
        return NULL;
    }

    return image;
}

static PResult add_default_image(Pigment* pigment, PStdBindless* bindless)
{
    unsigned char white[] = {255, 255, 255, 255};
    PImage* image         = add_image_from_pixels(pigment, bindless, white, 1, 1, P_FORMAT_R8G8B8A8_UNORM);
    if(image == NULL)
    {
        return PIGMENT_ERROR;
    }

    if(image_list_put(pigment, &bindless->images, image) == UINT32_MAX)
    {
        pigment_destroy_image(pigment, image);
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }
    return PIGMENT_SUCCESS;
}

static uint32_t batch_append_images(Pigment* pigment, PImageList* list, PImage** images, uint32_t count)
{
    if(count > list->limit - list->count
       || P_ARRAY_RESERVE_OBJECT(pigment, list->images, list->count, list->capacity, count, 1) != PIGMENT_SUCCESS)
    {
        return UINT32_MAX;
    }
    uint32_t start_slot = list->count;
    memcpy(list->images + start_slot, images, count * sizeof(*images));
    list->count += count;

    return start_slot;
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
            PImage* sampled                                    = pigment_std_render_target_color_sampled(tracked->rt, s);
            bindless->render_targets.images[tracked->slots[s]] = sampled;
            bindless->dirty                                    = P_TRUE;
        }

        if(tracked->has_depth)
        {
            uint32_t depth_slot                         = tracked->slots[tracked->color_count];
            PImage* sampled                             = pigment_std_render_target_depth_sampled(tracked->rt);
            bindless->render_targets.images[depth_slot] = sampled;
            bindless->dirty                             = P_TRUE;
        }
        tracked->last_seen_generation = current;
    }
}

static void release_bindless_allocation(Pigment* pigment, PStdBindlessVersion* version)
{
    if(atomic_fetch_sub_explicit(&version->allocation_refs, 1, memory_order_acq_rel) == 1)
    {
        pigment_destroy_descriptor_pool(pigment, version->pool);
        P_FREE(pigment, version);
    }
}

static void recycle_bindless_version(Pigment* pigment, void* resource)
{
    PStdBindlessVersion* version = resource;
    for(uint32_t i = 0; i < version->resource_count; i++)
    {
        pigment_release_resource(pigment, version->resources[i]);
    }

    P_FREE(pigment, version->resources);
    version->resources      = NULL;
    version->resource_count = 0;
    pigment_destroy_resource_tracker(pigment, version->tracker);
    version->tracker = NULL;

    // The cache can reuse this version only after this release.
    release_bindless_allocation(pigment, version);
}

static PStdBindlessVersion* acquire_bindless_version(Pigment* pigment, PStdBindless* bindless)
{
    PStdBindlessVersion* available = NULL;
    PStdBindlessVersion** link     = &bindless->versions;
    while(*link != NULL)
    {
        PStdBindlessVersion* version = *link;
        if(atomic_load_explicit(&version->allocation_refs, memory_order_acquire) == 1
           && pigment_descriptor_set_idle(pigment, version->set))
        {
            // Reuse the first available version and destroy the other available versions.
            if(available == NULL)
            {
                available = version;
            }
            else
            {
                *link = version->next;
                release_bindless_allocation(pigment, version);
                continue;
            }
        }
        link = &version->next;
    }

    if(available != NULL)
    {
        return available;
    }

    PStdBindlessVersion* version = P_NEW_FOR_OBJECT(pigment, version);
    if(version == NULL)
    {
        return NULL;
    }

    atomic_init(&version->allocation_refs, 1);
    atomic_init(&version->publication_refs, 0);

    PDescriptorPoolSize sizes[] = {
        {
         .type  = P_DESCRIPTOR_TYPE_SAMPLER,
         .count = bindless->samplers.capacity,
         },
        {
         .type  = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count = bindless->images.limit + bindless->cubemaps.limit + bindless->render_targets.limit,
         },
    };

    PDescriptorPoolDesc desc = {
        .pool_sizes              = sizes,
        .pool_size_count         = sizeof(sizes) / sizeof(sizes[0]),
        .max_sets                = 1,
        .allow_update_after_bind = P_TRUE,
    };

    version->pool = pigment_create_descriptor_pool(pigment, &desc);
    if(version->pool == NULL)
    {
        goto ERROR;
    }

    char name[64];
    snprintf(name, sizeof(name), "pigment_std_bindless_set_%llu", (unsigned long long) bindless->next_version_id);

    PDescriptorSetAllocate alloc = {
        .layout         = bindless->layout,
        .variable_count = bindless->images.limit,
        .name           = name,
    };

    PResult result = pigment_create_descriptor_sets(pigment, version->pool, &alloc, 1, &version->set);

    if(result != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    bindless->next_version_id++;

    version->next      = bindless->versions;
    bindless->versions = version;
    return version;

ERROR:
    release_bindless_allocation(pigment, version);
    return NULL;
}

static PResult publish_bindless(Pigment* pigment, PStdBindless* bindless)
{
    uint64_t count = (uint64_t) bindless->samplers.count + bindless->images.count + bindless->cubemaps.count + bindless->render_targets.count;
    if(count > UINT32_MAX
       || P_ARRAY_RESERVE_OBJECT(pigment, bindless->infos, 0, bindless->info_capacity, (uint32_t) count, 1) != PIGMENT_SUCCESS
       || P_ARRAY_RESERVE_OBJECT(pigment, bindless->writes, 0, bindless->write_capacity, (uint32_t) count, 1) != PIGMENT_SUCCESS)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    PStdBindlessVersion* version = acquire_bindless_version(pigment, bindless);
    if(version == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    PResourceTracker* tracker = pigment_create_resource_tracker(pigment);
    if(tracker == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    PResourceTracker** resources = P_NEW_ARRAY_FOR_OBJECT(pigment, resources, (uint32_t) count);
    if(resources == NULL)
    {
        pigment_destroy_resource_tracker(pigment, tracker);
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    uint32_t info_count  = 0;
    uint32_t write_count = 0;
    for(uint32_t i = 0; i < bindless->samplers.count; i++)
    {
        bindless->infos[info_count++] = (PDescriptorImageInfo) {
            .sampler = bindless->samplers.samplers[i]
        };
    }

    bindless->writes[write_count++] = (PDescriptorWrite) {
        .set         = version->set,
        .binding     = PIGMENT_BINDLESS_BINDING_SAMPLERS,
        .type        = P_DESCRIPTOR_TYPE_SAMPLER,
        .count       = info_count,
        .image_infos = bindless->infos,
    };

    PBindlessImageBinding bindings[] = {
        {        &bindless->images,         PIGMENT_BINDLESS_BINDING_IMAGES},
        {      &bindless->cubemaps,       PIGMENT_BINDLESS_BINDING_CUBEMAPS},
        {&bindless->render_targets, PIGMENT_BINDLESS_BINDING_RENDER_TARGETS},
    };

    for(uint32_t b = 0; b < sizeof(bindings) / sizeof(bindings[0]); b++)
    {
        PImageList* list        = bindings[b].list;
        PDescriptorWrite* write = NULL;
        for(uint32_t i = 0; i < list->count; i++)
        {
            PImage* image = list->images[i];
            if(image == NULL)
            {
                write = NULL;
                continue;
            }

            PImageViewDesc view_desc = {0};
            if(bindings[b].binding == PIGMENT_BINDLESS_BINDING_RENDER_TARGETS
               && (pigment_image_usage(image) & P_IMAGE_USAGE_RENDER_DEPTH) != 0)
            {
                view_desc.aspect = P_IMAGE_ASPECT_DEPTH;
            }

            PImageView* view = image_get_or_create_view(pigment, image, &view_desc);
            if(view == NULL)
            {
                goto ERROR;
            }

            bindless->infos[info_count] = (PDescriptorImageInfo) {
                .image = image,
                .view  = view
            };

            if(write == NULL)
            {
                write  = &bindless->writes[write_count];
                *write = (PDescriptorWrite) {
                    .set           = version->set,
                    .binding       = bindings[b].binding,
                    .array_element = i,
                    .type          = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .image_infos   = &bindless->infos[info_count],
                };
                write_count++;
            }

            write->count++;
            info_count++;
        }
    }

    for(uint32_t i = 0; i < info_count; i++)
    {
        PDescriptorImageInfo* info = &bindless->infos[i];
        PResource resource         = info->image != NULL
                                         ? (PResource) {.type = P_RESOURCE_TYPE_IMAGE, .image = info->image}
                                         : (PResource) {.type = P_RESOURCE_TYPE_SAMPLER, .sampler = info->sampler};
        resources[i]               = pigment_retain_resource(resource);
    }

    if(pigment_update_descriptors(pigment, bindless->writes, write_count, NULL, 0) != PIGMENT_SUCCESS)
    {
        for(uint32_t i = 0; i < info_count; i++)
        {
            pigment_release_resource(pigment, resources[i]);
        }
        goto ERROR;
    }

    version->resources      = resources;
    version->resource_count = info_count;
    atomic_store_explicit(&version->publication_refs, 1, memory_order_relaxed);

    version->tracker = tracker;
    atomic_fetch_add_explicit(&version->allocation_refs, 1, memory_order_relaxed);

    PStdBindlessVersion* previous = bindless->version;
    bindless->version             = version;
    bindless->dirty               = P_FALSE;
    pigment_std_release_bindless_version(pigment, previous);

    return PIGMENT_SUCCESS;

ERROR:
    P_FREE(pigment, resources);
    pigment_destroy_resource_tracker(pigment, tracker);
    return PIGMENT_ERROR_OUT_OF_MEMORY;
}
