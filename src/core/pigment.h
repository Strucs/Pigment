/**
 * Copyright 2025 Angel-Leduc TA
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
#include "camera.h"
#include "window.h"
#include "frame.h"
#include "mesh.h"
#include "texture.h"
#include "pipeline.h"

Pigment* init_pigment(PAppInfo* app_info, PWindowInfo* window_info, PigmentConfig* config);
void destroy_pigment(Pigment* pigment);
void pigment_wait_idle(Pigment* pigment);

void pigment_show_window(Pigment* pigment);
bool pigment_should_run(Pigment* pigment);
void pigment_poll_events(void);
void pigment_handle_inputs(Pigment* pigment);
void pigment_set_present_mode(Pigment* pigment, PPresentMode mode);

bool pigment_begin_frame(Pigment* pigment, PCamera* camera);
void pigment_end_frame(Pigment* pigment);

#endif
