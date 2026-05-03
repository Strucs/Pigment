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

#ifndef STD_DRAW_H
#define STD_DRAW_H

#include <cglm/types.h>
#include "defines.h"
#include "bindless.h"
#include "pipeline.h"
#include "mesh.h"
#include "camera.h"

typedef struct PInstanceRing PInstanceRing;
typedef struct PMaterials PMaterials;
typedef struct PLights PLights;

typedef struct PInstanceData {
    mat4 transform;
    uint32_t material_id;
} __attribute__((aligned(16))) PInstanceData;

typedef struct PDrawCall {
    PMeshBuffers* mesh;
    PInstanceData* instances;
    uint32_t instance_count;
    uint32_t first_index;
    uint32_t index_count;
} PDrawCall;

PInstanceRing* pigment_std_create_instance_ring(Pigment* pigment, uint32_t max_instances_per_frame);
void pigment_std_destroy_instance_ring(Pigment* pigment, PInstanceRing* ring);

void pigment_draw(Pigment* pigment, uint32_t window_index, PStdBindless* bindless, PInstanceRing* ring, PMaterials* materials, PLights* lights, PCamera* camera, PPipeline* pipeline, PDrawCall* draws, uint32_t draw_count);
void pigment_std_draw_skybox(Pigment* pigment, uint32_t window_index, PStdBindless* bindless, PPipeline* pipeline, PCamera* camera, uint32_t cubemap_slot, uint32_t sampler_slot);

#endif
