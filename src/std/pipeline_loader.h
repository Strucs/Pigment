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

#ifndef LOADER_H
#define LOADER_H

#include "defines.h"
#include "pipeline.h"

typedef enum PShaderType {
    P_SHADER_TYPE_VERTEX   = 0,
    P_SHADER_TYPE_FRAGMENT = 1,
} PShaderType;

char* load_shader_code(const char* file_path, uint32_t* shader_size);
uint32_t* compile_glsl_to_spv(Pigment* pigment, const char* source_code, uint32_t source_size, PShaderType type, const char* file_name, uint32_t* spv_size);

PLayout* default_pipeline_layout(Pigment* pigment);
PLayout* default_light_gizmo_pipeline_layout(Pigment* pigment);

PPipelineDesc default_graphic_pipeline_desc(Pigment* pigment, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format);
PPipelineDesc default_light_gizmo_pipeline_desc(Pigment* pigment, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format);

#endif
