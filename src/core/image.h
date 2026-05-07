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

typedef enum PImageType {
    P_IMAGE_TYPE_2D         = 0,    // default; depth=1, array_layers=1
    P_IMAGE_TYPE_2D_ARRAY   = 1,    // array_layers = N
    P_IMAGE_TYPE_CUBE       = 2,    // array_layers must be 6
    P_IMAGE_TYPE_CUBE_ARRAY = 3,    // array_layers must be 6 * N
    P_IMAGE_TYPE_3D         = 4,    // depth = D
} PImageType;

typedef struct PImageDesc {
    uint32_t width;
    uint32_t height;
    uint32_t depth;           // for P_IMAGE_TYPE_3D, otherwise 0/1
    uint32_t array_layers;    // for arrays/cubes, otherwise 0/1
    PFormat format;
    PImageUsage usage;
    PSampleCount samples;    // 0 or P_SAMPLE_COUNT_1 for no MSAA
    uint32_t mip_levels;     // 0 = single mip, otherwise full chain
    PImageType type;         // 0 = 2D
} PImageDesc;

typedef struct PBufferImageCopy {
    uint64_t buffer_offset;
    uint32_t buffer_row_length;
    uint32_t buffer_image_height;
    uint32_t mip_level;
    uint32_t base_array_layer;
    uint32_t layer_count;
    int32_t offset_x;
    int32_t offset_y;
    int32_t offset_z;
    uint32_t extent_w;
    uint32_t extent_h;
    uint32_t extent_d;
} PBufferImageCopy;

typedef struct PImageCopy {
    uint32_t src_mip_level;
    uint32_t src_base_array_layer;
    uint32_t src_layer_count;
    int32_t src_offset_x;
    int32_t src_offset_y;
    int32_t src_offset_z;
    uint32_t dst_mip_level;
    uint32_t dst_base_array_layer;
    uint32_t dst_layer_count;
    int32_t dst_offset_x;
    int32_t dst_offset_y;
    int32_t dst_offset_z;
    uint32_t extent_w;
    uint32_t extent_h;
    uint32_t extent_d;
} PImageCopy;

typedef struct PImageBlit {
    uint32_t src_mip_level;
    uint32_t src_base_array_layer;
    uint32_t src_layer_count;
    int32_t src_min_x;
    int32_t src_min_y;
    int32_t src_min_z;
    int32_t src_max_x;
    int32_t src_max_y;
    int32_t src_max_z;
    uint32_t dst_mip_level;
    uint32_t dst_base_array_layer;
    uint32_t dst_layer_count;
    int32_t dst_min_x;
    int32_t dst_min_y;
    int32_t dst_min_z;
    int32_t dst_max_x;
    int32_t dst_max_y;
    int32_t dst_max_z;
} PImageBlit;

uint32_t pigment_format_pixel_size(PFormat format);
PBool pigment_format_supports_linear_blit(Pigment* pigment, PFormat format);

void pigment_cmd_copy_buffer_to_image(Pigment* pigment, PCommandBuffer* cmd, PBuffer* src, PImage* dst, PImageLayout dst_layout, const PBufferImageCopy* regions, uint32_t region_count);
void pigment_cmd_copy_image_to_buffer(Pigment* pigment, PCommandBuffer* cmd, PImage* src, PImageLayout src_layout, PBuffer* dst, const PBufferImageCopy* regions, uint32_t region_count);
void pigment_cmd_copy_image(Pigment* pigment, PCommandBuffer* cmd, PImage* src, PImageLayout src_layout, PImage* dst, PImageLayout dst_layout, const PImageCopy* regions, uint32_t region_count);
void pigment_cmd_blit_image(Pigment* pigment, PCommandBuffer* cmd, PImage* src, PImageLayout src_layout, PImage* dst, PImageLayout dst_layout, const PImageBlit* regions, uint32_t region_count, PFilteringMode filter);

void pigment_cmd_generate_mipmaps(Pigment* pigment, PCommandBuffer* cmd, PImage* image, uint32_t base_layer, uint32_t layer_count, PImageLayout final_layout);

PImage* pigment_create_image(Pigment* pigment, const PImageDesc* desc);
void pigment_destroy_image(Pigment* pigment, PImage* image);
void pigment_image_resize(Pigment* pigment, PImage* image, uint32_t width, uint32_t height);

uint32_t pigment_image_width(PImage* image);
uint32_t pigment_image_height(PImage* image);

#endif
