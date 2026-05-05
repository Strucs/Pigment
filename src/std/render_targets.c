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

#include "render_targets.h"

#include "image.h"
#include "log_internal.h"
#include "pigment.h"
#include "resize.h"
#include "surface.h"

#include <stdlib.h>

struct PRenderTarget {
    PImage** colors;
    float* color_scales;
    float* color_aspect_ratios;
    uint32_t color_count;
    PImage* depth;
    float depth_scale;
    float depth_aspect_ratio;
    uint32_t window_index;
    uint32_t width;
    uint32_t height;
    uint32_t generation;
    uint32_t resize_handle;
};

static PImage* create_attachment(Pigment* pigment, const PAttachmentDesc* desc, PImageUsage attachment_usage, uint32_t fallback_w, uint32_t fallback_h, uint32_t* out_w, uint32_t* out_h);
static void on_swapchain_resize(Pigment* pigment, const PSwapchainResizeEvent* event, void* user_data);
static void resize_attachment(Pigment* pigment, PImage* image, float scale, float aspect_ratio, uint32_t base_w, uint32_t base_h);
static void compute_attachment_size(float scale, uint32_t base_w, uint32_t base_h, float aspect_ratio, uint32_t fallback_w, uint32_t fallback_h, uint32_t* out_w, uint32_t* out_h);
static bool any_attachment_tracked(const PRenderTargetDesc* desc);

PRenderTarget* pigment_std_create_render_target(Pigment* pigment, const PRenderTargetDesc* desc)
{
    if(pigment == NULL || desc == NULL)
    {
        return NULL;
    }

    PRenderTarget* target = calloc(1, sizeof(*target));
    if(target == NULL)
    {
        return NULL;
    }

    target->window_index       = desc->window_index;
    target->depth_scale        = desc->depth.scale;
    target->depth_aspect_ratio = desc->depth.aspect_ratio;

    uint32_t base_w = 0;
    uint32_t base_h = 0;

    PWindowRenderer* renderer = pigment_get_window_renderer(pigment, desc->window_index);
    if(renderer != NULL)
    {
        pigment_get_swapchain_size(renderer, &base_w, &base_h);
    }

    if(desc->color_count > 0 && desc->colors != NULL)
    {
        target->colors              = calloc(desc->color_count, sizeof(*target->colors));
        target->color_scales        = calloc(desc->color_count, sizeof(*target->color_scales));
        target->color_aspect_ratios = calloc(desc->color_count, sizeof(*target->color_aspect_ratios));
        if(target->colors == NULL || target->color_scales == NULL || target->color_aspect_ratios == NULL)
        {
            goto ERROR;
        }

        for(uint32_t i = 0; i < desc->color_count; i++)
        {
            uint32_t w = 0;
            uint32_t h = 0;

            target->colors[i]              = create_attachment(pigment, &desc->colors[i], P_IMAGE_USAGE_RENDER_COLOR, base_w, base_h, &w, &h);
            target->color_scales[i]        = desc->colors[i].scale;
            target->color_aspect_ratios[i] = desc->colors[i].aspect_ratio;
            if(target->colors[i] == NULL)
            {
                goto ERROR;
            }

            if(target->width == 0)
            {
                target->width  = w;
                target->height = h;
            }

            target->color_count++;
        }
    }

    if(desc->depth.format != P_FORMAT_UNDEFINED)
    {
        uint32_t w    = 0;
        uint32_t h    = 0;
        target->depth = create_attachment(pigment, &desc->depth, P_IMAGE_USAGE_RENDER_DEPTH, base_w, base_h, &w, &h);
        if(target->depth == NULL)
        {
            goto ERROR;
        }

        if(target->width == 0)
        {
            target->width  = w;
            target->height = h;
        }
    }

    if(any_attachment_tracked(desc))
    {
        target->resize_handle = pigment_register_swapchain_resize(pigment, on_swapchain_resize, target);
    }

    return target;

ERROR:
    PLOG_ERROR(pigment, "Failed to create render target");
    pigment_std_destroy_render_target(pigment, target);
    return NULL;
}

void pigment_std_destroy_render_target(Pigment* pigment, PRenderTarget* target)
{
    if(target == NULL)
    {
        return;
    }
    if(target->resize_handle != 0)
    {
        pigment_unregister_swapchain_resize(pigment, target->resize_handle);
    }
    if(target->colors != NULL)
    {
        for(uint32_t i = 0; i < target->color_count; i++)
        {
            pigment_destroy_image(pigment, target->colors[i]);
        }
        free(target->colors);
        free(target->color_scales);
        free(target->color_aspect_ratios);
    }

    pigment_destroy_image(pigment, target->depth);
    free(target);
}

PImage** pigment_std_render_target_colors(PRenderTarget* target)
{
    return (target != NULL) ? target->colors : NULL;
}

uint32_t pigment_std_render_target_color_count(PRenderTarget* target)
{
    return (target != NULL) ? target->color_count : 0;
}

PImage* pigment_std_render_target_depth(PRenderTarget* target)
{
    return (target != NULL) ? target->depth : NULL;
}

uint32_t pigment_std_render_target_width(PRenderTarget* target)
{
    return (target != NULL) ? target->width : 0;
}

uint32_t pigment_std_render_target_height(PRenderTarget* target)
{
    return (target != NULL) ? target->height : 0;
}

uint32_t pigment_std_render_target_generation(PRenderTarget* target)
{
    return (target != NULL) ? target->generation : 0;
}

PAttachmentRef pigment_std_render_target_color_ref(PRenderTarget* target, uint32_t index)
{
    if(target == NULL || index >= target->color_count)
    {
        return (PAttachmentRef) {0};
    }

    return (PAttachmentRef) {
        .image = target->colors[index]
    };
}

PAttachmentRef pigment_std_render_target_depth_ref(PRenderTarget* target)
{
    if(target == NULL)
    {
        return (PAttachmentRef) {0};
    }

    return (PAttachmentRef) {
        .image = target->depth
    };
}

PAttachmentRef pigment_std_render_target_color_layer_ref(PRenderTarget* target, uint32_t index, uint32_t base_layer, uint32_t layer_count)
{
    if(target == NULL || index >= target->color_count)
    {
        return (PAttachmentRef) {0};
    }

    return (PAttachmentRef) {
        .image       = target->colors[index],
        .base_layer  = base_layer,
        .layer_count = layer_count,
    };
}

PAttachmentRef pigment_std_render_target_depth_layer_ref(PRenderTarget* target, uint32_t base_layer, uint32_t layer_count)
{
    if(target == NULL)
    {
        return (PAttachmentRef) {0};
    }

    return (PAttachmentRef) {
        .image       = target->depth,
        .base_layer  = base_layer,
        .layer_count = layer_count,
    };
}

static void compute_attachment_size(float scale, uint32_t base_w, uint32_t base_h, float aspect_ratio, uint32_t fallback_w, uint32_t fallback_h, uint32_t* out_w, uint32_t* out_h)
{
    bool tracked = (scale > 0.0f);
    uint32_t w   = tracked ? (uint32_t) ((float) fallback_w * scale) : base_w;
    uint32_t h   = tracked ? (uint32_t) ((float) fallback_h * scale) : base_h;

    if(aspect_ratio > 0.0f)
    {
        h = (uint32_t) ((float) w / aspect_ratio);
    }

    if(w == 0)
    {
        w = 1;
    }
    if(h == 0)
    {
        h = 1;
    }

    *out_w = w;
    *out_h = h;
}

static PImage* create_attachment(Pigment* pigment, const PAttachmentDesc* desc, PImageUsage attachment_usage, uint32_t fallback_w, uint32_t fallback_h, uint32_t* out_w, uint32_t* out_h)
{
    compute_attachment_size(desc->scale, desc->width, desc->height, desc->aspect_ratio, fallback_w, fallback_h, out_w, out_h);

    PImageUsage usage = attachment_usage | desc->extra_usage;
    if(attachment_usage == P_IMAGE_USAGE_RENDER_COLOR)
    {
        usage |= P_IMAGE_USAGE_SAMPLED;
    }

    PImageDesc image_desc = {
        .width  = *out_w,
        .height = *out_h,
        .format = desc->format,
        .usage  = usage,
    };

    PImage* image = pigment_create_image(pigment, &image_desc);
    if(image == NULL)
    {
        return NULL;
    }

    return image;
}

static void on_swapchain_resize(Pigment* pigment, const PSwapchainResizeEvent* event, void* user_data)
{
    PRenderTarget* target = (PRenderTarget*) user_data;
    if(event->window_index != target->window_index)
    {
        return;
    }

    for(uint32_t i = 0; i < target->color_count; i++)
    {
        resize_attachment(pigment, target->colors[i], target->color_scales[i], target->color_aspect_ratios[i], event->width, event->height);
    }

    if(target->depth != NULL)
    {
        resize_attachment(pigment, target->depth, target->depth_scale, target->depth_aspect_ratio, event->width, event->height);
    }

    bool any_tracked = (target->depth_scale > 0.0f);
    for(uint32_t i = 0; i < target->color_count && !any_tracked; i++)
    {
        any_tracked = (target->color_scales[i] > 0.0f);
    }

    if(any_tracked)
    {
        target->width  = event->width;
        target->height = event->height;
        target->generation++;
    }
}

static void resize_attachment(Pigment* pigment, PImage* image, float scale, float aspect_ratio, uint32_t base_w, uint32_t base_h)
{
    if(image == NULL || scale <= 0.0f)
    {
        return;
    }

    uint32_t w = 0;
    uint32_t h = 0;
    compute_attachment_size(scale, 0, 0, aspect_ratio, base_w, base_h, &w, &h);

    pigment_image_resize(pigment, image, w, h);
}

static bool any_attachment_tracked(const PRenderTargetDesc* desc)
{
    if(desc->depth.format != P_FORMAT_UNDEFINED && desc->depth.scale > 0.0f)
    {
        return true;
    }

    for(uint32_t i = 0; i < desc->color_count; i++)
    {
        if(desc->colors[i].scale > 0.0f)
        {
            return true;
        }
    }

    return false;
}
