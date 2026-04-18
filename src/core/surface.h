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

#ifndef SURFACE_H
#define SURFACE_H

#include "defines.h"
#include "depth.h"

PSurface* create_surface(PInstance* instance, PWindow* window);
void destroy_surface(PSurface* surface, PInstance* instance);
PSwapchain* create_swapchain(uint32_t framebuffer_width, uint32_t framebuffer_height, PPresentMode preferred_mode, PSurface* surface, PDevice* device);
void destroy_swapchain(PSwapchain* swapchain, PDevice* device);
int create_image_views(PSwapchain* swapchain, PDevice* device);
int recreate_swapchain(PWindowRenderer* renderer, uint32_t framebuffer_width, uint32_t framebuffer_height, PPresentMode preferred_mode, PDevice* device);

#endif
