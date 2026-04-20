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

#ifndef DESCRIPTOR_H
#define DESCRIPTOR_H

#include "defines.h"

PDescriptor* create_descriptor(uint32_t max_samplers, uint32_t max_images, PDevice* device);
int update_descriptor(PDescriptor* descriptor, PUniformBuffers* buffers, PImageList* images, PSamplerList* samplers, uint32_t max_samplers, uint32_t max_images, PDevice* device, uint32_t descriptor_count);

void destroy_descriptor(PDescriptor* descriptor, PDevice* device);

#endif
