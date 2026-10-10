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

#ifndef PIGMENT_FRAME_H
#define PIGMENT_FRAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "buffers.h"
#include "defines.h"
#include "pipeline.h"

typedef struct PDrawIndirectCommand {
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
} PDrawIndirectCommand;

typedef struct PDrawIndexedIndirectCommand {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t first_instance;
} PDrawIndexedIndirectCommand;

typedef struct PViewport {
    float x;
    float y;
    float width;
    float height;
    float min_depth;
    float max_depth;
} PViewport;

typedef struct PScissor {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} PScissor;

typedef struct PAttachmentRef {
    PImage* image;
    uint32_t base_layer;
    uint32_t layer_count;
    uint32_t mip_level;
    PImage* resolve_image;    // NULL = no MSAA resolve. Otherwise, must be a 1-sample image with matching format/extent.
    uint32_t resolve_base_layer;
    uint32_t resolve_mip_level;
    PResolveMode resolve_mode;
    PLoadOp load_op;              // CLEAR (default), LOAD (preserve), DONT_CARE (undefined). Depth aspect for depth/stencil attachments.
    PStoreOp store_op;            // AUTO (default), STORE, DONT_CARE. Depth aspect for depth/stencil attachments.
    PLoadOp stencil_load_op;      // stencil aspect only, ignored if image has no stencil
    PStoreOp stencil_store_op;    // stencil aspect only, ignored if image has no stencil

    /*
     * UNDEFINED (default): auto-transition to SHADER_READ_ONLY if image has SAMPLED usage, else no transition.
     *                      Set explicitly to force a target layout at end of pass.
     */
    PImageLayout final_layout;
} PAttachmentRef;

typedef struct PSwapchainPassDesc {
    float clear_color[4];
    PBool no_depth;    // skip the depth/stencil attachment
} PSwapchainPassDesc;

struct PRenderPassDesc {
    PAttachmentRef* color_attachments;
    uint32_t color_count;
    PAttachmentRef depth_attachment;
    float clear_color[4];
    float depth_clear_value;
    uint32_t stencil_clear_value;    // ignored if depth_attachment.image has no stencil aspect
    uint32_t layer_count;
    uint32_t view_mask;
};

/**
 * @brief Wait for a slot's submitted GPU work.
 *
 * Waits for all submits associated with the slot on every queue used.
 * Does not wait for CPU recording or presentation.
 *
 * @param pigment Pigment instance.
 * @param renderer Frame owner.
 * @param slot Slot index below max_frames_in_flight.
 */
PIGMENT_API void pigment_wait_frame_ready(Pigment* pigment, PWindowRenderer* renderer, uint32_t slot);

/**
 * @brief Acquire a frame and drain pending deletions.
 *
 * The caller must wait for prior GPU use of the slot and synchronize renderer, frame and queue access.
 * Close every context with pigment_present_frame before recreating or destroying the renderer.
 *
 * @param pigment Pigment instance.
 * @param renderer Frame owner.
 * @param slot Slot index below max_frames_in_flight.
 * @param timeout_ns Image acquisition timeout in nanoseconds. Zero polls, UINT64_MAX requests an indefinite wait.
 * @param out_frame Context valid until present, or NULL on failure.
 *
 * @return PIGMENT_SUCCESS (including suboptimal), PIGMENT_NOT_READY, PIGMENT_TIMEOUT, PIGMENT_RECREATE_REQUIRED or an error.
 * NOT_READY also covers an active slot or an indefinite acquisition requiring pending frames to be presented first.
 */
PIGMENT_API PResult pigment_begin_frame_context(Pigment* pigment, PWindowRenderer* renderer, uint32_t slot, uint64_t timeout_ns, PFrame** out_frame);

/**
 * @brief Get the frame slot.
 *
 * @param frame Frame context.
 *
 * @return Slot index, or UINT32_MAX for a NULL or inactive frame.
 */
PIGMENT_API uint32_t pigment_frame_slot(const PFrame* frame);

/**
 * @brief Get the acquired image for explicit image commands.
 *
 * @param frame Frame context.
 *
 * @return Acquired image, or NULL if unavailable.
 */
PIGMENT_API PImage* pigment_frame_image(const PFrame* frame);

/**
 * @brief Begin a pass using the frame's attachments.
 *
 * Requires exclusive recording access to the frame and its attachments.
 *
 * @param pigment Pigment instance.
 * @param cmd Recording command buffer.
 * @param frame Acquired frame.
 * @param desc Pass settings, or NULL for defaults.
 */
PIGMENT_API void pigment_cmd_begin_swapchain_pass(Pigment* pigment, PCommandBuffer* cmd, const PFrame* frame, const PSwapchainPassDesc* desc);

/**
 * @brief End the pass and transition the image for presentation.
 *
 * @param cmd Command buffer recording the pass.
 * @param frame Acquired frame.
 */
PIGMENT_API void pigment_cmd_end_swapchain_pass(PCommandBuffer* cmd, const PFrame* frame);

/**
 * @brief Present the frame and release its CPU slot.
 *
 * Call after a successful pigment_queue_submit with signal_present set.
 * If the swapchain is already out of date, only closes the context.
 * Recreation waits for GPU use and retires images that were not presented.
 * Caller must synchronize access to the frame, renderer and present queue.
 *
 * @param pigment Pigment instance.
 * @param frame Submitted frame.
 */
PIGMENT_API void pigment_present_frame(Pigment* pigment, PFrame* frame);

/**
 * @brief Return the number of frames the renderer keeps in flight.
 *
 * @param pigment Pigment instance.
 *
 * @return The configured maximum number of frames in flight.
 */
PIGMENT_API uint32_t pigment_max_frames_in_flight(Pigment* pigment);

PIGMENT_API void pigment_begin_render_pass(Pigment* pigment, PCommandBuffer* cmd, const PRenderPassDesc* desc);
PIGMENT_API void pigment_end_render_pass(Pigment* pigment, PCommandBuffer* cmd, const PRenderPassDesc* desc);

PIGMENT_API void pigment_cmd_push_constants(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline, uint32_t offset, uint32_t size, const void* data);

/**
 * @brief Bind one or more vertex buffers for classic vertex input.
 *
 * @param pigment Pigment instance.
 * @param cmd Command buffer to record into.
 * @param first_binding First binding slot (matches PVertexBindingDesc.binding).
 * @param buffer_count Number of buffers (and offsets) to bind.
 * @param buffers Array of `buffer_count` non-NULL PBuffer pointers.
 * @param offsets Per-buffer byte offsets. NULL = all zero.
 */
PIGMENT_API void pigment_cmd_bind_vertex_buffers(Pigment* pigment, PCommandBuffer* cmd, uint32_t first_binding, uint32_t buffer_count, PBuffer** buffers, const uint64_t* offsets);

PIGMENT_API void pigment_cmd_draw(Pigment* pigment, PCommandBuffer* cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
PIGMENT_API void pigment_cmd_draw_indexed(Pigment* pigment, PCommandBuffer* cmd, PBuffer* index_buffer, PIndexType index_type, uint64_t index_buffer_offset, uint32_t first_index, uint32_t index_count, int32_t vertex_offset, uint32_t instance_count, uint32_t first_instance);

/**
 * Indirect draws. The draw parameters are read from a GPU buffer of PDrawIndirectCommand / PDrawIndexedIndirectCommand.
 *
 * draw_count > 1 requires P_FEATURE_MULTI_DRAW_INDIRECT.
 * firstInstance != 0 in any command requires P_FEATURE_DRAW_INDIRECT_FIRST_INSTANCE.
 * The _count variants require P_FEATURE_DRAW_INDIRECT_COUNT.
 */
PIGMENT_API void pigment_cmd_draw_indirect(Pigment* pigment, PCommandBuffer* cmd, PBuffer* indirect_buffer, uint64_t indirect_offset, uint32_t draw_count, uint32_t stride);
PIGMENT_API void pigment_cmd_draw_indexed_indirect(Pigment* pigment, PCommandBuffer* cmd, PBuffer* index_buffer, PIndexType index_type, uint64_t index_offset, PBuffer* indirect_buffer, uint64_t indirect_offset, uint32_t draw_count, uint32_t stride);
PIGMENT_API void pigment_cmd_draw_indirect_count(Pigment* pigment, PCommandBuffer* cmd, PBuffer* indirect_buffer, uint64_t indirect_offset, PBuffer* count_buffer, uint64_t count_offset, uint32_t max_draw_count, uint32_t stride);
PIGMENT_API void pigment_cmd_draw_indexed_indirect_count(Pigment* pigment, PCommandBuffer* cmd, PBuffer* index_buffer, PIndexType index_type, uint64_t index_offset, PBuffer* indirect_buffer, uint64_t indirect_offset, PBuffer* count_buffer, uint64_t count_offset, uint32_t max_draw_count, uint32_t stride);

PIGMENT_API void pigment_cmd_set_depth(Pigment* pigment, PCommandBuffer* cmd, PBool test, PBool write, PCompareOp op);
PIGMENT_API void pigment_cmd_set_cull(Pigment* pigment, PCommandBuffer* cmd, PCullMode mode, PFrontFace face);
PIGMENT_API void pigment_cmd_set_stencil_test(Pigment* pigment, PCommandBuffer* cmd, PBool enable);
PIGMENT_API void pigment_cmd_set_stencil_op(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, PStencilOp fail_op, PStencilOp pass_op, PStencilOp depth_fail_op, PCompareOp compare_op);
PIGMENT_API void pigment_cmd_set_stencil_compare_mask(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, uint32_t mask);
PIGMENT_API void pigment_cmd_set_stencil_write_mask(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, uint32_t mask);
PIGMENT_API void pigment_cmd_set_stencil_reference(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, uint32_t reference);
PIGMENT_API void pigment_cmd_set_viewport(Pigment* pigment, PCommandBuffer* cmd, const PViewport* viewports, uint32_t count);
PIGMENT_API void pigment_cmd_set_scissor(Pigment* pigment, PCommandBuffer* cmd, const PScissor* scissors, uint32_t count);
PIGMENT_API void pigment_cmd_set_depth_bias(Pigment* pigment, PCommandBuffer* cmd, PBool enable, float constant, float clamp, float slope);
PIGMENT_API void pigment_cmd_set_depth_bounds(Pigment* pigment, PCommandBuffer* cmd, PBool enable, float min, float max);

#ifdef __cplusplus
}
#endif

#endif
