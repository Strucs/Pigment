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

#ifndef SURFACE_H
#define SURFACE_H

#include "defines.h"

PSurface* create_surface(Pigment* pigment, PWindow* window);
void destroy_surface(Pigment* pigment, PSurface* surface);
PSwapchain* create_swapchain(Pigment* pigment, uint32_t framebuffer_width, uint32_t framebuffer_height, PPresentMode preferred_mode, bool transparent, PSurface* surface);
void destroy_swapchain(Pigment* pigment, PSwapchain* swapchain);
int create_image_views(Pigment* pigment, PSwapchain* swapchain);
int recreate_swapchain(Pigment* pigment, PWindowRenderer* renderer, uint32_t framebuffer_width, uint32_t framebuffer_height, PPresentMode preferred_mode);
PFormat pigment_get_color_format(PWindowRenderer* renderer);
PFormat pigment_get_depth_format(PWindowRenderer* renderer);
void pigment_get_swapchain_size(PWindowRenderer* renderer, uint32_t* out_width, uint32_t* out_height);

#endif
