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

#ifndef PIGMENT_STD_PRIMITIVES_H
#define PIGMENT_STD_PRIMITIVES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "mesh.h"
#include "vertex.h"

#include "pigment/defines.h"

typedef struct PMeshData {
    PVertex* vertices;
    uint32_t vertex_count;
    uint32_t* indices;
    uint32_t index_count;
} PMeshData;

PIGMENT_API PMeshData pigment_cube_mesh(Pigment* pigment);
PIGMENT_API PMeshData pigment_quad_mesh(Pigment* pigment);
PIGMENT_API PMeshData pigment_plane_mesh(Pigment* pigment, uint32_t segments);
PIGMENT_API PMeshData pigment_sphere_mesh(Pigment* pigment, uint32_t lat_segments, uint32_t lon_segments);

PIGMENT_API void pigment_free_mesh_data(Pigment* pigment, PMeshData* mesh);

/**
 * @brief Create mesh buffers and record their upload without submitting.
 *
 * `cmd` must be recording. All copies use staging. CPU data can be freed on return.
 * List all accessing queues in shared_queues and supply waits between dependent submissions.
 * On failure, the mesh is destroyed. Discard the recording.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer for the copies.
 * @param data CPU mesh data, copied before returning.
 * @param shared_queues Accessing queues, or NULL for exclusive buffers.
 * @param shared_queue_count Number of shared queues.
 *
 * @return Created mesh, or NULL on failure.
 */
PIGMENT_API PMeshBuffers* pigment_cmd_upload_mesh_data(Pigment* pigment, PCommandBuffer* cmd, const PMeshData* data, PDeviceQueue* const* shared_queues, uint32_t shared_queue_count);

/**
 * @brief Create mesh buffers and submit their upload.
 *
 * Convenience helper that records and submits on the calling thread without waiting for the GPU.
 * All copies use staging. CPU data can be freed on return. The pool must belong to the
 * family of `queue`. Sync `out_handle` before using the mesh on another queue.
 *
 * @param pigment Pigment instance.
 * @param pool Pool for the transfer command buffer.
 * @param queue Queue to submit on, or NULL for the default graphics queue.
 * @param data CPU mesh data, copied before returning.
 * @param shared_queues Accessing queues, or NULL for exclusive buffers.
 * @param shared_queue_count Number of shared queues.
 * @param out_handle Optional, receives the submit handle. Cleared on failure.
 *
 * @return Created mesh, or NULL on failure.
 */
PIGMENT_API PMeshBuffers* pigment_upload_mesh_data(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, const PMeshData* data, PDeviceQueue* const* shared_queues, uint32_t shared_queue_count, PSubmitHandle* out_handle);

#ifdef __cplusplus
}
#endif

#endif
