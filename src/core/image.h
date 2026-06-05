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

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

typedef enum PImageUsage {
    P_IMAGE_USAGE_SAMPLED       = 1 << 0,
    P_IMAGE_USAGE_RENDER_COLOR  = 1 << 1,
    P_IMAGE_USAGE_RENDER_DEPTH  = 1 << 2,
    P_IMAGE_USAGE_STORAGE       = 1 << 3,
    P_IMAGE_USAGE_TRANSFER_SRC  = 1 << 4,
    P_IMAGE_USAGE_TRANSFER_DST  = 1 << 5,
    P_IMAGE_USAGE_HOST_TRANSFER = 1 << 6,    // pigment_image_write / _read, needs P_FEATURE_HOST_IMAGE_COPY
} PImageUsage;

typedef enum PImageType {
    P_IMAGE_TYPE_2D         = 0,    // default; depth=1, array_layers=1
    P_IMAGE_TYPE_2D_ARRAY   = 1,    // array_layers = N
    P_IMAGE_TYPE_CUBE       = 2,    // array_layers must be 6
    P_IMAGE_TYPE_CUBE_ARRAY = 3,    // array_layers must be 6 * N
    P_IMAGE_TYPE_3D         = 4,    // depth = D
} PImageType;

typedef enum PImageFlags {
    P_IMAGE_FLAG_HOST_MAPPED = 1 << 0
} PImageFlags;

typedef enum PFormatFeature {
    P_FORMAT_FEATURE_SAMPLED                  = 1 << 0,
    P_FORMAT_FEATURE_SAMPLED_FILTER_LINEAR    = 1 << 1,
    P_FORMAT_FEATURE_STORAGE                  = 1 << 2,
    P_FORMAT_FEATURE_COLOR_ATTACHMENT         = 1 << 3,
    P_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND   = 1 << 4,
    P_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT = 1 << 5,
    P_FORMAT_FEATURE_BLIT_SRC                 = 1 << 6,
    P_FORMAT_FEATURE_BLIT_DST                 = 1 << 7,
    P_FORMAT_FEATURE_TRANSFER_SRC             = 1 << 8,
    P_FORMAT_FEATURE_TRANSFER_DST             = 1 << 9,
} PFormatFeature;

typedef struct PImageDesc {
    uint32_t width;
    uint32_t height;
    uint32_t depth;           // for P_IMAGE_TYPE_3D, otherwise 0/1
    uint32_t array_layers;    // for arrays/cubes, otherwise 0/1
    PFormat format;
    PImageUsage usage;
    PSampleCount samples;                  // 0 or P_SAMPLE_COUNT_1 for no MSAA
    uint32_t mip_levels;                   // 0 = single mip, otherwise full chain
    PImageType type;                       // 0 = 2D
    PImageFlags flags;                     // 0 = none. P_IMAGE_FLAG_HOST_MAPPED requires 2D, 1 mip, 1 layer, 1 sample
    PDeviceQueue* const* shared_queues;    // NULL = EXCLUSIVE (one queue family at a time, any family). List 2+ queues to share across their families.
    uint32_t shared_queue_count;
    const char* name;
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

typedef struct PHostImageCopy {
    void* host_pointer;    // source pixels for _write, destination for _read
    uint32_t memory_row_length;
    uint32_t memory_image_height;
    uint32_t mip_level;
    uint32_t base_array_layer;
    uint32_t layer_count;
    int32_t offset_x;
    int32_t offset_y;
    int32_t offset_z;
    uint32_t extent_w;
    uint32_t extent_h;
    uint32_t extent_d;
} PHostImageCopy;

typedef struct PHostImageTransition {
    PImage* image;
    PImageLayout old_layout;
    PImageLayout new_layout;
    uint32_t base_mip;
    uint32_t mip_count;    // 0 = remaining
    uint32_t base_layer;
    uint32_t layer_count;    // 0 = remaining
} PHostImageTransition;

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

typedef struct PFormatInfo {
    uint32_t block_width;
    uint32_t block_height;
    uint32_t block_size;
} PFormatInfo;

/**
 * @brief Return the block layout of a format.
 *
 * Uncompressed formats report a 1x1 block whose size is the texel size. Block-compressed
 * formats (BC, ETC2, ASTC) report their real block dimensions. Unknown or depth/stencil
 * formats report a zeroed struct.
 *
 * @param format Format to describe.
 *
 * @return Block width, height and byte size for the format.
 */
PIGMENT_API PFormatInfo pigment_format_info(PFormat format);

/**
 * @brief Return the byte size of one mip level of an image.
 *
 * The extent is rounded up to whole blocks, so the size is correct for block-compressed formats.
 *
 * @param format Texel format.
 * @param width Level width in texels.
 * @param height Level height in texels.
 *
 * @return Size in bytes, or 0 for a format without a defined block layout.
 */
PIGMENT_API uint64_t pigment_format_image_size(PFormat format, uint32_t width, uint32_t height);

/**
 * @brief Ask the driver what a format can be used for on the current device.
 *
 * Reports which operations an image of this format supports: sampling, linear filtering,
 * storage, color or depth attachment, blit and transfer. The answer covers normally created
 * images, not P_IMAGE_FLAG_HOST_MAPPED ones. Use it to pick a format or to check one before
 * creating an image.
 *
 * @param pigment Pigment instance.
 * @param format Format to query.
 *
 * @return Bitmask of supported PFormatFeature bits, or 0 if the format is unsupported.
 */
PIGMENT_API PFormatFeature pigment_format_features(Pigment* pigment, PFormat format);

PIGMENT_API void pigment_cmd_copy_buffer_to_image(Pigment* pigment, PCommandBuffer* cmd, PBuffer* src, PImage* dst, PImageLayout dst_layout, const PBufferImageCopy* regions, uint32_t region_count);
PIGMENT_API void pigment_cmd_copy_image_to_buffer(Pigment* pigment, PCommandBuffer* cmd, PImage* src, PImageLayout src_layout, PBuffer* dst, const PBufferImageCopy* regions, uint32_t region_count);
PIGMENT_API void pigment_cmd_copy_image(Pigment* pigment, PCommandBuffer* cmd, PImage* src, PImageLayout src_layout, PImage* dst, PImageLayout dst_layout, const PImageCopy* regions, uint32_t region_count);
PIGMENT_API void pigment_cmd_blit_image(Pigment* pigment, PCommandBuffer* cmd, PImage* src, PImageLayout src_layout, PImage* dst, PImageLayout dst_layout, const PImageBlit* regions, uint32_t region_count, PFilteringMode filter);

PIGMENT_API void pigment_cmd_generate_mipmaps(Pigment* pigment, PCommandBuffer* cmd, PImage* image, uint32_t base_layer, uint32_t layer_count, PImageLayout final_layout);

PIGMENT_API PImage* pigment_create_image(Pigment* pigment, const PImageDesc* desc);
PIGMENT_API void pigment_destroy_image(Pigment* pigment, PImage* image);
PIGMENT_API void pigment_image_resize(Pigment* pigment, PImage* image, uint32_t width, uint32_t height);

/**
 * @brief Get or create a cached view of an image from a description.
 * 
 * @param pigment Pigment instance.
 * @param image Image to view.
 * @param desc View description, zeroed fields inherit from the image.
 *
 * @return Cached view, or NULL on failure.
 */
PIGMENT_API PImageView* image_get_or_create_view(Pigment* pigment, PImage* image, const PImageViewDesc* desc);

PIGMENT_API uint32_t pigment_image_width(PImage* image);
PIGMENT_API uint32_t pigment_image_height(PImage* image);

/**
 * @brief Return the usage flags the image was actually created with.
 *
 * @param image Image to query.
 *
 * @return Bitmask of PImageUsage flags, or 0 if image is NULL.
 */
PIGMENT_API PImageUsage pigment_image_usage(PImage* image);

PIGMENT_API void* pigment_image_mapped(PImage* image);
PIGMENT_API uint64_t pigment_image_row_pitch(PImage* image);
PIGMENT_API void pigment_image_flush(Pigment* pigment, PImage* image);
PIGMENT_API void pigment_image_invalidate(Pigment* pigment, PImage* image);

/**
 * @brief Block until the GPU has finished every submitted command that used the image.
 *
 * @param pigment Pigment instance.
 * @param image Image to wait on.
 */
PIGMENT_API void pigment_image_wait(Pigment* pigment, PImage* image);

/**
 * Require P_FEATURE_HOST_IMAGE_COPY, on an image created with P_IMAGE_USAGE_HOST_TRANSFER.
 */

PIGMENT_API void pigment_image_write(Pigment* pigment, PImage* image, PImageLayout layout, const PHostImageCopy* regions, uint32_t region_count);
PIGMENT_API void pigment_image_read(Pigment* pigment, PImage* image, PImageLayout layout, const PHostImageCopy* regions, uint32_t region_count);
PIGMENT_API void pigment_image_host_transition(Pigment* pigment, const PHostImageTransition* transitions, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
