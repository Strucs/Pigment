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

#include "pigment/std/draw.h"

#include "ndc_vert_spv.h"
#include "ndc_frag_spv.h"

#define DEFAULT_MAX_INSTANCES 4096U
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
    PInstanceRing* ring;
} PStdCanvasBlendSlot;

struct PStdCanvas {
    PLayout* layout;
    PStdBindless* bindless;
    PStdCanvasBlendSlot* slots;
    uint32_t slot_count;

    PStdCanvasBlendSlot* active_slot;
    uint32_t target_w;
    uint32_t target_h;
};

static PStdCanvasBlendSlot* find_slot_for_mode(PStdCanvas* canvas, PBlendMode mode);
static void pixel_to_ndc(uint32_t screen_w, uint32_t screen_h, int32_t x_px, int32_t y_px, int32_t w_px, int32_t h_px, float* out_pos_x, float* out_pos_y, float* out_size_x, float* out_size_y);
static void anchor_to_pixel(const PStdCanvas* canvas, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, int32_t* out_x, int32_t* out_y);
static void uv_rect_from_region(int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, uint32_t tex_w, uint32_t tex_h, float out_uv[4]);
static void push_quad(Pigment* pigment, PStdCanvas* canvas, float x, float y, float w, float h, const float color[4], int32_t image_idx, int32_t sampler_idx, const float uv_rect[4]);

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

    PSampleCount samples   = (config->samples == 0) ? P_SAMPLE_COUNT_1 : config->samples;
    uint32_t max_instances = (config->max_instances_per_frame == 0) ? DEFAULT_MAX_INSTANCES : config->max_instances_per_frame;

    PStdCanvas* canvas = P_NEW_FOR_OBJECT(pigment, canvas);
    if(canvas == NULL)
    {
        return NULL;
    }
    canvas->layout      = NULL;
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

    canvas->layout = pigment_create_layout(pigment, &layout_desc);
    if(canvas->layout == NULL)
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
            .layout               = canvas->layout,
            .vertex_shader        = (const uint32_t*) ndc_vert_spv,
            .vertex_shader_size   = (uint32_t) sizeof(ndc_vert_spv),
            .fragment_shader      = (const uint32_t*) ndc_frag_spv,
            .fragment_shader_size = (uint32_t) sizeof(ndc_frag_spv),
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

        PInstanceRing* ring = pigment_std_create_instance_ring(pigment, sizeof(PStdCanvasInstance), max_instances);
        if(ring == NULL)
        {
            pigment_destroy_pipeline(pigment, pipeline);
            goto FREE;
        }

        canvas->slots[i].mode     = mode;
        canvas->slots[i].pipeline = pipeline;
        canvas->slots[i].ring     = ring;
        canvas->slot_count        = i + 1;
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
            if(canvas->slots[i].ring != NULL)
            {
                pigment_std_destroy_instance_ring(pigment, canvas->slots[i].ring);
            }
            if(canvas->slots[i].pipeline != NULL)
            {
                pigment_destroy_pipeline(pigment, canvas->slots[i].pipeline);
            }
        }
        P_FREE(pigment, canvas->slots);
    }
    if(canvas->layout != NULL)
    {
        pigment_destroy_layout(pigment, canvas->layout);
    }
    P_FREE(pigment, canvas);
}

PPipeline* pigment_std_canvas_pipeline(PStdCanvas* canvas, PBlendMode blend_mode)
{
    PStdCanvasBlendSlot* slot = find_slot_for_mode(canvas, blend_mode);
    return (slot != NULL) ? slot->pipeline : NULL;
}

void pigment_std_canvas_begin(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, uint32_t frame_index, uint32_t target_w, uint32_t target_h, PBlendMode blend_mode)
{
    if(pigment == NULL || canvas == NULL || cmd == NULL)
    {
        return;
    }

    PStdCanvasBlendSlot* slot = find_slot_for_mode(canvas, blend_mode);
    if(slot == NULL)
    {
        return;
    }

    pigment_std_instance_ring_sync_frame(slot->ring, frame_index);
    pigment_std_instance_ring_use(pigment, cmd, slot->ring);

    canvas->active_slot = slot;
    canvas->target_w    = target_w;
    canvas->target_h    = target_h;

    pigment_bind_pipeline(pigment, cmd, slot->pipeline);

    PDescriptorSet* bindless_set = pigment_std_bindless_set(pigment, canvas->bindless, cmd, frame_index);
    pigment_cmd_bind_descriptor_sets(pigment, cmd, slot->pipeline, 0, &bindless_set, 1, NULL, 0);

    pigment_cmd_set_depth(pigment, cmd, P_FALSE, P_FALSE, P_COMPARE_OP_ALWAYS);
}

void pigment_std_canvas_end(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd)
{
    if(pigment == NULL || canvas == NULL || cmd == NULL || canvas->active_slot == NULL)
    {
        return;
    }

    PStdCanvasBlendSlot* slot = canvas->active_slot;
    uint32_t instance_count   = pigment_std_instance_ring_cursor(slot->ring);
    if(instance_count == 0)
    {
        canvas->active_slot = NULL;
        return;
    }

    pigment_std_instance_ring_flush_range(pigment, slot->ring, 0, instance_count);

    PStdCanvasPushConstants push = {
        .instance_buffer_address = pigment_std_instance_ring_frame_address(slot->ring),
    };

    pigment_cmd_push_constants(pigment, cmd, slot->pipeline, 0, sizeof(push), &push);
    pigment_cmd_draw(pigment, cmd, 6, instance_count, 0, 0);

    canvas->active_slot = NULL;
}

void pigment_std_canvas_rect_ndc(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, float x, float y, float w, float h, const float color[4])
{
    (void) cmd;
    push_quad(pigment, canvas, x, y, w, h, color, -1, -1, FULL_UV);
}

void pigment_std_canvas_rect_pixel(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, int32_t x, int32_t y, int32_t w, int32_t h, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    float ndc_x = 0.0f, ndc_y = 0.0f, ndc_w = 0.0f, ndc_h = 0.0f;
    pixel_to_ndc(canvas->target_w, canvas->target_h, x, y, w, h, &ndc_x, &ndc_y, &ndc_w, &ndc_h);
    pigment_std_canvas_rect_ndc(pigment, canvas, cmd, ndc_x, ndc_y, ndc_w, ndc_h, color);
}

void pigment_std_canvas_rect_anchor(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    int32_t base_x = 0, base_y = 0;
    anchor_to_pixel(canvas, anchor, offset_x, offset_y, w, h, &base_x, &base_y);
    pigment_std_canvas_rect_pixel(pigment, canvas, cmd, base_x, base_y, w, h, color);
}

void pigment_std_canvas_image_ndc(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, float x, float y, float w, float h, uint32_t image_idx, uint32_t sampler_idx, const float color[4])
{
    (void) cmd;
    push_quad(pigment, canvas, x, y, w, h, color, (int32_t) image_idx, (int32_t) sampler_idx, FULL_UV);
}

void pigment_std_canvas_image_pixel(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    float ndc_x = 0.0f, ndc_y = 0.0f, ndc_w = 0.0f, ndc_h = 0.0f;
    pixel_to_ndc(canvas->target_w, canvas->target_h, x, y, w, h, &ndc_x, &ndc_y, &ndc_w, &ndc_h);
    pigment_std_canvas_image_ndc(pigment, canvas, cmd, ndc_x, ndc_y, ndc_w, ndc_h, image_idx, sampler_idx, color);
}

void pigment_std_canvas_image_anchor(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    int32_t base_x = 0, base_y = 0;
    anchor_to_pixel(canvas, anchor, offset_x, offset_y, w, h, &base_x, &base_y);
    pigment_std_canvas_image_pixel(pigment, canvas, cmd, base_x, base_y, w, h, image_idx, sampler_idx, color);
}

void pigment_std_canvas_image_region_ndc(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, float x, float y, float w, float h, uint32_t image_idx, uint32_t sampler_idx, int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, const float color[4])
{
    (void) cmd;
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

void pigment_std_canvas_image_region_pixel(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    float ndc_x = 0.0f, ndc_y = 0.0f, ndc_w = 0.0f, ndc_h = 0.0f;
    pixel_to_ndc(canvas->target_w, canvas->target_h, x, y, w, h, &ndc_x, &ndc_y, &ndc_w, &ndc_h);
    pigment_std_canvas_image_region_ndc(pigment, canvas, cmd, ndc_x, ndc_y, ndc_w, ndc_h, image_idx, sampler_idx, src_x, src_y, src_w, src_h, color);
}

void pigment_std_canvas_image_region_anchor(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, const float color[4])
{
    if(canvas == NULL || canvas->target_w == 0 || canvas->target_h == 0)
    {
        return;
    }

    int32_t base_x = 0, base_y = 0;
    anchor_to_pixel(canvas, anchor, offset_x, offset_y, w, h, &base_x, &base_y);
    pigment_std_canvas_image_region_pixel(pigment, canvas, cmd, base_x, base_y, w, h, image_idx, sampler_idx, src_x, src_y, src_w, src_h, color);
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

    switch(anchor)
    {
        case P_STD_CANVAS_ANCHOR_TOP_LEFT:
            base_x = offset_x;
            base_y = offset_y;
            break;
        case P_STD_CANVAS_ANCHOR_TOP_CENTER:
            base_x = sw / 2 - w / 2 + offset_x;
            base_y = offset_y;
            break;
        case P_STD_CANVAS_ANCHOR_TOP_RIGHT:
            base_x = sw - w - offset_x;
            base_y = offset_y;
            break;
        case P_STD_CANVAS_ANCHOR_MIDDLE_LEFT:
            base_x = offset_x;
            base_y = sh / 2 - h / 2 + offset_y;
            break;
        case P_STD_CANVAS_ANCHOR_CENTER:
            base_x = sw / 2 - w / 2 + offset_x;
            base_y = sh / 2 - h / 2 + offset_y;
            break;
        case P_STD_CANVAS_ANCHOR_MIDDLE_RIGHT:
            base_x = sw - w - offset_x;
            base_y = sh / 2 - h / 2 + offset_y;
            break;
        case P_STD_CANVAS_ANCHOR_BOTTOM_LEFT:
            base_x = offset_x;
            base_y = sh - h - offset_y;
            break;
        case P_STD_CANVAS_ANCHOR_BOTTOM_CENTER:
            base_x = sw / 2 - w / 2 + offset_x;
            base_y = sh - h - offset_y;
            break;
        case P_STD_CANVAS_ANCHOR_BOTTOM_RIGHT:
            base_x = sw - w - offset_x;
            base_y = sh - h - offset_y;
            break;
    }

    *out_x = base_x;
    *out_y = base_y;
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

    PStdCanvasInstance* inst = (PStdCanvasInstance*) pigment_std_instance_ring_alloc(pigment, canvas->active_slot->ring, 1);
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
