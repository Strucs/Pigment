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

#ifndef PIGMENT_SAMPLER_H
#define PIGMENT_SAMPLER_H

#include "defines.h"

typedef enum PFilteringMode {
    P_FILTERING_MODE_NEAREST = 0,
    P_FILTERING_MODE_LINEAR  = 1,
} PFilteringMode;

typedef enum PAddressMode {
    P_ADDRESS_MODE_REPEAT          = 0,
    P_ADDRESS_MODE_MIRRORED_REPEAT = 1,
    P_ADDRESS_MODE_CLAMP_TO_EDGE   = 2,
    P_ADDRESS_MODE_CLAMP_TO_BORDER = 3,
} PAddressMode;

struct PSamplerDesc {
    PFilteringMode mag_filter;     // P_FILTERING_MODE_NEAREST or P_FILTERING_MODE_LINEAR
    PFilteringMode min_filter;     // P_FILTERING_MODE_NEAREST or P_FILTERING_MODE_LINEAR
    PFilteringMode mipmap_mode;    // P_FILTERING_MODE_NEAREST or P_FILTERING_MODE_LINEAR
    PAddressMode address_mode;     // 0 = REPEAT

    float max_anisotropy;    // 0 = off, otherwise clamped to device max
    bool compare_enable;     // depth-compare sampler (shadow maps)
    PCompareOp compare_op;
};

PSampler* pigment_create_sampler(Pigment* pigment, const PSamplerDesc* desc);
void pigment_destroy_sampler(Pigment* pigment, PSampler* sampler);

#endif
