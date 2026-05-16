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

#ifndef PIGMENT_DEVICE_H
#define PIGMENT_DEVICE_H

#include "defines.h"

/**
 * @brief Bytes the device can still allocate for memory matching the given flags.
 *
 * @param pigment Pigment instance.
 * @param flags Memory property flags the memory must satisfy to be counted.
 *
 * @return Allocatable bytes, or UINT64_MAX when the budget cannot be measured.
 */
uint64_t pigment_memory_budget(Pigment* pigment, PMemoryFlags flags);

#endif
