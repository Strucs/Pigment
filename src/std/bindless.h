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

#ifdef __cplusplus
extern "C" {
#endif

#include "render_targets.h"

#include "pigment/defines.h"
#include "pigment/descriptor.h"
#include "pigment/sampler.h"

#define PIGMENT_DEFAULT_MAX_IMAGES 128
#define PIGMENT_DEFAULT_MAX_SAMPLERS 16
#define PIGMENT_DEFAULT_MAX_CUBEMAPS 16
#define PIGMENT_DEFAULT_MAX_RENDER_TARGETS 16

/**
 * @brief Bindless descriptors with versions retained by recorded commands.
 *
 * The caller must serialize calls on the same bindless and changes to registered render targets.
 * Upload helpers also require exclusive access to the default graphics queue during submission.
 */
typedef struct PStdBindless PStdBindless;
typedef struct PStdBindlessVersion PStdBindlessVersion;
typedef struct PStdPipelineLayouts PStdPipelineLayouts;

/**
 * @brief Check bindless capacities without allocating GPU resources.
 *
 * Checks descriptor type limits, the fragment resource limit, the pool budget
 * and support for the complete layout. Does not guarantee allocation success.
 * Other pipeline resources and color attachments must be checked separately.
 *
 * @param pigment Pigment instance.
 * @param max_images Image slots, including the default white image. Must be nonzero.
 * @param max_samplers Sampler slots. Must be at least two for the default samplers.
 * @param max_cubemaps Cubemap slots. Zero reserves one descriptor.
 * @param max_render_targets Render target image slots. Zero reserves one descriptor.
 *
 * @return `true` if the requested capacities are supported.
 */
PIGMENT_API PBool pigment_std_bindless_supported(Pigment* pigment, uint32_t max_images, uint32_t max_samplers, uint32_t max_cubemaps, uint32_t max_render_targets);

PIGMENT_API PStdBindless* pigment_std_create_bindless(Pigment* pigment, uint32_t max_images, uint32_t max_samplers, uint32_t max_cubemaps, uint32_t max_render_targets);
PIGMENT_API void pigment_std_destroy_bindless(Pigment* pigment, PStdBindless* bindless);

PIGMENT_API PStdPipelineLayouts** pigment_std_bindless_pipeline_layouts_slot(PStdBindless* bindless);

PIGMENT_API uint32_t pigment_std_add_image(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format);
PIGMENT_API uint32_t pigment_std_add_image_batch(Pigment* pigment, PStdBindless* bindless, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count);

/**
 * @brief Register an already-created image into the bindless image array.
 *
 * Ownership transfers on success. Changes are published by pigment_std_bindless_prepare.
 * The caller must order sampling after upload completion through submission waits.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless context.
 * @param image Image to register, already uploaded and in a sampleable layout.
 *
 * @return Image slot, or UINT32_MAX on failure.
 */
PIGMENT_API uint32_t pigment_std_register_image(Pigment* pigment, PStdBindless* bindless, PImage* image);

/**
 * @brief Remove an image from future descriptor versions.
 *
 * The index can be reused in a new version. Recorded commands keep their old descriptors and image.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless context.
 * @param slot Image slot returned by pigment_std_register_image or pigment_std_add_image.
 */
PIGMENT_API void pigment_std_unregister_image(Pigment* pigment, PStdBindless* bindless, uint32_t slot);

PIGMENT_API uint32_t pigment_std_add_sampler(Pigment* pigment, PStdBindless* bindless, const PSamplerDesc* desc);

// 6 face buffers in the order: +X, -X, +Y, -Y, +Z, -Z. All faces must share width/height/format.
PIGMENT_API uint32_t pigment_std_add_cubemap(Pigment* pigment, PStdBindless* bindless, const unsigned char* faces[6], uint32_t face_width, uint32_t face_height, PFormat format);

/**
 * @brief Remove a cubemap from future descriptor versions.
 *
 * Recorded commands keep their old descriptors and image.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless context.
 * @param slot Cubemap slot returned by pigment_std_add_cubemap.
 */
PIGMENT_API void pigment_std_unregister_cubemap(Pigment* pigment, PStdBindless* bindless, uint32_t slot);

/**
 * @brief Register a render target's color and depth images into the bindless.
 *
 * Call once with slots = NULL to receive the required count, then allocate and
 * call again with the buffer. Unlike `pigment_std_register_image`, bindless does
 * not take ownership.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless context.
 * @param rt Render target to register.
 * @param slots Destination buffer, or NULL to only query the count.
 * @param slot_count In: capacity of slots (ignored if slots is NULL). Out: required count if slots was
 *                   NULL, otherwise slots actually written.
 *
 * @return PIGMENT_SUCCESS on success, an error code otherwise.
 */
PIGMENT_API PResult pigment_std_register_render_target(Pigment* pigment, PStdBindless* bindless, PRenderTarget* rt, uint32_t* slots, uint32_t* slot_count);

/**
 * @brief Unregister a render target and return all its slots to the free list.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless context.
 * @param rt Render target to unregister.
 */
PIGMENT_API void pigment_std_unregister_render_target(Pigment* pigment, PStdBindless* bindless, PRenderTarget* rt);

PIGMENT_API PDescriptorSetLayout* pigment_std_bindless_layout(PStdBindless* bindless);

/**
 * @brief Get the image associated with the given bindless image slot.
 *
 * @param bindless Bindless context.
 * @param image_slot Image slot returned by pigment_std_add_image / register_image.
 *
 * @return Image associated with the given slot, or NULL if the slot is invalid or unoccupied.
 */
PIGMENT_API PImage* pigment_std_bindless_image(PStdBindless* bindless, uint32_t image_slot);

/**
 * @brief Prepare immutable descriptors and retain their resources.
 *
 * Serialize access to bindless and registered render targets during preparation.
 * The returned version survives later changes, image resizes and bindless destruction.
 * Release it after its workers finish recording. Command buffers retain their own references.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless context.
 *
 * @return Prepared version, or NULL on failure.
 */
PIGMENT_API PStdBindlessVersion* pigment_std_bindless_prepare(Pigment* pigment, PStdBindless* bindless);

/**
 * @brief Retain prepared descriptors and their resources for a recording.
 *
 * Concurrent calls are allowed with separate command buffers and pools.
 * Never update the returned set. Keep the prepared reference until the call returns.
 *
 * @param pigment Pigment instance.
 * @param version Prepared version.
 * @param cmd Recording command buffer.
 *
 * @return Descriptor set, or NULL on invalid input.
 */
PIGMENT_API PDescriptorSet* pigment_std_bindless_version_set(Pigment* pigment, const PStdBindlessVersion* version, PCommandBuffer* cmd);

/**
 * @brief Release one prepared reference without waiting for the GPU.
 *
 * @param pigment Pigment instance.
 * @param version Prepared version, or NULL.
 */
PIGMENT_API void pigment_std_release_bindless_version(Pigment* pigment, PStdBindlessVersion* version);

#ifdef __cplusplus
}
#endif

#endif
