/**
 * Copyright 2025 Angel-Leduc TA
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

#include "depth.h"
#include "structs.h"

extern int create_image(VkImage* image, VkDeviceMemory* image_memory, uint32_t width, uint32_t height, uint32_t mip_levels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device);
extern int transition_image_layout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels, VkCommandPool command_pool, PDevice* device);
extern VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels, VkDevice device);

VkFormat find_depth_format(VkPhysicalDevice physical_device);
VkFormat find_supported_format(VkFormat* candidates, uint32_t candidates_number, VkImageTiling tiling, VkFormatFeatureFlags features, VkPhysicalDevice physical_device);

int create_depth_resources(PSwapchain* swapchain, PCommands* commands, PDevice* device)
{
    VkFormat depth_format = find_depth_format(device->physical_device);

    if(create_image(&swapchain->depth_image, &swapchain->depth_image_memory, swapchain->extent.width, swapchain->extent.height, 1, depth_format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    swapchain->depth_image_view = create_image_view(swapchain->depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT, 1, device->logical_device);
    if(swapchain->depth_image_view == NULL)
    {
        goto ERROR;
    }
    if(transition_image_layout(swapchain->depth_image, depth_format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1, commands->command_pool, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    return PIGMENT_SUCCESS;

ERROR:
    return PIGMENT_ERROR;
}

void destroy_depth_resources(PSwapchain* swapchain, PDevice* device)
{
    if(swapchain == NULL)
    {
        return;
    }
    vkDestroyImageView(device->logical_device, swapchain->depth_image_view, NULL);
    vkDestroyImage(device->logical_device, swapchain->depth_image, NULL);
    vkFreeMemory(device->logical_device, swapchain->depth_image_memory, NULL);
}

VkFormat find_supported_format(VkFormat* candidates, uint32_t candidates_number, VkImageTiling tiling, VkFormatFeatureFlags features, VkPhysicalDevice physical_device)
{
    VkFormatProperties properties;

    for(size_t i = 0; i < candidates_number; i++)
    {
        vkGetPhysicalDeviceFormatProperties(physical_device, candidates[i], &properties);

        if(tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
        {
            return candidates[i];
        }
        else if(tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
        {
            return candidates[i];
        }
    }

    fprintf(stderr, "Failed to find supported format!\n");
    return VK_FORMAT_UNDEFINED;
}

VkFormat find_depth_format(VkPhysicalDevice physical_device)
{
    VkFormat candidates[]    = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    uint32_t candidates_size = sizeof(candidates) / sizeof(candidates[0]);

    return find_supported_format(
        candidates,
        candidates_size,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
        physical_device
    );
}
