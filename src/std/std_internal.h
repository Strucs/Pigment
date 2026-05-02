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

#ifndef STD_INTERNAL_H
#define STD_INTERNAL_H

#include "pigment_vk.h"

typedef struct PStdPushConstants {
    VkDeviceAddress vertex_buffer;
    VkDeviceAddress instance_buffer;
    VkDeviceAddress camera_buffer;
    VkDeviceAddress material_buffer;
    VkDeviceAddress light_buffer;
} PStdPushConstants;

typedef struct PStdGizmoPushConstants {
    VkDeviceAddress vertex_buffer;
    VkDeviceAddress camera_buffer;
    VkDeviceAddress light_buffer;
    float scale;
} PStdGizmoPushConstants;

#endif
