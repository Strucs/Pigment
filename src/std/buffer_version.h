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

#ifndef PIGMENT_STD_BUFFER_VERSION_H
#define PIGMENT_STD_BUFFER_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pigment/defines.h"

/**
 * @brief Immutable GPU data prepared for one or more recording workers.
 *
 * The source object may change or be destroyed after preparation.
 * Publish the pointer to workers with CPU synchronization. Release each prepared reference
 * after its workers finish recording. Command buffers retain their own references.
 */
typedef struct PStdBufferVersion PStdBufferVersion;

/**
 * @brief Retain a prepared buffer in cmd and return its GPU address.
 *
 * Concurrent calls are allowed with separate command buffers and pools.
 * For use across queue families, declare them when creating the source object.
 * Queue sharing does not synchronize GPU accesses.
 *
 * @param pigment Pigment instance.
 * @param version Prepared version, or NULL.
 * @param cmd Recording command buffer.
 *
 * @return Buffer address, or 0 on invalid input.
 */
PIGMENT_API uint64_t pigment_std_buffer_version_address(Pigment* pigment, const PStdBufferVersion* version, PCommandBuffer* cmd);

/**
 * @brief Get the number of records in a prepared version.
 *
 * @param version Prepared version, or NULL.
 *
 * @return Record count, or 0 for NULL.
 */
PIGMENT_API uint32_t pigment_std_buffer_version_count(const PStdBufferVersion* version);

/**
 * @brief Release one prepared reference without waiting for the GPU.
 *
 * @param pigment Pigment instance.
 * @param version Prepared version, or NULL.
 */
PIGMENT_API void pigment_std_release_buffer_version(Pigment* pigment, PStdBufferVersion* version);

#ifdef __cplusplus
}
#endif

#endif
