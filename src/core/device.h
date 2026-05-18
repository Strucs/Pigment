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

#ifndef PIGMENT_DEVICE_H
#define PIGMENT_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

typedef struct PDeviceLimits {
    uint32_t max_sampled_images;           // sampled images bindable in one descriptor set
    uint32_t max_samplers;                 // samplers bindable in one descriptor set
    uint32_t max_storage_images;           // storage images bindable in one descriptor set
    uint32_t max_storage_buffers;          // storage buffers bindable in one descriptor set
    uint32_t max_bound_descriptor_sets;    // descriptor sets bound at once
    uint32_t max_push_constants_size;      // push constant bytes, at least 128
    uint32_t max_image_dimension_2d;       // largest width or height of a 2D image
    uint32_t max_color_attachments;        // color attachments in one render pass
} PDeviceLimits;

/**
 * @brief Bytes the device can still allocate for memory matching the given flags.
 *
 * @param pigment Pigment instance.
 * @param flags Memory property flags the memory must satisfy to be counted.
 *
 * @return Allocatable bytes, or UINT64_MAX when the budget cannot be measured.
 */
uint64_t pigment_memory_budget(Pigment* pigment, PMemoryFlags flags);

/**
 * @brief Query the hardware limits of the selected device.
 *
 * @param pigment Pigment instance.
 *
 * @return The device limits, every field zero when `pigment` is NULL.
 */
PDeviceLimits pigment_device_limits(Pigment* pigment);

#ifdef __cplusplus
}
#endif

#endif
