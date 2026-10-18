/**
 * Copyright 2026 Angel-Leduc TA
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

#ifndef PIGMENT_STD_MATERIAL_H
#define PIGMENT_STD_MATERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

#include "pigment/defines.h"

/**
 * @brief CPU material data with GPU versions retained by command buffers.
 *
 * The caller must serialize access to this object. Changes are published by
 * pigment_std_materials_address and leave previously recorded versions unchanged.
 */
typedef struct PMaterials PMaterials;

typedef struct PIGMENT_ALIGN(16) PMaterialDesc {
    PVec4 base_color_factor;
    PVec4 emissive_factor;

    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_scale;

    int32_t albedo_image;
    int32_t albedo_sampler;
    int32_t metallic_roughness_image;
    int32_t metallic_roughness_sampler;
    int32_t normal_image;
    int32_t normal_sampler;
    int32_t emissive_image;
    int32_t emissive_sampler;
    int32_t occlusion_image;
    int32_t occlusion_sampler;
} PMaterialDesc;

PIGMENT_API PMaterials* pigment_std_create_materials(Pigment* pigment, uint32_t initial_size);
PIGMENT_API void pigment_std_destroy_materials(Pigment* pigment, PMaterials* materials);

PIGMENT_API uint32_t pigment_std_material_create(Pigment* pigment, PMaterials* materials, const PMaterialDesc* desc);
PIGMENT_API void pigment_std_material_update(Pigment* pigment, PMaterials* materials, uint32_t id, const PMaterialDesc* desc);
PIGMENT_API void pigment_std_material_destroy(Pigment* pigment, PMaterials* materials, uint32_t id);

/**
 * @brief Publish the materials and retain their buffer in a command buffer.
 *
 * The address belongs to this recording. Request it again for each new recording.
 * Unchanged data shares its version. Modified data reuses an idle buffer or allocates one.
 * If publication fails, CPU changes are preserved for the next call.
 * The caller must serialize access to this object.
 *
 * @param pigment Pigment instance.
 * @param materials Materials to publish.
 * @param cmd Command buffer that uses the returned address.
 *
 * @return Buffer address, or 0 on invalid input or publication failure.
 */
PIGMENT_API uint64_t pigment_std_materials_address(Pigment* pigment, PMaterials* materials, PCommandBuffer* cmd);

#ifdef __cplusplus
}
#endif

#endif
