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

#include "ndc_vert_spv.h"
#include "ndc_frag_spv.h"

typedef struct PStdCanvasPushConstants {
    float pos[2];
    float size[2];
    float color[4];
} PStdCanvasPushConstants;

typedef struct PStdCanvasBlendSlot {
    PBlendMode mode;
    PPipeline* pipeline;
} PStdCanvasBlendSlot;

struct PStdCanvas {
    PLayout* layout;
    PStdCanvasBlendSlot* slots;
    uint32_t slot_count;
    PPipeline* active_pipeline;
};

static PPipeline* find_pipeline_for_mode(const PStdCanvas* canvas, PBlendMode mode)
{
    if(canvas == NULL)
    {
        return NULL;
    }
    for(uint32_t i = 0; i < canvas->slot_count; i++)
    {
        if(canvas->slots[i].mode == mode)
        {
            return canvas->slots[i].pipeline;
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

PStdCanvas* pigment_std_create_canvas(Pigment* pigment, const PStdCanvasConfig* config)
{
    if(pigment == NULL || config == NULL)
    {
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

    PSampleCount samples = (config->samples == 0) ? P_SAMPLE_COUNT_1 : config->samples;

    PStdCanvas* canvas = P_NEW_FOR_OBJECT(pigment, canvas);
    if(canvas == NULL)
    {
        return NULL;
    }
    canvas->layout          = NULL;
    canvas->slots           = NULL;
    canvas->slot_count      = 0;
    canvas->active_pipeline = NULL;

    PLayoutDesc layout_desc = {
        .set_layouts      = NULL,
        .set_layout_count = 0,
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

        canvas->slots[i].mode     = mode;
        canvas->slots[i].pipeline = pipeline;
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
    return find_pipeline_for_mode(canvas, blend_mode);
}

void pigment_std_canvas_begin(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, PBlendMode blend_mode)
{
    if(pigment == NULL || canvas == NULL || cmd == NULL)
    {
        return;
    }

    PPipeline* pipeline = find_pipeline_for_mode(canvas, blend_mode);
    if(pipeline == NULL)
    {
        return;
    }
    canvas->active_pipeline = pipeline;

    pigment_bind_pipeline(pigment, cmd, pipeline);
    pigment_cmd_set_depth(pigment, cmd, P_FALSE, P_FALSE, P_COMPARE_OP_ALWAYS);
}

void pigment_std_canvas_rect_ndc(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, float x, float y, float w, float h, const float color[4])
{
    if(pigment == NULL || canvas == NULL || cmd == NULL || color == NULL || canvas->active_pipeline == NULL)
    {
        return;
    }

    PStdCanvasPushConstants push = {
        .pos   = {x, y},
        .size  = {w, h},
        .color = {color[0], color[1], color[2], color[3]},
    };

    pigment_cmd_push_constants(pigment, cmd, canvas->active_pipeline, 0, sizeof(push), &push);
    pigment_cmd_draw(pigment, cmd, 6, 1, 0, 0);
}

void pigment_std_canvas_rect_pixel(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, PWindowRenderer* renderer, int32_t x, int32_t y, int32_t w, int32_t h, const float color[4])
{
    if(renderer == NULL)
    {
        return;
    }
    uint32_t screen_w = 0, screen_h = 0;
    pigment_get_swapchain_size(renderer, &screen_w, &screen_h);
    if(screen_w == 0 || screen_h == 0)
    {
        return;
    }

    float ndc_x = 0.0f, ndc_y = 0.0f, ndc_w = 0.0f, ndc_h = 0.0f;
    pixel_to_ndc(screen_w, screen_h, x, y, w, h, &ndc_x, &ndc_y, &ndc_w, &ndc_h);
    pigment_std_canvas_rect_ndc(pigment, canvas, cmd, ndc_x, ndc_y, ndc_w, ndc_h, color);
}

void pigment_std_canvas_rect_anchor(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, PWindowRenderer* renderer, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, const float color[4])
{
    if(renderer == NULL)
    {
        return;
    }
    uint32_t screen_w = 0, screen_h = 0;
    pigment_get_swapchain_size(renderer, &screen_w, &screen_h);
    if(screen_w == 0 || screen_h == 0)
    {
        return;
    }

    int32_t base_x = 0;
    int32_t base_y = 0;
    int32_t sw     = (int32_t) screen_w;
    int32_t sh     = (int32_t) screen_h;

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

    pigment_std_canvas_rect_pixel(pigment, canvas, cmd, renderer, base_x, base_y, w, h, color);
}
