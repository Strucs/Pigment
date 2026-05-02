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
#include "window.h"
#include "frame.h"
#include "texture.h"
#include "pipeline.h"
#include "surface.h"
#include "buffers.h"
#include "image.h"

Pigment* init_pigment(PAppInfo* app_info, PWindowInfo* window_info, PigmentConfig* config);
void destroy_pigment(Pigment* pigment);
void pigment_wait_idle(Pigment* pigment);

void pigment_show_window(Pigment* pigment, uint32_t window_index);
bool pigment_should_run(Pigment* pigment);

void pigment_set_present_mode(Pigment* pigment, uint32_t window_index, PPresentMode mode);

void pigment_wait_frame_ready(Pigment* pigment, uint32_t window_index);
bool pigment_begin_frame(Pigment* pigment, uint32_t window_index);
void pigment_end_frame(Pigment* pigment, uint32_t window_index);

void pigment_begin_swapchain_pass(Pigment* pigment, uint32_t window_index);
void pigment_end_swapchain_pass(Pigment* pigment, uint32_t window_index);

PWindow* pigment_get_window(Pigment* pigment, uint32_t window_index);
PWindowRenderer* pigment_get_window_renderer(Pigment* pigment, uint32_t window_index);
uint32_t pigment_window_count(Pigment* pigment);

bool pigment_supports(Pigment* pigment, PFeature feature);

#endif
