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

/**
 * @brief Defer destruction of a command pool until the GPU is done with it. All command buffers
 *        still allocated from this pool are freed along with it.
 *
 * @param pigment Pigment instance.
 * @param pool Pool to destroy.
 */
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
 * @brief Defer destruction of a command buffer until the GPU is done with it. The buffer is
 *        also freed automatically when its source pool is destroyed, so calling this is only
 *        needed for early release before the pool goes away.
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

/**
 * @brief Stamp a custom resource tracker at submit time.
 *
 * The destroy of the resource will be able to wait on the precise GPU completion value of the
 * last submit that stamped this tracker. Use this for user types embedding a zero-initialized
 * PResourceTracker, for example an aggregate resource wrapping several built-in objects, or a
 * wrapper around raw Vulkan handles you allocated yourself. For Pigment's built-in resource
 * types use the dedicated pigment_cmd_use_buffer, pigment_cmd_use_image and
 * pigment_cmd_use_sampler.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer being recorded.
 * @param tracker Pointer to the embedded tracker (e.g. &my_custom_resource->tracker).
 */
void pigment_cmd_use(Pigment* pigment, PCommandBuffer* cmd, PResourceTracker* tracker);

/**
 * @brief Stamp a buffer's tracker at submit time.
 *
 * Required when the buffer is touched by the command buffer but Pigment cannot see it in the
 * recorded commands, typically when its GPU address (from pigment_buffer_address) is baked
 * into push constants or into another buffer's content. Buffers passed directly as PBuffer*
 * arguments to pigment_cmd_* functions are stamped automatically.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer being recorded.
 * @param buffer Buffer used by the command buffer.
 */
void pigment_cmd_use_buffer(Pigment* pigment, PCommandBuffer* cmd, PBuffer* buffer);

/**
 * @brief Stamp an image's tracker at submit time.
 *
 * Required when the image is touched by the command buffer but Pigment cannot see it in the
 * recorded commands, typically when it is referenced indirectly via an index in push constants
 * or another buffer. Images passed directly as PImage* arguments to pigment_cmd_* functions
 * are stamped automatically.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer being recorded.
 * @param image Image used by the command buffer.
 */
void pigment_cmd_use_image(Pigment* pigment, PCommandBuffer* cmd, PImage* image);

/**
 * @brief Stamp a sampler's tracker at submit time.
 *
 * Samplers are never tracked automatically because they are never referenced directly in a
 * recorded command. They are always accessed indirectly through descriptor sets bound to the
 * command buffer. Call this when you bind a descriptor set containing this sampler and want a
 * precise destroy. In most cases samplers are long-lived and destroyed only at shutdown, so
 * manual stamping is rarely needed.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer being recorded.
 * @param sampler Sampler used by the command buffer.
 */
void pigment_cmd_use_sampler(Pigment* pigment, PCommandBuffer* cmd, PSampler* sampler);

#endif
