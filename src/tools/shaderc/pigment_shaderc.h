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

#ifndef PIGMENT_SHADERC_H
#define PIGMENT_SHADERC_H

#include <stdint.h>

typedef enum PShaderType {
    P_SHADER_TYPE_VERTEX   = 0,
    P_SHADER_TYPE_FRAGMENT = 1,
} PShaderType;

uint32_t* pigment_shaderc_compile_glsl(const char* source_code, uint32_t source_size, PShaderType type, const char* file_name, uint32_t* spv_size);
void pigment_shaderc_free(uint32_t* spv);

#endif
