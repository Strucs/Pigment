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

#ifndef PIGMENT_ALLOC_H
#define PIGMENT_ALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

#include <string.h>

PIGMENT_API void* p_alloc_impl(Pigment* pigment, uint64_t size, uint64_t alignment, PAllocScope scope);
PIGMENT_API void* p_calloc_impl(Pigment* pigment, uint64_t count, uint64_t size, uint64_t alignment, PAllocScope scope);
PIGMENT_API void* p_realloc_impl(Pigment* pigment, void* ptr, uint64_t old_size, uint64_t new_size, uint64_t alignment, PAllocScope scope);
PIGMENT_API void p_free_impl(Pigment* pigment, void* ptr);

// === ALLOC HELPERS ===

#define P_ALLOC_COMMAND(pigment, size, align) p_alloc_impl((pigment), (size), (align), P_ALLOC_SCOPE_COMMAND)
#define P_ALLOC_OBJECT(pigment, size, align) p_alloc_impl((pigment), (size), (align), P_ALLOC_SCOPE_OBJECT)
#define P_ALLOC_CACHE(pigment, size, align) p_alloc_impl((pigment), (size), (align), P_ALLOC_SCOPE_CACHE)
#define P_ALLOC_DEVICE(pigment, size, align) p_alloc_impl((pigment), (size), (align), P_ALLOC_SCOPE_DEVICE)
#define P_ALLOC_INSTANCE(pigment, size, align) p_alloc_impl((pigment), (size), (align), P_ALLOC_SCOPE_INSTANCE)

// === CALLOC HELPERS ===

#define P_CALLOC_COMMAND(pigment, count, size, align) p_calloc_impl((pigment), (count), (size), (align), P_ALLOC_SCOPE_COMMAND)
#define P_CALLOC_OBJECT(pigment, count, size, align) p_calloc_impl((pigment), (count), (size), (align), P_ALLOC_SCOPE_OBJECT)
#define P_CALLOC_CACHE(pigment, count, size, align) p_calloc_impl((pigment), (count), (size), (align), P_ALLOC_SCOPE_CACHE)
#define P_CALLOC_DEVICE(pigment, count, size, align) p_calloc_impl((pigment), (count), (size), (align), P_ALLOC_SCOPE_DEVICE)
#define P_CALLOC_INSTANCE(pigment, count, size, align) p_calloc_impl((pigment), (count), (size), (align), P_ALLOC_SCOPE_INSTANCE)

// === REALLOC HELPERS ===

#define P_REALLOC_COMMAND(pigment, ptr, old, new_size, align) p_realloc_impl((pigment), (ptr), (old), (new_size), (align), P_ALLOC_SCOPE_COMMAND)
#define P_REALLOC_OBJECT(pigment, ptr, old, new_size, align) p_realloc_impl((pigment), (ptr), (old), (new_size), (align), P_ALLOC_SCOPE_OBJECT)
#define P_REALLOC_CACHE(pigment, ptr, old, new_size, align) p_realloc_impl((pigment), (ptr), (old), (new_size), (align), P_ALLOC_SCOPE_CACHE)
#define P_REALLOC_DEVICE(pigment, ptr, old, new_size, align) p_realloc_impl((pigment), (ptr), (old), (new_size), (align), P_ALLOC_SCOPE_DEVICE)
#define P_REALLOC_INSTANCE(pigment, ptr, old, new_size, align) p_realloc_impl((pigment), (ptr), (old), (new_size), (align), P_ALLOC_SCOPE_INSTANCE)

// === FREE ===

#define P_FREE(pigment, ptr) p_free_impl((pigment), (ptr))

// === TYPE-INFERRED ALLOCATORS ===

#define P_NEW_FOR_COMMAND(pigment, ptr) ((__typeof__(ptr)) P_CALLOC_COMMAND((pigment), 1, sizeof(*(ptr)), _Alignof(__typeof__(*(ptr)))))
#define P_NEW_FOR_OBJECT(pigment, ptr) ((__typeof__(ptr)) P_CALLOC_OBJECT((pigment), 1, sizeof(*(ptr)), _Alignof(__typeof__(*(ptr)))))
#define P_NEW_FOR_CACHE(pigment, ptr) ((__typeof__(ptr)) P_CALLOC_CACHE((pigment), 1, sizeof(*(ptr)), _Alignof(__typeof__(*(ptr)))))
#define P_NEW_FOR_DEVICE(pigment, ptr) ((__typeof__(ptr)) P_CALLOC_DEVICE((pigment), 1, sizeof(*(ptr)), _Alignof(__typeof__(*(ptr)))))
#define P_NEW_FOR_INSTANCE(pigment, ptr) ((__typeof__(ptr)) P_CALLOC_INSTANCE((pigment), 1, sizeof(*(ptr)), _Alignof(__typeof__(*(ptr)))))

#define P_NEW_ARRAY_FOR_COMMAND(pigment, ptr, n) ((__typeof__(ptr)) P_CALLOC_COMMAND((pigment), (n), sizeof(*(ptr)), _Alignof(__typeof__(*(ptr)))))
#define P_NEW_ARRAY_FOR_OBJECT(pigment, ptr, n) ((__typeof__(ptr)) P_CALLOC_OBJECT((pigment), (n), sizeof(*(ptr)), _Alignof(__typeof__(*(ptr)))))
#define P_NEW_ARRAY_FOR_CACHE(pigment, ptr, n) ((__typeof__(ptr)) P_CALLOC_CACHE((pigment), (n), sizeof(*(ptr)), _Alignof(__typeof__(*(ptr)))))
#define P_NEW_ARRAY_FOR_DEVICE(pigment, ptr, n) ((__typeof__(ptr)) P_CALLOC_DEVICE((pigment), (n), sizeof(*(ptr)), _Alignof(__typeof__(*(ptr)))))
#define P_NEW_ARRAY_FOR_INSTANCE(pigment, ptr, n) ((__typeof__(ptr)) P_CALLOC_INSTANCE((pigment), (n), sizeof(*(ptr)), _Alignof(__typeof__(*(ptr)))))

// === STACK OR HEAP HELPERS ===

#define P_STACK_OR_HEAP_THRESHOLD 8

#define P_STACK_OR_HEAP(T, name, count)                                \
    T _##name##_stack[P_STACK_OR_HEAP_THRESHOLD];                      \
    T* name = ((count) <= P_STACK_OR_HEAP_THRESHOLD) ? _##name##_stack \
                                                     : (T*) P_ALLOC_COMMAND(pigment, (count) * sizeof(T), _Alignof(T))

#define P_STACK_OR_HEAP_FREE(pigment, name)           \
    do                                                \
    {                                                 \
        if((void*) (name) != (void*) _##name##_stack) \
        {                                             \
            P_FREE((pigment), (name));                \
        }                                             \
    }                                                 \
    while(0)

// === DYNAMIC ARRAY RESERVE ===

static inline PResult p_array_reserve_impl(Pigment* pigment, void** ptr, uint32_t count, uint32_t* cap, uint32_t add, uint32_t initial, uint64_t elem_size, uint64_t elem_align, PAllocScope scope)
{
    if((uint64_t) count + add <= *cap)
    {
        return PIGMENT_SUCCESS;
    }
    uint32_t new_cap = (*cap == 0) ? initial : *cap;
    while(new_cap < count + add)
    {
        new_cap *= 2;
    }
    void* new_ptr = p_realloc_impl(pigment, *ptr, elem_size * (uint64_t) *cap, elem_size * (uint64_t) new_cap, elem_align, scope);
    if(new_ptr == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }
    *ptr = new_ptr;
    *cap = new_cap;
    return PIGMENT_SUCCESS;
}

#define P_ARRAY_RESERVE_COMMAND(pigment, array, count, cap, add, initial) \
    p_array_reserve_impl((pigment), (void**) &(array), (count), &(cap), (add), (initial), sizeof(*(array)), _Alignof(__typeof__(*(array))), P_ALLOC_SCOPE_COMMAND)

#define P_ARRAY_RESERVE_OBJECT(pigment, array, count, cap, add, initial) \
    p_array_reserve_impl((pigment), (void**) &(array), (count), &(cap), (add), (initial), sizeof(*(array)), _Alignof(__typeof__(*(array))), P_ALLOC_SCOPE_OBJECT)

#define P_ARRAY_RESERVE_CACHE(pigment, array, count, cap, add, initial) \
    p_array_reserve_impl((pigment), (void**) &(array), (count), &(cap), (add), (initial), sizeof(*(array)), _Alignof(__typeof__(*(array))), P_ALLOC_SCOPE_CACHE)

#define P_ARRAY_RESERVE_DEVICE(pigment, array, count, cap, add, initial) \
    p_array_reserve_impl((pigment), (void**) &(array), (count), &(cap), (add), (initial), sizeof(*(array)), _Alignof(__typeof__(*(array))), P_ALLOC_SCOPE_DEVICE)

#define P_ARRAY_RESERVE_INSTANCE(pigment, array, count, cap, add, initial) \
    p_array_reserve_impl((pigment), (void**) &(array), (count), &(cap), (add), (initial), sizeof(*(array)), _Alignof(__typeof__(*(array))), P_ALLOC_SCOPE_INSTANCE)

#ifdef __cplusplus
}
#endif

#endif
