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

#include "image.h"
#include "internal.h"
#include "log_internal.h"

#include <math.h>
#include <stdlib.h>

static VkImageUsageFlags translate_usage(PImageUsage usage);
static VkImageAspectFlags compute_aspect(VkFormat format, PImageUsage usage);

uint32_t pigment_format_pixel_size(PFormat format)
{
    switch(format)
    {
        case P_FORMAT_R8_UNORM:
            return 1;
        case P_FORMAT_R8G8_UNORM:
            return 2;
        case P_FORMAT_R8G8B8A8_UNORM:
        case P_FORMAT_R8G8B8A8_SRGB:
        case P_FORMAT_B8G8R8A8_UNORM:
        case P_FORMAT_B8G8R8A8_SRGB:
            return 4;
        case P_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        default:
            return 0;
    }
}

bool pigment_format_supports_linear_blit(Pigment* pigment, PFormat format)
{
    if(pigment == NULL || pigment->device == NULL)
    {
        return false;
    }
    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(pigment->device->physical_device, (VkFormat) format, &format_properties);
    return (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

VkImageLayout image_layout_to_vk(PImageLayout layout)
{
    switch(layout)
    {
        case P_IMAGE_LAYOUT_UNDEFINED:
            return VK_IMAGE_LAYOUT_UNDEFINED;
        case P_IMAGE_LAYOUT_GENERAL:
            return VK_IMAGE_LAYOUT_GENERAL;
        case P_IMAGE_LAYOUT_COLOR_ATTACHMENT:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case P_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case P_IMAGE_LAYOUT_SHADER_READ_ONLY:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case P_IMAGE_LAYOUT_TRANSFER_SRC:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case P_IMAGE_LAYOUT_TRANSFER_DST:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case P_IMAGE_LAYOUT_PRESENT:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

void pigment_cmd_copy_buffer_to_image(Pigment* pigment, PCommandBuffer* cmd, PBuffer* src, PImage* dst, PImageLayout dst_layout, const PBufferImageCopy* regions, uint32_t region_count)
{
    if(pigment == NULL || cmd == NULL || src == NULL || dst == NULL || regions == NULL || region_count == 0)
    {
        return;
    }

    VkBufferImageCopy* vk_regions = calloc(region_count, sizeof(*vk_regions));
    if(vk_regions == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < region_count; i++)
    {
        const PBufferImageCopy* r = &regions[i];
        vk_regions[i]             = (VkBufferImageCopy) {
            .bufferOffset                    = (VkDeviceSize) r->buffer_offset,
            .bufferRowLength                 = r->buffer_row_length,
            .bufferImageHeight               = r->buffer_image_height,
            .imageSubresource.aspectMask     = dst->aspect,
            .imageSubresource.mipLevel       = r->mip_level,
            .imageSubresource.baseArrayLayer = r->base_array_layer,
            .imageSubresource.layerCount     = r->layer_count,
            .imageOffset                     = {r->offset_x, r->offset_y, r->offset_z},
            .imageExtent                     = {r->extent_w, r->extent_h, r->extent_d},
        };
    }

    vkCmdCopyBufferToImage(cmd->buffer, src->buffer, dst->image, image_layout_to_vk(dst_layout), region_count, vk_regions);

    free(vk_regions);
}

void pigment_cmd_generate_mipmaps(Pigment* pigment, PCommandBuffer* cmd, PImage* image, uint32_t base_layer, uint32_t layer_count, PImageLayout final_layout)
{
    if(pigment == NULL || cmd == NULL || image == NULL || image->mip_levels == 0 || layer_count == 0)
    {
        return;
    }

    VkImageLayout vk_layout = image_layout_to_vk(final_layout);

    VkImageMemoryBarrier2 barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .image               = image->image,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange    = {image->aspect, 0, 1, base_layer, layer_count},
    };

    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier,
    };

    int32_t mip_width  = (int32_t) image->width;
    int32_t mip_height = (int32_t) image->height;

    for(uint32_t i = 1; i < image->mip_levels; i++)
    {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcStageMask                  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask                 = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask                  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask                 = VK_ACCESS_2_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier2(cmd->buffer, &dep);

        VkImageBlit blit = {
            .srcSubresource.aspectMask     = image->aspect,
            .srcSubresource.mipLevel       = i - 1,
            .srcSubresource.baseArrayLayer = base_layer,
            .srcSubresource.layerCount     = layer_count,
            .srcOffsets                    = {{0, 0, 0},                                                  {mip_width, mip_height, 1}},
            .dstSubresource.aspectMask     = image->aspect,
            .dstSubresource.mipLevel       = i,
            .dstSubresource.baseArrayLayer = base_layer,
            .dstSubresource.layerCount     = layer_count,
            .dstOffsets                    = {{0, 0, 0}, {mip_width > 1 ? mip_width / 2 : 1, mip_height > 1 ? mip_height / 2 : 1, 1}},
        };

        vkCmdBlitImage(cmd->buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout     = vk_layout;
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

        vkCmdPipelineBarrier2(cmd->buffer, &dep);

        if(mip_width > 1)
        {
            mip_width /= 2;
        }
        if(mip_height > 1)
        {
            mip_height /= 2;
        }
    }

    barrier.subresourceRange.baseMipLevel = image->mip_levels - 1;
    barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout                     = vk_layout;
    barrier.srcStageMask                  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask                 = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask                  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask                 = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    vkCmdPipelineBarrier2(cmd->buffer, &dep);
}

static int allocate_resources(Pigment* pigment, PImage* image, uint32_t width, uint32_t height);
static void free_resources(Pigment* pigment, PImage* image);
static int tracked_reserve(PTrackedImageList* list, uint32_t additional);
static int find_tracked(PTrackedImageList* list, PImage* image);

static void translate_image_type(PImageType type, VkImageType* out_image_type, VkImageViewType* out_view_type, VkImageCreateFlags* out_flags)
{
    *out_flags = 0;
    switch(type)
    {
        case P_IMAGE_TYPE_2D:
            *out_image_type = VK_IMAGE_TYPE_2D;
            *out_view_type  = VK_IMAGE_VIEW_TYPE_2D;
            break;
        case P_IMAGE_TYPE_2D_ARRAY:
            *out_image_type = VK_IMAGE_TYPE_2D;
            *out_view_type  = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            break;
        case P_IMAGE_TYPE_CUBE:
            *out_image_type = VK_IMAGE_TYPE_2D;
            *out_view_type  = VK_IMAGE_VIEW_TYPE_CUBE;
            *out_flags      = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            break;
        case P_IMAGE_TYPE_CUBE_ARRAY:
            *out_image_type = VK_IMAGE_TYPE_2D;
            *out_view_type  = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            *out_flags      = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            break;
        case P_IMAGE_TYPE_3D:
            *out_image_type = VK_IMAGE_TYPE_3D;
            *out_view_type  = VK_IMAGE_VIEW_TYPE_3D;
            break;
    }
}

PImage* pigment_create_image(Pigment* pigment, const PImageDesc* desc)
{
    if(pigment == NULL || desc == NULL || desc->width == 0 || desc->height == 0)
    {
        return NULL;
    }

    PImage* image = calloc(1, sizeof(*image));
    if(image == NULL)
    {
        return NULL;
    }

    image->vk_format    = (VkFormat) desc->format;
    image->vk_usage     = translate_usage(desc->usage);
    image->vk_samples   = (desc->samples == 0) ? VK_SAMPLE_COUNT_1_BIT : (VkSampleCountFlagBits) desc->samples;
    image->mip_levels   = (desc->mip_levels == 0) ? 1 : desc->mip_levels;
    image->aspect       = compute_aspect(image->vk_format, desc->usage);
    image->depth        = (desc->depth == 0) ? 1 : desc->depth;
    image->array_layers = (desc->array_layers == 0) ? 1 : desc->array_layers;
    translate_image_type(desc->type, &image->vk_image_type, &image->vk_view_type, &image->vk_create_flags);

    if(allocate_resources(pigment, image, desc->width, desc->height) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create image (%ux%u, format=%d)", desc->width, desc->height, desc->format);
        free(image);
        return NULL;
    }

    return image;
}

void pigment_destroy_image(Pigment* pigment, PImage* image)
{
    if(pigment == NULL || image == NULL)
    {
        return;
    }
    pigment_image_untrack(pigment, image);
    free_resources(pigment, image);
    free(image);
}

void pigment_image_resize(Pigment* pigment, PImage* image, uint32_t width, uint32_t height)
{
    if(pigment == NULL || image == NULL || width == 0 || height == 0)
    {
        return;
    }
    if(image->width == width && image->height == height)
    {
        return;
    }

    vkDeviceWaitIdle(pigment->device->logical_device);
    free_resources(pigment, image);

    if(allocate_resources(pigment, image, width, height) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to resize image to %ux%u", width, height);
    }
}

uint32_t pigment_image_width(PImage* image)
{
    return (image != NULL) ? image->width : 0;
}

uint32_t pigment_image_height(PImage* image)
{
    return (image != NULL) ? image->height : 0;
}

#define PIGMENT_TRACKED_INITIAL_CAPACITY 4

PTrackedImageList* create_tracked_image_list(void)
{
    return calloc(1, sizeof(PTrackedImageList));
}

void destroy_tracked_image_list(PTrackedImageList* list)
{
    if(list == NULL)
    {
        return;
    }

    free(list->tracked_images);
    free(list);
}

void pigment_image_track_swapchain(Pigment* pigment, PImage* image, uint32_t window_index, float scale)
{
    if(pigment == NULL || image == NULL || window_index >= pigment->window_count || scale <= 0.0f)
    {
        return;
    }

    PTrackedImageList* list = pigment->tracked_images;

    int idx = find_tracked(list, image);
    if(idx >= 0)
    {
        list->tracked_images[idx].window_index = window_index;
        list->tracked_images[idx].scale        = scale;
    }
    else
    {
        if(tracked_reserve(list, 1) != PIGMENT_SUCCESS)
        {
            PLOG_ERROR(pigment, "Failed to reserve tracked image slot");
            return;
        }
        list->tracked_images[list->count++] = (PTrackedImage) {
            .image        = image,
            .window_index = window_index,
            .scale        = scale,
        };
    }

    PSwapchain* swapchain = pigment->renderers[window_index]->swapchain;
    pigment_image_resize(pigment, image, (uint32_t) ((float) swapchain->extent.width * scale), (uint32_t) ((float) swapchain->extent.height * scale));
}

void pigment_image_untrack(Pigment* pigment, PImage* image)
{
    if(pigment == NULL || image == NULL)
    {
        return;
    }
    PTrackedImageList* list = pigment->tracked_images;

    int idx = find_tracked(list, image);
    if(idx < 0)
    {
        return;
    }

    list->tracked_images[idx] = list->tracked_images[--list->count];
}

void pigment_image_resize_tracked(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return;
    }

    PTrackedImageList* list = pigment->tracked_images;
    PSwapchain* swapchain   = pigment->renderers[window_index]->swapchain;
    for(uint32_t i = 0; i < list->count; i++)
    {
        PTrackedImage* t = &list->tracked_images[i];
        if(t->window_index != window_index)
        {
            continue;
        }

        uint32_t new_w = (uint32_t) ((float) swapchain->extent.width * t->scale);
        uint32_t new_h = (uint32_t) ((float) swapchain->extent.height * t->scale);

        pigment_image_resize(pigment, t->image, new_w, new_h);
    }
}

int create_vk_image(Pigment* pigment, VkImage* image, PVkAllocation** allocation, VkImageType image_type, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip_levels, uint32_t array_layers, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkMemoryPropertyFlags properties)
{
    VkImageCreateInfo image_create_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = image_type,
        .extent.width  = width,
        .extent.height = height,
        .extent.depth  = depth,
        .mipLevels     = mip_levels,
        .arrayLayers   = array_layers,
        .format        = format,
        .tiling        = tiling,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage         = usage,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .flags         = flags,
    };

    PVkAllocator* alloc = pigment->allocator;

    VkResult result = alloc->create_image(alloc->user_data, &image_create_info, properties, image, allocation);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create image (result: %d)", result);
        return PIGMENT_ERROR;
    }

    return PIGMENT_SUCCESS;
}

VkImageView create_image_view(Pigment* pigment, VkImage image, VkImageViewType view_type, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels, uint32_t array_layers)
{
    VkDevice device = pigment->device->logical_device;
    VkImageView image_view;

    VkImageViewCreateInfo view_create_info = {
        .sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image                           = image,
        .viewType                        = view_type,
        .format                          = format,
        .subresourceRange.aspectMask     = aspect_flags,
        .subresourceRange.baseMipLevel   = 0,
        .subresourceRange.levelCount     = mip_levels,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount     = array_layers,
    };

    VkResult result;
    if((result = vkCreateImageView(device, &view_create_info, NULL, &image_view)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create image view! (result: %d)", result);
        return NULL;
    }

    return image_view;
}

static VkImageUsageFlags translate_usage(PImageUsage usage)
{
    VkImageUsageFlags out = 0;
    if(usage & P_IMAGE_USAGE_SAMPLED)
    {
        out |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if(usage & P_IMAGE_USAGE_RENDER_COLOR)
    {
        out |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if(usage & P_IMAGE_USAGE_RENDER_DEPTH)
    {
        out |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if(usage & P_IMAGE_USAGE_STORAGE)
    {
        out |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if(usage & P_IMAGE_USAGE_TRANSFER_SRC)
    {
        out |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if(usage & P_IMAGE_USAGE_TRANSFER_DST)
    {
        out |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    return out;
}

static VkImageAspectFlags compute_aspect(VkFormat format, PImageUsage usage)
{
    if(usage & P_IMAGE_USAGE_RENDER_DEPTH)
    {
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if(format == VK_FORMAT_D32_SFLOAT_S8_UINT
           || format == VK_FORMAT_D24_UNORM_S8_UINT
           || format == VK_FORMAT_D16_UNORM_S8_UINT)
        {
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        return aspect;
    }

    return VK_IMAGE_ASPECT_COLOR_BIT;
}

static int allocate_resources(Pigment* pigment, PImage* image, uint32_t width, uint32_t height)
{
    if(create_vk_image(pigment, &image->image, &image->image_allocation, image->vk_image_type, width, height, image->depth, image->mip_levels, image->array_layers, image->vk_format, VK_IMAGE_TILING_OPTIMAL, image->vk_usage, image->vk_create_flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != PIGMENT_SUCCESS)
    {
        return PIGMENT_ERROR;
    }

    VkImageAspectFlags view_aspect = (image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    image->image_view = create_image_view(pigment, image->image, image->vk_view_type, image->vk_format, view_aspect, image->mip_levels, image->array_layers);
    if(image->image_view == NULL)
    {
        pigment->allocator->destroy_image(pigment->allocator->user_data, image->image, image->image_allocation);
        image->image            = VK_NULL_HANDLE;
        image->image_allocation = NULL;
        return PIGMENT_ERROR;
    }

    image->width  = width;
    image->height = height;

    return PIGMENT_SUCCESS;
}

static void free_resources(Pigment* pigment, PImage* image)
{
    PVkAllocator* alloc = pigment->allocator;

    if(image->image_view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(pigment->device->logical_device, image->image_view, NULL);
        image->image_view = VK_NULL_HANDLE;
    }

    if(image->image != VK_NULL_HANDLE)
    {
        alloc->destroy_image(alloc->user_data, image->image, image->image_allocation);
        image->image            = VK_NULL_HANDLE;
        image->image_allocation = NULL;
    }
}

static int tracked_reserve(PTrackedImageList* list, uint32_t additional)
{
    uint32_t needed = list->count + additional;
    if(needed <= list->capacity)
    {
        return PIGMENT_SUCCESS;
    }

    uint32_t new_capacity = (list->capacity == 0) ? PIGMENT_TRACKED_INITIAL_CAPACITY : list->capacity;
    while(new_capacity < needed)
    {
        new_capacity *= 2;
    }

    PTrackedImage* new_ptr = realloc(list->tracked_images, new_capacity * sizeof(*new_ptr));
    if(new_ptr == NULL)
    {
        return PIGMENT_ERROR;
    }

    list->tracked_images = new_ptr;
    list->capacity       = new_capacity;

    return PIGMENT_SUCCESS;
}

static int find_tracked(PTrackedImageList* list, PImage* image)
{
    for(uint32_t i = 0; i < list->count; i++)
    {
        if(list->tracked_images[i].image == image)
        {
            return (int) i;
        }
    }

    return -1;
}
