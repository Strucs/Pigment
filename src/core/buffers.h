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

#ifndef BUFFERS_H
#define BUFFERS_H

#include "defines.h"

PUniformBuffers* create_uniform_buffers(PDevice* device, uint32_t uniform_buffers_count);
void destroy_uniform_buffers(PUniformBuffers* buffers, PDevice* device, const uint32_t uniform_buffers_count);

#endif
