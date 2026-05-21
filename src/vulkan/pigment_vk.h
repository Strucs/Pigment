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

#ifdef __cplusplus
extern "C" {
#endif

#include "pigment.h"

#include <volk.h>

typedef struct PVkAllocation PVkAllocation;
typedef struct PVkAllocator PVkAllocator;

typedef enum PVkAllocationFlagBits {
    P_VK_ALLOCATION_HOST_RANDOM_BIT    = 1 << 0,
    P_VK_ALLOCATION_DEDICATED_BIT      = 1 << 1,
    P_VK_ALLOCATION_PERSISTENT_MAP_BIT = 1 << 2,
} PVkAllocationFlagBits;

typedef VkFlags PVkAllocationFlags;

typedef struct PVkAllocationCreateInfo {
    PVkAllocationFlags flags;
    VkMemoryPropertyFlags required_flags;
    VkMemoryPropertyFlags preferred_flags;
    const char* debug_name;
} PVkAllocationCreateInfo;

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

    VkResult (*create_buffer)(void* user_data, const VkBufferCreateInfo* buffer_info, const PVkAllocationCreateInfo* alloc_info, VkBuffer* out_buffer, PVkAllocation** out_allocation);
    void (*destroy_buffer)(void* user_data, VkBuffer buffer, PVkAllocation* allocation);

    VkResult (*create_image)(void* user_data, const VkImageCreateInfo* image_info, const PVkAllocationCreateInfo* alloc_info, VkImage* out_image, PVkAllocation** out_allocation);
    void (*destroy_image)(void* user_data, VkImage image, PVkAllocation* allocation);

    VkResult (*map)(void* user_data, PVkAllocation* allocation, void** out_data);
    void (*unmap)(void* user_data, PVkAllocation* allocation);

    VkMemoryPropertyFlags (*get_memory_flags)(void* user_data, PVkAllocation* allocation);
    void (*flush)(void* user_data, PVkAllocation* allocation, VkDeviceSize offset, VkDeviceSize size);
    void (*invalidate)(void* user_data, PVkAllocation* allocation, VkDeviceSize offset, VkDeviceSize size);

    void (*destroy)(void* user_data);
};

typedef struct PVkDefaultAllocatorCreateInfo {
    VkDeviceSize block_size;
} PVkDefaultAllocatorCreateInfo;

PIGMENT_API PVkAllocator* pigment_vk_create_default_allocator(Pigment* pigment, const PVkDefaultAllocatorCreateInfo* info);
PIGMENT_API void pigment_vk_destroy_allocator(PVkAllocator* allocator);

/**
 * @brief Build a VkAllocationCallbacks from a PAllocator. Use to plug a
 * Pigment CPU allocator into third-party GPU allocators (VMA, custom backends)
 * that need VkAllocationCallbacks before init_pigment is called.
 *
 * The resulting callbacks capture &allocator in pUserData, so allocator must
 * stay alive at least as long as any consumer of out.
 *
 * @param allocator The Pigment CPU allocator to bridge.
 * @param out The output VkAllocationCallbacks struct to populate.
 */
PIGMENT_API void pigment_vk_build_allocation_callbacks(const PAllocator* allocator, VkAllocationCallbacks* out);

PIGMENT_API VkInstance pigment_vk_instance(Pigment* pigment);
PIGMENT_API VkDevice pigment_vk_device(Pigment* pigment);
PIGMENT_API VkPhysicalDevice pigment_vk_physical_device(Pigment* pigment);
PIGMENT_API VkQueue pigment_vk_queue(PDeviceQueue* queue);

PIGMENT_API VkBuffer pigment_vk_buffer(PBuffer* buffer);
PIGMENT_API VkDeviceAddress pigment_vk_buffer_address(PBuffer* buffer);

PIGMENT_API VkImage pigment_vk_image(PImage* image);
PIGMENT_API VkImageView pigment_vk_image_view(PImageView* view);
PIGMENT_API VkSampler pigment_vk_sampler(PSampler* sampler);

PIGMENT_API VkPipeline pigment_vk_pipeline(PPipeline* pipeline);
PIGMENT_API VkPipelineLayout pigment_vk_pipeline_layout(PLayout* layout);
PIGMENT_API VkPipelineCache pigment_vk_pipeline_cache(PPipelineCache* cache);

PIGMENT_API VkSurfaceKHR pigment_vk_surface(PWindowRenderer* renderer);
PIGMENT_API VkSwapchainKHR pigment_vk_swapchain(PWindowRenderer* renderer);
PIGMENT_API VkQueue pigment_vk_present_queue(PWindowRenderer* renderer);

PIGMENT_API VkDescriptorSet pigment_vk_descriptor_set(PDescriptorSet* set);
PIGMENT_API VkDescriptorSetLayout pigment_vk_descriptor_set_layout(PDescriptorSetLayout* layout);
PIGMENT_API VkDescriptorPool pigment_vk_descriptor_pool(PDescriptorPool* pool);

PIGMENT_API VkCommandBuffer pigment_vk_command_buffer(PCommandBuffer* cmd);
PIGMENT_API VkCommandPool pigment_vk_command_pool(PCommandPool* pool);

/**
 * @brief Same as pigment_defer_destroy, but gated on a custom VkFence.
 *
 * @param pigment Pigment instance.
 * @param destroy_fn The function to call to destroy the resource.
 * @param resource The resource to destroy. Passed as the second argument to destroy_fn.
 * @param fence The fence whose signaling will release the destroy.
 */
PIGMENT_API void pigment_vk_fence_defer_destroy(Pigment* pigment, PDestroyFn destroy_fn, void* resource, VkFence fence);

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

#ifdef __cplusplus
}
#endif

#endif
