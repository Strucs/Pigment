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

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

#include "pigment/defines.h"

typedef struct PIGMENT_ALIGN(16) PVertex {
    PVec3 pos;
    float uv_x;
    PVec3 normal;
    float uv_y;
    PVec4 color;
} PVertex;


#ifdef __cplusplus
}
#endif

#endif
