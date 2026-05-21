// Copyright 2026 Angel-Leduc TA
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PIGMENT_CMD_SYNC_H
#define PIGMENT_CMD_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

typedef struct PSyncFlags {
    PPipelineStage stages;
    PMemoryAccess access;
} PSyncFlags;

typedef enum PDependencyFlags {
    P_DEPENDENCY_NONE      = 0,
    P_DEPENDENCY_BY_REGION = 1 << 0,
} PDependencyFlags;

typedef struct PImageBarrier {
    PImage* image;
    PImageLayout old_layout;
    PImageLayout new_layout;
    PSyncFlags src;
    PSyncFlags dst;
    PDependencyFlags flags;
    uint32_t base_mip;
    uint32_t mip_count;    // 0 = remaining
    uint32_t base_layer;
    uint32_t layer_count;         // 0 = remaining
    uint32_t src_queue_family;    // equal to dst_queue_family = no ownership transfer
    uint32_t dst_queue_family;
} PImageBarrier;

typedef struct PBufferBarrier {
    PBuffer* buffer;
    PSyncFlags src;
    PSyncFlags dst;
    PDependencyFlags flags;
    uint64_t offset;
    uint64_t size;                // 0 = whole buffer
    uint32_t src_queue_family;    // equal to dst_queue_family = no ownership transfer
    uint32_t dst_queue_family;
} PBufferBarrier;

typedef struct PMemoryBarrier {
    PSyncFlags src;
    PSyncFlags dst;
    PDependencyFlags flags;
} PMemoryBarrier;

PIGMENT_API void pigment_cmd_image_barriers(Pigment* pigment, PCommandBuffer* cmd, const PImageBarrier* barriers, uint32_t count);
PIGMENT_API void pigment_cmd_buffer_barriers(Pigment* pigment, PCommandBuffer* cmd, const PBufferBarrier* barriers, uint32_t count);
PIGMENT_API void pigment_cmd_memory_barriers(Pigment* pigment, PCommandBuffer* cmd, const PMemoryBarrier* barriers, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
