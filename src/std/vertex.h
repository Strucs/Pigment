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

#ifndef PIGMENT_STD_VERTEX_H
#define PIGMENT_STD_VERTEX_H

#include <cglm/types.h>
#include "defines.h"

typedef struct PVertex {
    vec3 pos;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
} __attribute__((aligned(16))) PVertex;

#endif
