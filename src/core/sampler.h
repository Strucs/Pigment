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

typedef enum FilteringMode {
    NEAREST = 0,
    LINEAR  = 1,
} FilteringMode;

typedef enum PAddressMode {
    P_ADDRESS_MODE_REPEAT          = 0,
    P_ADDRESS_MODE_MIRRORED_REPEAT = 1,
    P_ADDRESS_MODE_CLAMP_TO_EDGE   = 2,
    P_ADDRESS_MODE_CLAMP_TO_BORDER = 3,
} PAddressMode;

struct PSamplerDesc {
    FilteringMode mag_filter;     // NEAREST or LINEAR
    FilteringMode min_filter;     // NEAREST or LINEAR
    FilteringMode mipmap_mode;    // NEAREST or LINEAR
    PAddressMode address_mode;    // 0 = REPEAT

    float max_anisotropy;         // 0 = off, otherwise clamped to device max
    bool compare_enable;          // depth-compare sampler (shadow maps)
    PCompareOp compare_op;
};

PSampler* pigment_create_sampler(Pigment* pigment, const PSamplerDesc* desc);
void pigment_destroy_sampler(Pigment* pigment, PSampler* sampler);

#endif
