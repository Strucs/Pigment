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

#ifndef TEXTURE_H
#define TEXTURE_H

#include "defines.h"

typedef enum FilteringMode {
    NEAREST = 0,
    LINEAR  = 1,
} FilteringMode;

typedef enum PAddressMode {
    P_ADDRESS_MODE_REPEAT          = 0,
    P_ADDRESS_MODE_MIRRORED_REPEAT = 1,
    P_ADDRESS_MODE_CLAMP_TO_EDGE   = 2,
    P_ADDRESS_MODE_CLAMP_TO_BORDER = 3,
} PAddressMode;

struct PSamplerDesc {
    FilteringMode mag_filter;     // NEAREST or LINEAR
    FilteringMode min_filter;     // NEAREST or LINEAR
    FilteringMode mipmap_mode;    // NEAREST or LINEAR
    PAddressMode address_mode;    // 0 = REPEAT
};

PImageList* create_images(void);
uint32_t pigment_upload_image(Pigment* pigment, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format, uint32_t pool_index);
uint32_t pigment_upload_image_batch(Pigment* pigment, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count, uint32_t pool_index);
uint32_t pigment_add_sampler(Pigment* pigment, PSamplerDesc* desc);
int add_image_from_pixels(PImageList* image_list, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format, PCommandPools* command_pools, uint32_t pool_index, PDevice* device);
int add_default_image(PImageList* image_list, PCommandPools* command_pools, uint32_t pool_index, PDevice* device);
void destroy_images(PImageList* image_list, PDevice* device);
PSamplerList* create_samplers(uint32_t max_samplers, PDevice* device);
void destroy_samplers(PSamplerList* sampler_list, PDevice* device);

#endif
