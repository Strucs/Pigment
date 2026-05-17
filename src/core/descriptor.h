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

#ifndef PIGMENT_DESCRIPTOR_H
#define PIGMENT_DESCRIPTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"
#include "pipeline.h"

typedef enum PDescriptorType {
    P_DESCRIPTOR_TYPE_SAMPLER                = 0,
    P_DESCRIPTOR_TYPE_SAMPLED_IMAGE          = 1,
    P_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 2,
    P_DESCRIPTOR_TYPE_STORAGE_IMAGE          = 3,
    P_DESCRIPTOR_TYPE_UNIFORM_BUFFER         = 4,
    P_DESCRIPTOR_TYPE_STORAGE_BUFFER         = 5,
} PDescriptorType;

typedef enum PDescriptorBindingFlags {
    P_DESCRIPTOR_BINDING_NONE_BIT              = 0,
    P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT   = 1 << 0,
    P_DESCRIPTOR_BINDING_VARIABLE_COUNT_BIT    = 1 << 1,
    P_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT = 1 << 2,
} PDescriptorBindingFlags;

typedef enum PImageDescriptorLayout {
    P_IMAGE_DESCRIPTOR_LAYOUT_AUTO                    = 0,
    P_IMAGE_DESCRIPTOR_LAYOUT_SHADER_READ_ONLY        = 1,
    P_IMAGE_DESCRIPTOR_LAYOUT_GENERAL                 = 2,
    P_IMAGE_DESCRIPTOR_LAYOUT_DEPTH_READ_ONLY         = 3,
    P_IMAGE_DESCRIPTOR_LAYOUT_DEPTH_STENCIL_READ_ONLY = 4,
} PImageDescriptorLayout;

typedef struct PDescriptorBinding {
    uint32_t binding;
    PDescriptorType type;
    uint32_t count;
    PShaderStageFlags stages;
    PDescriptorBindingFlags flags;
} PDescriptorBinding;

typedef struct PDescriptorSetLayoutDesc {
    const PDescriptorBinding* bindings;
    uint32_t binding_count;
    const char* name;
} PDescriptorSetLayoutDesc;

typedef struct PDescriptorPoolSize {
    PDescriptorType type;
    uint32_t count;
} PDescriptorPoolSize;

typedef struct PDescriptorPoolDesc {
    const PDescriptorPoolSize* pool_sizes;
    uint32_t pool_size_count;
    uint32_t max_sets;
    PBool allow_update_after_bind;
    PBool allow_free_set;
    const char* name;
} PDescriptorPoolDesc;

typedef struct PDescriptorImageInfo {
    PSampler* sampler;
    PImage* image;
    PImageDescriptorLayout layout;    // 0 = auto (SHADER_READ_ONLY for sampled, GENERAL for storage)
} PDescriptorImageInfo;

typedef struct PDescriptorBufferInfo {
    PBuffer* buffer;
    uint64_t offset;
    uint64_t range;    // 0 = whole size
} PDescriptorBufferInfo;

typedef struct PDescriptorWrite {
    PDescriptorSet* set;
    uint32_t binding;
    uint32_t array_element;
    uint32_t count;
    PDescriptorType type;
    const PDescriptorImageInfo* image_infos;
    const PDescriptorBufferInfo* buffer_infos;
} PDescriptorWrite;

typedef struct PDescriptorCopy {
    PDescriptorSet* src;
    PDescriptorSet* dst;
    uint32_t src_binding;
    uint32_t src_array_element;
    uint32_t dst_binding;
    uint32_t dst_array_element;
    uint32_t count;
} PDescriptorCopy;

typedef struct PDescriptorSetAllocate {
    PDescriptorSetLayout* layout;
    uint32_t variable_count;    // 0 if no VARIABLE_COUNT binding
    const char* name;
} PDescriptorSetAllocate;

PDescriptorSetLayout* pigment_create_descriptor_set_layout(Pigment* pigment, const PDescriptorSetLayoutDesc* desc);
void pigment_destroy_descriptor_set_layout(Pigment* pigment, PDescriptorSetLayout* layout);

PDescriptorPool* pigment_create_descriptor_pool(Pigment* pigment, const PDescriptorPoolDesc* desc);
void pigment_destroy_descriptor_pool(Pigment* pigment, PDescriptorPool* pool);
void pigment_reset_descriptor_pool(Pigment* pigment, PDescriptorPool* pool);

PResult pigment_create_descriptor_sets(Pigment* pigment, PDescriptorPool* pool, const PDescriptorSetAllocate* allocs, uint32_t count, PDescriptorSet** out_sets);
void pigment_destroy_descriptor_sets(Pigment* pigment, PDescriptorSet** sets, uint32_t count);

void pigment_update_descriptors(Pigment* pigment, const PDescriptorWrite* writes, uint32_t write_count, const PDescriptorCopy* copies, uint32_t copy_count);

void pigment_cmd_bind_descriptor_sets(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline, uint32_t first_set, PDescriptorSet* const* sets, uint32_t set_count, const uint32_t* dynamic_offsets, uint32_t dynamic_offset_count);

#ifdef __cplusplus
}
#endif

#endif
