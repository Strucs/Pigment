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

#ifndef PIGMENT_STD_PIPELINE_LOADER_H
#define PIGMENT_STD_PIPELINE_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bindless.h"
#include "camera.h"
#include "file_io.h"

#include "pigment/defines.h"
#include "pigment/descriptor.h"
#include "pigment/pipeline.h"

PIGMENT_API char* load_shader_code(Pigment* pigment, const IOCallbacks* io, const char* file_path, uint32_t* shader_size);

PIGMENT_API PLayout* default_pipeline_layout(Pigment* pigment, PStdBindless* bindless);
PIGMENT_API PLayout* default_light_gizmo_pipeline_layout(Pigment* pigment, PStdBindless* bindless);
PIGMENT_API PLayout* default_skybox_pipeline_layout(Pigment* pigment, PStdBindless* bindless);

PIGMENT_API PPipelineDesc default_graphic_pipeline_desc(Pigment* pigment, PStdBindless* bindless, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format, PSampleCount samples);
PIGMENT_API PPipelineDesc default_light_gizmo_pipeline_desc(Pigment* pigment, PStdBindless* bindless, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format, PSampleCount samples);
PIGMENT_API PPipelineDesc default_skybox_pipeline_desc(Pigment* pigment, PStdBindless* bindless, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format, PSampleCount samples);
PIGMENT_API PPipelineDesc default_crt_pipeline_desc(Pigment* pigment, PStdBindless* bindless, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format, PSampleCount samples);

#ifdef __cplusplus
}
#endif

#endif
