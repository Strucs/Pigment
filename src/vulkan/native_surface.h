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

#ifndef PIGMENT_VK_NATIVE_SURFACE_H
#define PIGMENT_VK_NATIVE_SURFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pigment/defines.h"

#include <volk.h>

PIGMENT_API VkSurfaceKHR create_vk_surface_from_handles(Pigment* pigment, const PWindowHandles* handles);

#ifdef __cplusplus
}
#endif

#endif
