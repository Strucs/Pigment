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

#ifndef PIGMENT_STD_RENDER_TARGETS_H
#define PIGMENT_STD_RENDER_TARGETS_H

#include "defines.h"
#include "frame.h"
#include "image.h"

typedef struct PRenderTarget PRenderTarget;

typedef struct PAttachmentDesc {
    PFormat format;
    PImageUsage extra_usage;

    /**
     *   scale > 0  -> auto-resize to (swapchain_size * scale) and track swapchain.
     *   scale == 0 -> fixed at (width, height).
     *                          height = width / aspect_ratio
     */
    float scale;

    uint32_t width;
    uint32_t height;

    /**
     *     scale > 0, aspect_ratio > 0   -> width = swapchain_w * scale,
     *                                      height = width / aspect_ratio
     *     scale == 0, aspect_ratio > 0  -> width = desc->width
     *                                      height = width / aspect_ratio if desc->height == 0,
     *                                      otherwise height = desc->height
     */
    float aspect_ratio;
} PAttachmentDesc;

typedef struct PRenderTargetDesc {
    uint32_t window_index;
    const PAttachmentDesc* colors;    // NULL allowed if color_count == 0
    uint32_t color_count;
    PAttachmentDesc depth;    // depth.format == 0 -> no depth target
} PRenderTargetDesc;

PRenderTarget* pigment_std_create_render_target(Pigment* pigment, const PRenderTargetDesc* desc);
void pigment_std_destroy_render_target(Pigment* pigment, PRenderTarget* target);

PImage** pigment_std_render_target_colors(PRenderTarget* target);
uint32_t pigment_std_render_target_color_count(PRenderTarget* target);
PImage* pigment_std_render_target_depth(PRenderTarget* target);
uint32_t pigment_std_render_target_width(PRenderTarget* target);
uint32_t pigment_std_render_target_height(PRenderTarget* target);
uint32_t pigment_std_render_target_generation(PRenderTarget* target);

PAttachmentRef pigment_std_render_target_color_ref(PRenderTarget* target, uint32_t index);
PAttachmentRef pigment_std_render_target_depth_ref(PRenderTarget* target);
PAttachmentRef pigment_std_render_target_color_layer_ref(PRenderTarget* target, uint32_t index, uint32_t base_layer, uint32_t layer_count);
PAttachmentRef pigment_std_render_target_depth_layer_ref(PRenderTarget* target, uint32_t base_layer, uint32_t layer_count);

#endif
