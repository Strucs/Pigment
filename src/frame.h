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

#ifndef FRAME_H
#define FRAME_H

#include "defines.h"

bool begin_frame(PBuffers* buffers, PSwapchain** swapchain, PSync** sync, PCommands* commands, PDescriptor* descriptor, PPipeline* pipeline, PSurface* surface, PWindow* window, PDevice* device, uint32_t max_frame, uint32_t* out_image_index);
void end_frame(PSwapchain** swapchain, PSync** sync, PCommands* commands, PDevice* device, uint32_t image_index, uint32_t max_frame);

#endif
