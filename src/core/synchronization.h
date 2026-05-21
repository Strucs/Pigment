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

#ifndef PIGMENT_SYNCHRONIZATION_H
#define PIGMENT_SYNCHRONIZATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

PIGMENT_API PSync* create_sync(Pigment* pigment, const uint32_t max_frame, const uint32_t swapchain_image_count);
PIGMENT_API void destroy_sync(Pigment* pigment, PSync* sync, PSwapchain* swapchain, const uint32_t max_frame);
PIGMENT_API PResult recreate_image_available_semaphore(Pigment* pigment, PSync* sync, uint32_t index);
PIGMENT_API PResult recreate_render_finished_semaphores(Pigment* pigment, PSync* sync, uint32_t old_count, uint32_t new_count);

#ifdef __cplusplus
}
#endif

#endif
