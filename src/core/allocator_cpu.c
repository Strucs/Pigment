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

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #include <malloc.h>
#else
    #include <stdint.h>
#endif

static void default_free(void* user_data, void* ptr);
static void* default_alloc(void* user_data, uint64_t size, uint64_t alignment, PAllocScope scope);
static void* default_calloc(void* user_data, uint64_t size, uint64_t alignment, PAllocScope scope);
static void* default_realloc(void* user_data, void* ptr, uint64_t old_size, uint64_t new_size, uint64_t alignment, PAllocScope scope);

#if !defined(_WIN32)
typedef struct {
    void* raw;
    uint64_t size;
} alloc_header;
#endif

const PAllocator pigment_default_allocator = {
    .user_data             = NULL,
    .alloc                 = default_alloc,
    .calloc                = default_calloc,
    .realloc               = default_realloc,
    .free                  = default_free,
    .internal_alloc_notify = NULL,
    .internal_free_notify  = NULL,
};

static void* default_alloc(void* user_data, uint64_t size, uint64_t alignment, PAllocScope scope)
{
    (void) user_data;
    (void) scope;
#if defined(_WIN32)
    return _aligned_malloc((size_t) size, (size_t) alignment);
#else
    size_t total = (size_t) (size + sizeof(alloc_header) + alignment);
    void* raw    = malloc(total);
    if(raw == NULL)
    {
        return NULL;
    }
    uintptr_t aligned = ((uintptr_t) raw + sizeof(alloc_header) + alignment - 1) & ~((uintptr_t) alignment - 1);
    alloc_header* h   = (alloc_header*) (aligned - sizeof(alloc_header));
    h->raw            = raw;
    h->size           = size;
    return (void*) aligned;
#endif
}

static void* default_calloc(void* user_data, uint64_t size, uint64_t alignment, PAllocScope scope)
{
    void* ptr = default_alloc(user_data, size, alignment, scope);
    if(ptr != NULL)
    {
        memset(ptr, 0, (size_t) size);
    }
    return ptr;
}

static void* default_realloc(void* user_data, void* ptr, uint64_t old_size, uint64_t new_size, uint64_t alignment, PAllocScope scope)
{
    (void) user_data;
    (void) scope;
    (void) old_size;
    if(ptr == NULL)
    {
        return default_alloc(user_data, new_size, alignment, scope);
    }
    if(new_size == 0)
    {
        default_free(user_data, ptr);
        return NULL;
    }

#if defined(_WIN32)
    return _aligned_realloc(ptr, (size_t) new_size, (size_t) alignment);
#else
    alloc_header* h   = (alloc_header*) ((char*) ptr - sizeof(alloc_header));
    uint64_t real_old = h->size;

    void* new_ptr = default_alloc(user_data, new_size, alignment, scope);
    if(new_ptr == NULL)
    {
        return NULL;
    }

    uint64_t copy_size = (real_old < new_size) ? real_old : new_size;
    if(copy_size > 0)
    {
        memcpy(new_ptr, ptr, (size_t) copy_size);
    }

    default_free(user_data, ptr);
    return new_ptr;
#endif
}

static void default_free(void* user_data, void* ptr)
{
    (void) user_data;
    if(ptr == NULL)
    {
        return;
    }

#if defined(_WIN32)
    _aligned_free(ptr);
#else
    alloc_header* h = (alloc_header*) ((char*) ptr - sizeof(alloc_header));
    free(h->raw);
#endif
}

// === VULKAN BRIDGE =====================================================

static PAllocScope vk_to_pigment_scope(VkSystemAllocationScope vk_scope)
{
    switch(vk_scope)
    {
        case VK_SYSTEM_ALLOCATION_SCOPE_COMMAND:
            return P_ALLOC_SCOPE_COMMAND;
        case VK_SYSTEM_ALLOCATION_SCOPE_OBJECT:
            return P_ALLOC_SCOPE_OBJECT;
        case VK_SYSTEM_ALLOCATION_SCOPE_CACHE:
            return P_ALLOC_SCOPE_CACHE;
        case VK_SYSTEM_ALLOCATION_SCOPE_DEVICE:
            return P_ALLOC_SCOPE_DEVICE;
        case VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE:
            return P_ALLOC_SCOPE_INSTANCE;
        default:
            return P_ALLOC_SCOPE_OBJECT;
    }
}

static void* VKAPI_CALL vk_bridge_alloc(void* user_data, size_t size, size_t alignment, VkSystemAllocationScope vk_scope)
{
    const PAllocator* a = (const PAllocator*) user_data;
    return a->alloc(a->user_data, (uint64_t) size, (uint64_t) alignment, vk_to_pigment_scope(vk_scope));
}

static void* VKAPI_CALL vk_bridge_realloc(void* user_data, void* orig, size_t size, size_t alignment, VkSystemAllocationScope vk_scope)
{
    const PAllocator* a = (const PAllocator*) user_data;
    // Vulkan does not pass the old size; the allocator must handle that internally if needed.
    return a->realloc(a->user_data, orig, 0, (uint64_t) size, (uint64_t) alignment, vk_to_pigment_scope(vk_scope));
}

static void VKAPI_CALL vk_bridge_free(void* user_data, void* ptr)
{
    if(ptr == NULL)
    {
        return;
    }
    const PAllocator* a = (const PAllocator*) user_data;
    a->free(a->user_data, ptr);
}

static void VKAPI_CALL vk_bridge_internal_alloc(void* user_data, size_t size, VkInternalAllocationType vk_type, VkSystemAllocationScope vk_scope)
{
    (void) vk_type;
    const PAllocator* a = (const PAllocator*) user_data;
    if(a->internal_alloc_notify != NULL)
    {
        a->internal_alloc_notify(a->user_data, (uint64_t) size, P_INTERNAL_ALLOCATION_TYPE_EXECUTABLE, vk_to_pigment_scope(vk_scope));
    }
}

static void VKAPI_CALL vk_bridge_internal_free(void* user_data, size_t size, VkInternalAllocationType vk_type, VkSystemAllocationScope vk_scope)
{
    (void) vk_type;
    const PAllocator* a = (const PAllocator*) user_data;
    if(a->internal_free_notify != NULL)
    {
        a->internal_free_notify(a->user_data, (uint64_t) size, P_INTERNAL_ALLOCATION_TYPE_EXECUTABLE, vk_to_pigment_scope(vk_scope));
    }
}

void pigment_vk_build_allocation_callbacks(const PAllocator* allocator, VkAllocationCallbacks* out)
{
    if(allocator == NULL || out == NULL)
    {
        return;
    }
    out->pUserData             = (void*) allocator;
    out->pfnAllocation         = vk_bridge_alloc;
    out->pfnReallocation       = vk_bridge_realloc;
    out->pfnFree               = vk_bridge_free;
    out->pfnInternalAllocation = (allocator->internal_alloc_notify != NULL) ? vk_bridge_internal_alloc : NULL;
    out->pfnInternalFree       = (allocator->internal_free_notify != NULL) ? vk_bridge_internal_free : NULL;
}
