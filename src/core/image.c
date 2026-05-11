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
#include "commands.h"
#include "deletion.h"
#include "internal.h"
#include "log_internal.h"

#include <math.h>
#include <stdlib.h>

#define PIGMENT_VIEW_CACHE_INITIAL_CAPACITY 4

static void destroy_image_immediate(Pigment* pigment, void* resource);
static void destroy_image_resources(Pigment* pigment, void* resource);
static void translate_image_type(PImageType type, VkImageType* out_image_type, VkImageCreateFlags* out_flags);
static VkImageUsageFlags translate_usage(PImageUsage usage);
static VkImageAspectFlags compute_aspect(VkFormat format, PImageUsage usage);
static PResult allocate_resources(Pigment* pigment, PImage* image, uint32_t width, uint32_t height);
static void free_resources(Pigment* pigment, PImage* image);
static PImageViewType derive_view_type(PImage* image, uint32_t layer_count);
static VkImageAspectFlags aspect_to_vk(PImageAspect aspect, PImage* image);
static VkImageViewType view_type_to_vk(PImageViewType type);
static const char* resolve_view_name(const PImageViewDesc* view_desc, const PImage* image, char* buffer, size_t buffer_size);

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

typedef struct PImageResources {
    VkImage image;
    PVkAllocation* allocation;
    PImageViewCache view_cache;
} PImageResources;

PBool pigment_format_supports_linear_blit(Pigment* pigment, PFormat format)
{
    if(pigment == NULL || pigment->device == NULL)
    {
        return P_FALSE;
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
    pigment_cmd_use_buffer(pigment, cmd, src);
    pigment_cmd_use_image(pigment, cmd, dst);

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

void pigment_cmd_copy_image_to_buffer(Pigment* pigment, PCommandBuffer* cmd, PImage* src, PImageLayout src_layout, PBuffer* dst, const PBufferImageCopy* regions, uint32_t region_count)
{
    if(pigment == NULL || cmd == NULL || src == NULL || dst == NULL || regions == NULL || region_count == 0)
    {
        return;
    }
    pigment_cmd_use_image(pigment, cmd, src);
    pigment_cmd_use_buffer(pigment, cmd, dst);

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
            .imageSubresource.aspectMask     = src->aspect,
            .imageSubresource.mipLevel       = r->mip_level,
            .imageSubresource.baseArrayLayer = r->base_array_layer,
            .imageSubresource.layerCount     = r->layer_count,
            .imageOffset                     = {r->offset_x, r->offset_y, r->offset_z},
            .imageExtent                     = {r->extent_w, r->extent_h, r->extent_d},
        };
    }

    vkCmdCopyImageToBuffer(cmd->buffer, src->image, image_layout_to_vk(src_layout), dst->buffer, region_count, vk_regions);

    free(vk_regions);
}

void pigment_cmd_copy_image(Pigment* pigment, PCommandBuffer* cmd, PImage* src, PImageLayout src_layout, PImage* dst, PImageLayout dst_layout, const PImageCopy* regions, uint32_t region_count)
{
    if(pigment == NULL || cmd == NULL || src == NULL || dst == NULL || regions == NULL || region_count == 0)
    {
        return;
    }
    pigment_cmd_use_image(pigment, cmd, src);
    pigment_cmd_use_image(pigment, cmd, dst);

    VkImageCopy* vk_regions = calloc(region_count, sizeof(*vk_regions));
    if(vk_regions == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < region_count; i++)
    {
        const PImageCopy* r = &regions[i];
        vk_regions[i]       = (VkImageCopy) {
            .srcSubresource = {.aspectMask = src->aspect, .mipLevel = r->src_mip_level, .baseArrayLayer = r->src_base_array_layer, .layerCount = r->src_layer_count},
            .srcOffset      = {r->src_offset_x, r->src_offset_y, r->src_offset_z},
            .dstSubresource = {.aspectMask = dst->aspect, .mipLevel = r->dst_mip_level, .baseArrayLayer = r->dst_base_array_layer, .layerCount = r->dst_layer_count},
            .dstOffset      = {r->dst_offset_x, r->dst_offset_y, r->dst_offset_z},
            .extent         = {r->extent_w, r->extent_h, r->extent_d},
        };
    }

    vkCmdCopyImage(cmd->buffer, src->image, image_layout_to_vk(src_layout), dst->image, image_layout_to_vk(dst_layout), region_count, vk_regions);

    free(vk_regions);
}

void pigment_cmd_blit_image(Pigment* pigment, PCommandBuffer* cmd, PImage* src, PImageLayout src_layout, PImage* dst, PImageLayout dst_layout, const PImageBlit* regions, uint32_t region_count, PFilteringMode filter)
{
    if(pigment == NULL || cmd == NULL || src == NULL || dst == NULL || regions == NULL || region_count == 0)
    {
        return;
    }
    pigment_cmd_use_image(pigment, cmd, src);
    pigment_cmd_use_image(pigment, cmd, dst);

    VkImageBlit* vk_regions = calloc(region_count, sizeof(*vk_regions));
    if(vk_regions == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < region_count; i++)
    {
        const PImageBlit* r = &regions[i];
        vk_regions[i]       = (VkImageBlit) {
            .srcSubresource = {.aspectMask = src->aspect, .mipLevel = r->src_mip_level, .baseArrayLayer = r->src_base_array_layer, .layerCount = r->src_layer_count},
            .srcOffsets     = {{r->src_min_x, r->src_min_y, r->src_min_z}, {r->src_max_x, r->src_max_y, r->src_max_z}},
            .dstSubresource = {.aspectMask = dst->aspect, .mipLevel = r->dst_mip_level, .baseArrayLayer = r->dst_base_array_layer, .layerCount = r->dst_layer_count},
            .dstOffsets     = {{r->dst_min_x, r->dst_min_y, r->dst_min_z}, {r->dst_max_x, r->dst_max_y, r->dst_max_z}},
        };
    }

    VkFilter vk_filter = (filter == P_FILTERING_MODE_LINEAR) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    vkCmdBlitImage(cmd->buffer, src->image, image_layout_to_vk(src_layout), dst->image, image_layout_to_vk(dst_layout), region_count, vk_regions, vk_filter);

    free(vk_regions);
}

void pigment_cmd_generate_mipmaps(Pigment* pigment, PCommandBuffer* cmd, PImage* image, uint32_t base_layer, uint32_t layer_count, PImageLayout final_layout)
{
    if(pigment == NULL || cmd == NULL || image == NULL || image->mip_levels == 0 || layer_count == 0)
    {
        return;
    }

    int32_t mip_width  = (int32_t) image->width;
    int32_t mip_height = (int32_t) image->height;

    for(uint32_t i = 1; i < image->mip_levels; i++)
    {
        PImageBarrier prepare_src = {
            .image       = image,
            .old_layout  = P_IMAGE_LAYOUT_TRANSFER_DST,
            .new_layout  = P_IMAGE_LAYOUT_TRANSFER_SRC,
            .src         = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
            .dst         = {P_PIPELINE_STAGE_TRANSFER_BIT,  P_MEMORY_ACCESS_TRANSFER_READ_BIT},
            .base_mip    = i - 1,
            .mip_count   = 1,
            .base_layer  = base_layer,
            .layer_count = layer_count,
        };
        pigment_cmd_image_barriers(pigment, cmd, &prepare_src, 1);

        PImageBlit blit = {
            .src_mip_level        = i - 1,
            .src_base_array_layer = base_layer,
            .src_layer_count      = layer_count,
            .src_max_x            = mip_width,
            .src_max_y            = mip_height,
            .src_max_z            = 1,
            .dst_mip_level        = i,
            .dst_base_array_layer = base_layer,
            .dst_layer_count      = layer_count,
            .dst_max_x            = mip_width > 1 ? mip_width / 2 : 1,
            .dst_max_y            = mip_height > 1 ? mip_height / 2 : 1,
            .dst_max_z            = 1,
        };
        pigment_cmd_blit_image(pigment, cmd, image, P_IMAGE_LAYOUT_TRANSFER_SRC, image, P_IMAGE_LAYOUT_TRANSFER_DST, &blit, 1, P_FILTERING_MODE_LINEAR);

        PImageBarrier finalize_src = {
            .image       = image,
            .old_layout  = P_IMAGE_LAYOUT_TRANSFER_SRC,
            .new_layout  = final_layout,
            .src         = {       P_PIPELINE_STAGE_TRANSFER_BIT,       P_MEMORY_ACCESS_TRANSFER_READ_BIT},
            .dst         = {P_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, P_MEMORY_ACCESS_SHADER_SAMPLED_READ_BIT},
            .base_mip    = i - 1,
            .mip_count   = 1,
            .base_layer  = base_layer,
            .layer_count = layer_count,
        };
        pigment_cmd_image_barriers(pigment, cmd, &finalize_src, 1);

        if(mip_width > 1)
        {
            mip_width /= 2;
        }
        if(mip_height > 1)
        {
            mip_height /= 2;
        }
    }

    PImageBarrier finalize_last = {
        .image       = image,
        .old_layout  = P_IMAGE_LAYOUT_TRANSFER_DST,
        .new_layout  = final_layout,
        .src         = {       P_PIPELINE_STAGE_TRANSFER_BIT,      P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
        .dst         = {P_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, P_MEMORY_ACCESS_SHADER_SAMPLED_READ_BIT},
        .base_mip    = image->mip_levels - 1,
        .mip_count   = 1,
        .base_layer  = base_layer,
        .layer_count = layer_count,
    };
    pigment_cmd_image_barriers(pigment, cmd, &finalize_last, 1);
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

    image->vk_format       = (VkFormat) desc->format;
    image->vk_usage        = translate_usage(desc->usage);
    image->vk_samples      = (desc->samples == 0) ? VK_SAMPLE_COUNT_1_BIT : (VkSampleCountFlagBits) desc->samples;
    image->vk_sharing_mode = (VkSharingMode) desc->sharing_mode;
    image->mip_levels      = (desc->mip_levels == 0) ? 1 : desc->mip_levels;
    image->aspect          = compute_aspect(image->vk_format, desc->usage);
    image->depth           = (desc->depth == 0) ? 1 : desc->depth;
    image->array_layers    = (desc->array_layers == 0) ? 1 : desc->array_layers;
    image->name            = desc->name;
    translate_image_type(desc->type, &image->vk_image_type, &image->vk_create_flags);

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

    pigment_defer_destroy_tracked(pigment, destroy_image_immediate, image, &image->tracker);
}

static void destroy_image_immediate(Pigment* pigment, void* resource)
{
    PImage* image = (PImage*) resource;
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

    PImageResources* old = malloc(sizeof(*old));
    if(old == NULL)
    {
        PLOG_ERROR(pigment, "Failed to allocate old resource bundle for resize, falling back to blocking path");
        device_wait_idle(pigment);
        free_resources(pigment, image);
        if(allocate_resources(pigment, image, width, height) != PIGMENT_SUCCESS)
        {
            PLOG_ERROR(pigment, "Failed to resize image to %ux%u", width, height);
        }
        return;
    }

    old->image      = image->image;
    old->allocation = image->image_allocation;
    old->view_cache = image->view_cache;

    image->image            = VK_NULL_HANDLE;
    image->image_allocation = NULL;
    image->view_cache       = (PImageViewCache) {0};

    if(allocate_resources(pigment, image, width, height) != PIGMENT_SUCCESS)
    {
        image->image            = old->image;
        image->image_allocation = old->allocation;
        image->view_cache       = old->view_cache;
        free(old);
        PLOG_ERROR(pigment, "Failed to resize image to %ux%u", width, height);
        return;
    }

    pigment_defer_destroy(pigment, destroy_image_resources, old);
}

static void destroy_image_resources(Pigment* pigment, void* resource)
{
    PImageResources* res = (PImageResources*) resource;
    PVkAllocator* alloc  = pigment->allocator;

    for(uint32_t i = 0; i < res->view_cache.count; i++)
    {
        vkDestroyImageView(pigment->device->logical_device, res->view_cache.views[i]->view, NULL);
        free(res->view_cache.views[i]);
    }
    free(res->view_cache.views);

    if(res->image != VK_NULL_HANDLE)
    {
        alloc->destroy_image(alloc->user_data, res->image, res->allocation);
    }
    free(res);
}

uint32_t pigment_image_width(PImage* image)
{
    return (image != NULL) ? image->width : 0;
}

uint32_t pigment_image_height(PImage* image)
{
    return (image != NULL) ? image->height : 0;
}

PResult create_vk_image(Pigment* pigment, PImage* image, uint32_t width, uint32_t height, VkImageTiling tiling, const PVkAllocationCreateInfo* alloc_info)
{
    VkImageCreateInfo image_create_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = image->vk_image_type,
        .extent.width  = width,
        .extent.height = height,
        .extent.depth  = image->depth,
        .mipLevels     = image->mip_levels,
        .arrayLayers   = image->array_layers,
        .format        = image->vk_format,
        .tiling        = tiling,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage         = image->vk_usage,
        .samples       = image->vk_samples ? image->vk_samples : VK_SAMPLE_COUNT_1_BIT,
        .sharingMode   = image->vk_sharing_mode,
        .flags         = image->vk_create_flags,
    };

    PVkAllocator* alloc = pigment->allocator;

    VkResult result = alloc->create_image(alloc->user_data, &image_create_info, alloc_info, &image->image, &image->image_allocation);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create image (result: %d)", result);
        return PIGMENT_ERROR_VULKAN;
    }

    return PIGMENT_SUCCESS;
}

VkImageView create_image_view(Pigment* pigment, VkImage image, VkImageViewType view_type, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t base_mip, uint32_t mip_count, uint32_t base_layer, uint32_t layer_count)
{
    VkDevice device = pigment->device->logical_device;
    VkImageView image_view;

    VkImageViewCreateInfo view_create_info = {
        .sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image                           = image,
        .viewType                        = view_type,
        .format                          = format,
        .subresourceRange.aspectMask     = aspect_flags,
        .subresourceRange.baseMipLevel   = base_mip,
        .subresourceRange.levelCount     = mip_count,
        .subresourceRange.baseArrayLayer = base_layer,
        .subresourceRange.layerCount     = layer_count,
    };

    VkResult result;
    if((result = vkCreateImageView(device, &view_create_info, NULL, &image_view)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create image view! (result: %d)", result);
        return NULL;
    }

    return image_view;
}

PImageView* image_get_or_create_view(Pigment* pigment, PImage* image, const PImageViewDesc* desc)
{
    if(image == NULL || desc == NULL)
    {
        return NULL;
    }

    PImageViewDesc view_desc = *desc;
    if(view_desc.format == P_FORMAT_UNDEFINED)
    {
        view_desc.format = (PFormat) image->vk_format;
    }
    if(view_desc.aspect == P_IMAGE_ASPECT_INHERIT)
    {
        view_desc.aspect = (PImageAspect) image->aspect;
    }
    if(view_desc.layer_count == 0)
    {
        view_desc.layer_count = image->array_layers - view_desc.base_layer;
    }
    if(view_desc.mip_count == 0)
    {
        view_desc.mip_count = image->mip_levels - view_desc.base_mip;
    }
    if(view_desc.view_type == P_IMAGE_VIEW_TYPE_AUTO)
    {
        view_desc.view_type = derive_view_type(image, view_desc.layer_count);
    }

    PImageViewCache* cache = &image->view_cache;
    for(uint32_t i = 0; i < cache->count; i++)
    {
        const PImageViewDesc* key = &cache->views[i]->desc;
        if(key->format == view_desc.format
           && key->aspect == view_desc.aspect
           && key->view_type == view_desc.view_type
           && key->base_layer == view_desc.base_layer
           && key->layer_count == view_desc.layer_count
           && key->base_mip == view_desc.base_mip
           && key->mip_count == view_desc.mip_count)
        {
            return cache->views[i];
        }
    }

    VkImageAspectFlags vk_aspect = aspect_to_vk(view_desc.aspect, image);
    VkImageViewType vk_view_type = view_type_to_vk(view_desc.view_type);
    VkFormat vk_format           = (VkFormat) view_desc.format;

    VkImageView new_handle = create_image_view(pigment, image->image, vk_view_type, vk_format, vk_aspect, view_desc.base_mip, view_desc.mip_count, view_desc.base_layer, view_desc.layer_count);
    if(new_handle == VK_NULL_HANDLE)
    {
        return NULL;
    }

    if(cache->count >= cache->capacity)
    {
        uint32_t new_capacity = cache->capacity == 0 ? PIGMENT_VIEW_CACHE_INITIAL_CAPACITY : cache->capacity * 2;
        PImageView** new_ptr  = realloc(cache->views, new_capacity * sizeof(*new_ptr));
        if(new_ptr == NULL)
        {
            vkDestroyImageView(pigment->device->logical_device, new_handle, NULL);
            return NULL;
        }
        cache->views    = new_ptr;
        cache->capacity = new_capacity;
    }

    PImageView* view = malloc(sizeof(*view));
    if(view == NULL)
    {
        vkDestroyImageView(pigment->device->logical_device, new_handle, NULL);
        return NULL;
    }

    view->desc = view_desc;
    view->view = new_handle;

    char auto_name[160];
    set_object_name(pigment->device->logical_device, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t) new_handle, resolve_view_name(&view_desc, image, auto_name, sizeof(auto_name)));

    cache->views[cache->count++] = view;
    return view;
}

void image_destroy_view_cache(Pigment* pigment, PImage* image)
{
    if(image == NULL || image->view_cache.views == NULL)
    {
        return;
    }
    PImageViewCache* cache = &image->view_cache;

    for(uint32_t i = 0; i < cache->count; i++)
    {
        vkDestroyImageView(pigment->device->logical_device, cache->views[i]->view, NULL);
        free(cache->views[i]);
    }

    free(cache->views);
    *cache = (PImageViewCache) {0};
}

static void translate_image_type(PImageType type, VkImageType* out_image_type, VkImageCreateFlags* out_flags)
{
    *out_flags = 0;
    switch(type)
    {
        case P_IMAGE_TYPE_2D:
        case P_IMAGE_TYPE_2D_ARRAY:
            *out_image_type = VK_IMAGE_TYPE_2D;
            break;
        case P_IMAGE_TYPE_CUBE:
        case P_IMAGE_TYPE_CUBE_ARRAY:
            *out_image_type = VK_IMAGE_TYPE_2D;
            *out_flags      = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            break;
        case P_IMAGE_TYPE_3D:
            *out_image_type = VK_IMAGE_TYPE_3D;
            break;
    }
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
        if(format == VK_FORMAT_S8_UINT)
        {
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        }

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

static PResult allocate_resources(Pigment* pigment, PImage* image, uint32_t width, uint32_t height)
{
    PVkAllocationCreateInfo alloc_info = {
        .required_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .debug_name     = image->name,
    };

    if(create_vk_image(pigment, image, width, height, VK_IMAGE_TILING_OPTIMAL, &alloc_info) != PIGMENT_SUCCESS)
    {
        return PIGMENT_ERROR_VULKAN;
    }

    image->width  = width;
    image->height = height;

    // INHERIT format/aspect from image, AUTO view_type, 0 layers/mips -> all
    PImageViewDesc full_view_desc = {0};

    if(image_get_or_create_view(pigment, image, &full_view_desc) == NULL)
    {
        pigment->allocator->destroy_image(pigment->allocator->user_data, image->image, image->image_allocation);
        image->image            = VK_NULL_HANDLE;
        image->image_allocation = NULL;
        return PIGMENT_ERROR_VULKAN;
    }

    return PIGMENT_SUCCESS;
}

static void free_resources(Pigment* pigment, PImage* image)
{
    PVkAllocator* alloc = pigment->allocator;

    image_destroy_view_cache(pigment, image);

    if(image->image != VK_NULL_HANDLE)
    {
        alloc->destroy_image(alloc->user_data, image->image, image->image_allocation);
        image->image            = VK_NULL_HANDLE;
        image->image_allocation = NULL;
    }
}

static PImageViewType derive_view_type(PImage* image, uint32_t layer_count)
{
    if(image->vk_image_type == VK_IMAGE_TYPE_3D)
    {
        return P_IMAGE_VIEW_TYPE_3D;
    }

    PBool cube = (image->vk_create_flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0;
    if(layer_count == 1)
    {
        return P_IMAGE_VIEW_TYPE_2D;
    }

    if(cube && layer_count == 6)
    {
        return P_IMAGE_VIEW_TYPE_CUBE;
    }

    if(cube && layer_count > 6 && (layer_count % 6) == 0)
    {
        return P_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }

    return P_IMAGE_VIEW_TYPE_2D_ARRAY;
}

static VkImageAspectFlags aspect_to_vk(PImageAspect aspect, PImage* image)
{
    switch(aspect)
    {
        case P_IMAGE_ASPECT_INHERIT:
            return image->aspect;
        case P_IMAGE_ASPECT_COLOR:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        case P_IMAGE_ASPECT_DEPTH:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case P_IMAGE_ASPECT_STENCIL:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        case P_IMAGE_ASPECT_DEPTH_STENCIL:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    return image->aspect;
}

static VkImageViewType view_type_to_vk(PImageViewType type)
{
    switch(type)
    {
        case P_IMAGE_VIEW_TYPE_2D:
            return VK_IMAGE_VIEW_TYPE_2D;
        case P_IMAGE_VIEW_TYPE_2D_ARRAY:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case P_IMAGE_VIEW_TYPE_CUBE:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case P_IMAGE_VIEW_TYPE_CUBE_ARRAY:
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        case P_IMAGE_VIEW_TYPE_3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        default:
            return VK_IMAGE_VIEW_TYPE_2D;
    }
}

static const char* resolve_view_name(const PImageViewDesc* view_desc, const PImage* image, char* buffer, size_t buffer_size)
{
    if(view_desc->name != NULL)
    {
        return view_desc->name;
    }
    if(image->name == NULL)
    {
        return NULL;
    }

    char mip_part[32]   = {0};
    char layer_part[32] = {0};

    if(view_desc->base_mip != 0 || view_desc->mip_count != image->mip_levels)
    {
        if(view_desc->mip_count == 1)
        {
            snprintf(mip_part, sizeof(mip_part), "_mip%u", view_desc->base_mip);
        }
        else
        {
            snprintf(mip_part, sizeof(mip_part), "_mip%u-%u", view_desc->base_mip, view_desc->base_mip + view_desc->mip_count - 1);
        }
    }

    if(view_desc->base_layer != 0 || view_desc->layer_count != image->array_layers)
    {
        if(view_desc->layer_count == 1)
        {
            snprintf(layer_part, sizeof(layer_part), "_layer%u", view_desc->base_layer);
        }
        else
        {
            snprintf(layer_part, sizeof(layer_part), "_layer%u-%u", view_desc->base_layer, view_desc->base_layer + view_desc->layer_count - 1);
        }
    }

    snprintf(buffer, buffer_size, "%s:view%s%s", image->name, mip_part, layer_part);
    return buffer;
}
