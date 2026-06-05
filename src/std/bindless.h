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

typedef struct PStdBindless PStdBindless;
typedef struct PStdPipelineLayouts PStdPipelineLayouts;

PIGMENT_API PStdBindless* pigment_std_create_bindless(Pigment* pigment, uint32_t max_images, uint32_t max_samplers, uint32_t max_cubemaps, uint32_t max_render_targets);
PIGMENT_API void pigment_std_destroy_bindless(Pigment* pigment, PStdBindless* bindless);

PIGMENT_API PStdPipelineLayouts** pigment_std_bindless_pipeline_layouts_slot(PStdBindless* bindless);

PIGMENT_API uint32_t pigment_std_add_image(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format);
PIGMENT_API uint32_t pigment_std_add_image_batch(Pigment* pigment, PStdBindless* bindless, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count);

/**
 * @brief Register an already-created image into the bindless image array.
 *
 * The bindless takes ownership and destroys it with pigment_std_destroy_bindless,
 * so the caller must not destroy it afterwards.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless context.
 * @param image Image to register, already uploaded and in a sampleable layout.
 *
 * @return Image slot, or UINT32_MAX on failure.
 */
PIGMENT_API uint32_t pigment_std_register_image(Pigment* pigment, PStdBindless* bindless, PImage* image);

/**
 * @brief Unregister an image and free its bindless slot for reuse.
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
 * @brief Unregister a cubemap and free its bindless slot for reuse.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless context.
 * @param slot Cubemap slot returned by pigment_std_add_cubemap.
 */
PIGMENT_API void pigment_std_unregister_cubemap(Pigment* pigment, PStdBindless* bindless, uint32_t slot);
    
/**
 * @brief Register a render target's color and depth images into the bindless.
 *
 * Unlike `pigment_std_register_image`, bindless does not take ownership.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless context.
 * @param rt Render target to register.
 * @param out_slots Out buffer receiving the assigned slots.
 * @param out_capacity Capacity of out_slots, must be at least color_count plus one when a depth is present.
 *
 * @return Number of slots written to out_slots, or 0 on failure.
 */
PIGMENT_API uint32_t pigment_std_register_render_target(Pigment* pigment, PStdBindless* bindless, PRenderTarget* rt, uint32_t* out_slots, uint32_t out_capacity);

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
 * @brief Get the bindless descriptor set for the current frame and stamp all registered resources as in-use on the given command buffer.
 *
 * @param pigment Pigment instance.
 * @param bindless Bindless context.
 * @param cmd Command buffer to record resource usage into. Can be NULL if the caller only needs the descriptor set and will handle resource lifetime manually.
 * @param current_frame Current frame in flight index.
 *
 * @return Descriptor set for the current frame, NULL otherwise.
 */
PIGMENT_API PDescriptorSet* pigment_std_bindless_set(Pigment* pigment, PStdBindless* bindless, PCommandBuffer* cmd, uint32_t current_frame);

#ifdef __cplusplus
}
#endif

#endif
