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

#ifndef PIGMENT_RESIZE_H
#define PIGMENT_RESIZE_H

#include "defines.h"

uint32_t pigment_register_swapchain_resize(Pigment* pigment, PSwapchainResizeFn func, void* user_data);
void pigment_unregister_swapchain_resize(Pigment* pigment, uint32_t handle);

#endif
