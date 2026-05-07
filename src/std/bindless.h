/**
 * Copyright 2025-2026 Angel-Leduc TA
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

#ifndef PIGMENT_STD_BINDLESS_H
#define PIGMENT_STD_BINDLESS_H

#include "defines.h"
#include "descriptor.h"
#include "sampler.h"
#include "render_targets.h"

#define PIGMENT_DEFAULT_MAX_IMAGES 128
#define PIGMENT_DEFAULT_MAX_SAMPLERS 16
#define PIGMENT_DEFAULT_MAX_CUBEMAPS 16
#define PIGMENT_DEFAULT_MAX_RENDER_TARGETS 16

typedef struct PStdBindless PStdBindless;

PStdBindless* pigment_std_create_bindless(Pigment* pigment, uint32_t max_images, uint32_t max_samplers, uint32_t max_cubemaps, uint32_t max_render_targets);
void pigment_std_destroy_bindless(Pigment* pigment, PStdBindless* bindless);

uint32_t pigment_std_upload_image(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format);
uint32_t pigment_std_upload_image_batch(Pigment* pigment, PStdBindless* bindless, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count);
uint32_t pigment_std_add_sampler(Pigment* pigment, PStdBindless* bindless, const PSamplerDesc* desc);

// 6 face buffers in the order: +X, -X, +Y, -Y, +Z, -Z. All faces must share width/height/format.
uint32_t pigment_std_upload_cubemap(Pigment* pigment, PStdBindless* bindless, const unsigned char* faces[6], uint32_t face_width, uint32_t face_height, PFormat format);

uint32_t pigment_std_register_render_target(Pigment* pigment, PStdBindless* bindless, PRenderTarget* rt);
uint32_t pigment_std_register_render_target_depth(Pigment* pigment, PStdBindless* bindless, PRenderTarget* rt);

PDescriptorSetLayout* pigment_std_bindless_layout(PStdBindless* bindless);
PDescriptorSet* pigment_std_bindless_set(Pigment* pigment, PStdBindless* bindless, uint32_t current_frame);

#endif
