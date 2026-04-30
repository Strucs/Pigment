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

#include "defines.h"
#include "pipeline.h"

typedef struct PStdDrawState PStdDrawState;

typedef struct PDrawCall {
    PMeshBuffers* mesh;
    mat4* transforms;
    uint32_t instance_count;
    uint32_t first_index;
    uint32_t index_count;
    uint32_t image_index;
    uint32_t sampler_index;
} PDrawCall;

PStdDrawState* pigment_std_draw_init(Pigment* pigment, uint32_t max_transforms_per_frame);
void pigment_std_draw_shutdown(Pigment* pigment, PStdDrawState* state);

void pigment_draw(Pigment* pigment, PStdDrawState* state, uint32_t window_index, PPipeline* pipeline, PDrawCall* draws, uint32_t draw_count);

#endif
