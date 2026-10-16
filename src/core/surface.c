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
#include "native_surface.h"
#include "swapchain_event.h"
#include "synchronization.h"

#include "internal.h"

static void destroy_renderer_immediate(Pigment* pigment, void* resource);
static PResult create_swapchain_image_wrappers(Pigment* pigment, PSwapchain* swapchain, VkImageUsageFlags usage);
static void destroy_swapchain_image_wrappers(Pigment* pigment, PSwapchain* swapchain);
static const VkFormat* preferred_formats_for_color_space(VkColorSpaceKHR color_space, uint32_t* out_count);
static VkSurfaceFormatKHR choose_surface_format(VkSurfaceFormatKHR* available_formats, uint32_t formats_count, PColorSpace preferred);
static VkPresentModeKHR choose_surface_present_mode(Pigment* pigment, VkPresentModeKHR* available_present_modes, uint32_t present_modes_count, PPresentMode preferred);
static VkExtent2D choose_swapchain_extent(const VkSurfaceCapabilitiesKHR capabilities, uint32_t framebuffer_width, uint32_t framebuffer_height);
static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported, PBool transparent);
static PFormat pick_depth_format(Pigment* pigment);
static PResult create_swapchain_depth(Pigment* pigment, PSwapchain* swapchain, const PSwapchainDesc* settings);
static void destroy_swapchain_depth(Pigment* pigment, PSwapchain* swapchain);
static PResult create_swapchain_color_multisample(Pigment* pigment, PSwapchain* swapchain, const PSwapchainDesc* settings);
static void destroy_swapchain_color_multisample(Pigment* pigment, PSwapchain* swapchain);
static VkSampleCountFlagBits clamp_sample_count(Pigment* pigment, PSampleCount requested);
static VkColorSpaceKHR color_space_to_vk(PColorSpace color_space);
static PColorSpace color_space_from_vk(VkColorSpaceKHR color_space);
static SwapChainSupportDetails* query_swapchain_support(Pigment* pigment, VkPhysicalDevice device, VkSurfaceKHR surface);
static void destroy_support_details(Pigment* pigment, SwapChainSupportDetails* details);
static void destroy_surface(Pigment* pigment, PSurface* surface);
static PSwapchain* create_swapchain(Pigment* pigment, const PSwapchainDesc* desc, PSurface* surface, PSwapchain* old_swapchain);
static void destroy_swapchain(Pigment* pigment, PSwapchain* swapchain);
static void retire_swapchain(Pigment* pigment, void* resource);
static void release_swapchain(Pigment* pigment, PSwapchain* swapchain);
static void destroy_renderer_internal(Pigment* pigment, PWindowRenderer* renderer);

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

    if((desc->shared_queue_count > 0 && desc->shared_queues == NULL) || desc->shared_queue_count == UINT32_MAX)
    {
        PLOG_ERROR(pigment, "Invalid swapchain queue list.");
        return NULL;
    }

    for(uint32_t i = 0; i < desc->shared_queue_count; i++)
    {
        PBool found = P_FALSE;
        for(uint32_t j = 0; j < pigment->device->queue_count; j++)
        {
            found |= desc->shared_queues[i] == &pigment->device->queues[j];
        }

        if(!found)
        {
            PLOG_ERROR(pigment, "Swapchain queues must belong to this Pigment instance.");
            return NULL;
        }
    }

    PWindowRenderer* renderer = NULL;
    PSurface* surface         = NULL;
    VkSurfaceKHR vk_surface   = create_vk_surface_from_handles(pigment, handles);
    if(vk_surface == VK_NULL_HANDLE)
    {
        goto ERROR;
    }

    surface = P_NEW_FOR_OBJECT(pigment, surface);
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

    renderer = P_NEW_FOR_OBJECT(pigment, renderer);
    if(renderer == NULL)
    {
        goto ERROR;
    }

    renderer->surface     = surface;
    renderer->desc        = *desc;
    renderer->desc.width  = width;
    renderer->desc.height = height;

    if(pigment_resource_tracker_init(pigment, &renderer->tracker) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    renderer->shared_queues = P_NEW_ARRAY_FOR_OBJECT(pigment, renderer->shared_queues, desc->shared_queue_count + 1);
    if(renderer->shared_queues == NULL)
    {
        goto ERROR;
    }

    renderer->shared_queues[0] = device_find_queue(pigment->device, P_QUEUE_GRAPHICS_BIT);
    if(renderer->shared_queues[0] == NULL)
    {
        PLOG_ERROR(pigment, "Cannot create renderer without a graphics queue.");
        goto ERROR;
    }

    if(desc->shared_queue_count > 0)
    {
        memcpy(renderer->shared_queues + 1, desc->shared_queues, desc->shared_queue_count * sizeof(*renderer->shared_queues));
    }

    renderer->desc.shared_queues      = renderer->shared_queues;
    renderer->desc.shared_queue_count = desc->shared_queue_count + 1;

    renderer->swapchain = create_swapchain(pigment, &renderer->desc, surface, NULL);
    if(renderer->swapchain == NULL)
    {
        goto ERROR;
    }

    renderer->swapchain->renderer = renderer;
    resource_tracker_retain(&renderer->tracker);

    renderer->per_slot_trackers = P_NEW_ARRAY_FOR_OBJECT(pigment, renderer->per_slot_trackers, pigment->config.max_frames_in_flight);
    if(renderer->per_slot_trackers == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < pigment->config.max_frames_in_flight; i++)
    {
        if(pigment_resource_tracker_init(pigment, &renderer->per_slot_trackers[i]) != PIGMENT_SUCCESS)
        {
            goto ERROR;
        }
    }

    renderer->frames = P_NEW_ARRAY_FOR_OBJECT(pigment, renderer->frames, pigment->config.max_frames_in_flight);
    if(renderer->frames == NULL)
    {
        goto ERROR;
    }

    return renderer;

ERROR:
    if(renderer != NULL)
    {
        pigment_renderer_destroy(pigment, renderer);
    }
    else if(surface != NULL)
    {
        destroy_surface(pigment, surface);
    }
    else if(vk_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(pigment->instance->vulkan_instance, vk_surface, &pigment->vk_alloc);
    }

    return NULL;
}

void pigment_renderer_destroy(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return;
    }

    PSwapchain* swapchain = renderer->swapchain;
    renderer->swapchain   = NULL;
    release_swapchain(pigment, swapchain);
    pigment_defer_destroy_tracked(pigment, destroy_renderer_immediate, renderer, &renderer->tracker);
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
    vkDestroySurfaceKHR(pigment->instance->vulkan_instance, surface->surface, &pigment->vk_alloc);
    P_FREE(pigment, surface);
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

    return (PFormat) renderer->swapchain->depth[0]->vk_format;
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

PResult recreate_swapchain(Pigment* pigment, PWindowRenderer* renderer)
{
    PSwapchain* old_swapchain = renderer->swapchain;
    PSwapchain* new_swapchain = create_swapchain(pigment, &renderer->desc, renderer->surface, old_swapchain);
    if(new_swapchain == NULL)
    {
        PLOG_ERROR(pigment, "Failed to recreate swapchain.");
        return PIGMENT_ERROR;
    }

    new_swapchain->renderer = renderer;
    resource_tracker_retain(&renderer->tracker);
    renderer->swapchain = new_swapchain;
    release_swapchain(pigment, old_swapchain);
    return PIGMENT_SUCCESS;
}

PResult pigment_recreate_swapchain(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return PIGMENT_ERROR;
    }

    if(!renderer->needs_recreate)
    {
        return PIGMENT_SUCCESS;
    }

    renderer->needs_recreate = P_FALSE;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pigment->device->physical_device, renderer->surface->surface, &caps);
    if(caps.currentExtent.width != 0xFFFFFFFF)
    {
        renderer->desc.width  = caps.currentExtent.width;
        renderer->desc.height = caps.currentExtent.height;
    }
    if(renderer->desc.width == 0 || renderer->desc.height == 0)
    {
        renderer->needs_recreate = P_TRUE;
        return PIGMENT_ERROR;
    }

    if(recreate_swapchain(pigment, renderer) != PIGMENT_SUCCESS)
    {
        renderer->needs_recreate = P_TRUE;
        return PIGMENT_ERROR;
    }

    PSwapchainRecreateEvent event = {
        .renderer = renderer,
        .width    = renderer->swapchain->extent.width,
        .height   = renderer->swapchain->extent.height,
    };
    dispatch_swapchain_recreate(pigment, &event);

    return PIGMENT_SUCCESS;
}

static SwapChainSupportDetails* query_swapchain_support(Pigment* pigment, VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapChainSupportDetails* details;
    details = P_NEW_FOR_OBJECT(pigment, details);
    if(details == NULL)
    {
        goto ERROR;
    }

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details->capabilities);

    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details->formats_count, NULL);
    details->formats = P_NEW_ARRAY_FOR_OBJECT(pigment, details->formats, details->formats_count);
    if(details->formats == NULL)
    {
        goto ERROR;
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details->formats_count, details->formats);

    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details->present_modes_count, NULL);
    details->present_modes = P_NEW_ARRAY_FOR_OBJECT(pigment, details->present_modes, details->present_modes_count);
    if(details->present_modes == NULL)
    {
        goto ERROR;
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details->present_modes_count, details->present_modes);

    return details;

ERROR:
    destroy_support_details(pigment, details);
    return NULL;
}

static void destroy_support_details(Pigment* pigment, SwapChainSupportDetails* details)
{
    if(details != NULL)
    {
        P_FREE(pigment, details->formats);
        P_FREE(pigment, details->present_modes);
        P_FREE(pigment, details);
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

static VkPresentModeKHR choose_surface_present_mode(Pigment* pigment, VkPresentModeKHR* available_present_modes, uint32_t present_modes_count, PPresentMode preferred)
{
    VkPresentModeKHR requested = (VkPresentModeKHR) preferred;

    for(size_t i = 0; i < present_modes_count; i++)
    {
        if(available_present_modes[i] == requested)
        {
            return requested;
        }
    }

    PLOG_INFO(pigment, "Requested present mode is unavailable, falling back to FIFO.");
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
    uint32_t* shared_families                = NULL;

    swapchain = P_NEW_FOR_OBJECT(pigment, swapchain);
    if(swapchain == NULL)
    {
        goto ERROR;
    }

    if(pigment_resource_tracker_init(pigment, &swapchain->tracker) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    PDeviceQueue* graphics = device_find_queue(device, P_QUEUE_GRAPHICS_BIT);
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

    support_details = query_swapchain_support(pigment, device->physical_device, surface->surface);
    if(support_details == NULL)
    {
        goto ERROR;
    }

    VkSurfaceFormatKHR surface_format = choose_surface_format(support_details->formats, support_details->formats_count, desc->color_space);
    VkPresentModeKHR present_mode     = choose_surface_present_mode(pigment, support_details->present_modes, support_details->present_modes_count, desc->present_mode);
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

    create_info.minImageCount      = image_count;
    create_info.imageFormat        = surface_format.format;
    create_info.imageColorSpace    = surface_format.colorSpace;
    create_info.imageExtent        = extent;
    create_info.imageArrayLayers   = 1;
    VkImageUsageFlags wanted_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create_info.imageUsage         = wanted_usage & support_details->capabilities.supportedUsageFlags;
    create_info.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    shared_families = P_NEW_ARRAY_FOR_COMMAND(pigment, shared_families, (uint64_t) desc->shared_queue_count + 1);
    if(shared_families == NULL)
    {
        goto ERROR;
    }

    uint32_t family_count = unique_queue_families(desc->shared_queues, desc->shared_queue_count, shared_families);
    PBool present_shared  = P_FALSE;

    for(uint32_t i = 0; i < family_count; i++)
    {
        present_shared |= shared_families[i] == present_family_index;
    }

    if(!present_shared)
    {
        shared_families[family_count++] = present_family_index;
    }

    if(family_count > 1)
    {
        create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = family_count;
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

    if(old_swapchain != NULL && !old_swapchain->retired)
    {
        create_info.oldSwapchain = old_swapchain->swapchain;
        // Vulkan retires oldSwapchain even if creation fails.
        old_swapchain->retired   = P_TRUE;
    }

    VkResult result;
    if((result = vkCreateSwapchainKHR(device->logical_device, &create_info, &pigment->vk_alloc, &(swapchain->swapchain))) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create swap chain! (result: %d)", result);
        goto ERROR;
    }

    vkGetSwapchainImagesKHR(device->logical_device, swapchain->swapchain, &image_count, NULL);

    swapchain->images = P_NEW_ARRAY_FOR_OBJECT(pigment, swapchain->images, image_count);
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
    swapchain->min_image_count      = support_details->capabilities.minImageCount;
    swapchain->image_format         = surface_format.format;
    swapchain->color_space          = surface_format.colorSpace;
    swapchain->extent               = extent;
    swapchain->samples              = clamp_sample_count(pigment, desc->samples);
    swapchain->present_queue        = present_queue;
    swapchain->present_family_index = present_family_index;

    if(create_swapchain_image_wrappers(pigment, swapchain, create_info.imageUsage) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    if(create_swapchain_depth(pigment, swapchain, desc) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    if(create_swapchain_color_multisample(pigment, swapchain, desc) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    swapchain->sync = create_sync(pigment, pigment->config.max_frames_in_flight, swapchain->image_count);
    if(swapchain->sync == NULL)
    {
        goto ERROR;
    }

    P_FREE(pigment, shared_families);
    destroy_support_details(pigment, support_details);

    return swapchain;

ERROR:
    P_FREE(pigment, shared_families);
    destroy_support_details(pigment, support_details);
    release_swapchain(pigment, swapchain);
    return NULL;
}

static void destroy_swapchain(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain != NULL)
    {
        PDevice* device = pigment->device;
        destroy_sync(pigment, swapchain->sync, swapchain, pigment->config.max_frames_in_flight);
        destroy_swapchain_color_multisample(pigment, swapchain);
        destroy_swapchain_depth(pigment, swapchain);
        P_FREE(pigment, swapchain->image_wrappers);
        vkDestroySwapchainKHR(device->logical_device, swapchain->swapchain, &pigment->vk_alloc);
        P_FREE(pigment, swapchain->images);
        pigment_resource_tracker_destroy(pigment, &swapchain->tracker);
        if(swapchain->renderer != NULL)
        {
            resource_tracker_release(pigment, &swapchain->renderer->tracker);
        }
        P_FREE(pigment, swapchain);
    }
}

static void destroy_swapchain_immediate(Pigment* pigment, void* resource)
{
    PSwapchain* swapchain = resource;
    // Shutdown drains deletion callbacks without polling their wait targets.
    vkWaitForFences(pigment->device->logical_device, pigment->config.max_frames_in_flight, swapchain->sync->present_fences, VK_TRUE, UINT64_MAX);
    destroy_swapchain(pigment, swapchain);
}

static void release_swapchain(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain == NULL)
    {
        return;
    }

    if(swapchain->tracker.last_used == NULL)
    {
        destroy_swapchain(pigment, swapchain);
        return;
    }

    destroy_swapchain_image_wrappers(pigment, swapchain);
    pigment_defer_destroy_tracked(pigment, retire_swapchain, swapchain, &swapchain->tracker);
}

static void retire_swapchain(Pigment* pigment, void* resource)
{
    PSwapchain* swapchain = resource;
    if(swapchain->renderer == NULL)
    {
        destroy_swapchain(pigment, swapchain);
        return;
    }

    const VkFence* fences = swapchain->sync->present_fences;
    if(fences == NULL)
    {
        device_wait_idle(pigment);
        destroy_swapchain(pigment, swapchain);
        return;
    }

    defer_destroy_swapchain(pigment, destroy_swapchain_immediate, swapchain, fences, pigment->config.max_frames_in_flight);
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

static PResult create_swapchain_depth(Pigment* pigment, PSwapchain* swapchain, const PSwapchainDesc* settings)
{
    PImageDesc desc = {
        .width              = swapchain->extent.width,
        .height             = swapchain->extent.height,
        .format             = pick_depth_format(pigment),
        .usage              = P_IMAGE_USAGE_RENDER_DEPTH,
        .samples            = (PSampleCount) swapchain->samples,
        .mip_levels         = 1,
        .shared_queues      = settings->shared_queues,
        .shared_queue_count = settings->shared_queue_count,
    };

    uint32_t count   = pigment->config.max_frames_in_flight;
    swapchain->depth = P_NEW_ARRAY_FOR_OBJECT(pigment, swapchain->depth, count);
    if(swapchain->depth == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        swapchain->depth[i] = pigment_create_image(pigment, &desc);
        if(swapchain->depth[i] == NULL)
        {
            return PIGMENT_ERROR;
        }
    }

    return PIGMENT_SUCCESS;
}

static void destroy_swapchain_depth(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain->depth != NULL)
    {
        for(uint32_t i = 0; i < pigment->config.max_frames_in_flight; i++)
        {
            pigment_destroy_image(pigment, swapchain->depth[i]);
        }
        P_FREE(pigment, swapchain->depth);
        swapchain->depth = NULL;
    }
}

static PResult create_swapchain_color_multisample(Pigment* pigment, PSwapchain* swapchain, const PSwapchainDesc* settings)
{
    if(swapchain->samples == VK_SAMPLE_COUNT_1_BIT)
    {
        return PIGMENT_SUCCESS;
    }

    PImageDesc desc = {
        .width              = swapchain->extent.width,
        .height             = swapchain->extent.height,
        .format             = (PFormat) swapchain->image_format,
        .usage              = P_IMAGE_USAGE_RENDER_COLOR,
        .samples            = (PSampleCount) swapchain->samples,
        .mip_levels         = 1,
        .shared_queues      = settings->shared_queues,
        .shared_queue_count = settings->shared_queue_count,
    };

    uint32_t count               = pigment->config.max_frames_in_flight;
    swapchain->color_multisample = P_NEW_ARRAY_FOR_OBJECT(pigment, swapchain->color_multisample, count);
    if(swapchain->color_multisample == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        swapchain->color_multisample[i] = pigment_create_image(pigment, &desc);
        if(swapchain->color_multisample[i] == NULL)
        {
            return PIGMENT_ERROR;
        }
    }

    return PIGMENT_SUCCESS;
}

static void destroy_swapchain_color_multisample(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain->color_multisample != NULL)
    {
        for(uint32_t i = 0; i < pigment->config.max_frames_in_flight; i++)
        {
            pigment_destroy_image(pigment, swapchain->color_multisample[i]);
        }
        P_FREE(pigment, swapchain->color_multisample);
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

static PResult create_swapchain_image_wrappers(Pigment* pigment, PSwapchain* swapchain, VkImageUsageFlags usage)
{
    swapchain->image_wrappers = P_NEW_ARRAY_FOR_OBJECT(pigment, swapchain->image_wrappers, swapchain->image_count);
    if(swapchain->image_wrappers == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    for(uint32_t i = 0; i < swapchain->image_count; i++)
    {
        swapchain->image_wrappers[i] = pigment_vk_wrap_image(pigment, swapchain->images[i], swapchain->image_format, usage, swapchain->extent.width, swapchain->extent.height, &swapchain->tracker);
        if(swapchain->image_wrappers[i] == NULL)
        {
            return PIGMENT_ERROR_OUT_OF_MEMORY;
        }
    }

    return PIGMENT_SUCCESS;
}

static void destroy_swapchain_image_wrappers(Pigment* pigment, PSwapchain* swapchain)
{
    if(swapchain == NULL || swapchain->image_wrappers == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < swapchain->image_count; i++)
    {
        if(swapchain->image_wrappers[i] != NULL)
        {
            pigment_destroy_image(pigment, swapchain->image_wrappers[i]);
        }
    }
}

static void destroy_renderer_internal(Pigment* pigment, PWindowRenderer* renderer)
{
    if(renderer == NULL)
    {
        return;
    }

    destroy_surface(pigment, renderer->surface);
    pigment_resource_tracker_destroy(pigment, &renderer->tracker);
    if(renderer->per_slot_trackers != NULL)
    {
        for(uint32_t i = 0; i < pigment->config.max_frames_in_flight; i++)
        {
            pigment_resource_tracker_destroy(pigment, &renderer->per_slot_trackers[i]);
        }
        P_FREE(pigment, renderer->per_slot_trackers);
    }
    P_FREE(pigment, renderer->shared_queues);
    P_FREE(pigment, renderer->frames);
    P_FREE(pigment, renderer);
}

VkSurfaceKHR pigment_vk_surface(PWindowRenderer* renderer)
{
    return (renderer != NULL && renderer->surface != NULL) ? renderer->surface->surface : VK_NULL_HANDLE;
}

VkSwapchainKHR pigment_vk_swapchain(PWindowRenderer* renderer)
{
    return (renderer != NULL && renderer->swapchain != NULL) ? renderer->swapchain->swapchain : VK_NULL_HANDLE;
}

VkQueue pigment_vk_present_queue(PWindowRenderer* renderer)
{
    return (renderer != NULL && renderer->swapchain != NULL) ? renderer->swapchain->present_queue : VK_NULL_HANDLE;
}
