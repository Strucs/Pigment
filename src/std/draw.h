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
 * @brief Immutable draw descriptions and GPU versions retained for recording workers.
 *
 * Prepared batches retain their resources independently of the source objects.
 * Mesh buffers are retained, not copied. Synchronize any writes to their contents.
 */
typedef struct PStdDrawBatch PStdDrawBatch;

/**
 * @brief Copy draw data and prepare the resources used by the batch.
 *
 * Serialize access to source objects and meshes during preparation. The batch survives
 * their later changes or destruction. Each call replaces the CPU instances with its data.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless resources.
 * @param instances Instance storage for PInstanceData records.
 * @param materials Materials, or NULL.
 * @param lights Lights, or NULL.
 * @param camera Camera, or NULL.
 * @param draws Draw descriptions to copy.
 * @param draw_count Number of descriptions.
 *
 * @return Prepared batch, or NULL on invalid input, empty batch or failure.
 */
PIGMENT_API PStdDrawBatch* pigment_std_prepare_draws(Pigment* pigment, PStdBindless* bindless, PInstances* instances, PMaterials* materials, PLights* lights, PCamera* camera, const PDrawCall* draws, uint32_t draw_count);

/**
 * @brief Prepare a skybox batch with retained bindless and camera versions.
 *
 * Serialize access to the source objects during preparation.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless resources.
 * @param camera Camera to capture.
 * @param cubemap_slot Cubemap slot.
 * @param sampler_slot Sampler slot.
 *
 * @return Prepared batch, or NULL on invalid input or failure.
 */
PIGMENT_API PStdDrawBatch* pigment_std_prepare_skybox(Pigment* pigment, PStdBindless* bindless, PCamera* camera, uint32_t cubemap_slot, uint32_t sampler_slot);

/**
 * @brief Prepare a CRT batch with captured dimensions, time and bindless resources.
 *
 * Serialize access to bindless during preparation.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless resources.
 * @param width Target width in pixels.
 * @param height Target height in pixels.
 * @param texture_slot Render target slot.
 * @param sampler_slot Sampler slot.
 * @param time Effect time in seconds.
 *
 * @return Prepared batch, or NULL on invalid input or failure.
 */
PIGMENT_API PStdDrawBatch* pigment_std_prepare_crt(Pigment* pigment, PStdBindless* bindless, uint32_t width, uint32_t height, uint32_t texture_slot, uint32_t sampler_slot, float time);

/**
 * @brief Prepare light gizmos with retained camera, lights and mesh buffers.
 *
 * Serialize access to source objects and the mesh during preparation.
 *
 * @param pigment Pigment instance.
 * @param camera Camera, or NULL.
 * @param lights Lights to capture.
 * @param sphere_mesh Mesh used by each gizmo.
 * @param sphere_index_count Number of indices per gizmo.
 * @param scale Gizmo scale.
 *
 * @return Prepared batch, or NULL on invalid input, empty lights or failure.
 */
PIGMENT_API PStdDrawBatch* pigment_std_prepare_light_gizmos(Pigment* pigment, PCamera* camera, PLights* lights, const PMeshBuffers* sphere_mesh, uint32_t sphere_index_count, float scale);

/**
 * @brief Release a batch after all its recording workers have finished.
 *
 * Command buffers retain their resources until reset or destruction.
 *
 * @param pigment Pigment instance.
 * @param batch Prepared batch, or NULL.
 */
PIGMENT_API void pigment_std_release_draws(Pigment* pigment, PStdDrawBatch* batch);

/**
 * @brief Record prepared draws without modifying their source objects.
 *
 * Workers may share a batch with separate command buffers and pools.
 * Keep the batch, pipeline and its layout alive for the duration of each call.
 * Skybox and CRT batches set their depth and cull states. Other batches keep caller state.
 *
 * @param pigment Pigment instance.
 * @param cmd Recording command buffer.
 * @param batch Prepared draw batch.
 * @param pipeline Compatible pipeline already bound to cmd.
 */
PIGMENT_API void pigment_std_draw(Pigment* pigment, PCommandBuffer* cmd, const PStdDrawBatch* batch, PPipeline* pipeline);

#ifdef __cplusplus
}
#endif

#endif
