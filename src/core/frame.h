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

#include "defines.h"
#include "pipeline.h"
#include "buffers.h"
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
 * @brief Block until the next frame slot's previous GPU work has completed.
 *
 * @param pigment Pigment instance.
 * @param renderer The renderer whose next frame slot to wait on.
 */
void pigment_wait_frame_ready(Pigment* pigment, PWindowRenderer* renderer);

/**
 * @brief Begin a new frame and return the command buffer ready for recording.
 *
 * Drains the deletion queue at entry. Must be called after
 * pigment_wait_frame_ready to ensure the previous frame's resources
 * are freed before potentially reusing them.
 *
 * @param pigment Pigment instance.
 * @param renderer The renderer to begin a frame on.
 *
 * @return The frame command buffer ready for recording, or NULL if the swapchain is being recreated.
 */
PCommandBuffer* pigment_begin_frame(Pigment* pigment, PWindowRenderer* renderer);

/**
 * @brief End recording on the current frame's command buffer.
 *
 * @param pigment Pigment instance.
 * @param renderer The renderer whose current frame command buffer to close.
 */
void pigment_end_recording_frame(Pigment* pigment, PWindowRenderer* renderer);

/**
 * @brief Return the current frame slot index of the renderer.
 *
 * @param renderer The renderer to query.
 *
 * @return The current frame slot index in [0, max_frames_in_flight).
 */
uint32_t pigment_renderer_current_frame(PWindowRenderer* renderer);

/**
 * @brief Return the current frame's command buffer (same one returned by pigment_begin_frame).
 *
 * @param renderer The renderer to query.
 *
 * @return The current frame's command buffer, or NULL if no frame in progress.
 */
PCommandBuffer* pigment_renderer_frame_cmd(PWindowRenderer* renderer);

/**
 * @brief Begin a render pass targeting the swapchain image.
 *
 * @param pigment Pigment instance.
 * @param renderer The renderer whose swapchain image to render to.
 */
void pigment_begin_swapchain_pass(Pigment* pigment, PWindowRenderer* renderer);

/**
 * @brief End the swapchain render pass.
 *
 * @param renderer The renderer whose swapchain pass to close.
 */
void pigment_end_swapchain_pass(PWindowRenderer* renderer);

/**
 * @brief Submit the current frame's command buffer to the GPU.
 *
 * Caller guarantees external sync on the queue (single-thread, submission
 * thread, or external mutex).
 *
 * @param pigment Pigment instance.
 * @param renderer The renderer whose current frame to submit.
 *
 * @return PSubmitHandle tracking the GPU completion of this frame's submit.
 */
PSubmitHandle pigment_queue_submit_frame(Pigment* pigment, PWindowRenderer* renderer);

/**
 * @brief Present the swapchain image and cycle to the next slot.
 *
 * @param pigment Pigment instance.
 * @param renderer The renderer to present.
 */
void pigment_present(Pigment* pigment, PWindowRenderer* renderer);

void pigment_begin_render_pass(Pigment* pigment, PCommandBuffer* cmd, const PRenderPassDesc* desc);
void pigment_end_render_pass(Pigment* pigment, PCommandBuffer* cmd, const PRenderPassDesc* desc);

void pigment_cmd_push_constants(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline, uint32_t offset, uint32_t size, const void* data);
void pigment_cmd_draw(Pigment* pigment, PCommandBuffer* cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
void pigment_cmd_draw_indexed(Pigment* pigment, PCommandBuffer* cmd, PBuffer* index_buffer, PIndexType index_type, uint64_t index_buffer_offset, uint32_t first_index, uint32_t index_count, int32_t vertex_offset, uint32_t instance_count, uint32_t first_instance);

/**
 * Indirect draws. The draw parameters are read from a GPU buffer of PDrawIndirectCommand / PDrawIndexedIndirectCommand.
 *
 * draw_count > 1 requires P_FEATURE_MULTI_DRAW_INDIRECT.
 * firstInstance != 0 in any command requires P_FEATURE_DRAW_INDIRECT_FIRST_INSTANCE.
 * The _count variants require P_FEATURE_DRAW_INDIRECT_COUNT.
 */
void pigment_cmd_draw_indirect(Pigment* pigment, PCommandBuffer* cmd, PBuffer* indirect_buffer, uint64_t indirect_offset, uint32_t draw_count, uint32_t stride);
void pigment_cmd_draw_indexed_indirect(Pigment* pigment, PCommandBuffer* cmd, PBuffer* index_buffer, PIndexType index_type, uint64_t index_offset, PBuffer* indirect_buffer, uint64_t indirect_offset, uint32_t draw_count, uint32_t stride);
void pigment_cmd_draw_indirect_count(Pigment* pigment, PCommandBuffer* cmd, PBuffer* indirect_buffer, uint64_t indirect_offset, PBuffer* count_buffer, uint64_t count_offset, uint32_t max_draw_count, uint32_t stride);
void pigment_cmd_draw_indexed_indirect_count(Pigment* pigment, PCommandBuffer* cmd, PBuffer* index_buffer, PIndexType index_type, uint64_t index_offset, PBuffer* indirect_buffer, uint64_t indirect_offset, PBuffer* count_buffer, uint64_t count_offset, uint32_t max_draw_count, uint32_t stride);

void pigment_cmd_set_depth(Pigment* pigment, PCommandBuffer* cmd, PBool test, PBool write, PCompareOp op);
void pigment_cmd_set_cull(Pigment* pigment, PCommandBuffer* cmd, PCullMode mode, PFrontFace face);
void pigment_cmd_set_stencil_test(Pigment* pigment, PCommandBuffer* cmd, PBool enable);
void pigment_cmd_set_stencil_op(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, PStencilOp fail_op, PStencilOp pass_op, PStencilOp depth_fail_op, PCompareOp compare_op);
void pigment_cmd_set_stencil_compare_mask(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, uint32_t mask);
void pigment_cmd_set_stencil_write_mask(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, uint32_t mask);
void pigment_cmd_set_stencil_reference(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, uint32_t reference);
void pigment_cmd_set_viewport(Pigment* pigment, PCommandBuffer* cmd, const PViewport* viewports, uint32_t count);
void pigment_cmd_set_scissor(Pigment* pigment, PCommandBuffer* cmd, const PScissor* scissors, uint32_t count);
void pigment_cmd_set_depth_bias(Pigment* pigment, PCommandBuffer* cmd, PBool enable, float constant, float clamp, float slope);
void pigment_cmd_set_depth_bounds(Pigment* pigment, PCommandBuffer* cmd, PBool enable, float min, float max);

#endif
