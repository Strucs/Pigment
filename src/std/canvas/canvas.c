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

#include "canvas.h"

#include "pigment/pigment.h"

#include "pigment/std/bindless.h"
#include "pigment/std/instances.h"

#include <stdatomic.h>

#include "ndc_vert_spv.h"
#include "ndc_frag_spv.h"

#define DEFAULT_INITIAL_INSTANCES 4096
#define FULL_UV ((const float[4]) {0.0f, 0.0f, 1.0f, 1.0f})

typedef struct PIGMENT_ALIGN(16) PStdCanvasInstance {
    float transform[4][4];
    float color[4];
    float uv_rect[4];
    int32_t image_idx;
    int32_t sampler_idx;
} PStdCanvasInstance;

typedef struct PStdCanvasPushConstants {
    uint64_t instance_buffer_address;
} PStdCanvasPushConstants;

typedef struct PStdCanvasBlendSlot {
    PBlendMode mode;
    PPipeline* pipeline;
    PInstances* instances;
} PStdCanvasBlendSlot;

typedef struct PStdCanvasResources {
    PLayout* layout;
    _Atomic uint32_t references;
} PStdCanvasResources;

struct PStdCanvas {
    PStdCanvasResources* resources;
    PStdBindless* bindless;
    PStdCanvasBlendSlot* slots;
    uint32_t slot_count;

    PStdCanvasBlendSlot* active_slot;
    uint32_t target_w;
    uint32_t target_h;
};

struct PStdCanvasBatch {
    PStdCanvasResources* resources;
    PPipeline* pipeline;
    PResourceTracker* pipeline_reference;
    PStdBindlessVersion* bindless;
    PStdBufferVersion* instances;
};

static PStdCanvasBlendSlot* find_slot_for_mode(PStdCanvas* canvas, PBlendMode mode);
static void pixel_to_ndc(uint32_t screen_w, uint32_t screen_h, int32_t x_px, int32_t y_px, int32_t w_px, int32_t h_px, float* out_pos_x, float* out_pos_y, float* out_size_x, float* out_size_y);
static void anchor_to_pixel(const PStdCanvas* canvas, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, int32_t* out_x, int32_t* out_y);
static void uv_rect_from_region(int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, uint32_t tex_w, uint32_t tex_h, float out_uv[4]);
static void push_quad(Pigment* pigment, PStdCanvas* canvas, float x, float y, float w, float h, const float color[4], int32_t image_idx, int32_t sampler_idx, const float uv_rect[4]);

static void release_canvas_resources(Pigment* pigment, PStdCanvasResources* resources)
{
    if(resources != NULL && atomic_fetch_sub_explicit(&resources->references, 1, memory_order_acq_rel) == 1)
    {
        pigment_destroy_layout(pigment, resources->layout);
        P_FREE(pigment, resources);
    }
}

PStdCanvas* pigment_std_create_canvas(Pigment* pigment, const PStdCanvasConfig* config)
{
    if(pigment == NULL || config == NULL)
    {
        return NULL;
    }

    if(config->bindless == NULL)
    {
        PLOG_ERROR(pigment, "PStdCanvasConfig.bindless is required (the fragment shader samples the bindless image table).");
        return NULL;
    }

    static const PBlendMode default_blend_modes[] = {P_BLEND_MODE_ALPHA};
    const PBlendMode* blend_modes                 = config->blend_modes;
    uint32_t blend_mode_count                     = config->blend_mode_count;
    if(blend_modes == NULL || blend_mode_count == 0)
    {
        blend_modes      = default_blend_modes;
        blend_mode_count = 1;
    }

    PSampleCount samples       = (config->samples == 0) ? P_SAMPLE_COUNT_1 : config->samples;
    uint32_t initial_instances = (config->initial_instances == 0) ? DEFAULT_INITIAL_INSTANCES : config->initial_instances;

    PStdCanvas* canvas = P_NEW_FOR_OBJECT(pigment, canvas);
    if(canvas == NULL)
    {
        return NULL;
    }
    canvas->resources = P_NEW_FOR_OBJECT(pigment, canvas->resources);
    if(canvas->resources == NULL)
    {
        P_FREE(pigment, canvas);
        return NULL;
    }
    atomic_init(&canvas->resources->references, 1);
    canvas->bindless    = config->bindless;
    canvas->slots       = NULL;
    canvas->slot_count  = 0;
    canvas->active_slot = NULL;

    PDescriptorSetLayout* bindless_layout = pigment_std_bindless_layout(config->bindless);

    PLayoutDesc layout_desc = {
        .set_layouts      = &bindless_layout,
        .set_layout_count = 1,
        .push_size        = sizeof(PStdCanvasPushConstants),
        .push_stages      = P_SHADER_STAGE_VERTEX_BIT | P_SHADER_STAGE_FRAGMENT_BIT,
        .name             = "std_canvas_layout",
    };

    canvas->resources->layout = pigment_create_layout(pigment, &layout_desc);
    if(canvas->resources->layout == NULL)
    {
        goto FREE;
    }

    canvas->slots = P_NEW_ARRAY_FOR_OBJECT(pigment, canvas->slots, blend_mode_count);
    if(canvas->slots == NULL)
    {
        goto FREE;
    }

    PFormat color_format = config->color_format;
    for(uint32_t i = 0; i < blend_mode_count; i++)
    {
        PBlendMode mode = blend_modes[i];

        PPipelineDesc pipeline_desc = {
            .layout               = canvas->resources->layout,
            .vertex_shader        = (const uint32_t*) ndc_vert_spv,
            .vertex_shader_size   = (uint32_t) ndc_vert_spv_size,
            .fragment_shader      = (const uint32_t*) ndc_frag_spv,
            .fragment_shader_size = (uint32_t) ndc_frag_spv_size,
            .color_formats        = &color_format,
            .color_format_count   = 1,
            .blend_modes          = &mode,
            .blend_mode_count     = 1,
            .depth_format         = P_FORMAT_UNDEFINED,
            .polygon_mode         = P_POLYGON_MODE_FILL,
            .topology             = P_TOPOLOGY_TRIANGLE_LIST,
            .sample_count         = samples,
            .name                 = "std_canvas_pipeline",
        };

        PPipeline* pipeline = NULL;
        if(pigment_create_graphic_pipelines(pigment, NULL, &pipeline_desc, 1, &pipeline) != PIGMENT_SUCCESS)
        {
            goto FREE;
        }

        PInstancesDesc instances_desc = {
            .instance_size      = sizeof(PStdCanvasInstance),
            .initial_capacity   = initial_instances,
            .shared_queues      = config->shared_queues,
            .shared_queue_count = config->shared_queue_count,
        };

        PInstances* instances = pigment_std_create_instances(pigment, &instances_desc);
        if(instances == NULL)
        {
            pigment_destroy_pipeline(pigment, pipeline);
            goto FREE;
        }

        canvas->slots[i].mode      = mode;
        canvas->slots[i].pipeline  = pipeline;
        canvas->slots[i].instances = instances;
        canvas->slot_count         = i + 1;
    }

    return canvas;

FREE:
    pigment_std_destroy_canvas(pigment, canvas);
    return NULL;
}

void pigment_std_destroy_canvas(Pigment* pigment, PStdCanvas* canvas)
{
    if(pigment == NULL || canvas == NULL)
    {
        return;
    }
    if(canvas->slots != NULL)
    {
        for(uint32_t i = 0; i < canvas->slot_count; i++)
        {
            if(canvas->slots[i].instances != NULL)
            {
                pigment_std_destroy_instances(pigment, canvas->slots[i].instances);
            }
            if(canvas->slots[i].pipeline != NULL)
            {
                pigment_destroy_pipeline(pigment, canvas->slots[i].pipeline);
            }
        }
        P_FREE(pigment, canvas->slots);
    }
    release_canvas_resources(pigment, canvas->resources);
    P_FREE(pigment, canvas);
}

PPipeline* pigment_std_canvas_pipeline(PStdCanvas* canvas, PBlendMode blend_mode)
{
    PStdCanvasBlendSlot* slot = find_slot_for_mode(canvas, blend_mode);
    return (slot != NULL) ? slot->pipeline : NULL;
}

void pigment_std_canvas_begin(Pigment* pigment, PStdCanvas* canvas, uint32_t target_w, uint32_t target_h, PBlendMode blend_mode)
{
    if(pigment == NULL || canvas == NULL)
    {
        return;
    }
    canvas->active_slot = find_slot_for_mode(canvas, blend_mode);
    if(canvas->active_slot != NULL)
    {
        pigment_std_instances_reset(canvas->active_slot->instances);
        canvas->target_w = target_w;
        canvas->target_h = target_h;
    }
}

PStdCanvasBatch* pigment_std_canvas_prepare(Pigment* pigment, PStdCanvas* canvas)
{
    if(pigment == NULL || canvas == NULL || canvas->active_slot == NULL
       || pigment_std_instances_count(canvas->active_slot->instances) == 0)
    {
        return NULL;
    }
    PStdCanvasBatch* batch = P_NEW_FOR_OBJECT(pigment, batch);
    if(batch == NULL)
    {
        return NULL;
    }
    batch->resources = canvas->resources;
    atomic_fetch_add_explicit(&batch->resources->references, 1, memory_order_relaxed);
    batch->pipeline           = canvas->active_slot->pipeline;
    batch->pipeline_reference = pigment_retain_resource((PResource) {.type = P_RESOURCE_TYPE_PIPELINE, .pipeline = batch->pipeline});
    batch->bindless           = pigment_std_bindless_prepare(pigment, canvas->bindless);
    batch->instances          = pigment_std_instances_prepare(pigment, canvas->active_slot->instances);
    if(batch->bindless == NULL || batch->instances == NULL)
    {
        pigment_std_release_canvas_batch(pigment, batch);
        return NULL;
    }
    canvas->active_slot = NULL;
    return batch;
}

void pigment_std_draw_canvas(Pigment* pigment, PCommandBuffer* cmd, const PStdCanvasBatch* batch)
{
    if(pigment == NULL || cmd == NULL || batch == NULL)
    {
        return;
    }
    PDescriptorSet* set = pigment_std_bindless_version_set(pigment, batch->bindless, cmd);
    pigment_bind_pipeline(pigment, cmd, batch->pipeline);
    pigment_cmd_bind_descriptor_sets(pigment, cmd, batch->pipeline, 0, &set, 1, NULL, 0);
    pigment_cmd_set_cull(pigment, cmd, P_CULL_MODE_NONE, P_FRONT_FACE_COUNTER_CLOCKWISE);
    pigment_cmd_set_depth(pigment, cmd, P_FALSE, P_FALSE, P_COMPARE_OP_ALWAYS);
    PStdCanvasPushConstants push = {
        .instance_buffer_address = pigment_std_buffer_version_address(pigment, batch->instances, cmd),
    };
    pigment_cmd_push_constants(pigment, cmd, batch->pipeline, 0, sizeof(push), &push);
    pigment_cmd_draw(pigment, cmd, 6, pigment_std_buffer_version_count(batch->instances), 0, 0);
}

void pigment_std_release_canvas_batch(Pigment* pigment, PStdCanvasBatch* batch)
{
    if(pigment == NULL || batch == NULL)
    {
        return;
    }
    pigment_release_resource(pigment, batch->pipeline_reference);
    release_canvas_resources(pigment, batch->resources);
    pigment_std_release_bindless_version(pigment, batch->bindless);
    pigment_std_release_buffer_version(pigment, batch->instances);
    P_FREE(pigment, batch);
}

void pigment_std_canvas_rect_ndc(Pigment* pigment, PStdCanvas* canvas, float x, float y, float w, float h, const float color[4])
{
    push_quad(pigment, canvas, x, y, w, h, color, -1, -1, FULL_UV);
}

void pigment_std_canvas_rect_pixel(Pigment* pigment, PStdCanvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    float ndc_x = 0.0f, ndc_y = 0.0f, ndc_w = 0.0f, ndc_h = 0.0f;
    pixel_to_ndc(canvas->target_w, canvas->target_h, x, y, w, h, &ndc_x, &ndc_y, &ndc_w, &ndc_h);
    pigment_std_canvas_rect_ndc(pigment, canvas, ndc_x, ndc_y, ndc_w, ndc_h, color);
}

void pigment_std_canvas_rect_anchor(Pigment* pigment, PStdCanvas* canvas, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    int32_t base_x = 0, base_y = 0;
    anchor_to_pixel(canvas, anchor, offset_x, offset_y, w, h, &base_x, &base_y);
    pigment_std_canvas_rect_pixel(pigment, canvas, base_x, base_y, w, h, color);
}

void pigment_std_canvas_image_ndc(Pigment* pigment, PStdCanvas* canvas, float x, float y, float w, float h, uint32_t image_idx, uint32_t sampler_idx, const float color[4])
{
    push_quad(pigment, canvas, x, y, w, h, color, (int32_t) image_idx, (int32_t) sampler_idx, FULL_UV);
}

void pigment_std_canvas_image_pixel(Pigment* pigment, PStdCanvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    float ndc_x = 0.0f, ndc_y = 0.0f, ndc_w = 0.0f, ndc_h = 0.0f;
    pixel_to_ndc(canvas->target_w, canvas->target_h, x, y, w, h, &ndc_x, &ndc_y, &ndc_w, &ndc_h);
    pigment_std_canvas_image_ndc(pigment, canvas, ndc_x, ndc_y, ndc_w, ndc_h, image_idx, sampler_idx, color);
}

void pigment_std_canvas_image_anchor(Pigment* pigment, PStdCanvas* canvas, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    int32_t base_x = 0, base_y = 0;
    anchor_to_pixel(canvas, anchor, offset_x, offset_y, w, h, &base_x, &base_y);
    pigment_std_canvas_image_pixel(pigment, canvas, base_x, base_y, w, h, image_idx, sampler_idx, color);
}

void pigment_std_canvas_image_region_ndc(Pigment* pigment, PStdCanvas* canvas, float x, float y, float w, float h, uint32_t image_idx, uint32_t sampler_idx, int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, const float color[4])
{
    if(canvas == NULL)
    {
        return;
    }

    PImage* image  = pigment_std_bindless_image(canvas->bindless, image_idx);
    uint32_t tex_w = (image != NULL) ? pigment_image_width(image) : 0;
    uint32_t tex_h = (image != NULL) ? pigment_image_height(image) : 0;
    if(tex_w == 0 || tex_h == 0)
    {
        return;
    }

    float uv_rect[4];
    uv_rect_from_region(src_x, src_y, src_w, src_h, tex_w, tex_h, uv_rect);
    push_quad(pigment, canvas, x, y, w, h, color, (int32_t) image_idx, (int32_t) sampler_idx, uv_rect);
}

void pigment_std_canvas_image_region_pixel(Pigment* pigment, PStdCanvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    float ndc_x = 0.0f, ndc_y = 0.0f, ndc_w = 0.0f, ndc_h = 0.0f;
    pixel_to_ndc(canvas->target_w, canvas->target_h, x, y, w, h, &ndc_x, &ndc_y, &ndc_w, &ndc_h);
    pigment_std_canvas_image_region_ndc(pigment, canvas, ndc_x, ndc_y, ndc_w, ndc_h, image_idx, sampler_idx, src_x, src_y, src_w, src_h, color);
}

void pigment_std_canvas_image_region_anchor(Pigment* pigment, PStdCanvas* canvas, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    int32_t base_x = 0, base_y = 0;
    anchor_to_pixel(canvas, anchor, offset_x, offset_y, w, h, &base_x, &base_y);
    pigment_std_canvas_image_region_pixel(pigment, canvas, base_x, base_y, w, h, image_idx, sampler_idx, src_x, src_y, src_w, src_h, color);
}

static PStdCanvasBlendSlot* find_slot_for_mode(PStdCanvas* canvas, PBlendMode mode)
{
    if(canvas == NULL)
    {
        return NULL;
    }
    for(uint32_t i = 0; i < canvas->slot_count; i++)
    {
        if(canvas->slots[i].mode == mode)
        {
            return &canvas->slots[i];
        }
    }
    return NULL;
}

static void pixel_to_ndc(uint32_t screen_w, uint32_t screen_h, int32_t x_px, int32_t y_px, int32_t w_px, int32_t h_px, float* out_pos_x, float* out_pos_y, float* out_size_x, float* out_size_y)
{
    *out_pos_x  = (float) x_px / (float) screen_w * 2.0f - 1.0f;
    *out_pos_y  = (float) y_px / (float) screen_h * 2.0f - 1.0f;
    *out_size_x = (float) w_px / (float) screen_w * 2.0f;
    *out_size_y = (float) h_px / (float) screen_h * 2.0f;
}

static void anchor_to_pixel(const PStdCanvas* canvas, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, int32_t* out_x, int32_t* out_y)
{
    int32_t sw = (int32_t) canvas->target_w;
    int32_t sh = (int32_t) canvas->target_h;

    int32_t base_x = offset_x;
    int32_t base_y = offset_y;

    // Horizontal alignment.
    switch(anchor)
    {
        case P_STD_CANVAS_ANCHOR_TOP_LEFT:
        case P_STD_CANVAS_ANCHOR_MIDDLE_LEFT:
        case P_STD_CANVAS_ANCHOR_BOTTOM_LEFT:
            base_x = offset_x;
            break;
        case P_STD_CANVAS_ANCHOR_TOP_CENTER:
        case P_STD_CANVAS_ANCHOR_CENTER:
        case P_STD_CANVAS_ANCHOR_BOTTOM_CENTER:
            base_x = sw / 2 - w / 2 + offset_x;
            break;
        case P_STD_CANVAS_ANCHOR_TOP_RIGHT:
        case P_STD_CANVAS_ANCHOR_MIDDLE_RIGHT:
        case P_STD_CANVAS_ANCHOR_BOTTOM_RIGHT:
            base_x = sw - w - offset_x;
            break;
    }

    // Vertical alignment.
    switch(anchor)
    {
        case P_STD_CANVAS_ANCHOR_TOP_LEFT:
        case P_STD_CANVAS_ANCHOR_TOP_CENTER:
        case P_STD_CANVAS_ANCHOR_TOP_RIGHT:
            base_y = offset_y;
            break;
        case P_STD_CANVAS_ANCHOR_MIDDLE_LEFT:
        case P_STD_CANVAS_ANCHOR_CENTER:
        case P_STD_CANVAS_ANCHOR_MIDDLE_RIGHT:
            base_y = sh / 2 - h / 2 + offset_y;
            break;
        case P_STD_CANVAS_ANCHOR_BOTTOM_LEFT:
        case P_STD_CANVAS_ANCHOR_BOTTOM_CENTER:
        case P_STD_CANVAS_ANCHOR_BOTTOM_RIGHT:
            base_y = sh - h - offset_y;
            break;
    }

    if(out_x != NULL)
    {
        *out_x = base_x;
    }
    if(out_y != NULL)
    {
        *out_y = base_y;
    }
}

static void uv_rect_from_region(int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, uint32_t tex_w, uint32_t tex_h, float out_uv[4])
{
    out_uv[0] = (float) src_x / (float) tex_w;
    out_uv[1] = (float) src_y / (float) tex_h;
    out_uv[2] = (float) (src_x + src_w) / (float) tex_w;
    out_uv[3] = (float) (src_y + src_h) / (float) tex_h;
}

static void push_quad(Pigment* pigment, PStdCanvas* canvas, float x, float y, float w, float h, const float color[4], int32_t image_idx, int32_t sampler_idx, const float uv_rect[4])
{
    if(pigment == NULL || canvas == NULL || color == NULL || canvas->active_slot == NULL)
    {
        return;
    }

    PStdCanvasInstance* inst = (PStdCanvasInstance*) pigment_std_instances_alloc(pigment, canvas->active_slot->instances, 1);
    if(inst == NULL)
    {
        return;
    }

    inst->transform[0][0] = w;
    inst->transform[0][1] = 0;
    inst->transform[0][2] = 0;
    inst->transform[0][3] = 0;

    inst->transform[1][0] = 0;
    inst->transform[1][1] = h;
    inst->transform[1][2] = 0;
    inst->transform[1][3] = 0;

    inst->transform[2][0] = 0;
    inst->transform[2][1] = 0;
    inst->transform[2][2] = 1;
    inst->transform[2][3] = 0;

    inst->transform[3][0] = x;
    inst->transform[3][1] = y;
    inst->transform[3][2] = 0;
    inst->transform[3][3] = 1;

    inst->color[0] = color[0];
    inst->color[1] = color[1];
    inst->color[2] = color[2];
    inst->color[3] = color[3];

    inst->uv_rect[0] = uv_rect[0];
    inst->uv_rect[1] = uv_rect[1];
    inst->uv_rect[2] = uv_rect[2];
    inst->uv_rect[3] = uv_rect[3];

    inst->image_idx   = image_idx;
    inst->sampler_idx = sampler_idx;
}
