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

#ifndef PIGMENT_STD_CANVAS_H
#define PIGMENT_STD_CANVAS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pigment/defines.h"
#include "pigment/frame.h"
#include "pigment/pipeline.h"

typedef struct PStdCanvas PStdCanvas;
typedef struct PStdBindless PStdBindless;

typedef enum PStdCanvasAnchor {
    P_STD_CANVAS_ANCHOR_TOP_LEFT      = 0,
    P_STD_CANVAS_ANCHOR_TOP_CENTER    = 1,
    P_STD_CANVAS_ANCHOR_TOP_RIGHT     = 2,
    P_STD_CANVAS_ANCHOR_MIDDLE_LEFT   = 3,
    P_STD_CANVAS_ANCHOR_CENTER        = 4,
    P_STD_CANVAS_ANCHOR_MIDDLE_RIGHT  = 5,
    P_STD_CANVAS_ANCHOR_BOTTOM_LEFT   = 6,
    P_STD_CANVAS_ANCHOR_BOTTOM_CENTER = 7,
    P_STD_CANVAS_ANCHOR_BOTTOM_RIGHT  = 8,
} PStdCanvasAnchor;

/**
 * @brief Configuration for creating a 2D canvas.
 */
typedef struct PStdCanvasConfig {
    PFormat color_format;                  // Format of the color attachment of the target. Required.
    PSampleCount samples;                  // Sample count of the target. 0 defaults to P_SAMPLE_COUNT_1.
    const PBlendMode* blend_modes;         // Blend modes to support. NULL defaults to a single P_BLEND_MODE_ALPHA pipeline.
    uint32_t blend_mode_count;             // Number of entries in blend_modes. Ignored when blend_modes is NULL.
    uint32_t initial_instances;            // Initial instance capacity per blend mode. 0 defaults to 4096.
    PStdBindless* bindless;                // Bindless image table sampled by textured draws. Required.
    PDeviceQueue* const* shared_queues;    // NULL = EXCLUSIVE. Copied list. Include every queue family that accesses the instance buffers.
    uint32_t shared_queue_count;
} PStdCanvasConfig;

/**
 * @brief Create a 2D drawing context for simple shapes like rectangles. It internally manages one pipeline per declared blend mode.
 *
 * @param pigment Pigment instance.
 * @param config Canvas configuration (color format, samples, supported blend modes).
 *
 * @return A new PStdCanvas context, or NULL on failure.
 */
PIGMENT_API PStdCanvas* pigment_std_create_canvas(Pigment* pigment, const PStdCanvasConfig* config);

/**
 * @brief Destroy a canvas context and free its resources. Does not free the Pigment instance.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context to destroy.
 */
PIGMENT_API void pigment_std_destroy_canvas(Pigment* pigment, PStdCanvas* canvas);

/**
 * @brief Get the pipeline used internally by the canvas context for a given blend mode.
 *
 * Useful for setting render states or push constant ranges.
 *
 * @param canvas Canvas context.
 * @param blend_mode Blend mode whose pipeline should be returned. Must be one of the modes declared at creation.
 *
 * @return Internal pipeline used for 2D rendering with the requested blend mode, or NULL if not declared.
 */
PIGMENT_API PPipeline* pigment_std_canvas_pipeline(PStdCanvas* canvas, PBlendMode blend_mode);

typedef struct PStdCanvasBatch PStdCanvasBatch;

/**
 * @brief Start a new CPU batch for the given blend mode.
 *
 * Serialize calls on the same canvas and access to its bindless resources.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param target_w Target width in pixels.
 * @param target_h Target height in pixels.
 * @param blend_mode Blend mode declared at canvas creation.
 */
PIGMENT_API void pigment_std_canvas_begin(Pigment* pigment, PStdCanvas* canvas, uint32_t target_w, uint32_t target_h, PBlendMode blend_mode);

/**
 * @brief Prepare immutable canvas data and close the CPU batch on success.
 *
 * The batch survives later canvas changes or destruction. Failure keeps CPU data for retry.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 *
 * @return Prepared batch, or NULL on invalid input, empty batch or failure.
 */
PIGMENT_API PStdCanvasBatch* pigment_std_canvas_prepare(Pigment* pigment, PStdCanvas* canvas);

/**
 * @brief Record a prepared canvas batch, binding its pipeline and disabling depth and culling.
 *
 * Workers may share the batch with separate command buffers and pools.
 *
 * @param pigment Pigment instance.
 * @param cmd Recording command buffer.
 * @param batch Prepared batch, kept alive until the call returns.
 */
PIGMENT_API void pigment_std_draw_canvas(Pigment* pigment, PCommandBuffer* cmd, const PStdCanvasBatch* batch);

/**
 * @brief Release a canvas batch after its workers finish recording.
 *
 * Command buffers retain their resources until reset or destruction.
 *
 * @param pigment Pigment instance.
 * @param batch Prepared batch, or NULL.
 */
PIGMENT_API void pigment_std_release_canvas_batch(Pigment* pigment, PStdCanvasBatch* batch);

/**
 * @brief Append a filled rectangle in NDC space.
 *
 * - x, y: top-left corner. Both axes go from -1 (top/left) to +1 (bottom/right).
 *
 * - w, h: width/height. Range [0, 2] (2 = fullscreen).
 *
 * Example: x=-1, y=-1, w=2, h=2 draws a fullscreen quad.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param x X coordinate of the rectangle's top-left corner in NDC space.
 * @param y Y coordinate of the rectangle's top-left corner in NDC space.
 * @param w Width of the rectangle in NDC space.
 * @param h Height of the rectangle in NDC space.
 * @param color Color of the rectangle as an array of 4 floats (RGBA).
 */
PIGMENT_API void pigment_std_canvas_rect_ndc(Pigment* pigment, PStdCanvas* canvas, float x, float y, float w, float h, const float color[4]);

/**
 * @brief Append a filled rectangle in pixel coordinates (top-left origin, Y down).
 *
 * Pixel coordinates are relative to the target dimensions passed to `begin`.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param x X coordinate of the rectangle's top-left corner in pixel coordinates.
 * @param y Y coordinate of the rectangle's top-left corner in pixel coordinates.
 * @param w Width of the rectangle in pixel coordinates.
 * @param h Height of the rectangle in pixel coordinates.
 * @param color Color of the rectangle as an array of 4 floats (RGBA).
 */
PIGMENT_API void pigment_std_canvas_rect_pixel(Pigment* pigment, PStdCanvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h, const float color[4]);

/**
 * @brief Append a filled rectangle anchored to one of 9 screen positions, offset in pixels.
 *
 * Positioning is relative to the target dimensions passed to `begin`.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param anchor anchor point for positioning the rectangle.
 * @param offset_x X offset from the anchor point in pixels.
 * @param offset_y Y offset from the anchor point in pixels.
 * @param w Width of the rectangle in pixel coordinates.
 * @param h Height of the rectangle in pixel coordinates.
 * @param color Color of the rectangle as an array of 4 floats (RGBA).
 */
PIGMENT_API void pigment_std_canvas_rect_anchor(Pigment* pigment, PStdCanvas* canvas, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, const float color[4]);

/**
 * @brief Draw a textured rectangle in NDC space.
 *
 * - x, y: top-left corner. Both axes go from -1 (top/left) to +1 (bottom/right).
 *
 * - w, h: width/height. Range [0, 2] (2 = fullscreen).
 *
 * Example: x=-1, y=-1, w=2, h=2 draws a fullscreen quad.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param x X coordinate of the rectangle's top-left corner in NDC space.
 * @param y Y coordinate of the rectangle's top-left corner in NDC space.
 * @param w Width of the rectangle in NDC space.
 * @param h Height of the rectangle in NDC space.
 * @param image_idx Bindless image slot to sample.
 * @param sampler_idx Bindless sampler slot to sample with.
 * @param color Tint multiplied with the sampled texel (RGBA).
 */
PIGMENT_API void pigment_std_canvas_image_ndc(Pigment* pigment, PStdCanvas* canvas, float x, float y, float w, float h, uint32_t image_idx, uint32_t sampler_idx, const float color[4]);

/**
 * @brief Draw a textured rectangle in pixel coordinates, sampling a bindless image.
 *
 * Pixel coordinates are relative to the target dimensions passed to `begin`.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param x X coordinate of the rectangle's top-left corner in pixel coordinates.
 * @param y Y coordinate of the rectangle's top-left corner in pixel coordinates.
 * @param w Width of the rectangle in pixel coordinates.
 * @param h Height of the rectangle in pixel coordinates.
 * @param image_idx Bindless image slot to sample.
 * @param sampler_idx Bindless sampler slot to sample with.
 * @param color Tint multiplied with the sampled texel (RGBA).
 */
PIGMENT_API void pigment_std_canvas_image_pixel(Pigment* pigment, PStdCanvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, const float color[4]);

/**
 * @brief Draw a textured rectangle anchored to one of 9 screen positions, offset in pixels.
 *
 * Positioning is relative to the target dimensions passed to `begin`.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param anchor anchor point for positioning the rectangle.
 * @param offset_x X offset from the anchor point in pixels.
 * @param offset_y Y offset from the anchor point in pixels.
 * @param w Width of the rectangle in pixel coordinates.
 * @param h Height of the rectangle in pixel coordinates.
 * @param image_idx Bindless image slot to sample.
 * @param sampler_idx Bindless sampler slot to sample with.
 * @param color Tint multiplied with the sampled texel (RGBA).
 */
PIGMENT_API void pigment_std_canvas_image_anchor(Pigment* pigment, PStdCanvas* canvas, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, const float color[4]);

/**
 * @brief Draw a textured rectangle from a sub-region of an image in NDC space.
 *
 * - x, y: top-left corner. Both axes go from -1 (top/left) to +1 (bottom/right).
 *
 * - w, h: width/height. Range [0, 2] (2 = fullscreen).
 *
 * Example: x=-1, y=-1, w=2, h=2 draws a fullscreen quad.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param x X coordinate of the rectangle's top-left corner in NDC space.
 * @param y Y coordinate of the rectangle's top-left corner in NDC space.
 * @param w Width of the rectangle in NDC space.
 * @param h Height of the rectangle in NDC space.
 * @param image_idx Bindless image slot to sample.
 * @param sampler_idx Bindless sampler slot to sample with.
 * @param src_x Source sub-region left, in image pixels.
 * @param src_y Source sub-region top, in image pixels.
 * @param src_w Source sub-region width, in image pixels.
 * @param src_h Source sub-region height, in image pixels.
 * @param color Tint multiplied with the sampled texel (RGBA).
 */
PIGMENT_API void pigment_std_canvas_image_region_ndc(Pigment* pigment, PStdCanvas* canvas, float x, float y, float w, float h, uint32_t image_idx, uint32_t sampler_idx, int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, const float color[4]);

/**
 * @brief Draw a textured rectangle from a sub-region of an image in pixel coordinates.
 *
 * Pixel coordinates are relative to the target dimensions passed to `begin`.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param x X coordinate of the rectangle's top-left corner in pixel coordinates.
 * @param y Y coordinate of the rectangle's top-left corner in pixel coordinates.
 * @param w Width of the rectangle in pixel coordinates.
 * @param h Height of the rectangle in pixel coordinates.
 * @param image_idx Bindless image slot to sample.
 * @param sampler_idx Bindless sampler slot to sample with.
 * @param src_x Source sub-region left, in image pixels.
 * @param src_y Source sub-region top, in image pixels.
 * @param src_w Source sub-region width, in image pixels.
 * @param src_h Source sub-region height, in image pixels.
 * @param color Tint multiplied with the sampled texel (RGBA).
 */
PIGMENT_API void pigment_std_canvas_image_region_pixel(Pigment* pigment, PStdCanvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, const float color[4]);

/**
 * @brief Draw a textured rectangle from a sub-region of an image anchored to one of 9 screen positions, offset in pixels.
 *
 * Positioning is relative to the target dimensions passed to `begin`.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param anchor anchor point for positioning the rectangle.
 * @param offset_x X offset from the anchor point in pixels.
 * @param offset_y Y offset from the anchor point in pixels.
 * @param w Width of the rectangle in pixel coordinates.
 * @param h Height of the rectangle in pixel coordinates.
 * @param image_idx Bindless image slot to sample.
 * @param sampler_idx Bindless sampler slot to sample with.
 * @param src_x Source sub-region left, in image pixels.
 * @param src_y Source sub-region top, in image pixels.
 * @param src_w Source sub-region width, in image pixels.
 * @param src_h Source sub-region height, in image pixels.
 * @param color Tint multiplied with the sampled texel (RGBA).
 */
PIGMENT_API void pigment_std_canvas_image_region_anchor(Pigment* pigment, PStdCanvas* canvas, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, uint32_t image_idx, uint32_t sampler_idx, int32_t src_x, int32_t src_y, int32_t src_w, int32_t src_h, const float color[4]);

#ifdef __cplusplus
}
#endif

#endif
