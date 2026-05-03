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

#include "surface.h"
#include "image.h"
#include "internal.h"
#include "log_internal.h"

static void destroy_image_views(PSwapchain* swapchain, PDevice* device);
static VkSurfaceFormatKHR choose_surface_format(VkSurfaceFormatKHR* available_formats, uint32_t formats_count);
static VkPresentModeKHR choose_surface_present_modes(VkPresentModeKHR* available_present_modes, uint32_t present_modes_count, PPresentMode preferred);
static VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR capabilities, uint32_t framebuffer_width, uint32_t framebuffer_height);
static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported, bool transparent);
static PFormat find_supported_depth_format(Pigment* pigment);
static int create_swapchain_depth(Pigment* pigment, PSwapchain* swapchain);
static void destroy_swapchain_depth(Pigment* pigment, PSwapchain* swapchain);

static inline uint32_t clamp(uint32_t value, uint32_t min, uint32_t max)
{
    const uint32_t temp = value < min ? min : value;
    return temp > max ? max : temp;
}

PSurface* create_surface(Pigment* pigment, PWindow* window)
{
    PSurface* surface = malloc(sizeof(*surface));
    if(surface == NULL)
    {
        return NULL;
    }

    if(!window_create_vk_surface(pigment, window, &surface->surface))
    {
        free(surface);
        return NULL;
    }
    return surface;
}

void destroy_surface(Pigment* pigment, PSurface* surface)
{
    if(surface == NULL)
    {
        return;
    }
    vkDestroySurfaceKHR(pigment->instance->vulkan_instance, surface->surface, NULL);
    free(surface);
}

SwapChainSupportDetails* get_support_details(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapChainSupportDetails* details;
    details = calloc(1, sizeof(*details));
    if(details == NULL)
    {
        goto ERROR;
    }

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details->capabilities);

    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details->formats_count, NULL);
    details->formats = malloc(details->formats_count * sizeof(*details->formats));
    if(details->formats == NULL)
    {
        goto ERROR;
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details->formats_count, details->formats);

    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details->present_modes_count, NULL);
    details->present_modes = malloc(details->present_modes_count * sizeof(*details->present_modes));
    if(details->present_modes == NULL)
    {
        goto ERROR;
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details->present_modes_count, details->present_modes);

    return details;

ERROR:
    destroy_support_details(details);
    return NULL;
}

void destroy_support_details(SwapChainSupportDetails* details)
{
    if(details != NULL)
    {
        free(details->formats);
        free(details->present_modes);
        free(details);
    }
}

static VkSurfaceFormatKHR choose_surface_format(VkSurfaceFormatKHR* available_formats, uint32_t formats_count)
{
    for(size_t i = 0; i < formats_count; i++)
    {
        if(available_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && available_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return available_formats[i];
        }
    }

    return available_formats[0];
}

static VkPresentModeKHR choose_surface_present_modes(VkPresentModeKHR* available_present_modes, uint32_t present_modes_count, PPresentMode preferred)
{
    VkPresentModeKHR requested = (VkPresentModeKHR) preferred;

    for(size_t i = 0; i < present_modes_count; i++)
    {
        if(available_present_modes[i] == requested)
        {
            return requested;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR capabilities, uint32_t framebuffer_width, uint32_t framebuffer_height)
{
    if(capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }

    VkExtent2D actual_extent = {
        framebuffer_width,
        framebuffer_height
    };

    actual_extent.width  = clamp(actual_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actual_extent.height = clamp(actual_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actual_extent;
}

static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported, bool transparent)
{
    if(transparent)
    {
        if(supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
        {
            return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        }
        if(supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
        {
            return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        }
        if(supported & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
        {
            return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }
        if(supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
        {
            return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        }
    }
    else
    {
        if(supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
        {
            return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        }
        if(supported & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
        {
            return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }
        if(supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
        {
            return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        }
        if(supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
        {
            return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        }
    }

    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

PSwapchain* create_swapchain(Pigment* pigment, uint32_t framebuffer_width, uint32_t framebuffer_height, PPresentMode preferred_mode, bool transparent, PSurface* surface)
{
    PDevice* device                          = pigment->device;
    PSwapchain* swapchain                    = NULL;
    QueueFamilyIndices* indices              = NULL;
    SwapChainSupportDetails* support_details = NULL;

    swapchain = calloc(1, sizeof(*swapchain));
    if(swapchain == NULL)
    {
        goto ERROR;
    }

    support_details = get_support_details(device->physical_device, surface->surface);
    if(support_details == NULL)
    {
        goto ERROR;
    }

    VkSurfaceFormatKHR surface_format = choose_surface_format(support_details->formats, support_details->formats_count);
    VkPresentModeKHR present_mode     = choose_surface_present_modes(support_details->present_modes, support_details->present_modes_count, preferred_mode);
    VkExtent2D extent                 = choose_swap_extent(support_details->capabilities, framebuffer_width, framebuffer_height);

    uint32_t image_count = support_details->capabilities.minImageCount + 1;
    // support_details->capabilities.maxImageCount = 0 means there is no maximum number of images
    if(support_details->capabilities.maxImageCount > 0 && image_count > support_details->capabilities.maxImageCount)
    {
        image_count = support_details->capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType   = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface->surface
    };

    create_info.minImageCount    = image_count;
    create_info.imageFormat      = surface_format.format;
    create_info.imageColorSpace  = surface_format.colorSpace;
    create_info.imageExtent      = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    indices = find_queue_families(device->physical_device, surface->surface);
    if(indices == NULL)
    {
        goto ERROR;
    }
    uint32_t queue_families_indices[] = {indices->graphics_family.value, indices->present_family.value};

    if(indices->graphics_family.value != indices->present_family.value)
    {
        create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = sizeof(queue_families_indices) / sizeof(queue_families_indices[0]);
        create_info.pQueueFamilyIndices   = queue_families_indices;
    }
    else
    {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if(support_details->capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
    {
        create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }
    else
    {
        create_info.preTransform = support_details->capabilities.currentTransform;
    }
    create_info.compositeAlpha = choose_composite_alpha(support_details->capabilities.supportedCompositeAlpha, transparent);
    create_info.presentMode    = present_mode;
    create_info.clipped        = VK_TRUE;

    create_info.oldSwapchain = VK_NULL_HANDLE;

    VkResult result;
    if((result = vkCreateSwapchainKHR(device->logical_device, &create_info, NULL, &(swapchain->swapchain))) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create swap chain! (result: %d)", result);
        goto ERROR;
    }

    vkGetSwapchainImagesKHR(device->logical_device, swapchain->swapchain, &image_count, NULL);

    swapchain->images = malloc(image_count * sizeof(*swapchain->images));
    if(swapchain->images == NULL)
    {
        goto ERROR;
    }

    if((result = vkGetSwapchainImagesKHR(device->logical_device, swapchain->swapchain, &image_count, swapchain->images)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to get swapchain images! (result: %d)", result);
        goto ERROR;
    }

    swapchain->image_count   = image_count;
    swapchain->image_format  = surface_format.format;
    swapchain->extent        = extent;
    swapchain->current_frame = 0;

    if(create_image_views(pigment, swapchain) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    if(create_swapchain_depth(pigment, swapchain) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    destroy_support_details(support_details);
    free(indices);

    return swapchain;

ERROR:
    destroy_support_details(support_details);
    free(indices);
    if(swapchain != NULL)
    {
        vkDestroySwapchainKHR(device->logical_device, swapchain->swapchain, NULL);
        free(swapchain);
    }
    return NULL;
}

void destroy_swapchain(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain != NULL)
    {
        PDevice* device = pigment->device;
        destroy_swapchain_depth(pigment, swapchain);
        destroy_image_views(swapchain, device);
        vkDestroySwapchainKHR(device->logical_device, swapchain->swapchain, NULL);
        free(swapchain);
    }
}

static PFormat find_supported_depth_format(Pigment* pigment)
{
    VkPhysicalDevice physical_device = pigment->device->physical_device;

    VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT};
    for(uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device, candidates[i], &props);
        if(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return (PFormat) candidates[i];
        }
    }

    PLOG_ERROR(pigment, "No supported depth format!");
    return P_FORMAT_UNDEFINED;
}

static int create_swapchain_depth(Pigment* pigment, PSwapchain* swapchain)
{
    PImageDesc desc = {
        .width      = swapchain->extent.width,
        .height     = swapchain->extent.height,
        .format     = find_supported_depth_format(pigment),
        .usage      = P_IMAGE_USAGE_RENDER_DEPTH,
        .samples    = P_SAMPLE_COUNT_1,
        .mip_levels = 1,
    };

    swapchain->depth = pigment_create_image(pigment, &desc);

    return (swapchain->depth != NULL) ? PIGMENT_SUCCESS : PIGMENT_ERROR;
}

static void destroy_swapchain_depth(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain->depth != NULL)
    {
        pigment_destroy_image(pigment, swapchain->depth);
        swapchain->depth = NULL;
    }
}

int create_image_views(Pigment* pigment, PSwapchain* swapchain)
{
    swapchain->image_views = calloc(swapchain->image_count, sizeof(*(swapchain->image_views)));
    if(swapchain->image_views == NULL)
    {
        return PIGMENT_ERROR;
    }

    for(size_t i = 0; i < swapchain->image_count; i++)
    {
        swapchain->image_views[i] = create_image_view(pigment, swapchain->images[i], VK_IMAGE_VIEW_TYPE_2D, swapchain->image_format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
    }

    return PIGMENT_SUCCESS;
}

static void destroy_image_views(PSwapchain* swapchain, PDevice* device)
{
    if(swapchain == NULL || swapchain->image_views == NULL)
    {
        return;
    }
    for(size_t i = 0; i < swapchain->image_count; i++)
    {
        vkDestroyImageView(device->logical_device, swapchain->image_views[i], NULL);
    }

    free(swapchain->image_views);
    free(swapchain->images);
}

int recreate_swapchain(Pigment* pigment, PWindowRenderer* renderer, uint32_t framebuffer_width, uint32_t framebuffer_height, PPresentMode preferred_mode)
{
    PDevice* device           = pigment->device;
    PSwapchain* new_swapchain = NULL;

    vkDeviceWaitIdle(device->logical_device);

    destroy_swapchain(pigment, renderer->swapchain);

    new_swapchain = create_swapchain(pigment, framebuffer_width, framebuffer_height, preferred_mode, renderer->transparent_framebuffer, renderer->surface);
    if(new_swapchain == NULL)
    {
        goto ERROR;
    }

    renderer->swapchain = new_swapchain;
    return PIGMENT_SUCCESS;

ERROR:
    PLOG_ERROR(pigment, "Failed to recreate swapchain.");
    destroy_swapchain(pigment, new_swapchain);
    return PIGMENT_ERROR;
}

PFormat pigment_get_color_format(PWindowRenderer* renderer)
{
    if(renderer == NULL || renderer->swapchain == NULL)
    {
        return P_FORMAT_UNDEFINED;
    }

    return (PFormat) renderer->swapchain->image_format;
}

PFormat pigment_get_depth_format(PWindowRenderer* renderer)
{
    if(renderer == NULL || renderer->swapchain == NULL)
    {
        return P_FORMAT_UNDEFINED;
    }

    return (PFormat) renderer->swapchain->depth->vk_format;
}
