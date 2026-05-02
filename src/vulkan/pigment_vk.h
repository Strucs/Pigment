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

#ifndef PIGMENT_VK_H
#define PIGMENT_VK_H

#include <pigment.h>
#include <volk.h>

typedef struct PVkAllocation PVkAllocation;
typedef struct PVkAllocator PVkAllocator;

/**
 * @brief Vulkan-specific initialization info. All fields are optional.
 *
 * req_*: hard requirement, init fails if absent.
 *
 * opt_*: enabled when available, skipped otherwise.
 */
typedef struct PVkInitInfo {
    void* instance_pnext_chain;

    /**
     * For third-party feature structs (mesh shader, ray tracing, ...) only.
     * Do NOT chain VkPhysicalDeviceVulkanXXFeatures here because Pigment uses default features.
     */
    void* device_pnext_chain;

    const char* const* req_instance_extensions;
    uint32_t req_instance_extensions_count;
    const char* const* opt_instance_extensions;
    uint32_t opt_instance_extensions_count;

    const char* const* req_device_extensions;
    uint32_t req_device_extensions_count;
    const char* const* opt_device_extensions;
    uint32_t opt_device_extensions_count;

    const char* const* req_instance_layers;
    uint32_t req_instance_layers_count;
    const char* const* opt_instance_layers;
    uint32_t opt_instance_layers_count;

    /* Merged with Pigment default features */

    const VkPhysicalDeviceFeatures* req_features;
    const VkPhysicalDeviceVulkan11Features* req_features_11;
    const VkPhysicalDeviceVulkan12Features* req_features_12;
    const VkPhysicalDeviceVulkan13Features* req_features_13;
    const VkPhysicalDeviceFeatures* opt_features;
    const VkPhysicalDeviceVulkan11Features* opt_features_11;
    const VkPhysicalDeviceVulkan12Features* opt_features_12;
    const VkPhysicalDeviceVulkan13Features* opt_features_13;

    /**
     * NULL = Pigment uses its default allocator. Caller owns the allocator
     * and is responsible for destroying it after destroy_pigment.
     */
    PVkAllocator* allocator;
} PVkInitInfo;

struct PVkAllocator {
    void* user_data;

    VkResult (*create_buffer)(void* user_data, const VkBufferCreateInfo* info, VkMemoryPropertyFlags properties, VkBuffer* out_buffer, PVkAllocation** out_allocation);
    void (*destroy_buffer)(void* user_data, VkBuffer buffer, PVkAllocation* allocation);

    VkResult (*create_image)(void* user_data, const VkImageCreateInfo* info, VkMemoryPropertyFlags properties, VkImage* out_image, PVkAllocation** out_allocation);
    void (*destroy_image)(void* user_data, VkImage image, PVkAllocation* allocation);

    VkResult (*map)(void* user_data, PVkAllocation* allocation, void** out_data);
    void (*unmap)(void* user_data, PVkAllocation* allocation);

    void (*destroy)(void* user_data);
};

typedef struct PVkDefaultAllocatorCreateInfo {
    VkDeviceSize block_size;
} PVkDefaultAllocatorCreateInfo;

PVkAllocator* pigment_vk_create_default_allocator(Pigment* pigment, const PVkDefaultAllocatorCreateInfo* info);
void pigment_vk_destroy_allocator(PVkAllocator* allocator);

VkBuffer pigment_vk_buffer(PBuffer* buffer);

static inline void pigment_vk_append_pnext(void* head, void* tail)
{
    if(head == NULL || tail == NULL)
    {
        return;
    }
    VkBaseOutStructure* current = (VkBaseOutStructure*) head;
    while(current->pNext != NULL)
    {
        current = (VkBaseOutStructure*) current->pNext;
    }
    current->pNext = (VkBaseOutStructure*) tail;
}

#endif
