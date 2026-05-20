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

PMaterials* pigment_std_create_materials(Pigment* pigment, uint32_t initial_size);
void pigment_std_destroy_materials(Pigment* pigment, PMaterials* materials);

uint32_t pigment_std_material_create(Pigment* pigment, PMaterials* materials, const PMaterialDesc* desc);
void pigment_std_material_update(Pigment* pigment, PMaterials* materials, uint32_t id, const PMaterialDesc* desc);
void pigment_std_material_destroy(Pigment* pigment, PMaterials* materials, uint32_t id);

uint64_t pigment_std_material_address(PMaterials* materials);

#ifdef __cplusplus
}
#endif

#endif
