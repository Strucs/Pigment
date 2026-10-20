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

#ifndef PIGMENT_STD_MESH_H
#define PIGMENT_STD_MESH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pigment/buffers.h"
#include "pigment/defines.h"

#include <stdlib.h>

typedef struct PMeshBuffers {
    PBuffer* vertex_buffer;
    PBuffer* index_buffer;
    uint32_t index_count;
    PIndexType index_type;
} PMeshBuffers;

typedef struct PMeshUploadDesc {
    const void* vertices;
    size_t vertices_size;
    const uint32_t* indices;
    uint32_t index_count;
    PDeviceQueue* const* shared_queues;    // NULL = EXCLUSIVE. Copied list. Include every queue family that uploads or uses the mesh.
    uint32_t shared_queue_count;
} PMeshUploadDesc;

/**
 * @brief Create mesh buffers and record their upload without submitting.
 *
 * `cmd` must be recording. All copies use staging. CPU data can be freed on return.
 * List all accessing queues in shared_queues and supply waits between dependent submissions.
 * On failure, the mesh is destroyed. Discard the recording.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer for the copies.
 * @param desc Mesh data and shared queues.
 *
 * @return Created mesh, or NULL on failure.
 */
PIGMENT_API PMeshBuffers* pigment_std_cmd_upload_mesh(Pigment* pigment, PCommandBuffer* cmd, const PMeshUploadDesc* desc);

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
 * @param desc Mesh data and shared queues.
 * @param out_handle Optional, receives the submit handle. Cleared on failure.
 *
 * @return Created mesh, or NULL on failure.
 */
PIGMENT_API PMeshBuffers* pigment_std_upload_mesh(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, const PMeshUploadDesc* desc, PSubmitHandle* out_handle);
PIGMENT_API void pigment_std_destroy_mesh(Pigment* pigment, PMeshBuffers* mesh);

PIGMENT_API uint64_t pigment_std_mesh_vertex_address(PMeshBuffers* mesh);

/**
 * @brief Stamp the mesh's vertex buffer as used by `cmd`.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer.
 * @param mesh Mesh to stamp.
 */
PIGMENT_API void pigment_std_mesh_use(Pigment* pigment, PCommandBuffer* cmd, PMeshBuffers* mesh);

#ifdef __cplusplus
}
#endif

#endif
