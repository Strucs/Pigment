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
#include "mesh.h"
#include "types.h"

#include "pigment/defines.h"
#include "pigment/pipeline.h"

typedef struct PInstanceRing PInstanceRing;
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
 * @brief Create a per-frame ring buffer for `instance_size`-byte instance records.
 *
 * The ring allocates `instance_size * max_instances_per_frame * frames_in_flight`
 * bytes so that each frame in flight has its own non-overlapping slot. Users
 * call `sync_frame` once per frame, then `alloc` to reserve instance slots,
 * and `frame_address` to push the slot's GPU address to a shader.
 */
PIGMENT_API PInstanceRing* pigment_std_create_instance_ring(Pigment* pigment, uint32_t instance_size, uint32_t max_instances_per_frame);
PIGMENT_API void pigment_std_destroy_instance_ring(Pigment* pigment, PInstanceRing* ring);

/**
 * @brief Sync the ring with the renderer's current frame.
 *
 * Resets the cursor when the frame index changes.
 *
 * @param ring Instance ring to sync.
 * @param current_frame Renderer's current frame index (from `pigment_renderer_current_frame`).
 */
PIGMENT_API void pigment_std_instance_ring_sync_frame(PInstanceRing* ring, uint32_t current_frame);

/**
 * @brief Stamp the ring's underlying buffer as used by `cmd`.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer.
 * @param ring Instance ring to stamp.
 */
PIGMENT_API void pigment_std_instance_ring_use(Pigment* pigment, PCommandBuffer* cmd, PInstanceRing* ring);

/**
 * @brief Get the number of allocated instances in the current frame's slot.
 *
 * @param ring Instance ring to query.
 *
 * @return Cursor position.
 */
PIGMENT_API uint32_t pigment_std_instance_ring_cursor(PInstanceRing* ring);

/**
 * @brief Reserve space for `instance_count` consecutive instances and advance the cursor.
 *
 * @param pigment Pigment instance.
 * @param ring Instance ring to allocate from.
 * @param instance_count Number of consecutive instances to reserve.
 *
 * @return Pointer to the first reserved instance slot, or NULL on failure or if capacity exceeded.
 */
PIGMENT_API void* pigment_std_instance_ring_alloc(Pigment* pigment, PInstanceRing* ring, uint32_t instance_count);

/**
 * @brief GPU device address of `instance[0]` for the most recently synced frame.
 *
 * @param ring Instance ring to query.
 *
 * @return GPU address of the current frame's instance slot, or 0 on failure.
 */
PIGMENT_API uint64_t pigment_std_instance_ring_frame_address(PInstanceRing* ring);

/**
 * @brief Flush a range of instances from CPU mapped memory to GPU-visible state.
 *
 * No-op if the ring's buffer is host-coherent.
 *
 * @param pigment Pigment instance.
 * @param ring Instance ring to flush.
 * @param first_instance Index of the first instance to flush within the current frame's slot.
 * @param instance_count Number of instances to flush.
 */
PIGMENT_API void pigment_std_instance_ring_flush_range(Pigment* pigment, PInstanceRing* ring, uint32_t first_instance, uint32_t instance_count);

PIGMENT_API void pigment_draw(Pigment* pigment, PWindowRenderer* renderer, PStdBindless* bindless, PInstanceRing* ring, PMaterials* materials, PLights* lights, PCamera* camera, PPipeline* pipeline, PDrawCall* draws, uint32_t draw_count);
PIGMENT_API void pigment_std_draw_skybox(Pigment* pigment, PWindowRenderer* renderer, PStdBindless* bindless, PPipeline* pipeline, PCamera* camera, uint32_t cubemap_slot, uint32_t sampler_slot);
PIGMENT_API void pigment_std_draw_crt(Pigment* pigment, PWindowRenderer* renderer, PStdBindless* bindless, PPipeline* pipeline, uint32_t texture_slot, uint32_t sampler_slot, float time);

#ifdef __cplusplus
}
#endif

#endif
