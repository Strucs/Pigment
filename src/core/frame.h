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

#ifndef FRAME_H
#define FRAME_H

#include "defines.h"
#include "pipeline.h"
#include "buffers.h"

typedef struct PAttachmentRef {
    PImage* image;
    uint32_t base_layer;
    uint32_t layer_count;
    uint32_t mip_level;
} PAttachmentRef;

struct PRenderPassDesc {
    PAttachmentRef* color_attachments;
    uint32_t color_count;
    PAttachmentRef depth_attachment;
    float clear_color[4];
    float depth_clear_value;
    uint32_t layer_count;
    uint32_t view_mask;
};

bool begin_frame(Pigment* pigment, PWindowRenderer* renderer, uint32_t* out_image_index);
void end_frame(Pigment* pigment, PWindowRenderer* renderer, uint32_t image_index, uint32_t max_frame);
void begin_swapchain_pass(Pigment* pigment, PWindowRenderer* renderer, uint32_t image_index);
void end_swapchain_pass(PWindowRenderer* renderer, uint32_t image_index);

void pigment_begin_render_pass(Pigment* pigment, PCommandBuffer* cmd, const PRenderPassDesc* desc);
void pigment_end_render_pass(Pigment* pigment, PCommandBuffer* cmd, const PRenderPassDesc* desc);

void pigment_cmd_push_constants(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline, uint32_t offset, uint32_t size, const void* data);
void pigment_cmd_draw(Pigment* pigment, PCommandBuffer* cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
void pigment_cmd_draw_indexed(Pigment* pigment, PCommandBuffer* cmd, PBuffer* index_buffer, PIndexType index_type, uint64_t index_buffer_offset, uint32_t first_index, uint32_t index_count, int32_t vertex_offset, uint32_t instance_count, uint32_t first_instance);

void pigment_cmd_set_depth(Pigment* pigment, PCommandBuffer* cmd, bool test, bool write, PCompareOp op);
void pigment_cmd_set_cull(Pigment* pigment, PCommandBuffer* cmd, PCullMode mode, PFrontFace face);
void pigment_cmd_set_stencil_test(Pigment* pigment, PCommandBuffer* cmd, bool enable);
void pigment_cmd_set_viewport(Pigment* pigment, PCommandBuffer* cmd, float x, float y, float width, float height, float min_depth, float max_depth);
void pigment_cmd_set_scissor(Pigment* pigment, PCommandBuffer* cmd, int32_t x, int32_t y, uint32_t width, uint32_t height);
void pigment_cmd_set_depth_bias(Pigment* pigment, PCommandBuffer* cmd, bool enable, float constant, float clamp, float slope);
void pigment_cmd_set_depth_bounds(Pigment* pigment, PCommandBuffer* cmd, bool enable, float min, float max);

#endif
