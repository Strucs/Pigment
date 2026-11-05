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

#ifndef PIGMENT_PIPELINE_H
#define PIGMENT_PIPELINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

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

typedef enum PStencilFaceFlags {
    P_STENCIL_FACE_FRONT_BIT      = 1,
    P_STENCIL_FACE_BACK_BIT       = 2,
    P_STENCIL_FACE_FRONT_AND_BACK = 3,
} PStencilFaceFlags;

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

typedef enum PFrontFace {
    P_FRONT_FACE_COUNTER_CLOCKWISE = 0,
    P_FRONT_FACE_CLOCKWISE         = 1,
} PFrontFace;

typedef enum PBlendMode {
    P_BLEND_MODE_OPAQUE              = 0,
    P_BLEND_MODE_ALPHA               = 1,
    P_BLEND_MODE_PREMULTIPLIED_ALPHA = 2,
    P_BLEND_MODE_ADDITIVE            = 3,
    P_BLEND_MODE_MULTIPLY            = 4,
    P_BLEND_MODE_SCREEN              = 5,
} PBlendMode;

typedef enum PBlendFactor {
    P_BLEND_FACTOR_ZERO                     = 0,
    P_BLEND_FACTOR_ONE                      = 1,
    P_BLEND_FACTOR_SRC_COLOR                = 2,
    P_BLEND_FACTOR_ONE_MINUS_SRC_COLOR      = 3,
    P_BLEND_FACTOR_DST_COLOR                = 4,
    P_BLEND_FACTOR_ONE_MINUS_DST_COLOR      = 5,
    P_BLEND_FACTOR_SRC_ALPHA                = 6,
    P_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA      = 7,
    P_BLEND_FACTOR_DST_ALPHA                = 8,
    P_BLEND_FACTOR_ONE_MINUS_DST_ALPHA      = 9,
    P_BLEND_FACTOR_CONSTANT_COLOR           = 10,
    P_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR = 11,
    P_BLEND_FACTOR_CONSTANT_ALPHA           = 12,
    P_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA = 13,
    P_BLEND_FACTOR_SRC_ALPHA_SATURATE       = 14,
    P_BLEND_FACTOR_COUNT,
} PBlendFactor;

typedef enum PBlendOp {
    P_BLEND_OP_ADD              = 0,
    P_BLEND_OP_SUBTRACT         = 1,
    P_BLEND_OP_REVERSE_SUBTRACT = 2,
    P_BLEND_OP_MIN              = 3,
    P_BLEND_OP_MAX              = 4,
    P_BLEND_OP_COUNT,
} PBlendOp;

typedef enum PColorComponentFlags {
    P_COLOR_COMPONENT_R_BIT = 1 << 0,
    P_COLOR_COMPONENT_G_BIT = 1 << 1,
    P_COLOR_COMPONENT_B_BIT = 1 << 2,
    P_COLOR_COMPONENT_A_BIT = 1 << 3,
    P_COLOR_COMPONENT_RGB   = P_COLOR_COMPONENT_R_BIT | P_COLOR_COMPONENT_G_BIT | P_COLOR_COMPONENT_B_BIT,
    P_COLOR_COMPONENT_RGBA  = P_COLOR_COMPONENT_RGB | P_COLOR_COMPONENT_A_BIT,
} PColorComponentFlags;

typedef struct PBlendAttachmentDesc {
    PBool enable;
    PColorComponentFlags write_mask;    // Applies even when blending is disabled. 0 = no color writes.
    PBlendFactor src_color_factor;
    PBlendFactor dst_color_factor;
    PBlendOp color_op;
    PBlendFactor src_alpha_factor;
    PBlendFactor dst_alpha_factor;
    PBlendOp alpha_op;
} PBlendAttachmentDesc;

/**
 * @brief Build a blend preset with RGBA writes enabled, ready for customization.
 *
 * Unknown modes fall back to OPAQUE.
 */
PIGMENT_API PBlendAttachmentDesc pigment_blend_attachment(PBlendMode mode);

typedef struct PSpecializationEntry {
    uint32_t constant_id;
    uint32_t offset;
    uint64_t size;
} PSpecializationEntry;

typedef struct PSpecializationInfo {
    const PSpecializationEntry* entries;
    uint32_t entry_count;
    const void* data;
    uint64_t data_size;
} PSpecializationInfo;

/**
 * Shader stage inputs are borrowed until pipeline creation returns.
 * Code, entry point and specialization data may be released afterwards.
 */
typedef struct PShaderStageDesc {
    PShaderStageFlags stage;    // Exactly one P_SHADER_STAGE_* bit.
    const uint32_t* code;
    uint32_t code_size;         // SPIR-V size in bytes.
    const char* entry_point;    // NULL = "main".
    const PSpecializationInfo* specialization;
} PShaderStageDesc;

typedef enum PVertexInputRate {
    P_VERTEX_INPUT_RATE_VERTEX   = 0,
    P_VERTEX_INPUT_RATE_INSTANCE = 1,
} PVertexInputRate;

typedef struct PVertexBindingDesc {
    uint32_t binding;
    uint32_t stride;
    PVertexInputRate input_rate;
} PVertexBindingDesc;

typedef struct PVertexAttributeDesc {
    uint32_t location;
    uint32_t binding;
    PFormat format;
    uint32_t offset;
} PVertexAttributeDesc;

typedef struct PPipelineDesc {
    PLayout* layout;

    /**
     * Exactly one VERTEX or MESH stage and optionally one FRAGMENT stage, in any order.
     * MESH requires P_FEATURE_MESH_SHADER and cannot be combined with VERTEX.
     * An optional TASK stage requires MESH and P_FEATURE_TASK_SHADER.
     * A nonzero view_mask with MESH requires P_FEATURE_MULTIVIEW_MESH_SHADER.
     * The array and its referenced inputs are borrowed until creation returns.
     */
    const PShaderStageDesc* stages;
    uint32_t stage_count;

    const PFormat* color_formats;
    /**
     * Render targets (static, baked).
     * color_format_count = 0 for depth-only (shadow maps).
     * color_format_count = 1 for forward rendering.
     * color_format_count > 1 for MRT / G-buffer.
     */
    uint32_t color_format_count;
    PFormat depth_format;    // P_FORMAT_UNDEFINED = no depth attachment

    PPolygonMode polygon_mode;
    PBool depth_clamp;     // Requires P_FEATURE_DEPTH_CLAMP. Disabled by default. Creation fails if unavailable.
    PTopology topology;    // Ignored with MESH, which declares its output topology in the shader.

    /**
     * Classic indexed draws only. Requires LINE_STRIP, TRIANGLE_STRIP or TRIANGLE_FAN.
     * Must be disabled with MESH.
     * Restart index: 0xFFFF for UINT16, 0xFFFFFFFF for UINT32. Disabled by default.
     */
    PBool primitive_restart;

    /**
     * Array of color_format_count blend states. NULL = OPAQUE with RGBA writes.
     * Different states across attachments require P_FEATURE_INDEPENDENT_BLEND.
     */
    const PBlendAttachmentDesc* blend_attachments;
    float blend_constants[4];    // Shared by all attachments using CONSTANT_COLOR / CONSTANT_ALPHA factors.

    PSampleCount sample_count;

    PBool sample_shading_enable;    // Requires P_FEATURE_SAMPLE_RATE_SHADING. Creation fails if unavailable.
    float min_sample_shading;       // [0, 1] when enabled. Ignored otherwise.
    PBool alpha_to_coverage;

    /**
     * Multiview view mask. Must match the view_mask of the render pass this pipeline is bound in.
     * 0 = single-view (default). e.g. 0b111111 = 6 views simultaneously (cubemap shadow).
     * A nonzero mask requires P_FEATURE_MULTIVIEW.
     */
    uint32_t view_mask;

    /**
     * Classic vertex input declaration. Counts must be zero with MESH.
     */

    const PVertexBindingDesc* vertex_bindings;
    uint32_t vertex_binding_count;
    const PVertexAttributeDesc* vertex_attributes;
    uint32_t vertex_attribute_count;

    const char* name;
} PPipelineDesc;

typedef struct PComputePipelineDesc {
    PLayout* layout;

    PShaderStageDesc shader;    // Must use P_SHADER_STAGE_COMPUTE_BIT.

    const char* name;
} PComputePipelineDesc;

typedef struct PLayoutDesc {
    PDescriptorSetLayout** set_layouts;
    uint32_t set_layout_count;
    uint32_t push_size;
    PShaderStageFlags push_stages;
    const char* name;
} PLayoutDesc;

PIGMENT_API PLayout* pigment_create_layout(Pigment* pigment, const PLayoutDesc* desc);
PIGMENT_API void pigment_destroy_layout(Pigment* pigment, PLayout* layout);

/**
 * @brief Create a pipeline cache used to accelerate pigment_create_*_pipelines calls.
 *
 * @param pigment Pigment instance.
 * @param initial_data Bytes from a previous get_data call, or NULL for an empty cache. Bytes from
 *                     a different GPU / driver / version are silently discarded.
 * @param size Size of initial_data in bytes (0 if NULL).
 * @param out Receives the created cache.
 *
 * @return PIGMENT_SUCCESS on success, otherwise an error code.
 */
PIGMENT_API PResult pigment_create_pipeline_cache(Pigment* pigment, const void* initial_data, uint64_t size, PPipelineCache** out);

/**
 * @brief Destroy a pipeline cache.
 *
 * @param pigment Pigment instance.
 * @param cache Cache to destroy.
 */
PIGMENT_API void pigment_destroy_pipeline_cache(Pigment* pigment, PPipelineCache* cache);

/**
 * @brief Serialize the cache contents to bytes for persistence on disk.
 *
 * Call once with out_data = NULL to receive the required size, then allocate
 * and call again with the buffer.
 *
 * @param pigment Pigment instance.
 * @param cache Cache to query.
 * @param out_data Destination buffer, or NULL to only query the size.
 * @param out_size In: capacity of out_data (ignored if out_data is NULL). Out: required size if
 *                 out_data was NULL, otherwise bytes actually written.
 *
 * @return PIGMENT_SUCCESS on success, otherwise an error code.
 */
PIGMENT_API PResult pigment_pipeline_cache_get_data(Pigment* pigment, PPipelineCache* cache, void* out_data, uint64_t* out_size);

PIGMENT_API PResult pigment_create_graphic_pipelines(Pigment* pigment, PPipelineCache* cache, const PPipelineDesc* descs, uint32_t count, PPipeline** out);
PIGMENT_API PResult pigment_create_compute_pipelines(Pigment* pigment, PPipelineCache* cache, const PComputePipelineDesc* descs, uint32_t count, PPipeline** out);

PIGMENT_API void pigment_destroy_pipeline(Pigment* pigment, PPipeline* pipeline);

PIGMENT_API void pigment_bind_pipeline(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline);

#ifdef __cplusplus
}
#endif

#endif
