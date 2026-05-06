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

// window.c
const char* const* window_get_vk_instance_extensions(Pigment* pigment, uint32_t* out_count);
PBool window_create_vk_surface(Pigment* pigment, PWindow* window, VkSurfaceKHR* out_surface);

// device.c
QueueFamilyIndices* find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

// surface.c
SwapChainSupportDetails* get_support_details(VkPhysicalDevice device, VkSurfaceKHR surface);
void destroy_support_details(SwapChainSupportDetails* details);

// commands.c
PCommandPool* pigment_default_pool(Pigment* pigment);

// image.c
VkImageView create_image_view(Pigment* pigment, VkImage image, VkImageViewType view_type, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t base_mip, uint32_t mip_count, uint32_t base_layer, uint32_t layer_count);
PResult create_vk_image(Pigment* pigment, VkImage* image, PVkAllocation** allocation, VkImageType image_type, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip_levels, uint32_t array_layers, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkMemoryPropertyFlags properties);
VkImageLayout image_layout_to_vk(PImageLayout layout);
PImageView* image_get_or_create_view(Pigment* pigment, PImage* image, const PImageViewDesc* desc);
void image_destroy_view_cache(Pigment* pigment, PImage* image);

// resize.c
PResizeCallbackList* create_resize_callback_list(void);
void destroy_resize_callback_list(PResizeCallbackList* list);
void dispatch_swapchain_resize(Pigment* pigment, const PSwapchainResizeEvent* event);

#endif
