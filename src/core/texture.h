/**
 * Copyright 2025 Angel-Leduc TA
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

#ifndef TEXTURE_H
#define TEXTURE_H

#include "defines.h"

PImageList* create_images(void);
uint32_t pigment_upload_image(Pigment* pigment, const unsigned char* pixels, uint32_t width, uint32_t height);
uint32_t pigment_upload_image_batch(Pigment* pigment, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, uint32_t count);
uint32_t pigment_add_sampler(Pigment* pigment, PSamplerDesc* desc);
int add_image_from_pixels(PImageList* image_list, const unsigned char* pixels, uint32_t width, uint32_t height, PCommands* commands, PDevice* device);
int add_default_image(PImageList* image_list, PCommands* commands, PDevice* device);
void destroy_images(PImageList* image_list, PDevice* device);
PSamplerList* create_samplers(uint32_t max_samplers, PDevice* device);
void destroy_samplers(PSamplerList* sampler_list, PDevice* device);

#endif
