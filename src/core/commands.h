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

/**
 * @brief Submit description for command buffer submissions.
 */
typedef struct PSubmit {
    PCommandBuffer** cmds;
    uint32_t cmd_count;
} PSubmit;

/**
 * @brief Inheritance state used to begin recording a SECONDARY command buffer.
 *
 * Describes the dynamic rendering context the secondary will run inside (color/depth/stencil
 * formats, sample count, view mask). Must match the primary's render pass at execute time.
 */
typedef struct PCommandBufferInheritance {
    const PFormat* color_formats;
    uint32_t color_format_count;
    PFormat depth_format;      // P_FORMAT_UNDEFINED if no depth
    PFormat stencil_format;    // P_FORMAT_UNDEFINED if no stencil
    PSampleCount samples;      // 0 = 1 sample
    uint32_t view_mask;        // 0 = no multiview
} PCommandBufferInheritance;

/**
 * @brief Create a command pool tied to a queue family. The pool is tracked by Pigment and freed
 *        at shutdown if the user does not destroy it explicitly via pigment_destroy_command_pool.
 *
 * @param pigment Pigment instance.
 * @param desc Pool description (queue flags, transient / reset flags, debug name).
 *
 * @return Newly created command pool, or NULL on failure.
 */
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
 * @brief Reset every command buffer in this pool to the initial state at once, ready to be
 *        recorded again. Make sure the GPU is done with them first.
 *
 * If the pool was created without P_COMMAND_POOL_FLAG_RESET_BUFFER, this is the only way to
 * reset its buffers (individual reset is forbidden on those). Otherwise, this is a faster
 * alternative to resetting buffers one by one.
 *
 * @param pigment Pigment instance.
 * @param pool Pool to reset.
 */
void pigment_reset_command_pool(Pigment* pigment, PCommandPool* pool);

/**
 * @brief Allocate command buffers from a pool. Call pigment_begin_recording before recording into it.
 *
 * @param pigment Pigment instance.
 * @param pool Pool to allocate from.
 * @param level Primary or secondary. Secondary buffers are recorded inside a render pass and
 *              executed via a primary's cmd_execute_commands.
 * @param count Number of command buffers to allocate.
 * @param out_cmds Array to store the allocated command buffers.
 *
 * @return PIGMENT_SUCCESS on success, or an error code on failure.
 */
PResult pigment_create_command_buffers(Pigment* pigment, PCommandPool* pool, PCommandBufferLevel level, uint32_t count, PCommandBuffer** out_cmds);

/**
 * @brief Defer destruction of command buffers until the GPU is done with it. The buffers are
 *        also freed automatically when the source pool is destroyed, so calling this is only
 *        needed for early release before the pool goes away.
 *
 * @param pigment Pigment instance.
 * @param cmds Array of command buffers to destroy.
 * @param count Number of command buffers in the array.
 */
void pigment_destroy_command_buffers(Pigment* pigment, PCommandBuffer** cmds, uint32_t count);

/**
 * @brief Open a command buffer for recording. Required before any pigment_cmd_* function.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer to record into.
 * @param flags Usage flags for this recording session. SECONDARY recording inside a render pass
 *              must set P_CMD_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT.
 * @param inheritance Required for SECONDARY level inside a render pass, NULL otherwise.
 */
void pigment_begin_recording(Pigment* pigment, PCommandBuffer* cmd, PCommandBufferUsage flags, const PCommandBufferInheritance* inheritance);

/**
 * @brief Execute secondary command buffers from inside a primary's render pass. Secondaries must
 *        have been recorded with matching PCommandBufferInheritance.
 *
 * @param pigment Pigment instance.
 * @param primary Primary command buffer currently in a render pass.
 * @param secondaries Array of secondary command buffers to execute.
 * @param count Number of secondaries.
 */
void pigment_cmd_execute_commands(Pigment* pigment, PCommandBuffer* primary, PCommandBuffer** secondaries, uint32_t count);

/**
 * @brief Close a command buffer recording. The buffer becomes submittable.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer to finalize.
 */
void pigment_end_recording(Pigment* pigment, PCommandBuffer* cmd);

/**
 * @brief Submit one or more command-buffer batches to the GPU in a single call. Batches execute
 *        in array order on the queue. Each batch is tracked independently, the returned handle
 *        signals when the LAST batch is GPU-done.
 *
 * @param pigment Pigment instance.
 * @param submits Array of submit descriptors. Each one is an independent group of command buffers.
 * @param submit_count Number of submits in the array.
 *
 * @return Handle to wait for the entire submission to complete (= last batch signaled).
 */
PSubmitHandle pigment_queue_submit(Pigment* pigment, const PSubmit* submits, uint32_t submit_count);

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
