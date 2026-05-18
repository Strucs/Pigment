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

#include "internal.h"

void* p_alloc_impl(Pigment* pigment, uint64_t size, uint64_t alignment, PAllocScope scope)
{
    return pigment->cpu_allocator.alloc(pigment->cpu_allocator.user_data, size, alignment, scope);
}

void* p_calloc_impl(Pigment* pigment, uint64_t count, uint64_t size, uint64_t alignment, PAllocScope scope)
{
    return pigment->cpu_allocator.calloc(pigment->cpu_allocator.user_data, count * size, alignment, scope);
}

void* p_realloc_impl(Pigment* pigment, void* ptr, uint64_t old_size, uint64_t new_size, uint64_t alignment, PAllocScope scope)
{
    return pigment->cpu_allocator.realloc(pigment->cpu_allocator.user_data, ptr, old_size, new_size, alignment, scope);
}

void p_free_impl(Pigment* pigment, void* ptr)
{
    if(ptr == NULL)
    {
        return;
    }

    pigment->cpu_allocator.free(pigment->cpu_allocator.user_data, ptr);
}
