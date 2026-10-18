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

#ifndef PIGMENT_STD_DRAW_H
#define PIGMENT_STD_DRAW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bindless.h"
#include "camera.h"
#include "instances.h"
#include "mesh.h"
#include "types.h"

#include "pigment/defines.h"
#include "pigment/pipeline.h"

typedef struct PMaterials PMaterials;
typedef struct PLights PLights;

typedef struct PIGMENT_ALIGN(16) PInstanceData {
    PMat4 transform;
    uint32_t material_id;
} PInstanceData;

typedef struct PDrawCall {
    PMeshBuffers* mesh;
    PInstanceData* instances;
    uint32_t instance_count;
    uint32_t first_index;
    uint32_t index_count;
} PDrawCall;

/**
 * @brief Record a batch of draws with retained GPU data.
 *
 * Each call replaces the CPU instance data with this batch.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer to record into.
 * @param bindless Bindless resources.
 * @param instances Instance storage for PInstanceData records.
 * @param materials Materials, or NULL.
 * @param lights Lights, or NULL.
 * @param camera Camera, or NULL.
 * @param pipeline Pipeline to use.
 * @param draws Draw descriptions.
 * @param draw_count Number of draws.
 */
PIGMENT_API void pigment_draw(Pigment* pigment, PCommandBuffer* cmd, PStdBindless* bindless, PInstances* instances, PMaterials* materials, PLights* lights, PCamera* camera, PPipeline* pipeline, PDrawCall* draws, uint32_t draw_count);
PIGMENT_API void pigment_std_draw_skybox(Pigment* pigment, PCommandBuffer* cmd, PStdBindless* bindless, PPipeline* pipeline, PCamera* camera, uint32_t cubemap_slot, uint32_t sampler_slot);
PIGMENT_API void pigment_std_draw_crt(Pigment* pigment, PCommandBuffer* cmd, PWindowRenderer* renderer, PStdBindless* bindless, PPipeline* pipeline, uint32_t texture_slot, uint32_t sampler_slot, float time);

#ifdef __cplusplus
}
#endif

#endif
