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

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

/**
 * @brief One batch of command buffers to submit on a queue.
 */
typedef struct PSubmit {
    /**
     * @brief The target queue, NULL to default to graphics.
     */
    PDeviceQueue* queue;
    PCommandBuffer** cmds;
    uint32_t cmd_count;

    /**
     * @brief List of prior submits this batch should wait on before executing.
     */
    const PSubmitWait* waits;
    uint32_t wait_count;

    /**
     * @brief Optional frame whose slot tracks this submit's completion on any queue.
     */
    PFrame* frame;

    /**
     * @brief Wait for image acquisition once, before accessing the swapchain image.
     */
    PBool wait_acquire;

    /**
     * @brief Acquire wait stage. NONE defaults to ALL_COMMANDS.
     * Cover the first image access and its layout transition.
     */
    PPipelineStage acquire_stage;

    /**
     * @brief Signal presentation readiness. Required only on the final frame submit.
     */
    PBool signal_present;
} PSubmit;

typedef enum PResourceType {
    P_RESOURCE_TYPE_BUFFER,
    P_RESOURCE_TYPE_IMAGE,
    P_RESOURCE_TYPE_SAMPLER,
    P_RESOURCE_TYPE_PIPELINE,
} PResourceType;

/**
 * @brief Resource pointer tagged with its type.
 */
typedef struct PResource {
    PResourceType type;

    union {
        PBuffer* buffer;
        PImage* image;
        PSampler* sampler;
        PPipeline* pipeline;
    };
} PResource;

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
 * @brief Create a command pool tied to a queue family.
 *
 * @param pigment Pigment instance.
 * @param desc Pool description (queue flags, transient / reset flags, debug name).
 *
 * @return Newly created command pool, or NULL on failure.
 */
PIGMENT_API PCommandPool* pigment_create_command_pool(Pigment* pigment, PCommandPoolDesc* desc);

/**
 * @brief Defer destruction of a command pool until the GPU is done with it. All command buffers
 *        still allocated from this pool are freed along with it.
 *
 * Pending buffer callbacks retain the pool. The caller must stop using it after this call.
 *
 * @param pigment Pigment instance.
 * @param pool Pool to destroy.
 */
PIGMENT_API void pigment_destroy_command_pool(Pigment* pigment, PCommandPool* pool);

/**
 * @brief Free command buffers processed by deferred destruction.
 *
 * The caller must synchronize host access to the pool.
 * Allocation, recording begin, reset and buffer destruction also collect automatically.
 * Call pigment_drain_pending first to process completed GPU work.
 *
 * @param pigment Pigment instance.
 * @param pool Pool to collect from.
 */
PIGMENT_API void pigment_collect_command_pool(Pigment* pigment, PCommandPool* pool);

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
PIGMENT_API void pigment_reset_command_pool(Pigment* pigment, PCommandPool* pool);

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
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PIGMENT_API PResult pigment_create_command_buffers(Pigment* pigment, PCommandPool* pool, PCommandBufferLevel level, uint32_t count, PCommandBuffer** out_cmds);

/**
 * @brief Retire command buffers after their GPU work completes.
 *
 * NULL entries and consecutive duplicates are ignored.
 * Other duplicates and previously destroyed buffers cause undefined behavior.
 * Buffers may come from different pools. Synchronize host access to every affected pool.
 * Completed buffers can be reclaimed with pigment_collect_command_pool.
 *
 * @param pigment Pigment instance.
 * @param cmds Array of command buffers to destroy.
 * @param count Number of command buffers in the array.
 */
PIGMENT_API void pigment_destroy_command_buffers(Pigment* pigment, PCommandBuffer** cmds, uint32_t count);

/**
 * @brief Open a command buffer for recording. Required before any pigment_cmd_* function.
 *
 * The caller must wait for prior GPU use and synchronize access to the buffer and pool.
 * With P_COMMAND_POOL_FLAG_RESET_BUFFER, this also resets prior recording.
 * Otherwise, reset the pool before reusing the buffer.
 * A successful begin releases resources retained by the previous recording.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer to record into.
 * @param flags Usage flags for this recording session. SECONDARY recording inside a render pass
 *              must set P_CMD_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT.
 * @param inheritance Required for SECONDARY level inside a render pass, NULL otherwise.
 */
PIGMENT_API void pigment_begin_recording(Pigment* pigment, PCommandBuffer* cmd, PCommandBufferUsage flags, const PCommandBufferInheritance* inheritance);

/**
 * @brief Execute secondary command buffers from inside a primary's render pass. Secondaries must
 *        have been recorded with matching PCommandBufferInheritance.
 *
 * Their tracked resources are retained by the primary too. Keep the secondary buffers
 * and their pools alive and unchanged until the primary is reset or freed.
 *
 * @param pigment Pigment instance.
 * @param primary Primary command buffer currently in a render pass.
 * @param secondaries Array of secondary command buffers to execute.
 * @param count Number of secondaries.
 */
PIGMENT_API void pigment_cmd_execute_commands(Pigment* pigment, PCommandBuffer* primary, PCommandBuffer** secondaries, uint32_t count);

/**
 * @brief Close a command buffer recording. The buffer becomes submittable.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer to finalize.
 */
PIGMENT_API void pigment_end_recording(Pigment* pigment, PCommandBuffer* cmd);

/**
 * @brief Batched submit. Consecutive submits on the same queue are coalesced into one
 *        vkQueueSubmit2 call, cross-queue splits per queue.
 *
 * The caller must serialize access to each queue and frame.
 * Command buffers must belong to the queue family.
 * Frame tracking accepts any queue. Swapchain access and acquire/present flags use
 * the default graphics family or families added with PSwapchainDesc.shared_queues.
 * The caller defines GPU dependencies with waits and recorded barriers.
 * Independent work may precede the acquire wait. No submits are chained automatically.
 * Wait acquire once and signal present on the last frame submit. Both may share a submit.
 * No further submits may use that frame after signal_present.
 * The final submit must depend on acquisition and all work accessing the presented image.
 * Slot reuse waits for all frame submits, including work independent of presentation.
 * Empty command lists are allowed. Explicit waits also apply to frame submits.
 * On failure, earlier queue batches may have succeeded. Only successful submits
 * update frame state and receive nonzero output handles.
 *
 * @param pigment Pigment instance.
 * @param submits Array of submit descriptors.
 * @param submit_count Number of submits in the array.
 * @param handles_out Optional output array (size submit_count) filled with one handle per submit. NULL to skip.
 *
 * @return PIGMENT_SUCCESS on success, error code otherwise.
 */
PIGMENT_API PResult pigment_queue_submit(Pigment* pigment, const PSubmit* submits, uint32_t submit_count, PSubmitHandle* handles_out);

/**
 * @brief Return P_TRUE if the GPU has completed all work tracked by this handle.
 *
 * @param pigment Pigment instance.
 * @param handle Submit handle returned by pigment_queue_submit.
 *
 * @return P_TRUE if the submit is complete, P_FALSE if pending or on failure.
 */
PIGMENT_API PBool pigment_submit_complete(Pigment* pigment, PSubmitHandle handle);

/**
 * @brief Block until the GPU has completed all work tracked by this handle.
 *
 * @param pigment Pigment instance.
 * @param handle Submit handle returned by pigment_queue_submit.
 *
 * @return PIGMENT_SUCCESS on completion or no-op, otherwise an error code.
 */
PIGMENT_API PResult pigment_submit_wait(Pigment* pigment, PSubmitHandle handle);

PIGMENT_API void pigment_cmd_begin_label(Pigment* pigment, PCommandBuffer* cmd, const char* name);
PIGMENT_API void pigment_cmd_end_label(Pigment* pigment, PCommandBuffer* cmd);
PIGMENT_API void pigment_cmd_insert_label(Pigment* pigment, PCommandBuffer* cmd, const char* name);

/**
 * @brief Create a resource tracker for a user-defined resource type.
 *
 * A tracker lets a custom resource be stamped at submit time via pigment_cmd_use, so its
 * deferred destroy can wait on the exact GPU completion value of the last submit that used it.
 * Built-in buffers, images, samplers, pipelines and descriptor sets already own trackers.
 *
 * @param pigment Pigment instance.
 *
 * @return A new tracker, or NULL on failure. Release it with pigment_destroy_resource_tracker.
 */
PIGMENT_API PResourceTracker* pigment_create_resource_tracker(Pigment* pigment);

/**
 * @brief Destroy a tracker created by pigment_create_resource_tracker.
 * Shared image wrappers, recorded uses and pending GPU work delay its release.
 *
 * @param pigment Pigment instance.
 * @param tracker Tracker to destroy.
 */
PIGMENT_API void pigment_destroy_resource_tracker(Pigment* pigment, PResourceTracker* tracker);

/**
 * @brief Retain a custom resource for this recording and track its GPU use.
 *
 * References last until the command buffer is reset or freed, including after submission.
 * Destroy through pigment_defer_destroy_tracked. Synchronize the first recorded use
 * with destruction. A retained CPU reference allows later recordings until its release.
 * For built-in resources, use pigment_cmd_use_buffer, pigment_cmd_use_image or
 * pigment_cmd_use_sampler for indirect accesses such as GPU addresses or bindless indices.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer being recorded.
 * @param tracker Custom tracker or retained resource reference.
 */
PIGMENT_API void pigment_cmd_use(Pigment* pigment, PCommandBuffer* cmd, PResourceTracker* tracker);

/**
 * @brief Block until the GPU has finished every submit stamped on the tracker.
 *
 * This waits for submitted work only. Recorded references remain retained.
 *
 * @param pigment Pigment instance.
 * @param tracker Tracker from pigment_create_resource_tracker.
 *
 * @return PIGMENT_SUCCESS on completion or no-op, otherwise an error code.
 */
PIGMENT_API PResult pigment_resource_tracker_wait(Pigment* pigment, const PResourceTracker* tracker);

/**
 * @brief Retain a resource for later recordings.
 *
 * Serialize with destruction and image resize. Images retain their current allocation.
 * Record uses with pigment_cmd_use and release with pigment_release_resource.
 * A pipeline's layout must remain alive until recording is complete.
 *
 * @param resource Resource with the union member matching its type.
 *
 * @return Retained tracker, or NULL for an invalid type or NULL resource.
 */
PIGMENT_API PResourceTracker* pigment_retain_resource(PResource resource);

/**
 * @brief Release a CPU reference obtained from pigment_retain_resource.
 *
 * Recorded references remain retained. Finish recording uses before releasing the last CPU reference.
 *
 * @param pigment Pigment instance.
 * @param tracker Retained tracker, or NULL.
 */
PIGMENT_API void pigment_release_resource(Pigment* pigment, PResourceTracker* tracker);

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
PIGMENT_API void pigment_cmd_use_buffer(Pigment* pigment, PCommandBuffer* cmd, PBuffer* buffer);

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
PIGMENT_API void pigment_cmd_use_image(Pigment* pigment, PCommandBuffer* cmd, PImage* image);

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
PIGMENT_API void pigment_cmd_use_sampler(Pigment* pigment, PCommandBuffer* cmd, PSampler* sampler);

#ifdef __cplusplus
}
#endif

#endif
