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

#ifndef PIGMENT_H
#define PIGMENT_H

#include "defines.h"
#include "log.h"
#include "frame.h"
#include "sampler.h"
#include "pipeline.h"
#include "surface.h"
#include "buffers.h"
#include "image.h"
#include "resize.h"
#include "commands.h"
#include "cmd_sync.h"

Pigment* init_pigment(PAppInfo* app_info, PigmentConfig* config);
void destroy_pigment(Pigment* pigment);
void pigment_wait_idle(Pigment* pigment);

void pigment_wait_frame_ready(Pigment* pigment, PWindowRenderer* renderer);
PCommandBuffer* pigment_begin_frame(Pigment* pigment, PWindowRenderer* renderer);
void pigment_end_frame(Pigment* pigment, PWindowRenderer* renderer);
uint32_t pigment_renderer_current_frame(PWindowRenderer* renderer);
PCommandBuffer* pigment_renderer_frame_cmd(PWindowRenderer* renderer);

void pigment_begin_swapchain_pass(Pigment* pigment, PWindowRenderer* renderer);
void pigment_end_swapchain_pass(PWindowRenderer* renderer);

PBool pigment_supports(Pigment* pigment, PFeature feature);

PSampleCount pigment_get_max_sample_count(Pigment* pigment);
PBool pigment_supports_sample_count(Pigment* pigment, PSampleCount samples);

#endif
