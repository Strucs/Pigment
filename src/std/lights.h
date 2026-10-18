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

#include "camera.h"
#include "mesh.h"
#include "types.h"

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

PIGMENT_API PLights* pigment_std_create_lights(Pigment* pigment, uint32_t initial_size);
PIGMENT_API void pigment_std_destroy_lights(Pigment* pigment, PLights* lights);

PIGMENT_API void pigment_std_set_ambient(Pigment* pigment, PLights* lights, PVec3 color);

PIGMENT_API uint32_t pigment_std_light_create(Pigment* pigment, PLights* lights, const PLightDesc* desc);
PIGMENT_API void pigment_std_light_update(Pigment* pigment, PLights* lights, uint32_t id, const PLightDesc* desc);
PIGMENT_API void pigment_std_light_destroy(Pigment* pigment, PLights* lights, uint32_t id);

/**
 * @brief Publish the lights and retain their buffer in a command buffer.
 *
 * The address belongs to this recording. Request it again for each new recording.
 * Unchanged data shares its version. Modified data reuses an idle buffer or allocates one.
 * If publication fails, CPU changes are preserved for the next call.
 * The caller must serialize access to this object.
 *
 * @param pigment Pigment instance.
 * @param lights Lights to publish.
 * @param cmd Command buffer that uses the returned address.
 *
 * @return Buffer address, or 0 on invalid input or publication failure.
 */
PIGMENT_API uint64_t pigment_std_lights_address(Pigment* pigment, PLights* lights, PCommandBuffer* cmd);

PIGMENT_API void pigment_std_draw_light_gizmos(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline, PCamera* camera, PLights* lights, PMeshBuffers* sphere_mesh, uint32_t sphere_index_count, float scale);

#ifdef __cplusplus
}
#endif

#endif
