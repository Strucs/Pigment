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

#ifndef PIGMENT_STD_UPLOAD_H
#define PIGMENT_STD_UPLOAD_H

#include "pigment/defines.h"
#include "pigment/image.h"

typedef struct PBufferUploadDesc {
    PBuffer* dst;
    const void* data;
    uint64_t size;
    uint64_t offset;
} PBufferUploadDesc;

typedef enum PImageUploadFlags {
    P_IMAGE_UPLOAD_MIPMAPS = 1 << 0
} PImageUploadFlags;

typedef struct PImageUploadDesc {
    const unsigned char* const* layers;    // layer_count pointers.
    uint32_t layer_count;                  // 0/1 = single, 6 = cube, N = array, 6*N = cube array
    uint32_t width;
    uint32_t height;
    uint32_t depth;                        // 0/1 except P_IMAGE_TYPE_3D
    PFormat format;
    PImageType type;                       // 0 = 2D
    PImageUploadFlags flags;               // 0 = single mip
} PImageUploadDesc;

/**
 * @brief Upload data to `count` buffers in one command buffer and submit.
 *
 * Host-mapped destinations are written directly without staging.
 *
 * @param pigment Pigment instance.
 * @param pool Pool for the transfer command buffer.
 * @param uploads Array of count buffer uploads.
 * @param count Number of uploads.
 * @param out_handle Optional, receives the submit handle.
 *
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PResult pigment_std_buffer_upload(Pigment* pigment, PCommandPool* pool, const PBufferUploadDesc* uploads, uint32_t count, PSubmitHandle* out_handle);

/**
 * @brief Create `count` images from the pixel data in `uploads`.
 *
 * Single mip unless P_IMAGE_UPLOAD_MIPMAPS asks for a full chain.
 *
 * @param pigment Pigment instance.
 * @param pool Pool for the transfer command buffer.
 * @param uploads Array of count image descriptions.
 * @param count Number of uploads.
 * @param out_images Array of count slots for the created images, ready to sample.
 * @param out_handle Optional, receives the submit handle.
 *
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PResult pigment_std_image_upload(Pigment* pigment, PCommandPool* pool, const PImageUploadDesc* uploads, uint32_t count, PImage** out_images, PSubmitHandle* out_handle);

#endif
