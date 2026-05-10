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

// Private declarations for internal use. It must not be used in public headers.

#ifndef PIGMENT_INTERNAL_H
#define PIGMENT_INTERNAL_H

#include "structs.h"

static inline PBool name_in_list(const char* const* list, uint32_t count, const char* name)
{
    for(uint32_t i = 0; i < count; i++)
    {
        if(strcmp(list[i], name) == 0)
        {
            return P_TRUE;
        }
    }
    return P_FALSE;
}

static inline PBool extension_available(const VkExtensionProperties* available, uint32_t count, const char* name)
{
    for(uint32_t i = 0; i < count; i++)
    {
        if(strcmp(available[i].extensionName, name) == 0)
        {
            return P_TRUE;
        }
    }
    return P_FALSE;
}

static inline PBool format_has_depth(PFormat f)
{
    return f == P_FORMAT_D16_UNORM
           || f == P_FORMAT_D32_SFLOAT
           || f == P_FORMAT_D16_UNORM_S8_UINT
           || f == P_FORMAT_D24_UNORM_S8_UINT
           || f == P_FORMAT_D32_SFLOAT_S8_UINT;
}

static inline PBool format_has_stencil(PFormat f)
{
    return f == P_FORMAT_S8_UINT
           || f == P_FORMAT_D16_UNORM_S8_UINT
           || f == P_FORMAT_D24_UNORM_S8_UINT
           || f == P_FORMAT_D32_SFLOAT_S8_UINT;
}

static inline VkResolveModeFlagBits resolve_mode_to_vk(PResolveMode mode, PBool is_depth_stencil)
{
    if(mode == P_RESOLVE_MODE_AUTO)
    {
        return is_depth_stencil ? VK_RESOLVE_MODE_SAMPLE_ZERO_BIT : VK_RESOLVE_MODE_AVERAGE_BIT;
    }

    return (VkResolveModeFlagBits) mode;
}

static inline void set_object_name(VkDevice device, VkObjectType type, uint64_t handle, const char* name)
{
    if(name == NULL || handle == 0 || vkSetDebugUtilsObjectNameEXT == NULL)
    {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT info = {
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType   = type,
        .objectHandle = handle,
        .pObjectName  = name,
    };

    vkSetDebugUtilsObjectNameEXT(device, &info);
}

// device.c
PBool find_graphics_family(VkPhysicalDevice device, uint32_t* out_family);
PBool device_supports_surface(VkPhysicalDevice device, uint32_t family_index, VkSurfaceKHR surface);
PDeviceQueue* device_find_queue(PDevice* device, PQueueFlags required, PQueueFlags forbidden);
void device_wait_idle(Pigment* pigment);

static inline uint64_t device_queue_acquire_value(PDeviceQueue* queue)
{
    return atomic_fetch_add_explicit(&queue->next_value, 1, memory_order_relaxed) + 1;
}

static inline uint64_t device_queue_current_value(PDeviceQueue* queue)
{
    return atomic_load_explicit(&queue->next_value, memory_order_relaxed);
}

static inline PBool pigment_has_swapchain_maintenance1(Pigment* pigment)
{
    if(pigment == NULL || pigment->device == NULL || pigment->device->extensions == NULL)
    {
        return P_FALSE;
    }
    return name_in_list((const char* const*) pigment->device->extensions->names, pigment->device->extensions->size, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
}

// surface.c
PRendererList* create_renderer_list(Pigment* pigment);
void destroy_renderer_list(Pigment* pigment, PRendererList* list);
PResult recreate_swapchain(Pigment* pigment, PWindowRenderer* renderer);
VkSampleCountFlags supported_sample_counts(Pigment* pigment);

// commands.c
PCommandPool* pigment_default_pool(Pigment* pigment);
PCommandBuffer** create_command_buffers(Pigment* pigment, PCommandPool* pool, uint32_t count);
void destroy_command_buffers(Pigment* pigment, PCommandBuffer** command_buffers, uint32_t count);
void stamp_uses_submit(PCommandBuffer** cmds, uint32_t count, uint64_t value);

// image.c
VkImageView create_image_view(Pigment* pigment, VkImage image, VkImageViewType view_type, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t base_mip, uint32_t mip_count, uint32_t base_layer, uint32_t layer_count);
PResult create_vk_image(Pigment* pigment, PImage* image, uint32_t width, uint32_t height, VkImageTiling tiling, const PVkAllocationCreateInfo* alloc_info);
VkImageLayout image_layout_to_vk(PImageLayout layout);
PImageView* image_get_or_create_view(Pigment* pigment, PImage* image, const PImageViewDesc* desc);
void image_destroy_view_cache(Pigment* pigment, PImage* image);

// swapchain_event.c
PSwapchainCallbackList* create_swapchain_callback_list(void);
void destroy_swapchain_callback_list(PSwapchainCallbackList* list);
void dispatch_swapchain_recreate(Pigment* pigment, const PSwapchainRecreateEvent* event);

// deletion.c
PDeletionQueue* create_deletion_queue(Pigment* pigment);
void destroy_deletion_queue(Pigment* pigment, PDeletionQueue* queue);
void drain_deletion_queue(Pigment* pigment);
void defer_destroy_renderer(Pigment* pigment, void (*destroy_fn)(Pigment*, void*), void* resource, const PResourceTracker* tracker, VkFence present_fence);

#endif
