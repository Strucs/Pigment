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

#include "camera_math.h"
#include "std_internal.h"

void pigment_perspective(float fov_rad, float aspect, float near, mat4 out)
{
    float f = 1.0f / tanf(fov_rad * 0.5f);

    mat4 projection = {
        {f / aspect, 0.0f, 0.0f,  0.0f},
        {      0.0f,   -f, 0.0f,  0.0f},
        {      0.0f, 0.0f, 0.0f, -1.0f},
        {      0.0f, 0.0f, near,  0.0f}
    };

    glm_mat4_copy(projection, out);
}

void pigment_perspective_finite(float fov_rad, float aspect, float near, float far, mat4 out)
{
    float f = 1.0f / tanf(fov_rad * 0.5f);

    mat4 projection = {
        {f / aspect, 0.0f,                        0.0f,  0.0f},
        {      0.0f,   -f,                        0.0f,  0.0f},
        {      0.0f, 0.0f,         near / (far - near), -1.0f},
        {      0.0f, 0.0f, (far * near) / (far - near),  0.0f}
    };

    glm_mat4_copy(projection, out);
}

void pigment_ortho(float left, float right, float bottom, float top, float near, float far, mat4 out)
{
    mat4 projection = {
        {           2.0f / (right - left),                            0.0f,                0.0f, 0.0f},
        {                            0.0f,          -2.0f / (top - bottom),                0.0f, 0.0f},
        {                            0.0f,                            0.0f, 1.0f / (far - near), 0.0f},
        {-(right + left) / (right - left), (top + bottom) / (top - bottom),  far / (far - near), 1.0f}
    };

    glm_mat4_copy(projection, out);
}
