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

#include "surface.h"
#include "structs.h"

#include "lib/math.h"

extern void destroy_depth_resources(PSwapchain* swapchain, PDevice* device);
extern QueueFamilyIndices* find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

void destroy_image_views(PSwapchain* swapchain, PDevice* device);
void destroy_framebuffers(PSwapchain* swapchain, PDevice* device);
SwapChainSupportDetails* get_support_details(VkPhysicalDevice device, VkSurfaceKHR surface);
void destroy_support_details(SwapChainSupportDetails* details);
VkSurfaceFormatKHR choose_surface_format(VkSurfaceFormatKHR* available_formats, uint32_t formats_count);
VkPresentModeKHR choose_surface_present_modes(VkPresentModeKHR* available_present_modes, uint32_t present_modes_count);
VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR capabilities, GLFWwindow* window);
VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels, VkDevice device);

PSurface* create_surface(PInstance* instance, PWindow* window)
{
    PSurface* surface = malloc(sizeof(*surface));
    if(surface == NULL)
    {
        perror("create_surface");
        return NULL;
    }

    glfwCreateWindowSurface(instance->vulkan_instance, window->window, VK_NULL_HANDLE, &surface->surface);
    return surface;
}

void destroy_surface(PSurface* surface, PInstance* instance)
{
    if(surface == NULL)
    {
        return;
    }
    vkDestroySurfaceKHR(instance->vulkan_instance, surface->surface, NULL);
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
    details->present_modes = malloc(details->formats_count * sizeof(*details->present_modes));
    if(details->present_modes == NULL)
    {
        goto ERROR;
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details->present_modes_count, details->present_modes);

    return details;

ERROR:
    perror("get_support_details");
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

VkSurfaceFormatKHR choose_surface_format(VkSurfaceFormatKHR* available_formats, uint32_t formats_count)
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

VkPresentModeKHR choose_surface_present_modes(VkPresentModeKHR* available_present_modes, uint32_t present_modes_count)
{
    for(size_t i = 0; i < present_modes_count; i++)
    {
        if(available_present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return available_present_modes[i];
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR capabilities, GLFWwindow* window)
{
    if(capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actual_extent = {
        (uint32_t) width,
        (uint32_t) height
    };

    actual_extent.width  = clamp(actual_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actual_extent.height = clamp(actual_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actual_extent;
}

PSwapchain* create_swapchain(PDevice* device, PSurface* surface, PWindow* window)
{
    PSwapchain* swapchain = NULL;
    QueueFamilyIndices* indices = NULL;
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
    VkPresentModeKHR present_mode     = choose_surface_present_modes(support_details->present_modes, support_details->present_modes_count);
    VkExtent2D extent                 = choose_swap_extent(support_details->capabilities, window->window);

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

    create_info.preTransform   = support_details->capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode    = present_mode;
    create_info.clipped        = VK_TRUE;

    create_info.oldSwapchain = VK_NULL_HANDLE;

    if(vkCreateSwapchainKHR(device->logical_device, &create_info, NULL, &(swapchain->swapchain)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create swap chain!\n");
        goto ERROR;
    }

    vkGetSwapchainImagesKHR(device->logical_device, swapchain->swapchain, &image_count, NULL);

    swapchain->images = malloc(image_count * sizeof(*swapchain->images));
    if(swapchain->images == NULL)
    {
        goto ERROR;
    }

    vkGetSwapchainImagesKHR(device->logical_device, swapchain->swapchain, &image_count, swapchain->images);

    swapchain->image_count   = image_count;
    swapchain->image_format  = surface_format.format;
    swapchain->extent        = extent;
    swapchain->current_frame = 0;

    destroy_support_details(support_details);
    free(indices);

    return swapchain;

ERROR:
    destroy_support_details(support_details);
    free(indices);
    perror("create_swapchain");
    if(swapchain != NULL)
    {
        vkDestroySwapchainKHR(device->logical_device, swapchain->swapchain, NULL);
        free(swapchain);
    }
    return NULL;
}

void destroy_swapchain(PSwapchain* swapchain, PDevice* device)
{
    if(swapchain != NULL)
    {
        destroy_depth_resources(swapchain, device);
        destroy_framebuffers(swapchain, device);
        destroy_image_views(swapchain, device);
        vkDestroySwapchainKHR(device->logical_device, swapchain->swapchain, NULL);
        free(swapchain);
    }
}

VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels, VkDevice device)
{
    VkImageView image_view;

    VkImageViewCreateInfo view_create_info = {
        .sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image                           = image,
        .viewType                        = VK_IMAGE_VIEW_TYPE_2D,
        .format                          = format,
        .subresourceRange.aspectMask     = aspect_flags,
        .subresourceRange.baseMipLevel   = 0,
        .subresourceRange.levelCount     = mip_levels,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount     = 1
    };

    if(vkCreateImageView(device, &view_create_info, NULL, &image_view) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create texture image view!\n");
        return NULL;
    }

    return image_view;
}

int create_image_views(PSwapchain* swapchain, PDevice* device)
{
    swapchain->image_views = malloc(swapchain->image_count * sizeof(*(swapchain->image_views)));
    if(swapchain->image_views == NULL)
    {
        perror("create_image_views");
        return PIGMENT_ERROR;
    }

    for(size_t i = 0; i < swapchain->image_count; i++)
    {
        swapchain->image_views[i] = create_image_view(swapchain->images[i], swapchain->image_format, VK_IMAGE_ASPECT_COLOR_BIT, 1, device->logical_device);
    }

    free(swapchain->images);
    return PIGMENT_SUCCESS;
}

void destroy_image_views(PSwapchain* swapchain, PDevice* device)
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
}

int create_framebuffers(PSwapchain* swapchain, PRenderPass* render_pass, PDevice* device)
{
    swapchain->framebuffers = malloc(swapchain->image_count * sizeof(*swapchain->framebuffers));
    if(device == NULL)
    {
        goto ERROR;
    }

    for(size_t i = 0; i < swapchain->image_count; i++)
    {
        VkImageView attachments[] = {
            swapchain->image_views[i],
            swapchain->depth_image_view
        };

        VkFramebufferCreateInfo framebuffer_create_info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = render_pass->render_pass,
            .attachmentCount = sizeof(attachments) / sizeof(attachments[0]),
            .pAttachments    = attachments,
            .width           = swapchain->extent.width,
            .height          = swapchain->extent.height,
            .layers          = 1
        };

        if(vkCreateFramebuffer(device->logical_device, &framebuffer_create_info, NULL, &swapchain->framebuffers[i]) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create framebuffer!\n");
            goto ERROR;
        }
    }

    return PIGMENT_SUCCESS;

ERROR:
    perror("create_framebuffers");
    free(swapchain->framebuffers);
    return PIGMENT_ERROR;
}

void destroy_framebuffers(PSwapchain* swapchain, PDevice* device)
{
    if(swapchain == NULL || swapchain->framebuffers == NULL)
    {
        return;
    }
    for(size_t i = 0; i < swapchain->image_count; i++)
    {
        vkDestroyFramebuffer(device->logical_device, swapchain->framebuffers[i], NULL);
    }

    free(swapchain->framebuffers);
}

PSwapchain* recreate_swapchain(PSwapchain* previous_swapchain, PCommands* commands, PDevice* device, PSurface* surface, PWindow* window, PRenderPass* render_pass)
{
    PSwapchain* swapchain;

    int width = 0, height = 0;
    glfwGetFramebufferSize(window->window, &width, &height);
    while(width == 0 || height == 0)
    {
        glfwGetFramebufferSize(window->window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device->logical_device);

    destroy_swapchain(previous_swapchain, device);

    swapchain = create_swapchain(device, surface, window);
    if(swapchain == NULL)
    {
        goto ERROR;
    }
    if(create_image_views(swapchain, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    if(create_depth_resources(swapchain, commands, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }
    if(create_framebuffers(swapchain, render_pass, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    return swapchain;

ERROR:
    fprintf(stderr, "Failed to recreate swapchain!\n");
    destroy_swapchain(swapchain, device);
    return NULL;
}
