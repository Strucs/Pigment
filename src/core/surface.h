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

#ifndef PIGMENT_SURFACE_H
#define PIGMENT_SURFACE_H

#include "defines.h"

PWindowRenderer* pigment_renderer_create(Pigment* pigment, PCommandPool* pool, const PWindowHandles* handles, const PSwapchainDesc* desc);
void pigment_renderer_destroy(Pigment* pigment, PWindowRenderer* renderer);
void pigment_renderer_resize(PWindowRenderer* renderer, uint32_t width, uint32_t height);
void pigment_set_present_mode(PWindowRenderer* renderer, PPresentMode mode);
void pigment_set_color_space(PWindowRenderer* renderer, PColorSpace color_space);
void pigment_set_sample_count(PWindowRenderer* renderer, PSampleCount samples);

/**
 * @brief Recreate the renderer's swapchain if it was flagged dirty by an acquire failure or a
 *        pigment_renderer_resize / present_mode / sample_count / color_space change.
 *
 * Dispatches the swapchain recreate callbacks at the end.
 *
 * @param pigment Pigment instance.
 * @param renderer The renderer to recreate the swapchain for.
 *
 * @return PIGMENT_SUCCESS on success or no-op. On failure the renderer keeps a old swapchain
 *         and stays flagged so the user can retry next frame.
 */
PResult pigment_recreate_swapchain(Pigment* pigment, PWindowRenderer* renderer);

PFormat pigment_get_color_format(PWindowRenderer* renderer);
PFormat pigment_get_depth_format(PWindowRenderer* renderer);
PColorSpace pigment_get_color_space(PWindowRenderer* renderer);
PSampleCount pigment_get_sample_count(PWindowRenderer* renderer);
void pigment_get_swapchain_size(PWindowRenderer* renderer, uint32_t* out_width, uint32_t* out_height);

#endif
