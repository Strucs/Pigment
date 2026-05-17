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
    P_IMAGE_UPLOAD_MIPMAPS = 1 << 0    // allocate a full mip chain (populate levels 1+ via pigment_cmd_generate_mipmaps)
} PImageUploadFlags;

typedef struct PImageUploadDesc {
    const unsigned char* const* layers;    // layer_count pointers.
    uint32_t layer_count;                  // 0/1 = single, 6 = cube, N = array, 6*N = cube array
    uint32_t width;
    uint32_t height;
    uint32_t depth;    // 0/1 except P_IMAGE_TYPE_3D
    PFormat format;
    PImageType type;                       // 0 = 2D
    PImageUploadFlags flags;               // 0 = single mip
    PDeviceQueue* const* shared_queues;    // NULL = EXCLUSIVE. List every queue that touches the image (upload, finalize, sampling).
    uint32_t shared_queue_count;
} PImageUploadDesc;

/**
 * @brief Upload data to `count` buffers in one command buffer and submit.
 *
 * Host-mapped destinations are written directly without staging. The pool must belong to the
 * family of `queue`. A destination buffer touched by more than one queue family (uploaded on one,
 * consumed on another) must be created CONCURRENT across those queues.
 *
 * @param pigment Pigment instance.
 * @param pool Pool for the transfer command buffer.
 * @param queue Queue to submit on, or NULL for the default graphics queue.
 * @param uploads Array of count buffer uploads.
 * @param count Number of uploads.
 * @param out_handle Optional, receives the submit handle.
 *
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PResult pigment_std_buffer_upload(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, const PBufferUploadDesc* uploads, uint32_t count, PSubmitHandle* out_handle);

/**
 * @brief Create `count` images from the pixel data in `uploads`.
 *
 * Images are created with P_IMAGE_LAYOUT_TRANSFER_DST. Use pigment_std_image_finalize
 * to populate the mip chain (when P_IMAGE_UPLOAD_MIPMAPS was set) and transition to a sampleable
 * layout. The pool must belong to the family of `queue`. Sync `out_handle` before using the
 * images on a queue from a different family than `queue`.
 *
 * @param pigment Pigment instance.
 * @param pool Pool for the transfer command buffer.
 * @param queue Queue to submit on, or NULL for the default graphics queue.
 * @param uploads Array of count image descriptions.
 * @param count Number of uploads.
 * @param out_images Array of count slots for the created images.
 * @param out_handle Optional, receives the submit handle.
 *
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PResult pigment_std_image_upload(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, const PImageUploadDesc* uploads, uint32_t count, PImage** out_images, PSubmitHandle* out_handle);

/**
 * @brief Generate mip chains and transition uploaded images to a sampleable layout. Must run on a graphics queue.
 *
 * @param pigment Pigment instance.
 * @param pool Pool for the command buffer.
 * @param queue Queue to submit on, or NULL for the default graphics queue.
 * @param images Array of count images returned by pigment_std_image_upload.
 * @param uploads Array of count descriptions matching images (the same array passed to the upload).
 * @param count Number of images.
 * @param out_handle Optional, receives the submit handle.
 *
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PResult pigment_std_image_finalize(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, PImage* const* images, const PImageUploadDesc* uploads, uint32_t count, PSubmitHandle* out_handle);

#endif
