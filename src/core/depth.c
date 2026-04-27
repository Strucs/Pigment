/**
 * Copyright 2025-2026 Angel-Leduc TA
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
#include "internal.h"
#include "log_internal.h"

static VkFormat find_depth_format(Pigment* pigment);
static VkFormat find_supported_format(Pigment* pigment, VkFormat* candidates, uint32_t candidates_number, VkImageTiling tiling, VkFormatFeatureFlags features);

int create_depth_resources(Pigment* pigment, PSwapchain* swapchain)
{
    PDevice* device         = pigment->device;
    VkFormat depth_format   = find_depth_format(pigment);
    swapchain->depth_format = depth_format;

    if(create_vk_image(pigment, &swapchain->depth_image, &swapchain->depth_image_memory, swapchain->extent.width, swapchain->extent.height, 1, depth_format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    swapchain->depth_image_view = create_image_view(pigment, swapchain->depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
    if(swapchain->depth_image_view == NULL)
    {
        goto ERROR;
    }

    return PIGMENT_SUCCESS;

ERROR:
    vkDestroyImage(device->logical_device, swapchain->depth_image, NULL);
    vkFreeMemory(device->logical_device, swapchain->depth_image_memory, NULL);
    return PIGMENT_ERROR;
}

void destroy_depth_resources(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain == NULL)
    {
        return;
    }
    PDevice* device = pigment->device;
    vkDestroyImageView(device->logical_device, swapchain->depth_image_view, NULL);
    vkDestroyImage(device->logical_device, swapchain->depth_image, NULL);
    vkFreeMemory(device->logical_device, swapchain->depth_image_memory, NULL);
}

static VkFormat find_supported_format(Pigment* pigment, VkFormat* candidates, uint32_t candidates_number, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    VkPhysicalDevice physical_device = pigment->device->physical_device;
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

    PLOG_ERROR(pigment, "Failed to find supported format!");
    return VK_FORMAT_UNDEFINED;
}

static VkFormat find_depth_format(Pigment* pigment)
{
    VkFormat candidates[]    = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    uint32_t candidates_size = sizeof(candidates) / sizeof(candidates[0]);

    return find_supported_format(
        pigment,
        candidates,
        candidates_size,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}
