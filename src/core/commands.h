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

#ifndef PIGMENT_COMMANDS_H
#define PIGMENT_COMMANDS_H

#include "defines.h"

PCommandPoolList* create_command_pools(Pigment* pigment);
void destroy_command_pools(Pigment* pigment, PCommandPoolList* pools);

PCommandPool* pigment_create_command_pool(Pigment* pigment, PCommandPoolDesc* desc);
void pigment_destroy_command_pool(Pigment* pigment, PCommandPool* pool);

/**
 * @brief Allocate a command buffer from a pool. Call pigment_begin_recording before recording into it.
 *
 * @param pigment Pigment instance.
 * @param pool Pool to allocate from. NULL = default graphics pool.
 *
 * @return Newly allocated command buffer, or NULL on failure.
 */
PCommandBuffer* pigment_create_command_buffer(Pigment* pigment, PCommandPool* pool);

/**
 * @brief Defer destruction of a command buffer until the GPU is done with it.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer to destroy.
 */
void pigment_destroy_command_buffer(Pigment* pigment, PCommandBuffer* cmd);

/**
 * @brief Open a command buffer for recording. Required before any pigment_cmd_* function.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer to record into.
 * @param flags Usage flags for this recording session.
 */
void pigment_begin_recording(Pigment* pigment, PCommandBuffer* cmd, PCommandBufferUsage flags);

/**
 * @brief Close a command buffer recording. The buffer becomes submittable via pigment_queue_submit.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer to finalize.
 */
void pigment_end_recording(Pigment* pigment, PCommandBuffer* cmd);

/**
 * @brief Submit command buffers to the GPU. Does not wait for completion.
 *
 * @param pigment Pigment instance.
 * @param cmds Command buffers to submit, executed in array order.
 * @param count Number of command buffers in the array.
 *
 * @return Handle to query or wait for GPU completion of this submit.
 */
PSubmitHandle pigment_queue_submit(Pigment* pigment, PCommandBuffer** cmds, uint32_t count);

/**
 * @brief Returns P_TRUE if the GPU has completed all work tracked by this handle.
 *
 * @param pigment Pigment instance.
 * @param handle Submit handle returned by pigment_queue_submit.
 */
PBool pigment_submit_complete(Pigment* pigment, PSubmitHandle handle);

/**
 * @brief Block until the GPU has completed all work tracked by this handle.
 *
 * @param pigment Pigment instance.
 * @param handle Submit handle returned by pigment_queue_submit.
 */
void pigment_submit_wait(Pigment* pigment, PSubmitHandle handle);

void pigment_cmd_begin_label(Pigment* pigment, PCommandBuffer* cmd, const char* name);
void pigment_cmd_end_label(Pigment* pigment, PCommandBuffer* cmd);
void pigment_cmd_insert_label(Pigment* pigment, PCommandBuffer* cmd, const char* name);

#endif
