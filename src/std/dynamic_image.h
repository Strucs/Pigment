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

#ifndef PIGMENT_STD_DYNAMIC_IMAGE_H
#define PIGMENT_STD_DYNAMIC_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pigment/defines.h"

typedef struct PDynamicImage PDynamicImage;

typedef struct PDynamicImageDesc {
    uint32_t width;
    uint32_t height;
    PFormat format;
    uint32_t ring_size;                    // 0 = default (2). Number of persistent-mapped staging buffers.
    PDeviceQueue* const* shared_queues;    // NULL = EXCLUSIVE. List every queue family that updates or samples the image.
    uint32_t shared_queue_count;
    const char* name;
} PDynamicImageDesc;

/**
 * @brief Create a single-mip 2D image meant to be rewritten from the CPU every frame.
 *
 * @param pigment Pigment instance.
 * @param desc Image description.
 *
 * @return The created dynamic image, or NULL on failure.
 */
PIGMENT_API PDynamicImage* pigment_std_create_dynamic_image(Pigment* pigment, const PDynamicImageDesc* desc);

/**
 * @brief Destroy a dynamic image and its staging ring.
 *
 * @param pigment Pigment instance.
 * @param dynamic_image Dynamic image to destroy.
 */
PIGMENT_API void pigment_std_destroy_dynamic_image(Pigment* pigment, PDynamicImage* dynamic_image);

/**
 * @brief Update the dynamic image.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer the copy is recorded into.
 * @param dynamic_image Dynamic image to update.
 * @param pixels Source pixels, tightly packed.
 * @param size Byte size of the pixels buffe.
 *
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PIGMENT_API PResult pigment_std_update_dynamic_image(Pigment* pigment, PCommandBuffer* cmd, PDynamicImage* dynamic_image, const void* pixels, uint64_t size);

/**
 * @brief Return the underlying image, sampleable after the first update.
 *
 * @param dynamic_image Dynamic image.
 *
 * @return The backing image, or NULL.
 */
PIGMENT_API PImage* pigment_std_dynamic_image_get(PDynamicImage* dynamic_image);

#ifdef __cplusplus
}
#endif

#endif
