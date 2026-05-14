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

#include "pigment/defines.h"
#include "pigment/frame.h"
#include "pigment/pipeline.h"

typedef struct PStdCanvas PStdCanvas;

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
 * Creates a 2D drawing context for simple shapes like rectangles. It internally manages a pipeline and descriptor sets for this purpose.
 *
 * @param pigment Pigment instance.
 * @param color_format Format of the color attachment in the render target where the 2D shapes will be drawn. Must be renderable and filterable.
 * @param samples Sample count of the render target where the 2D shapes will be drawn. Must be compatible with the device and color_format.
 *
 * @return A new PStdCanvas context, or NULL on failure.
 */
PStdCanvas* pigment_std_create_canvas(Pigment* pigment, PFormat color_format, PSampleCount samples);

/**
 * Destroys a canvas context and frees its resources. Does not free the Pigment instance.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context to destroy.
 */
void pigment_std_destroy_canvas(Pigment* pigment, PStdCanvas* canvas);

/**
 * Get the pipeline used internally by the canvas context. Useful for setting render states or push constant ranges.
 *
 * @param canvas Canvas context.
 *
 * @return Internal pipeline used for 2D rendering.
 */
PPipeline* pigment_std_canvas_pipeline(PStdCanvas* canvas);

/**
 * Bind the canvas pipeline and disable depth test/write. Call once at the start of your 2D pass.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param cmd Command buffer to record into.
 */
void pigment_std_canvas_begin(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd);

/**
 * @brief Draws a filled rectangle in NDC space.
 *
 * - x, y: top-left corner. Both axes go from -1 (top/left) to +1 (bottom/right).
 *
 * - w, h: width/height. Range [0, 2] (2 = fullscreen).
 *
 * Example: x=-1, y=-1, w=2, h=2 draws a fullscreen quad.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param cmd Command buffer to record into.
 * @param x X coordinate of the rectangle's top-left corner in NDC space.
 * @param y Y coordinate of the rectangle's top-left corner in NDC space.
 * @param w Width of the rectangle in NDC space.
 * @param h Height of the rectangle in NDC space.
 * @param color Color of the rectangle as an array of 4 floats (RGBA).
 */
void pigment_std_canvas_rect_ndc(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, float x, float y, float w, float h, const float color[4]);

/**
 * @brief Draws a filled rectangle in pixel coordinates (top-left origin, Y down).
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param cmd Command buffer to record into.
 * @param renderer window renderer to query for swapchain size.
 * @param x X coordinate of the rectangle's top-left corner in pixel coordinates.
 * @param y Y coordinate of the rectangle's top-left corner in pixel coordinates.
 * @param w Width of the rectangle in pixel coordinates.
 * @param h Height of the rectangle in pixel coordinates.
 * @param color Color of the rectangle as an array of 4 floats (RGBA).
 */
void pigment_std_canvas_rect_pixel(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, PWindowRenderer* renderer, int32_t x, int32_t y, int32_t w, int32_t h, const float color[4]);

/**
 * @brief Draws a filled rectangle anchored to one of 9 screen positions, offset in pixels.
 *
 * @param pigment Pigment instance.
 * @param canvas Canvas context.
 * @param cmd Command buffer to record into.
 * @param renderer window renderer to query for swapchain size.
 * @param anchor anchor point for positioning the rectangle.
 * @param offset_x X offset from the anchor point in pixels.
 * @param offset_y Y offset from the anchor point in pixels.
 * @param w Width of the rectangle in pixel coordinates.
 * @param h Height of the rectangle in pixel coordinates.
 * @param color Color of the rectangle as an array of 4 floats (RGBA).
 */
void pigment_std_canvas_rect_anchor(Pigment* pigment, PStdCanvas* canvas, PCommandBuffer* cmd, PWindowRenderer* renderer, PStdCanvasAnchor anchor, int32_t offset_x, int32_t offset_y, int32_t w, int32_t h, const float color[4]);

#endif
