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

#ifndef PIPELINE_H
#define PIPELINE_H

#include "defines.h"

typedef enum PCompareOp {
    P_COMPARE_OP_NEVER            = 0,
    P_COMPARE_OP_LESS             = 1,
    P_COMPARE_OP_EQUAL            = 2,
    P_COMPARE_OP_LESS_OR_EQUAL    = 3,
    P_COMPARE_OP_GREATER          = 4,
    P_COMPARE_OP_NOT_EQUAL        = 5,
    P_COMPARE_OP_GREATER_OR_EQUAL = 6,
    P_COMPARE_OP_ALWAYS           = 7,
} PCompareOp;

typedef enum PStencilOp {
    P_STENCIL_OP_KEEP                = 0,
    P_STENCIL_OP_ZERO                = 1,
    P_STENCIL_OP_REPLACE             = 2,
    P_STENCIL_OP_INCREMENT_AND_CLAMP = 3,
    P_STENCIL_OP_DECREMENT_AND_CLAMP = 4,
    P_STENCIL_OP_INVERT              = 5,
    P_STENCIL_OP_INCREMENT_AND_WRAP  = 6,
    P_STENCIL_OP_DECREMENT_AND_WRAP  = 7,
} PStencilOp;

typedef enum PTopology {
    P_TOPOLOGY_POINT_LIST     = 0,
    P_TOPOLOGY_LINE_LIST      = 1,
    P_TOPOLOGY_LINE_STRIP     = 2,
    P_TOPOLOGY_TRIANGLE_LIST  = 3,
    P_TOPOLOGY_TRIANGLE_STRIP = 4,
    P_TOPOLOGY_TRIANGLE_FAN   = 5,
} PTopology;

typedef enum PPolygonMode {
    P_POLYGON_MODE_FILL  = 0,
    P_POLYGON_MODE_LINE  = 1,
    P_POLYGON_MODE_POINT = 2,
} PPolygonMode;

typedef enum PCullMode {
    P_CULL_MODE_NONE           = 0,
    P_CULL_MODE_FRONT          = 1,
    P_CULL_MODE_BACK           = 2,
    P_CULL_MODE_FRONT_AND_BACK = 3,
} PCullMode;

typedef enum PBlendMode {
    P_BLEND_MODE_OPAQUE              = 0,
    P_BLEND_MODE_ALPHA               = 1,
    P_BLEND_MODE_PREMULTIPLIED_ALPHA = 2,
    P_BLEND_MODE_ADDITIVE            = 3,
} PBlendMode;

typedef struct PPipelineDesc {
    const uint32_t* vertex_spv;
    uint32_t vertex_spv_size;
    const uint32_t* fragment_spv;
    uint32_t fragment_spv_size;
    PFormat color_format;
    PFormat depth_format;
    bool depth_test;
    bool depth_write;
    PCompareOp depth_compare_op;
    bool stencil_test;
    PCullMode cull_mode;
    PPolygonMode polygon_mode;
    PTopology topology;
    PBlendMode blend_mode;
} PPipelineDesc;

PPipelineBuild* pigment_pipeline_build_from_desc(Pigment* pigment, PPipelineDesc* desc);
void pigment_pipeline_build_destroy(Pigment* pigment, PPipelineBuild* build);

PPipelines* pigment_create_graphic_pipelines(Pigment* pigment, PPipelineBuild** builds, uint32_t count);
void pigment_destroy_pipelines(Pigment* pigment, PPipelines* pipelines);

void pigment_bind_pipeline(Pigment* pigment, PPipelines* pipelines, uint32_t pipeline_id);

#endif
