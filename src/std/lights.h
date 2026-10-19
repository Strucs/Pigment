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

#ifndef PIGMENT_STD_LIGHTS_H
#define PIGMENT_STD_LIGHTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"
#include "buffer_version.h"

#include "pigment/defines.h"

typedef struct PLights PLights;

typedef enum PLightType {
    P_LIGHT_TYPE_INVALID     = 0,
    P_LIGHT_TYPE_DIRECTIONAL = 1,
    P_LIGHT_TYPE_POINT       = 2,
    P_LIGHT_TYPE_SPOT        = 3,
} PLightType;

typedef struct PIGMENT_ALIGN(16) PLightDesc {
    PVec3 position;
    uint32_t type;

    PVec3 direction;
    float range;

    PVec3 color;
    float intensity;

    float inner_cone_cos;
    float outer_cone_cos;
} PLightDesc;

typedef struct PLightsDesc {
    uint32_t initial_capacity;
    PDeviceQueue* const* shared_queues;    // NULL = EXCLUSIVE. Copied list. Include every queue family that accesses the buffers.
    uint32_t shared_queue_count;
} PLightsDesc;

/**
 * @brief Create CPU lights data with reusable GPU versions.
 *
 * @param pigment Pigment instance.
 * @param desc Initial capacity and shared queues.
 *
 * @return Created object, or NULL on invalid input or failure.
 */
PIGMENT_API PLights* pigment_std_create_lights(Pigment* pigment, const PLightsDesc* desc);
PIGMENT_API void pigment_std_destroy_lights(Pigment* pigment, PLights* lights);

PIGMENT_API void pigment_std_set_ambient(Pigment* pigment, PLights* lights, PVec3 color);

PIGMENT_API uint32_t pigment_std_light_create(Pigment* pigment, PLights* lights, const PLightDesc* desc);
PIGMENT_API void pigment_std_light_update(Pigment* pigment, PLights* lights, uint32_t id, const PLightDesc* desc);
PIGMENT_API void pigment_std_light_destroy(Pigment* pigment, PLights* lights, uint32_t id);

/**
 * @brief Prepare an immutable GPU version of the lights.
 *
 * The caller must serialize access to this object. Failure preserves CPU data for retry.
 * Release the returned reference with pigment_std_release_buffer_version after recording.
 * Unchanged data shares its version.
 *
 * @param pigment Pigment instance.
 * @param lights Source object.
 *
 * @return Prepared version, or NULL on invalid input or publication failure.
 */
PIGMENT_API PStdBufferVersion* pigment_std_lights_prepare(Pigment* pigment, PLights* lights);

#ifdef __cplusplus
}
#endif

#endif
