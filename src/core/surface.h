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

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

/**
 * @brief Create a window renderer.
 *
 * @param pigment Pigment instance.
 * @param handles Native window handles.
 * @param desc Swapchain settings.
 *
 * @return Renderer, or NULL on failure.
 */
PIGMENT_API PWindowRenderer* pigment_renderer_create(Pigment* pigment, const PWindowHandles* handles, const PSwapchainDesc* desc);
PIGMENT_API void pigment_renderer_destroy(Pigment* pigment, PWindowRenderer* renderer);
PIGMENT_API void pigment_renderer_resize(PWindowRenderer* renderer, uint32_t width, uint32_t height);
PIGMENT_API void pigment_set_present_mode(PWindowRenderer* renderer, PPresentMode mode);
PIGMENT_API void pigment_set_color_space(PWindowRenderer* renderer, PColorSpace color_space);
PIGMENT_API void pigment_set_sample_count(PWindowRenderer* renderer, PSampleCount samples);

/**
 * @brief Recreate the renderer's surface and its associated swapchain using new native window handles.
 *
 * Requires all active frames to be completed or cancelled before recreation.
 * Old swapchains are released automatically.
 * Returns PIGMENT_NOT_READY until their destruction is complete.
 * Dispatches recreate callbacks on this thread. Synchronize access to their resources.
 *
 * @param pigment Pigment instance.
 * @param renderer The renderer to recreate the surface for.
 * @param handles Native window handles used to create the new surface.
 *
 * @return PIGMENT_SUCCESS on success.
 * PIGMENT_NOT_READY if active frames or old swapchain references remain.
 * PIGMENT_RECREATE_REQUIRED if the surface has no valid extent.
 * On failure, the renderer remains flagged for recreation.
 */
PIGMENT_API PResult pigment_recreate_surface(Pigment* pigment, PWindowRenderer* renderer, const PWindowHandles* handles);

/**
 * @brief Mark the renderer's swapchain as requiring recreation.
 *
 * Does not recreate or release any resources immediately.
 * The next acquisition returns PIGMENT_RECREATE_REQUIRED unless the surface is lost.
 * Call pigment_recreate_swapchain to perform the recreation.
 *
 * @param renderer The renderer whose swapchain should be invalidated.
 */
PIGMENT_API void pigment_renderer_invalidate_swapchain(PWindowRenderer* renderer);
/**
 * @brief Recreate the renderer's swapchain if it was flagged dirty by an acquire failure or a
 *        pigment_renderer_resize / present_mode / sample_count / color_space change.
 *
 * Active frames keep their original resources but will no longer be presented.
 * Old resources are released after recorded, GPU and presentation uses finish.
 * Dispatches recreate callbacks on this thread. Synchronize access to their resources.
 *
 * @param pigment Pigment instance.
 * @param renderer The renderer to recreate the swapchain for.
 *
 * @return PIGMENT_SUCCESS on success or no-op.
 * On failure, subsequent acquisitions return PIGMENT_RECREATE_REQUIRED
 * or PIGMENT_ERROR_SURFACE_LOST if the surface was lost.
 */
PIGMENT_API PResult pigment_recreate_swapchain(Pigment* pigment, PWindowRenderer* renderer);

PIGMENT_API PFormat pigment_get_color_format(PWindowRenderer* renderer);
PIGMENT_API PFormat pigment_get_depth_format(PWindowRenderer* renderer);
PIGMENT_API PColorSpace pigment_get_color_space(PWindowRenderer* renderer);
PIGMENT_API PSampleCount pigment_get_sample_count(PWindowRenderer* renderer);
PIGMENT_API void pigment_get_swapchain_size(PWindowRenderer* renderer, uint32_t* out_width, uint32_t* out_height);

#ifdef __cplusplus
}
#endif

#endif
