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

#ifndef PIGMENT_BUFFERS_H
#define PIGMENT_BUFFERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

typedef enum PBufferUsage {
    P_BUFFER_USAGE_VERTEX         = 1U << 0,
    P_BUFFER_USAGE_INDEX          = 1U << 1,
    P_BUFFER_USAGE_STORAGE        = 1U << 2,
    P_BUFFER_USAGE_UNIFORM        = 1U << 3,
    P_BUFFER_USAGE_SHADER_ADDRESS = 1U << 4,
    P_BUFFER_USAGE_TRANSFER_SRC   = 1U << 5,
    P_BUFFER_USAGE_TRANSFER_DST   = 1U << 6,
    P_BUFFER_USAGE_INDIRECT       = 1U << 7,
} PBufferUsage;

typedef struct PMemoryRequest {
    PMemoryFlags required;
    PMemoryFlags preferred;
} PMemoryRequest;

typedef enum PBufferFlags {
    P_BUFFER_DEDICATED_BIT = 1U << 0,
} PBufferFlags;

typedef enum PIndexType {
    P_INDEX_TYPE_UINT16 = 0,
    P_INDEX_TYPE_UINT32 = 1,
} PIndexType;

typedef struct PBufferDesc {
    uint64_t size;
    PBufferUsage usage;
    PMemoryRequest memory;
    PBufferFlags flags;
    PDeviceQueue* const* shared_queues;    // NULL = EXCLUSIVE. List the queues that access it (2+ families -> CONCURRENT).
    uint32_t shared_queue_count;
    const char* name;
} PBufferDesc;

typedef struct PBufferCopy {
    uint64_t src_offset;
    uint64_t dst_offset;
    uint64_t size;
} PBufferCopy;

/**
 * @brief Create a buffer that satisfies the required memory flags.
 *
 * @return The buffer on success, or NULL on failure.
 */
PIGMENT_API PBuffer* pigment_create_buffer(Pigment* pigment, const PBufferDesc* desc);
PIGMENT_API void pigment_destroy_buffer(Pigment* pigment, PBuffer* buffer);

PIGMENT_API void* pigment_buffer_mapped(PBuffer* buffer);
PIGMENT_API uint64_t pigment_buffer_address(PBuffer* buffer);
PIGMENT_API uint64_t pigment_buffer_size(PBuffer* buffer);

PIGMENT_API void pigment_buffer_flush(Pigment* pigment, PBuffer* buffer, uint64_t offset, uint64_t size);
PIGMENT_API void pigment_buffer_invalidate(Pigment* pigment, PBuffer* buffer, uint64_t offset, uint64_t size);

/**
 * @brief Check that a buffer has no recorded references or pending GPU uses.
 *
 * The caller must prevent new uses, writes and destruction during reuse.
 *
 * @param pigment Pigment instance.
 * @param buffer Buffer to query.
 *
 * @return `true` if idle, `false` for invalid input, active use or query failure.
 */
PIGMENT_API PBool pigment_buffer_idle(Pigment* pigment, const PBuffer* buffer);

/**
 * @brief Block until the GPU has finished every submitted command that used the buffer.
 *
 * @param pigment Pigment instance.
 * @param buffer Buffer to wait on.
 *
 * @return PIGMENT_SUCCESS on completion or no-op, otherwise an error code.
 */
PIGMENT_API PResult pigment_buffer_wait(Pigment* pigment, const PBuffer* buffer);

PIGMENT_API void pigment_cmd_copy_buffer(Pigment* pigment, PCommandBuffer* cmd, PBuffer* src, PBuffer* dst, const PBufferCopy* regions, uint32_t region_count);

#ifdef __cplusplus
}
#endif

#endif
