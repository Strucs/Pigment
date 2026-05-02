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

#ifndef PIGMENT_IMAGE_H
#define PIGMENT_IMAGE_H

#include "defines.h"

typedef enum PImageUsage {
    P_IMAGE_USAGE_SAMPLED      = 1 << 0,
    P_IMAGE_USAGE_RENDER_COLOR = 1 << 1,
    P_IMAGE_USAGE_RENDER_DEPTH = 1 << 2,
    P_IMAGE_USAGE_STORAGE      = 1 << 3,
    P_IMAGE_USAGE_TRANSFER_SRC = 1 << 4,
    P_IMAGE_USAGE_TRANSFER_DST = 1 << 5,
} PImageUsage;

typedef struct PImageDesc {
    uint32_t width;
    uint32_t height;
    PFormat format;
    PImageUsage usage;
    PSampleCount samples;    // 0 or P_SAMPLE_COUNT_1 for no MSAA
    uint32_t mip_levels;     // 0 = single mip, 1 = full mip chain
} PImageDesc;

PImage* pigment_create_image(Pigment* pigment, const PImageDesc* desc);
void pigment_destroy_image(Pigment* pigment, PImage* image);
void pigment_image_resize(Pigment* pigment, PImage* image, uint32_t width, uint32_t height);

uint32_t pigment_image_width(PImage* image);
uint32_t pigment_image_height(PImage* image);

/**
 * Track an image to a swapchain: when the swapchain is resized, the image is recreated with
 * width = swapchain.width * scale, height = swapchain.height * scale.
 * Untrack happens automatically when the image is destroyed.
 */
void pigment_image_track_swapchain(Pigment* pigment, PImage* image, uint32_t window_index, float scale);
void pigment_image_untrack(Pigment* pigment, PImage* image);

#endif
