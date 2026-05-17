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
} PBlendMode;

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

typedef struct PPipelineDesc {
    PLayout* layout;

    const uint32_t* vertex_spv;
    uint32_t vertex_spv_size;
    const uint32_t* fragment_spv;
    uint32_t fragment_spv_size;

    /**
     * Per-stage specialization constants. NULL = no specialization for that stage.
     */
    const PSpecializationInfo* vertex_specialization;
    const PSpecializationInfo* fragment_specialization;

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
    PTopology topology;

    /**
     * Per-attachment blend modes. NULL = all attachments OPAQUE.
     * If non-NULL, count must equal color_format_count.
     */
    const PBlendMode* blend_modes;
    uint32_t blend_mode_count;

    PSampleCount sample_count;

    PBool sample_shading_enable;
    float min_sample_shading;

    /**
     * Multiview view mask. Must match the view_mask of the render pass this pipeline is bound in.
     * 0 = single-view (default). e.g. 0b111111 = 6 views simultaneously (cubemap shadow).
     */
    uint32_t view_mask;

    const char* name;
} PPipelineDesc;

typedef struct PComputePipelineDesc {
    PLayout* layout;

    const uint32_t* compute_spv;
    uint32_t compute_spv_size;

    /**
     * Specialization constants applied to the compute shader.
     */
    const PSpecializationInfo* specialization;

    const char* name;
} PComputePipelineDesc;

typedef struct PLayoutDesc {
    PDescriptorSetLayout** set_layouts;
    uint32_t set_layout_count;
    uint32_t push_size;
    PShaderStageFlags push_stages;
    const char* name;
} PLayoutDesc;

PLayout* pigment_create_layout(Pigment* pigment, const PLayoutDesc* desc);
void pigment_destroy_layout(Pigment* pigment, PLayout* layout);

/**
 * @brief Create a pipeline cache used to accelerate pigment_create_*_pipelines calls.
 *
 * @param pigment Pigment instance.
 * @param initial_data Bytes from a previous get_data call, or NULL for an empty cache. Bytes from
 *                     a different GPU / driver / version are silently discarded.
 * @param size Size of initial_data in bytes (0 if NULL).
 * @param out Receives the created cache.
 *
 * @return PIGMENT_SUCCESS, PIGMENT_ERROR_OUT_OF_MEMORY, or PIGMENT_ERROR_VULKAN.
 */
PResult pigment_create_pipeline_cache(Pigment* pigment, const void* initial_data, uint64_t size, PPipelineCache** out);

/**
 * @brief Destroy a pipeline cache.
 *
 * @param pigment Pigment instance.
 * @param cache Cache to destroy.
 */
void pigment_destroy_pipeline_cache(Pigment* pigment, PPipelineCache* cache);

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
 * @return PIGMENT_SUCCESS or PIGMENT_ERROR_VULKAN.
 */
PResult pigment_pipeline_cache_get_data(Pigment* pigment, PPipelineCache* cache, void* out_data, uint64_t* out_size);

PResult pigment_create_graphic_pipelines(Pigment* pigment, PPipelineCache* cache, const PPipelineDesc* descs, uint32_t count, PPipeline** out);
PResult pigment_create_compute_pipelines(Pigment* pigment, PPipelineCache* cache, const PComputePipelineDesc* descs, uint32_t count, PPipeline** out);

void pigment_destroy_pipeline(Pigment* pigment, PPipeline* pipeline);

void pigment_bind_pipeline(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline);

#ifdef __cplusplus
}
#endif

#endif
