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

#ifndef PIGMENT_STD_TYPES_H
#define PIGMENT_STD_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef float PVec2[2];
typedef float PVec3[3];
typedef float PVec4[4];
typedef float PMat3[3][3];
typedef float PMat4[4][4];

#define P_MAT4_IDENTITY           \
    {                             \
        {1.0f, 0.0f, 0.0f, 0.0f}, \
        {0.0f, 1.0f, 0.0f, 0.0f}, \
        {0.0f, 0.0f, 1.0f, 0.0f}, \
        {0.0f, 0.0f, 0.0f, 1.0f}  \
    }

#ifdef __cplusplus
}
#endif

#endif
