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
#include "commands.h"
#include "deletion.h"
#include "image.h"
#include "internal.h"
#include "log_internal.h"
#include "native_surface.h"
#include "synchronization.h"

#define PIGMENT_RENDERERS_INITIAL_CAPACITY 4

static void destroy_renderer_immediate(Pigment* pigment, void* resource);
static PResult create_swapchain_image_views(Pigment* pigment, PSwapchain* swapchain);
static void destroy_swapchain_image_views(PSwapchain* swapchain, PDevice* device);
static const VkFormat* preferred_formats_for_color_space(VkColorSpaceKHR color_space, uint32_t* out_count);
static VkSurfaceFormatKHR choose_surface_format(VkSurfaceFormatKHR* available_formats, uint32_t formats_count, PColorSpace preferred);
static VkPresentModeKHR choose_surface_present_mode(VkPresentModeKHR* available_present_modes, uint32_t present_modes_count, PPresentMode preferred);
static VkExtent2D choose_swapchain_extent(const VkSurfaceCapabilitiesKHR capabilities, uint32_t framebuffer_width, uint32_t framebuffer_height);
static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported, PBool transparent);
static PFormat pick_depth_format(Pigment* pigment);
static PResult create_swapchain_depth(Pigment* pigment, PSwapchain* swapchain);
static void destroy_swapchain_depth(Pigment* pigment, PSwapchain* swapchain);
static PResult create_swapchain_color_multisample(Pigment* pigment, PSwapchain* swapchain);
static void destroy_swapchain_color_multisample(Pigment* pigment, PSwapchain* swapchain);
static VkSampleCountFlagBits clamp_sample_count(Pigment* pigment, PSampleCount requested);
static VkColorSpaceKHR color_space_to_vk(PColorSpace color_space);
static PColorSpace color_space_from_vk(VkColorSpaceKHR color_space);
static SwapChainSupportDetails* query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface);
static void destroy_support_details(SwapChainSupportDetails* details);
static void destroy_surface(Pigment* pigment, PSurface* surface);
static PSwapchain* create_swapchain(Pigment* pigment, const PSwapchainDesc* desc, PSurface* surface, PSwapchain* old_swapchain);
static void destroy_swapchain(Pigment* pigment, PSwapchain* swapchain);
static void destroy_renderer_internal(Pigment* pigment, PWindowRenderer* renderer);
static PResult renderer_list_append(Pigment* pigment, PWindowRenderer* renderer);
static void renderer_list_remove(PRendererList* list, PWindowRenderer* renderer);

static inline uint32_t clamp(uint32_t value, uint32_t min, uint32_t max)
{
    const uint32_t temp = value < min ? min : value;
    return temp > max ? max : temp;
}

PWindowRenderer* pigment_renderer_create(Pigment* pigment, const PWindowHandles* handles, const PSwapchainDesc* desc)
{
    if(pigment == NULL || handles == NULL || desc == NULL)
    {
        return NULL;
    }

    PWindowRenderer* renderer = NULL;
    PSurface* surface         = NULL;
    VkSurfaceKHR vk_surface   = create_vk_surface_from_handles(pigment, handles);
    if(vk_surface == VK_NULL_HANDLE)
    {
        goto ERROR;
    }

    surface = malloc(sizeof(*surface));
    if(surface == NULL)
    {
        goto ERROR;
    }
    surface->surface = vk_surface;

    uint32_t width  = desc->width;
    uint32_t height = desc->height;
    if(width == 0 || height == 0)
    {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pigment->device->physical_device, vk_surface, &caps);
        if(caps.currentExtent.width != 0xFFFFFFFF)
        {
            width  = caps.currentExtent.width;
            height = caps.currentExtent.height;
        }
        else
        {
            PLOG_ERROR(pigment, "Surface has no defined size (Wayland or similar). PSwapchainDesc.width/height must be set explicitly.");
            goto ERROR;
        }
    }

    renderer = calloc(1, sizeof(*renderer));
    if(renderer == NULL)
    {
        goto ERROR;
    }

    renderer->surface     = surface;
    renderer->desc        = *desc;
    renderer->desc.width  = width;
    renderer->desc.height = height;

    renderer->swapchain = create_swapchain(pigment, &renderer->desc, surface, NULL);
    if(renderer->swapchain == NULL)
    {
        goto ERROR;
    }

    renderer->command_buffers = create_command_buffers(pigment, pigment_default_pool(pigment), pigment->config.max_frames_in_flight);
    if(renderer->command_buffers == NULL)
    {
        goto ERROR;
    }

    renderer->sync = create_sync(pigment, pigment->config.max_frames_in_flight, renderer->swapchain->image_count);
    if(renderer->sync == NULL)
    {
        goto ERROR;
    }

    if(renderer_list_append(pigment, renderer) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    return renderer;

ERROR:
    if(renderer != NULL)
    {
        destroy_renderer_internal(pigment, renderer);
    }
    else if(surface != NULL)
    {
        destroy_surface(pigment, surface);
    }
    else if(vk_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(pigment->instance->vulkan_instance, vk_surface, NULL);
    }

    return NULL;
}

void pigment_renderer_destroy(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return;
    }

    renderer_list_remove(pigment->renderers, renderer);

    VkFence last_present_fence = VK_NULL_HANDLE;
    if(renderer->sync != NULL && renderer->sync->present_fences != NULL)
    {
        uint32_t max_frame     = pigment->config.max_frames_in_flight;
        uint32_t current_frame = renderer->swapchain->current_frame;
        uint32_t last_slot     = current_frame > 0 ? current_frame - 1 : max_frame - 1;
        last_present_fence     = renderer->sync->present_fences[last_slot];
    }
    else
    {
        device_wait_idle(pigment);
    }

    defer_destroy_renderer(pigment, destroy_renderer_immediate, renderer, &renderer->tracker, last_present_fence);
}

static void destroy_renderer_immediate(Pigment* pigment, void* resource)
{
    destroy_renderer_internal(pigment, (PWindowRenderer*) resource);
}

void pigment_renderer_resize(PWindowRenderer* renderer, uint32_t width, uint32_t height)
{
    if(renderer == NULL)
    {
        return;
    }
    renderer->desc.width     = width;
    renderer->desc.height    = height;
    renderer->needs_recreate = P_TRUE;
}

void pigment_set_present_mode(PWindowRenderer* renderer, PPresentMode mode)
{
    if(renderer == NULL)
    {
        return;
    }
    renderer->desc.present_mode = mode;
    renderer->needs_recreate    = P_TRUE;
}

void pigment_set_color_space(PWindowRenderer* renderer, PColorSpace color_space)
{
    if(renderer == NULL)
    {
        return;
    }
    renderer->desc.color_space = color_space;
    renderer->needs_recreate   = P_TRUE;
}

void pigment_set_sample_count(PWindowRenderer* renderer, PSampleCount samples)
{
    if(renderer == NULL)
    {
        return;
    }

    renderer->desc.samples   = samples;
    renderer->needs_recreate = P_TRUE;
}

static void destroy_surface(Pigment* pigment, PSurface* surface)
{
    if(surface == NULL)
    {
        return;
    }
    vkDestroySurfaceKHR(pigment->instance->vulkan_instance, surface->surface, NULL);
    free(surface);
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

PColorSpace pigment_get_color_space(PWindowRenderer* renderer)
{
    if(renderer == NULL || renderer->swapchain == NULL)
    {
        return P_COLOR_SPACE_SRGB_NONLINEAR;
    }

    return color_space_from_vk(renderer->swapchain->color_space);
}

PSampleCount pigment_get_sample_count(PWindowRenderer* renderer)
{
    if(renderer == NULL || renderer->swapchain == NULL)
    {
        return P_SAMPLE_COUNT_1;
    }

    return (PSampleCount) renderer->swapchain->samples;
}

void pigment_get_swapchain_size(PWindowRenderer* renderer, uint32_t* out_width, uint32_t* out_height)
{
    if(renderer == NULL || renderer->swapchain == NULL)
    {
        if(out_width != NULL)
        {
            *out_width = 0;
        }
        if(out_height != NULL)
        {
            *out_height = 0;
        }
        return;
    }

    if(out_width != NULL)
    {
        *out_width = renderer->swapchain->extent.width;
    }

    if(out_height != NULL)
    {
        *out_height = renderer->swapchain->extent.height;
    }
}

PRendererList* create_renderer_list(Pigment* pigment)
{
    (void) pigment;
    PRendererList* list = calloc(1, sizeof(*list));
    if(list == NULL)
    {
        return NULL;
    }

    list->capacity  = PIGMENT_RENDERERS_INITIAL_CAPACITY;
    list->renderers = malloc(list->capacity * sizeof(*list->renderers));

    if(list->renderers == NULL)
    {
        free(list);
        return NULL;
    }
    return list;
}

void destroy_renderer_list(Pigment* pigment, PRendererList* list)
{
    if(list == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < list->count; i++)
    {
        destroy_renderer_internal(pigment, list->renderers[i]);
    }
    free(list->renderers);
    free(list);
}

PResult recreate_swapchain(Pigment* pigment, PWindowRenderer* renderer)
{
    device_wait_idle(pigment);

    PSwapchain* old_swapchain = renderer->swapchain;
    PSwapchain* new_swapchain = create_swapchain(pigment, &renderer->desc, renderer->surface, old_swapchain);
    if(new_swapchain == NULL)
    {
        PLOG_ERROR(pigment, "Failed to recreate swapchain.");
        return PIGMENT_ERROR;
    }

    renderer->swapchain = new_swapchain;
    destroy_swapchain(pigment, old_swapchain);
    return PIGMENT_SUCCESS;
}

static SwapChainSupportDetails* query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface)
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

static void destroy_support_details(SwapChainSupportDetails* details)
{
    if(details != NULL)
    {
        free(details->formats);
        free(details->present_modes);
        free(details);
    }
}

static const VkFormat* preferred_formats_for_color_space(VkColorSpaceKHR color_space, uint32_t* out_count)
{
    static const VkFormat sdr[]             = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
    static const VkFormat hdr10[]           = {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_R16G16B16A16_SFLOAT};
    static const VkFormat extended_linear[] = {VK_FORMAT_R16G16B16A16_SFLOAT};
    static const VkFormat bt2020_linear[]   = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_A2B10G10R10_UNORM_PACK32};
    static const VkFormat display_p3[]      = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_A2B10G10R10_UNORM_PACK32};

    switch(color_space)
    {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
            *out_count = sizeof(sdr) / sizeof(sdr[0]);
            return sdr;
        case VK_COLOR_SPACE_HDR10_ST2084_EXT:
        case VK_COLOR_SPACE_HDR10_HLG_EXT:
            *out_count = sizeof(hdr10) / sizeof(hdr10[0]);
            return hdr10;
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
            *out_count = sizeof(extended_linear) / sizeof(extended_linear[0]);
            return extended_linear;
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
            *out_count = sizeof(bt2020_linear) / sizeof(bt2020_linear[0]);
            return bt2020_linear;
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:
            *out_count = sizeof(display_p3) / sizeof(display_p3[0]);
            return display_p3;
        default:
            *out_count = 0;
            return NULL;
    }
}

static VkSurfaceFormatKHR choose_surface_format(VkSurfaceFormatKHR* available_formats, uint32_t formats_count, PColorSpace preferred)
{
    VkColorSpaceKHR preferred_vk     = color_space_to_vk(preferred);
    uint32_t priority_count          = 0;
    const VkFormat* priority_formats = preferred_formats_for_color_space(preferred_vk, &priority_count);

    for(uint32_t p = 0; p < priority_count; p++)
    {
        for(size_t i = 0; i < formats_count; i++)
        {
            if(available_formats[i].format == priority_formats[p] && available_formats[i].colorSpace == preferred_vk)
            {
                return available_formats[i];
            }
        }
    }

    for(size_t i = 0; i < formats_count; i++)
    {
        if(available_formats[i].colorSpace == preferred_vk)
        {
            return available_formats[i];
        }
    }

    for(size_t i = 0; i < formats_count; i++)
    {
        if(available_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && available_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return available_formats[i];
        }
    }

    return available_formats[0];
}

static VkColorSpaceKHR color_space_to_vk(PColorSpace color_space)
{
    switch(color_space)
    {
        case P_COLOR_SPACE_SRGB_NONLINEAR:
            return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        case P_COLOR_SPACE_DISPLAY_P3_NONLINEAR:
            return VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT;
        case P_COLOR_SPACE_EXTENDED_SRGB_LINEAR:
            return VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        case P_COLOR_SPACE_BT2020_LINEAR:
            return VK_COLOR_SPACE_BT2020_LINEAR_EXT;
        case P_COLOR_SPACE_HDR10_ST2084:
            return VK_COLOR_SPACE_HDR10_ST2084_EXT;
        case P_COLOR_SPACE_HDR10_HLG:
            return VK_COLOR_SPACE_HDR10_HLG_EXT;
    }
    return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
}

static PColorSpace color_space_from_vk(VkColorSpaceKHR color_space)
{
    switch(color_space)
    {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
            return P_COLOR_SPACE_SRGB_NONLINEAR;
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:
            return P_COLOR_SPACE_DISPLAY_P3_NONLINEAR;
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
            return P_COLOR_SPACE_EXTENDED_SRGB_LINEAR;
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
            return P_COLOR_SPACE_BT2020_LINEAR;
        case VK_COLOR_SPACE_HDR10_ST2084_EXT:
            return P_COLOR_SPACE_HDR10_ST2084;
        case VK_COLOR_SPACE_HDR10_HLG_EXT:
            return P_COLOR_SPACE_HDR10_HLG;
        default:
            return P_COLOR_SPACE_SRGB_NONLINEAR;
    }
}

static VkPresentModeKHR choose_surface_present_mode(VkPresentModeKHR* available_present_modes, uint32_t present_modes_count, PPresentMode preferred)
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

static VkExtent2D choose_swapchain_extent(const VkSurfaceCapabilitiesKHR capabilities, uint32_t framebuffer_width, uint32_t framebuffer_height)
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

static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported, PBool transparent)
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

static PSwapchain* create_swapchain(Pigment* pigment, const PSwapchainDesc* desc, PSurface* surface, PSwapchain* old_swapchain)
{
    PDevice* device                          = pigment->device;
    PSwapchain* swapchain                    = NULL;
    SwapChainSupportDetails* support_details = NULL;

    swapchain = calloc(1, sizeof(*swapchain));
    if(swapchain == NULL)
    {
        goto ERROR;
    }

    PDeviceQueue* graphics = device_find_queue(device, P_QUEUE_GRAPHICS_BIT, 0);
    if(graphics == NULL)
    {
        PLOG_ERROR(pigment, "Cannot create swapchain: device has no graphics queue");
        goto ERROR;
    }

    uint32_t graphics_family_index = graphics->family_index;
    uint32_t present_family_index  = UINT32_MAX;
    VkQueue present_queue          = VK_NULL_HANDLE;

    if(device_supports_surface(device->physical_device, graphics_family_index, surface->surface))
    {
        present_family_index = graphics_family_index;
        present_queue        = graphics->queue;
    }
    else
    {
        for(uint32_t i = 0; i < device->queue_count; i++)
        {
            if(device->queues[i].family_index == graphics_family_index)
            {
                continue;
            }

            if(device_supports_surface(device->physical_device, device->queues[i].family_index, surface->surface))
            {
                present_family_index = device->queues[i].family_index;
                present_queue        = device->queues[i].queue;
                break;
            }
        }

        if(present_queue == VK_NULL_HANDLE)
        {
            PLOG_ERROR(pigment, "No queue family on the picked GPU supports presentation on this surface");
            goto ERROR;
        }

        PLOG_INFO(pigment, "Graphics family does not support present, using separate present family %u", present_family_index);
    }

    support_details = query_swapchain_support(device->physical_device, surface->surface);
    if(support_details == NULL)
    {
        goto ERROR;
    }

    VkSurfaceFormatKHR surface_format = choose_surface_format(support_details->formats, support_details->formats_count, desc->color_space);
    VkPresentModeKHR present_mode     = choose_surface_present_mode(support_details->present_modes, support_details->present_modes_count, desc->present_mode);
    VkExtent2D extent                 = choose_swapchain_extent(support_details->capabilities, desc->width, desc->height);

    uint32_t image_count = desc->image_count != 0 ? desc->image_count : support_details->capabilities.minImageCount + 1;
    if(image_count < support_details->capabilities.minImageCount)
    {
        image_count = support_details->capabilities.minImageCount;
    }
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

    uint32_t shared_families[2] = {graphics_family_index, present_family_index};
    if(present_family_index != graphics_family_index)
    {
        create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices   = shared_families;
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
    create_info.compositeAlpha = choose_composite_alpha(support_details->capabilities.supportedCompositeAlpha, desc->transparent);
    create_info.presentMode    = present_mode;
    create_info.clipped        = VK_TRUE;

    create_info.oldSwapchain = (old_swapchain != NULL) ? old_swapchain->swapchain : VK_NULL_HANDLE;

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

    swapchain->image_count          = image_count;
    swapchain->image_format         = surface_format.format;
    swapchain->color_space          = surface_format.colorSpace;
    swapchain->extent               = extent;
    swapchain->samples              = clamp_sample_count(pigment, desc->samples);
    swapchain->present_queue        = present_queue;
    swapchain->present_family_index = present_family_index;

    if(create_swapchain_image_views(pigment, swapchain) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    if(create_swapchain_depth(pigment, swapchain) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    if(create_swapchain_color_multisample(pigment, swapchain) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    destroy_support_details(support_details);

    return swapchain;

ERROR:
    destroy_support_details(support_details);
    if(swapchain != NULL)
    {
        vkDestroySwapchainKHR(device->logical_device, swapchain->swapchain, NULL);
        free(swapchain);
    }
    return NULL;
}

static void destroy_swapchain(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain != NULL)
    {
        PDevice* device = pigment->device;
        destroy_swapchain_color_multisample(pigment, swapchain);
        destroy_swapchain_depth(pigment, swapchain);
        destroy_swapchain_image_views(swapchain, device);
        vkDestroySwapchainKHR(device->logical_device, swapchain->swapchain, NULL);
        free(swapchain);
    }
}

static PFormat pick_depth_format(Pigment* pigment)
{
    VkPhysicalDevice physical_device = pigment->device->physical_device;

    VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT};
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

static PResult create_swapchain_depth(Pigment* pigment, PSwapchain* swapchain)
{
    PImageDesc desc = {
        .width      = swapchain->extent.width,
        .height     = swapchain->extent.height,
        .format     = pick_depth_format(pigment),
        .usage      = P_IMAGE_USAGE_RENDER_DEPTH,
        .samples    = (PSampleCount) swapchain->samples,
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

static PResult create_swapchain_color_multisample(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain->samples == VK_SAMPLE_COUNT_1_BIT)
    {
        return PIGMENT_SUCCESS;
    }

    PImageDesc desc = {
        .width      = swapchain->extent.width,
        .height     = swapchain->extent.height,
        .format     = (PFormat) swapchain->image_format,
        .usage      = P_IMAGE_USAGE_RENDER_COLOR,
        .samples    = (PSampleCount) swapchain->samples,
        .mip_levels = 1,
    };

    swapchain->color_multisample = pigment_create_image(pigment, &desc);

    return (swapchain->color_multisample != NULL) ? PIGMENT_SUCCESS : PIGMENT_ERROR;
}

static void destroy_swapchain_color_multisample(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain->color_multisample != NULL)
    {
        pigment_destroy_image(pigment, swapchain->color_multisample);
        swapchain->color_multisample = NULL;
    }
}

VkSampleCountFlags supported_sample_counts(Pigment* pigment)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pigment->device->physical_device, &props);
    return props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
}

static VkSampleCountFlagBits clamp_sample_count(Pigment* pigment, PSampleCount requested)
{
    if(requested == 0 || requested == P_SAMPLE_COUNT_1)
    {
        return VK_SAMPLE_COUNT_1_BIT;
    }

    VkSampleCountFlags supported = supported_sample_counts(pigment);
    VkSampleCountFlagBits req    = (VkSampleCountFlagBits) requested;

    if(supported & req)
    {
        return req;
    }

    VkSampleCountFlagBits fallback = VK_SAMPLE_COUNT_1_BIT;
    for(VkSampleCountFlagBits bit = VK_SAMPLE_COUNT_64_BIT; bit > VK_SAMPLE_COUNT_1_BIT; bit >>= 1)
    {
        if((supported & bit) && bit < req)
        {
            fallback = bit;
            break;
        }
    }

    PLOG_WARN(pigment, "Requested %dx MSAA not supported, falling back to %dx", (int) requested, (int) fallback);
    return fallback;
}

static PResult create_swapchain_image_views(Pigment* pigment, PSwapchain* swapchain)
{
    swapchain->image_views = calloc(swapchain->image_count, sizeof(*(swapchain->image_views)));
    if(swapchain->image_views == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    for(size_t i = 0; i < swapchain->image_count; i++)
    {
        swapchain->image_views[i] = create_image_view(pigment, swapchain->images[i], VK_IMAGE_VIEW_TYPE_2D, swapchain->image_format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
    }

    return PIGMENT_SUCCESS;
}

static void destroy_swapchain_image_views(PSwapchain* swapchain, PDevice* device)
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

static void destroy_renderer_internal(Pigment* pigment, PWindowRenderer* renderer)
{
    if(renderer == NULL)
    {
        return;
    }

    destroy_sync(pigment, renderer->sync, renderer->swapchain, pigment->config.max_frames_in_flight);
    destroy_command_buffers(pigment, renderer->command_buffers, pigment->config.max_frames_in_flight);
    destroy_swapchain(pigment, renderer->swapchain);
    destroy_surface(pigment, renderer->surface);
    free(renderer);
}

static PResult renderer_list_append(Pigment* pigment, PWindowRenderer* renderer)
{
    PRendererList* list = pigment->renderers;
    if(list->count >= list->capacity)
    {
        uint32_t new_capacity     = list->capacity * 2;
        PWindowRenderer** new_ptr = realloc(list->renderers, new_capacity * sizeof(*new_ptr));
        if(new_ptr == NULL)
        {
            return PIGMENT_ERROR_OUT_OF_MEMORY;
        }
        list->renderers = new_ptr;
        list->capacity  = new_capacity;
    }

    list->renderers[list->count++] = renderer;
    return PIGMENT_SUCCESS;
}

static void renderer_list_remove(PRendererList* list, PWindowRenderer* renderer)
{
    for(uint32_t i = 0; i < list->count; i++)
    {
        if(list->renderers[i] == renderer)
        {
            list->renderers[i] = list->renderers[--list->count];
            return;
        }
    }
}
