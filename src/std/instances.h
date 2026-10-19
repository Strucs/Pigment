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

#ifndef PIGMENT_STD_INSTANCES_H
#define PIGMENT_STD_INSTANCES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "buffer_version.h"

#include "pigment/defines.h"

typedef struct PInstances PInstances;

typedef struct PInstancesDesc {
    uint32_t instance_size;
    uint32_t initial_capacity;
    PDeviceQueue* const* shared_queues;    // NULL = EXCLUSIVE. Copied list. Include every queue family that accesses the buffers.
    uint32_t shared_queue_count;
} PInstancesDesc;

/**
 * @brief Create CPU instance storage with reusable GPU versions.
 *
 * The caller must serialize access to this object. Storage grows as needed.
 *
 * @param pigment Pigment instance.
 * @param desc Instance size, initial capacity and shared queues.
 *
 * @return Instance storage, or NULL on failure.
 */
PIGMENT_API PInstances* pigment_std_create_instances(Pigment* pigment, const PInstancesDesc* desc);

PIGMENT_API void pigment_std_destroy_instances(Pigment* pigment, PInstances* instances);

/**
 * @brief Clear CPU instances without affecting recorded GPU versions.
 *
 * @param instances Instance storage to reset.
 */
PIGMENT_API void pigment_std_instances_reset(PInstances* instances);

/**
 * @brief Get the number of instances allocated since the last reset.
 *
 * @param instances Instance storage to query.
 *
 * @return Allocated instance count.
 */
PIGMENT_API uint32_t pigment_std_instances_count(PInstances* instances);

/**
 * @brief Reserve consecutive CPU instances.
 *
 * The returned pointer is valid until the next allocation, reset or destruction.
 * Fill the instances before publishing their address.
 *
 * @param pigment Pigment instance.
 * @param instances Instance storage to allocate from.
 * @param instance_count Number of instances to reserve.
 *
 * @return Writable instance storage, or NULL on failure.
 */
PIGMENT_API void* pigment_std_instances_alloc(Pigment* pigment, PInstances* instances, uint32_t instance_count);

/**
 * @brief Prepare an immutable GPU version of the instances.
 *
 * The caller must serialize access to this object. Failure preserves CPU data for retry.
 * Release the returned reference with pigment_std_release_buffer_version after recording.
 * Each call copies the CPU instances.
 *
 * @param pigment Pigment instance.
 * @param instances Source object.
 *
 * @return Prepared version, or NULL on invalid input, empty storage or publication failure.
 */
PIGMENT_API PStdBufferVersion* pigment_std_instances_prepare(Pigment* pigment, PInstances* instances);

#ifdef __cplusplus
}
#endif

#endif
