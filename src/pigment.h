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
#include "lib/loader.h"

Pigment* init_pigment(PAppInfo* app_info, PWindowInfo* window_info, PModel* model, TexturesToLoad* textures_to_load, StringArray* texture_paths, uint32_t max_frame_in_flight);
void destroy_pigment(Pigment* pigment);

void pigment_draw_frame(Pigment* pigment);
void pigment_run(Pigment* pigment);

#endif
