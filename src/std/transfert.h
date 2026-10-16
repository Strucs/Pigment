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

#ifdef __cplusplus
extern "C" {
#endif

#include "pigment/defines.h"
#include "pigment/image.h"

typedef struct PBufferUploadDesc {
    PBuffer* dst;
    const void* data;
    uint64_t size;
    uint64_t offset;
} PBufferUploadDesc;

typedef enum PImageUploadFlags {
    P_IMAGE_GENERATE_MIPMAPS = 1 << 0    // generate a full mip chain by blit from level 0. Mutually exclusive with mip_count > 1.
} PImageUploadFlags;

typedef struct PImageUploadDesc {
    /*
     * Pixel data, one pointer per (mip, layer) cell. Length = mip_count * layer_count.
     *
     *   [m0 l0][m0 l1][m0 l2]  [m1 l0][m1 l1][m1 l2]  [m2 l0]...
     *    \____ mip 0 _______/   \____ mip 1 _______/
     */
    const unsigned char* const* layers;
    uint32_t layer_count;    // 0/1 = single, 6 = cube, N = array, 6*N = cube array
    uint32_t mip_count;      // 0/1 = level 0 only. >1 = precomputed mip chain supplied in layers.
    uint32_t width;
    uint32_t height;
    uint32_t depth;    // 0/1 except P_IMAGE_TYPE_3D
    PFormat format;
    PImageType type;                       // 0 = 2D
    PImageUploadFlags flags;               // 0 = single mip
    PDeviceQueue* const* shared_queues;    // NULL = EXCLUSIVE. List every queue that touches the image (upload, finalize, sampling).
    uint32_t shared_queue_count;
    const char* name;
} PImageUploadDesc;

/**
 * @brief Record buffer uploads without submitting.
 *
 * `cmd` must be recording. Share destinations across accessing queue families and supply GPU waits.
 * All copies use staging. Destinations need P_BUFFER_USAGE_TRANSFER_DST and change only on GPU execution.
 * CPU data can be freed on return. Staging is retained by `cmd` until reset or destruction.
 * On failure, discard the recording. Destination contents remain unchanged.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer for the copies.
 * @param uploads Array of count uploads.
 * @param count Number of uploads.
 *
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PIGMENT_API PResult pigment_std_cmd_buffer_upload(Pigment* pigment, PCommandBuffer* cmd, const PBufferUploadDesc* uploads, uint32_t count);

/**
 * @brief Create images and record their uploads without submitting.
 *
 * `cmd` must be recording. All copies use staging. CPU data can be freed on return.
 * Images reach TRANSFER_DST after execution. Use pigment_std_cmd_image_finalize before sampling.
 * List all accessing queues in shared_queues and supply waits between dependent submissions.
 * On failure, outputs are destroyed and cleared. Discard the recording.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer for the copies.
 * @param uploads Array of count image descriptions.
 * @param count Number of uploads.
 * @param out_images Array of count slots for the created images.
 *
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PIGMENT_API PResult pigment_std_cmd_image_upload(Pigment* pigment, PCommandBuffer* cmd, const PImageUploadDesc* uploads, uint32_t count, PImage** out_images);

/**
 * @brief Record mip generation and the transition to SHADER_READ_ONLY without submitting.
 *
 * `cmd` must be recording. Mip generation requires a graphics queue.
 * A separate upload submission must precede this one through an explicit wait.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer for finalization.
 * @param images Array of count uploaded images.
 * @param uploads Descriptions matching images.
 * @param count Number of images.
 *
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PIGMENT_API PResult pigment_std_cmd_image_finalize(Pigment* pigment, PCommandBuffer* cmd, PImage* const* images, const PImageUploadDesc* uploads, uint32_t count);

/**
 * @brief Upload data to `count` buffers in one command buffer and submit.
 *
 * Convenience helper that records and submits on the calling thread.
 * Host-mapped destinations must be idle and are written directly. The pool must belong to the
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
PIGMENT_API PResult pigment_std_buffer_upload(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, const PBufferUploadDesc* uploads, uint32_t count, PSubmitHandle* out_handle);

/**
 * @brief Create `count` images from the pixel data in `uploads`.
 *
 * Convenience helper that records and submits on the calling thread.
 * Mip level 0 is always uploaded. A precomputed mip chain (mip_count > 1) is uploaded in full.
 * Images are created with P_IMAGE_LAYOUT_TRANSFER_DST. Use pigment_std_image_finalize
 * to populate the mip chain (when P_IMAGE_GENERATE_MIPMAPS was set) and transition to a sampleable
 * layout. The pool must belong to the family of `queue`. Sync `out_handle` before using the
 * images on another queue.
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
PIGMENT_API PResult pigment_std_image_upload(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, const PImageUploadDesc* uploads, uint32_t count, PImage** out_images, PSubmitHandle* out_handle);

/**
 * @brief Finalize uploaded images and transition them to a sampleable layout.
 *
 * Convenience helper that records and submits on the calling thread.
 * Images flagged P_IMAGE_GENERATE_MIPMAPS get their mip chain generated by blit, which must run on a
 * graphics queue. Images uploaded with a precomputed chain or a single level are only transitioned,
 * so a batch without P_IMAGE_GENERATE_MIPMAPS may run on any queue, including a dedicated transfer queue.
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
PIGMENT_API PResult pigment_std_image_finalize(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, PImage* const* images, const PImageUploadDesc* uploads, uint32_t count, PSubmitHandle* out_handle);

#ifdef __cplusplus
}
#endif

#endif
