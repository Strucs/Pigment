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

#include "pigment/defines.h"

typedef struct PInstances PInstances;

/**
 * @brief Create CPU instance storage with reusable GPU versions.
 *
 * The caller must serialize access to this object. Storage grows as needed.
 *
 * @param pigment Pigment instance.
 * @param instance_size Size of one instance in bytes.
 * @param initial_capacity Initial number of instances.
 *
 * @return Instance storage, or NULL on failure.
 */
PIGMENT_API PInstances* pigment_std_create_instances(Pigment* pigment, uint32_t instance_size, uint32_t initial_capacity);

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
 * @brief Copy CPU instances to a GPU version and retain it in a command buffer.
 *
 * The address belongs to this recording. Request it again for each new recording.
 * Each call copies the data. Later CPU writes leave recorded versions unchanged.
 * If publication fails, CPU instances are preserved for the next call.
 * The caller must serialize access to this object.
 *
 * @param pigment Pigment instance.
 * @param instances Instance storage to publish.
 * @param cmd Command buffer that uses the returned address.
 *
 * @return Buffer address, or 0 on invalid input, empty storage or publication failure.
 */
PIGMENT_API uint64_t pigment_std_instances_address(Pigment* pigment, PInstances* instances, PCommandBuffer* cmd);

#ifdef __cplusplus
}
#endif

#endif
