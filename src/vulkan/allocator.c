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

#include "pigment_vk.h"

#include "structs.h"
#include "internal.h"
#include "log_internal.h"

#define DEFAULT_BLOCK_SIZE (4ULL * 1024 * 1024)    // 4 MiB
#define DEDICATED_THRESHOLD_DIVISOR 4ULL
#define INITIAL_FREE_RANGE_CAPACITY 16

typedef struct DefaultFreeRange {
    VkDeviceSize offset;
    VkDeviceSize size;
} DefaultFreeRange;

typedef struct DefaultBlock {
    VkDeviceMemory memory;
    VkDeviceSize block_size;
    void* mapped;
    DefaultFreeRange* free_ranges;
    uint32_t free_range_count;
    uint32_t free_range_capacity;
    struct DefaultBlock* next;
} DefaultBlock;

typedef struct DefaultPool {
    DefaultBlock* blocks;
} DefaultPool;

typedef struct DefaultAllocator {
    PVkAllocator vtable;
    Pigment* pigment;
    VkDevice device;
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkDeviceSize non_coherent_atom_size;
    VkDeviceSize block_size;
    VkDeviceSize dedicated_threshold;
    DefaultPool pools[VK_MAX_MEMORY_TYPES];
    pigment_rwlock_t lock;
} DefaultAllocator;

struct PVkAllocation {
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
    void* mapped;
    DefaultBlock* block;
    uint32_t memory_type_index;
};

static inline VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment);
static uint32_t find_memory_type_index(const VkPhysicalDeviceMemoryProperties* props, uint32_t type_filter, VkMemoryPropertyFlags properties);
static PBool memory_type_is_host_visible(const VkPhysicalDeviceMemoryProperties* props, uint32_t type_index);
static void destroy_block(DefaultAllocator* alloc, DefaultBlock* block);
static DefaultBlock* create_block(DefaultAllocator* alloc, uint32_t memory_type_index, VkDeviceSize size);
static PBool block_try_allocate(DefaultBlock* block, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize* out_offset);
static void block_release(DefaultBlock* block, VkDeviceSize offset, VkDeviceSize size);
static PBool block_is_empty(const DefaultBlock* block);
static PVkAllocation* create_allocation(VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, void* block_mapped, DefaultBlock* block, uint32_t memory_type_index);
static PVkAllocation* allocate_pooled(DefaultAllocator* alloc, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index);
static PVkAllocation* allocate_dedicated(DefaultAllocator* alloc, VkDeviceSize size, uint32_t memory_type_index);
static void release_allocation(DefaultAllocator* alloc, PVkAllocation* allocation);
static VkResult default_create_buffer(void* user_data, const VkBufferCreateInfo* buffer_info, const PVkAllocationCreateInfo* alloc_info, VkBuffer* out_buffer, PVkAllocation** out_allocation);
static void default_destroy_buffer(void* user_data, VkBuffer buffer, PVkAllocation* allocation);
static VkResult default_create_image(void* user_data, const VkImageCreateInfo* image_info, const PVkAllocationCreateInfo* alloc_info, VkImage* out_image, PVkAllocation** out_allocation);
static void default_destroy_image(void* user_data, VkImage image, PVkAllocation* allocation);
static VkResult default_map(void* user_data, PVkAllocation* allocation, void** out_data);
static void default_unmap(void* user_data, PVkAllocation* allocation);
static VkMemoryPropertyFlags default_get_memory_flags(void* user_data, PVkAllocation* allocation);
static void default_flush(void* user_data, PVkAllocation* allocation, VkDeviceSize offset, VkDeviceSize size);
static void default_invalidate(void* user_data, PVkAllocation* allocation, VkDeviceSize offset, VkDeviceSize size);
static void default_destroy(void* user_data);

PVkAllocator* pigment_vk_create_default_allocator(Pigment* pigment, const PVkDefaultAllocatorCreateInfo* info)
{
    if(pigment == NULL || pigment->device == NULL)
    {
        return NULL;
    }

    DefaultAllocator* alloc = calloc(1, sizeof(*alloc));
    if(alloc == NULL)
    {
        return NULL;
    }

    alloc->pigment = pigment;
    alloc->device  = pigment->device->logical_device;
    vkGetPhysicalDeviceMemoryProperties(pigment->device->physical_device, &alloc->memory_properties);

    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(pigment->device->physical_device, &device_properties);
    alloc->non_coherent_atom_size = device_properties.limits.nonCoherentAtomSize;
    if(alloc->non_coherent_atom_size == 0)
    {
        alloc->non_coherent_atom_size = 1;
    }

    alloc->block_size          = (info != NULL && info->block_size != 0) ? info->block_size : DEFAULT_BLOCK_SIZE;
    alloc->dedicated_threshold = alloc->block_size / DEDICATED_THRESHOLD_DIVISOR;

    (void) pigment_rwlock_init(&alloc->lock);

    alloc->vtable.user_data        = alloc;
    alloc->vtable.create_buffer    = default_create_buffer;
    alloc->vtable.destroy_buffer   = default_destroy_buffer;
    alloc->vtable.create_image     = default_create_image;
    alloc->vtable.destroy_image    = default_destroy_image;
    alloc->vtable.map              = default_map;
    alloc->vtable.unmap            = default_unmap;
    alloc->vtable.get_memory_flags = default_get_memory_flags;
    alloc->vtable.flush            = default_flush;
    alloc->vtable.invalidate       = default_invalidate;
    alloc->vtable.destroy          = default_destroy;

    return &alloc->vtable;
}

void pigment_vk_destroy_allocator(PVkAllocator* allocator)
{
    if(allocator == NULL || allocator->destroy == NULL)
    {
        return;
    }

    allocator->destroy(allocator->user_data);
}

static inline VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
    if(alignment == 0)
    {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint32_t find_memory_type_index(const VkPhysicalDeviceMemoryProperties* props, uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    for(uint32_t i = 0; i < props->memoryTypeCount; i++)
    {
        if((type_filter & (1 << i)) && (props->memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

static PBool memory_type_is_host_visible(const VkPhysicalDeviceMemoryProperties* props, uint32_t type_index)
{
    return (props->memoryTypes[type_index].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

static void destroy_block(DefaultAllocator* alloc, DefaultBlock* block)
{
    if(block == NULL)
    {
        return;
    }
    if(block->mapped != NULL)
    {
        vkUnmapMemory(alloc->device, block->memory);
    }
    if(block->memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(alloc->device, block->memory, NULL);
    }
    free(block->free_ranges);
    free(block);
}

static DefaultBlock* create_block(DefaultAllocator* alloc, uint32_t memory_type_index, VkDeviceSize size)
{
    DefaultBlock* block = calloc(1, sizeof(*block));
    if(block == NULL)
    {
        goto ERROR;
    }

    VkMemoryAllocateFlagsInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };
    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = size,
        .memoryTypeIndex = memory_type_index,
        .pNext           = &flags_info
    };

    VkResult result;
    if((result = vkAllocateMemory(alloc->device, &alloc_info, NULL, &block->memory)) != VK_SUCCESS)
    {
        PLOG_DEBUG(alloc->pigment, "vkAllocateMemory failed for block (size=%llu, type=%u, result=%d). Caller may retry on a fallback memory type.", (unsigned long long) size, memory_type_index, result);
        goto ERROR;
    }

    block->block_size = size;

    if(memory_type_is_host_visible(&alloc->memory_properties, memory_type_index))
    {
        if(vkMapMemory(alloc->device, block->memory, 0, VK_WHOLE_SIZE, 0, &block->mapped) != VK_SUCCESS)
        {
            block->mapped = NULL;
        }
    }

    block->free_ranges = malloc(INITIAL_FREE_RANGE_CAPACITY * sizeof(*block->free_ranges));
    if(block->free_ranges == NULL)
    {
        goto ERROR;
    }
    block->free_range_capacity   = INITIAL_FREE_RANGE_CAPACITY;
    block->free_ranges[0].offset = 0;
    block->free_ranges[0].size   = size;
    block->free_range_count      = 1;

    return block;

ERROR:
    destroy_block(alloc, block);
    return NULL;
}

static PBool block_try_allocate(DefaultBlock* block, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize* out_offset)
{
    for(uint32_t i = 0; i < block->free_range_count; i++)
    {
        DefaultFreeRange range      = block->free_ranges[i];
        VkDeviceSize aligned_offset = align_up(range.offset, alignment);
        VkDeviceSize padding        = aligned_offset - range.offset;

        if(padding + size > range.size)
        {
            continue;
        }

        VkDeviceSize remaining_after = range.size - padding - size;
        *out_offset                  = aligned_offset;

        if(padding > 0 && remaining_after > 0)
        {
            if(block->free_range_count + 1 > block->free_range_capacity)
            {
                uint32_t new_capacity        = block->free_range_capacity * 2;
                DefaultFreeRange* new_ranges = realloc(block->free_ranges, new_capacity * sizeof(*new_ranges));
                if(new_ranges == NULL)
                {
                    return P_FALSE;
                }
                block->free_ranges         = new_ranges;
                block->free_range_capacity = new_capacity;
            }

            memmove(&block->free_ranges[i + 1], &block->free_ranges[i], (block->free_range_count - i) * sizeof(*block->free_ranges));
            block->free_ranges[i].size       = padding;
            block->free_ranges[i + 1].offset = aligned_offset + size;
            block->free_ranges[i + 1].size   = remaining_after;
            block->free_range_count++;
        }
        else if(padding > 0)
        {
            block->free_ranges[i].size = padding;
        }
        else if(remaining_after > 0)
        {
            block->free_ranges[i].offset = aligned_offset + size;
            block->free_ranges[i].size   = remaining_after;
        }
        else
        {
            memmove(&block->free_ranges[i], &block->free_ranges[i + 1], (block->free_range_count - i - 1) * sizeof(*block->free_ranges));
            block->free_range_count--;
        }

        return P_TRUE;
    }

    return P_FALSE;
}

static void block_release(DefaultBlock* block, VkDeviceSize offset, VkDeviceSize size)
{
    uint32_t i = 0;
    while(i < block->free_range_count && block->free_ranges[i].offset < offset)
    {
        i++;
    }

    PBool merge_prev = (i > 0) && (block->free_ranges[i - 1].offset + block->free_ranges[i - 1].size == offset);
    PBool merge_next = (i < block->free_range_count) && (offset + size == block->free_ranges[i].offset);

    if(merge_prev && merge_next)
    {
        block->free_ranges[i - 1].size += size + block->free_ranges[i].size;
        memmove(&block->free_ranges[i], &block->free_ranges[i + 1], (block->free_range_count - i - 1) * sizeof(*block->free_ranges));
        block->free_range_count--;
    }
    else if(merge_prev)
    {
        block->free_ranges[i - 1].size += size;
    }
    else if(merge_next)
    {
        block->free_ranges[i].offset = offset;
        block->free_ranges[i].size += size;
    }
    else
    {
        if(block->free_range_count + 1 > block->free_range_capacity)
        {
            uint32_t new_capacity        = block->free_range_capacity * 2;
            DefaultFreeRange* new_ranges = realloc(block->free_ranges, new_capacity * sizeof(*new_ranges));
            if(new_ranges == NULL)
            {
                return;
            }
            block->free_ranges         = new_ranges;
            block->free_range_capacity = new_capacity;
        }

        memmove(&block->free_ranges[i + 1], &block->free_ranges[i], (block->free_range_count - i) * sizeof(*block->free_ranges));
        block->free_ranges[i].offset = offset;
        block->free_ranges[i].size   = size;
        block->free_range_count++;
    }
}

static PBool block_is_empty(const DefaultBlock* block)
{
    return block->free_range_count == 1 && block->free_ranges[0].offset == 0 && block->free_ranges[0].size == block->block_size;
}

static PVkAllocation* create_allocation(VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, void* block_mapped, DefaultBlock* block, uint32_t memory_type_index)
{
    PVkAllocation* allocation = malloc(sizeof(*allocation));
    if(allocation == NULL)
    {
        return NULL;
    }

    allocation->memory            = memory;
    allocation->offset            = offset;
    allocation->size              = size;
    allocation->mapped            = (block_mapped != NULL) ? ((char*) block_mapped + offset) : NULL;
    allocation->block             = block;
    allocation->memory_type_index = memory_type_index;

    return allocation;
}

static PVkAllocation* allocate_pooled(DefaultAllocator* alloc, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index)
{
    DefaultPool* pool = &alloc->pools[memory_type_index];

    for(DefaultBlock* block = pool->blocks; block != NULL; block = block->next)
    {
        VkDeviceSize offset;
        if(block_try_allocate(block, size, alignment, &offset))
        {
            return create_allocation(block->memory, offset, size, block->mapped, block, memory_type_index);
        }
    }

    DefaultBlock* block = create_block(alloc, memory_type_index, alloc->block_size);
    if(block == NULL)
    {
        return NULL;
    }

    block->next  = pool->blocks;
    pool->blocks = block;

    VkDeviceSize offset;
    if(!block_try_allocate(block, size, alignment, &offset))
    {
        return NULL;
    }

    return create_allocation(block->memory, offset, size, block->mapped, block, memory_type_index);
}

static PVkAllocation* allocate_dedicated(DefaultAllocator* alloc, VkDeviceSize size, uint32_t memory_type_index)
{
    VkMemoryAllocateFlagsInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };

    VkMemoryAllocateInfo info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = size,
        .memoryTypeIndex = memory_type_index,
        .pNext           = &flags_info
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;

    VkResult result;
    if((result = vkAllocateMemory(alloc->device, &info, NULL, &memory)) != VK_SUCCESS)
    {
        PLOG_DEBUG(alloc->pigment, "vkAllocateMemory failed for dedicated alloc (size=%llu, type=%u, result=%d). Caller may retry on a fallback memory type.", (unsigned long long) size, memory_type_index, result);
        return NULL;
    }

    void* mapped = NULL;
    if(memory_type_is_host_visible(&alloc->memory_properties, memory_type_index))
    {
        if(vkMapMemory(alloc->device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
        {
            mapped = NULL;
        }
    }

    PVkAllocation* allocation = create_allocation(memory, 0, size, mapped, NULL, memory_type_index);
    if(allocation == NULL)
    {
        if(mapped != NULL)
        {
            vkUnmapMemory(alloc->device, memory);
        }
        vkFreeMemory(alloc->device, memory, NULL);
        return NULL;
    }

    return allocation;
}

static void release_allocation(DefaultAllocator* allocator, PVkAllocation* allocation)
{
    if(allocation == NULL)
    {
        return;
    }

    if(allocation->block == NULL)
    {
        if(allocation->mapped != NULL)
        {
            vkUnmapMemory(allocator->device, allocation->memory);
        }
        vkFreeMemory(allocator->device, allocation->memory, NULL);
    }
    else
    {
        block_release(allocation->block, allocation->offset, allocation->size);
        if(block_is_empty(allocation->block))
        {
            DefaultPool* pool = &allocator->pools[allocation->memory_type_index];
            if(pool->blocks == allocation->block)
            {
                pool->blocks = allocation->block->next;
            }
            else
            {
                for(DefaultBlock* prev = pool->blocks; prev != NULL; prev = prev->next)
                {
                    if(prev->next == allocation->block)
                    {
                        prev->next = allocation->block->next;
                        break;
                    }
                }
            }
            destroy_block(allocator, allocation->block);
        }
    }

    free(allocation);
}

static VkResult default_create_buffer(void* user_data, const VkBufferCreateInfo* buffer_info, const PVkAllocationCreateInfo* alloc_info, VkBuffer* out_buffer, PVkAllocation** out_allocation)
{
    DefaultAllocator* alloc = (DefaultAllocator*) user_data;
    *out_buffer             = VK_NULL_HANDLE;
    *out_allocation         = NULL;

    VkResult result = vkCreateBuffer(alloc->device, buffer_info, NULL, out_buffer);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(alloc->pigment, "vkCreateBuffer failed (result=%d)", result);
        return result;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(alloc->device, *out_buffer, &requirements);

    VkMemoryPropertyFlags required_flags  = (alloc_info != NULL) ? alloc_info->required_flags : 0;
    VkMemoryPropertyFlags preferred_flags = (alloc_info != NULL) ? alloc_info->preferred_flags : 0;
    PVkAllocationFlags allocation_flags   = (alloc_info != NULL) ? alloc_info->flags : 0;

    uint32_t preferred_type_index = UINT32_MAX;
    uint32_t fallback_type_index  = find_memory_type_index(&alloc->memory_properties, requirements.memoryTypeBits, required_flags);
    if(preferred_flags != 0)
    {
        preferred_type_index = find_memory_type_index(&alloc->memory_properties, requirements.memoryTypeBits, required_flags | preferred_flags);
    }
    if(preferred_type_index == UINT32_MAX && fallback_type_index == UINT32_MAX)
    {
        PLOG_ERROR(alloc->pigment, "Failed to find suitable memory type for buffer");
        result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        goto ERROR;
    }

    PBool needs_dedicated = (allocation_flags & P_VK_ALLOCATION_DEDICATED_BIT) != 0
                            || requirements.size > alloc->dedicated_threshold;

    pigment_rwlock_wrlock(&alloc->lock);
    PVkAllocation* allocation = NULL;
    if(preferred_type_index != UINT32_MAX)
    {
        allocation = needs_dedicated ? allocate_dedicated(alloc, requirements.size, preferred_type_index)
                                     : allocate_pooled(alloc, requirements.size, requirements.alignment, preferred_type_index);
    }
    if(allocation == NULL && fallback_type_index != UINT32_MAX && fallback_type_index != preferred_type_index)
    {
        allocation = needs_dedicated ? allocate_dedicated(alloc, requirements.size, fallback_type_index)
                                     : allocate_pooled(alloc, requirements.size, requirements.alignment, fallback_type_index);
    }
    if(allocation == NULL)
    {
        pigment_rwlock_wrunlock(&alloc->lock);
        PLOG_ERROR(alloc->pigment, "Failed to allocate buffer memory (size=%llu, preferred_type=%u, fallback_type=%u)", (unsigned long long) requirements.size, preferred_type_index, fallback_type_index);
        result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        goto ERROR;
    }

    if((result = vkBindBufferMemory(alloc->device, *out_buffer, allocation->memory, allocation->offset)) != VK_SUCCESS)
    {
        PLOG_ERROR(alloc->pigment, "vkBindBufferMemory failed (result=%d)", result);
        release_allocation(alloc, allocation);
        pigment_rwlock_wrunlock(&alloc->lock);
        goto ERROR;
    }
    pigment_rwlock_wrunlock(&alloc->lock);

    if(alloc_info != NULL)
    {
        set_object_name(alloc->device, VK_OBJECT_TYPE_BUFFER, (uint64_t) *out_buffer, alloc_info->debug_name);
    }

    *out_allocation = allocation;
    return VK_SUCCESS;

ERROR:
    vkDestroyBuffer(alloc->device, *out_buffer, NULL);
    *out_buffer = VK_NULL_HANDLE;
    return result;
}

static void default_destroy_buffer(void* user_data, VkBuffer buffer, PVkAllocation* allocation)
{
    DefaultAllocator* alloc = (DefaultAllocator*) user_data;
    if(buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(alloc->device, buffer, NULL);
    }

    pigment_rwlock_wrlock(&alloc->lock);
    release_allocation(alloc, allocation);
    pigment_rwlock_wrunlock(&alloc->lock);
}

static VkResult default_create_image(void* user_data, const VkImageCreateInfo* image_info, const PVkAllocationCreateInfo* alloc_info, VkImage* out_image, PVkAllocation** out_allocation)
{
    DefaultAllocator* alloc = (DefaultAllocator*) user_data;
    *out_image              = VK_NULL_HANDLE;
    *out_allocation         = NULL;

    VkResult result = vkCreateImage(alloc->device, image_info, NULL, out_image);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(alloc->pigment, "vkCreateImage failed (result=%d)", result);
        return result;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(alloc->device, *out_image, &requirements);

    VkMemoryPropertyFlags required_flags  = (alloc_info != NULL) ? alloc_info->required_flags : 0;
    VkMemoryPropertyFlags preferred_flags = (alloc_info != NULL) ? alloc_info->preferred_flags : 0;
    PVkAllocationFlags allocation_flags   = (alloc_info != NULL) ? alloc_info->flags : 0;

    uint32_t preferred_type_index = UINT32_MAX;
    uint32_t fallback_type_index  = find_memory_type_index(&alloc->memory_properties, requirements.memoryTypeBits, required_flags);
    if(preferred_flags != 0)
    {
        preferred_type_index = find_memory_type_index(&alloc->memory_properties, requirements.memoryTypeBits, required_flags | preferred_flags);
    }
    if(preferred_type_index == UINT32_MAX && fallback_type_index == UINT32_MAX)
    {
        PLOG_ERROR(alloc->pigment, "Failed to find suitable memory type for image");
        result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        goto ERROR;
    }

    PBool needs_dedicated = (allocation_flags & P_VK_ALLOCATION_DEDICATED_BIT) != 0
                            || requirements.size > alloc->dedicated_threshold;

    pigment_rwlock_wrlock(&alloc->lock);
    PVkAllocation* allocation = NULL;
    if(preferred_type_index != UINT32_MAX)
    {
        allocation = needs_dedicated ? allocate_dedicated(alloc, requirements.size, preferred_type_index)
                                     : allocate_pooled(alloc, requirements.size, requirements.alignment, preferred_type_index);
    }
    if(allocation == NULL && fallback_type_index != UINT32_MAX && fallback_type_index != preferred_type_index)
    {
        allocation = needs_dedicated ? allocate_dedicated(alloc, requirements.size, fallback_type_index)
                                     : allocate_pooled(alloc, requirements.size, requirements.alignment, fallback_type_index);
    }
    if(allocation == NULL)
    {
        pigment_rwlock_wrunlock(&alloc->lock);
        PLOG_ERROR(alloc->pigment, "Failed to allocate image memory (size=%llu, preferred_type=%u, fallback_type=%u)", (unsigned long long) requirements.size, preferred_type_index, fallback_type_index);
        result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        goto ERROR;
    }

    if((result = vkBindImageMemory(alloc->device, *out_image, allocation->memory, allocation->offset)) != VK_SUCCESS)
    {
        PLOG_ERROR(alloc->pigment, "vkBindImageMemory failed (result=%d)", result);
        release_allocation(alloc, allocation);
        pigment_rwlock_wrunlock(&alloc->lock);
        goto ERROR;
    }
    pigment_rwlock_wrunlock(&alloc->lock);

    if(alloc_info != NULL)
    {
        set_object_name(alloc->device, VK_OBJECT_TYPE_IMAGE, (uint64_t) *out_image, alloc_info->debug_name);
    }

    *out_allocation = allocation;
    return VK_SUCCESS;

ERROR:
    vkDestroyImage(alloc->device, *out_image, NULL);
    *out_image = VK_NULL_HANDLE;
    return result;
}

static void default_destroy_image(void* user_data, VkImage image, PVkAllocation* allocation)
{
    DefaultAllocator* alloc = (DefaultAllocator*) user_data;
    if(image != VK_NULL_HANDLE)
    {
        vkDestroyImage(alloc->device, image, NULL);
    }

    pigment_rwlock_wrlock(&alloc->lock);
    release_allocation(alloc, allocation);
    pigment_rwlock_wrunlock(&alloc->lock);
}

static VkResult default_map(void* user_data, PVkAllocation* allocation, void** out_data)
{
    (void) user_data;

    if(allocation == NULL || allocation->mapped == NULL)
    {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    *out_data = allocation->mapped;
    return VK_SUCCESS;
}

static void default_unmap(void* user_data, PVkAllocation* allocation)
{
    (void) user_data;
    (void) allocation;
}

static VkMemoryPropertyFlags default_get_memory_flags(void* user_data, PVkAllocation* allocation)
{
    DefaultAllocator* alloc = (DefaultAllocator*) user_data;
    if(allocation == NULL || allocation->memory_type_index >= alloc->memory_properties.memoryTypeCount)
    {
        return 0;
    }
    return alloc->memory_properties.memoryTypes[allocation->memory_type_index].propertyFlags;
}

static PBool build_mapped_range(DefaultAllocator* alloc, PVkAllocation* allocation, VkDeviceSize offset, VkDeviceSize size, VkMappedMemoryRange* out_range)
{
    if(allocation == NULL || allocation->mapped == NULL)
    {
        return P_FALSE;
    }

    if(offset >= allocation->size)
    {
        return P_FALSE;
    }

    VkDeviceSize clamped_size = (size == VK_WHOLE_SIZE) ? (allocation->size - offset) : size;
    if(offset + clamped_size > allocation->size)
    {
        clamped_size = allocation->size - offset;
    }

    VkDeviceSize atom          = alloc->non_coherent_atom_size;
    VkDeviceSize start         = allocation->offset + offset;
    VkDeviceSize aligned_start = (start / atom) * atom;
    VkDeviceSize end           = start + clamped_size;
    VkDeviceSize aligned_end   = ((end + atom - 1) / atom) * atom;

    out_range->sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    out_range->pNext  = NULL;
    out_range->memory = allocation->memory;
    out_range->offset = aligned_start;
    out_range->size   = aligned_end - aligned_start;

    return P_TRUE;
}

static void default_flush(void* user_data, PVkAllocation* allocation, VkDeviceSize offset, VkDeviceSize size)
{
    DefaultAllocator* alloc = (DefaultAllocator*) user_data;
    VkMappedMemoryRange range;
    if(!build_mapped_range(alloc, allocation, offset, size, &range))
    {
        return;
    }
    vkFlushMappedMemoryRanges(alloc->device, 1, &range);
}

static void default_invalidate(void* user_data, PVkAllocation* allocation, VkDeviceSize offset, VkDeviceSize size)
{
    DefaultAllocator* alloc = (DefaultAllocator*) user_data;
    VkMappedMemoryRange range;
    if(!build_mapped_range(alloc, allocation, offset, size, &range))
    {
        return;
    }
    vkInvalidateMappedMemoryRanges(alloc->device, 1, &range);
}

static void default_destroy(void* user_data)
{
    DefaultAllocator* alloc = (DefaultAllocator*) user_data;
    if(alloc == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
    {
        DefaultBlock* block = alloc->pools[i].blocks;
        while(block != NULL)
        {
            DefaultBlock* next = block->next;
            destroy_block(alloc, block);
            block = next;
        }
    }

    pigment_rwlock_destroy(&alloc->lock);
    free(alloc);
}
