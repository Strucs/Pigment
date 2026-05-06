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

#ifndef PIGMENT_STD_CAMERA_MATH_H
#define PIGMENT_STD_CAMERA_MATH_H

#include <cglm/types.h>
#include "defines.h"

void pigment_perspective(float fov_rad, float aspect, float near, mat4 out);
void pigment_perspective_finite(float fov_rad, float aspect, float near, float far, mat4 out);
void pigment_ortho(float left, float right, float bottom, float top, float near, float far, mat4 out);

#endif
