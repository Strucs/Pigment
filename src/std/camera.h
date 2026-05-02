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

#ifndef STD_CAMERA_H
#define STD_CAMERA_H

#include <cglm/types.h>
#include "defines.h"

typedef struct PCamera PCamera;

PCamera* pigment_std_create_camera(Pigment* pigment);
void pigment_std_destroy_camera(Pigment* pigment, PCamera* camera);

void pigment_std_camera_set_view(PCamera* camera, mat4 view);
void pigment_std_camera_set_projection(PCamera* camera, mat4 projection);

uint64_t pigment_std_camera_frame_address(PCamera* camera, uint32_t current_frame);
void pigment_std_camera_upload(PCamera* camera, uint32_t current_frame);

#endif
