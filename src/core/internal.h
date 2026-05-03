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

static inline bool name_in_list(const char* const* list, uint32_t count, const char* name)
{
    for(uint32_t i = 0; i < count; i++)
    {
        if(strcmp(list[i], name) == 0)
        {
            return true;
        }
    }
    return false;
}

static inline bool extension_available(const VkExtensionProperties* available, uint32_t count, const char* name)
{
    for(uint32_t i = 0; i < count; i++)
    {
        if(strcmp(available[i].extensionName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

// window.c
const char* const* window_get_vk_instance_extensions(Pigment* pigment, uint32_t* out_count);
bool window_create_vk_surface(Pigment* pigment, PWindow* window, VkSurfaceKHR* out_surface);

// device.c
QueueFamilyIndices* find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

// surface.c
SwapChainSupportDetails* get_support_details(VkPhysicalDevice device, VkSurfaceKHR surface);
void destroy_support_details(SwapChainSupportDetails* details);

// commands.c
PCommandPool* pigment_default_pool(Pigment* pigment);

// image.c
VkImageView create_image_view(Pigment* pigment, VkImage image, VkImageViewType view_type, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels, uint32_t array_layers);
int create_vk_image(Pigment* pigment, VkImage* image, PVkAllocation** allocation, VkImageType image_type, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip_levels, uint32_t array_layers, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkMemoryPropertyFlags properties);
VkImageLayout image_layout_to_vk(PImageLayout layout);

PTrackedImageList* create_tracked_image_list(void);
void destroy_tracked_image_list(PTrackedImageList* list);
void pigment_image_resize_tracked(Pigment* pigment, uint32_t window_index);

#endif
