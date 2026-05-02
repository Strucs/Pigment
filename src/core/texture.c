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

#include "texture.h"
#include "buffers.h"
#include "image.h"
#include "internal.h"
#include "log_internal.h"

#include <math.h>

static int image_list_append(PImageList* image_list, PImage* image);
static int create_sampler(Pigment* pigment, PSampler* sampler, PSamplerDesc* desc);
static void cmd_transition_image_layout(Pigment* pigment, VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels);
static void cmd_copy_buffer_to_image(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
static void cmd_generate_mipmaps(VkCommandBuffer cmd, VkImage image, int32_t image_width, int32_t image_height, uint32_t mip_levels);
static int prepare_image_upload(Pigment* pigment, PImage** out_image, PBuffer** out_staging, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format);
static int batch_record_uploads(Pigment* pigment, VkCommandBuffer cmd, PImage** out_images, PBuffer** stagings, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count);
static uint32_t batch_append_images(PImageList* list, PImage** images, uint32_t count);
static void batch_write_descriptors(Pigment* pigment, uint32_t start_slot, uint32_t count);

static uint32_t pformat_pixel_size(PFormat format)
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

static inline int imax(int a, int b)
{
    return a > b ? a : b;
}

static int image_list_append(PImageList* image_list, PImage* image)
{
    if(image_list->count >= image_list->capacity)
    {
        uint32_t new_capacity = image_list->capacity * 2;
        PImage** new_ptr      = realloc(image_list->images, new_capacity * sizeof(*new_ptr));

        if(new_ptr == NULL)
        {
            return PIGMENT_ERROR;
        }

        image_list->images   = new_ptr;
        image_list->capacity = new_capacity;
    }

    image_list->images[image_list->count] = image;
    image_list->count++;

    return PIGMENT_SUCCESS;
}

PImageList* create_images(void)
{
    PImageList* image_list = calloc(1, sizeof(*image_list));
    if(image_list == NULL)
    {
        goto ERROR;
    }

    image_list->images = calloc(1, sizeof(*image_list->images));
    if(image_list->images == NULL)
    {
        goto ERROR;
    }

    image_list->capacity = 1;

    return image_list;

ERROR:
    free(image_list);
    return NULL;
}

void destroy_images(Pigment* pigment, PImageList* image_list)
{
    if(image_list == NULL)
    {
        return;
    }

    for(size_t i = 0; i < image_list->count; i++)
    {
        pigment_destroy_image(pigment, image_list->images[i]);
    }

    free(image_list->images);
    free(image_list);
}

uint32_t pigment_upload_image(Pigment* pigment, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format)
{
    if(pigment == NULL || pixels == NULL || width == 0 || height == 0)
    {
        return 0;
    }

    PCommandPool* pool = pigment_default_pool(pigment);
    if(pool == NULL)
    {
        return 0;
    }

    PImageList* images = pigment->images;
    uint32_t slot      = images->count;

    if(add_image_from_pixels(pigment, images, pixels, width, height, format, pool) != PIGMENT_SUCCESS)
    {
        return 0;
    }

    VkDescriptorImageInfo image_info = {
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView   = images->images[slot]->image_view,
    };

    for(uint32_t i = 0; i < pigment->config.max_frames_in_flight; i++)
    {
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = pigment->descriptor->descriptor_sets[i],
            .dstBinding      = 2,
            .dstArrayElement = slot,
            .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1,
            .pImageInfo      = &image_info,
        };
        vkUpdateDescriptorSets(pigment->device->logical_device, 1, &write, 0, NULL);
    }

    return slot;
}

uint32_t pigment_upload_image_batch(Pigment* pigment, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count)
{
    if(pigment == NULL || pixels == NULL || count == 0)
    {
        return 0;
    }

    PCommandPool* pool = pigment_default_pool(pigment);
    if(pool == NULL)
    {
        return 0;
    }

    PBuffer** stagings  = calloc(count, sizeof(*stagings));
    PImage** new_images = calloc(count, sizeof(*new_images));
    uint32_t start_slot = 0;

    if(stagings == NULL || new_images == NULL)
    {
        goto FREE;
    }

    VkCommandPool vk_pool = pool->pool;
    VkCommandBuffer cmd   = start_single_usage_commands(pigment, vk_pool);
    int result            = batch_record_uploads(pigment, cmd, new_images, stagings, pixels, widths, heights, formats, count);
    end_single_usage_commands(pigment, &cmd, vk_pool);

    if(result == PIGMENT_SUCCESS)
    {
        start_slot = batch_append_images(pigment->images, new_images, count);
        batch_write_descriptors(pigment, start_slot, count);
    }
    else
    {
        for(uint32_t i = 0; i < count; i++)
        {
            pigment_destroy_image(pigment, new_images[i]);
        }
    }

FREE:
    if(stagings != NULL)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            pigment_destroy_buffer(pigment, stagings[i]);
        }
    }
    free(stagings);
    free(new_images);
    return start_slot;
}

int add_image_from_pixels(Pigment* pigment, PImageList* image_list, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format, PCommandPool* pool)
{
    PBuffer* staging = NULL;
    PImage* image    = NULL;

    if(pool == NULL)
    {
        goto ERROR;
    }

    if(prepare_image_upload(pigment, &image, &staging, pixels, width, height, format) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    VkCommandBuffer cmd = start_single_usage_commands(pigment, pool->pool);
    cmd_transition_image_layout(pigment, cmd, image->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, image->mip_levels);
    cmd_copy_buffer_to_image(cmd, staging->buffer, image->image, width, height);
    cmd_generate_mipmaps(cmd, image->image, (int32_t) width, (int32_t) height, image->mip_levels);
    end_single_usage_commands(pigment, &cmd, pool->pool);

    pigment_destroy_buffer(pigment, staging);

    if(image_list_append(image_list, image) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    return PIGMENT_SUCCESS;

ERROR:
    pigment_destroy_buffer(pigment, staging);
    pigment_destroy_image(pigment, image);
    PLOG_ERROR(pigment, "Failed to add image from pixels.");
    return PIGMENT_ERROR;
}

int add_default_image(Pigment* pigment, PImageList* image_list, PCommandPool* pool)
{
    unsigned char white[] = {255, 255, 255, 255};
    return add_image_from_pixels(pigment, image_list, white, 1, 1, P_FORMAT_R8G8B8A8_UNORM, pool);
}

static int create_sampler(Pigment* pigment, PSampler* sampler, PSamplerDesc* desc)
{
    PDevice* device                       = pigment->device;
    VkPhysicalDeviceProperties properties = {0};
    vkGetPhysicalDeviceProperties(device->physical_device, &properties);

    VkSamplerAddressMode vk_address_mode = (VkSamplerAddressMode) desc->address_mode;

    VkSamplerCreateInfo sampler_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter               = (VkFilter) desc->mag_filter,
        .minFilter               = (VkFilter) desc->min_filter,
        .addressModeU            = vk_address_mode,
        .addressModeV            = vk_address_mode,
        .addressModeW            = vk_address_mode,
        .anisotropyEnable        = VK_TRUE,
        .maxAnisotropy           = properties.limits.maxSamplerAnisotropy,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
        .compareEnable           = VK_FALSE,
        .compareOp               = VK_COMPARE_OP_ALWAYS,
        .mipmapMode              = (VkSamplerMipmapMode) desc->mipmap_mode,
        .minLod                  = 0.0f,
        .maxLod                  = VK_LOD_CLAMP_NONE
    };

    VkResult result;
    if((result = vkCreateSampler(device->logical_device, &sampler_create_info, NULL, &sampler->sampler)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create image sampler! (result: %d)", result);
        return PIGMENT_ERROR;
    }

    return PIGMENT_SUCCESS;
}

uint32_t pigment_add_sampler(Pigment* pigment, PSamplerDesc* desc)
{
    if(pigment == NULL || desc == NULL)
    {
        return 0;
    }

    PSamplerList* samplers = pigment->samplers;

    for(uint32_t i = 0; i < samplers->count; i++)
    {
        // Check if a sampler with the same description already exists and return its slot if found
        if(samplers->descs[i].mag_filter == desc->mag_filter && samplers->descs[i].min_filter == desc->min_filter && samplers->descs[i].mipmap_mode == desc->mipmap_mode && samplers->descs[i].address_mode == desc->address_mode)
        {
            return i;
        }
    }

    uint32_t slot = samplers->count;

    if(slot >= samplers->capacity)
    {
        PLOG_ERROR(pigment, "Max samplers reached (%u)", samplers->capacity);
        return 0;
    }

    if(create_sampler(pigment, &samplers->samplers[slot], desc) != PIGMENT_SUCCESS)
    {
        return 0;
    }
    samplers->descs[slot] = *desc;
    samplers->count++;

    VkDescriptorImageInfo sampler_info = {
        .sampler = samplers->samplers[slot].sampler,
    };

    for(uint32_t i = 0; i < pigment->config.max_frames_in_flight; i++)
    {
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = pigment->descriptor->descriptor_sets[i],
            .dstBinding      = 1,
            .dstArrayElement = slot,
            .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1,
            .pImageInfo      = &sampler_info,
        };
        vkUpdateDescriptorSets(pigment->device->logical_device, 1, &write, 0, NULL);
    }

    return slot;
}

PSamplerList* create_samplers(Pigment* pigment, uint32_t max_samplers)
{
    PSamplerList* sampler_list = calloc(1, sizeof(*sampler_list));
    if(sampler_list == NULL)
    {
        goto ERROR;
    }

    sampler_list->samplers = malloc(max_samplers * sizeof(*sampler_list->samplers));
    sampler_list->descs    = malloc(max_samplers * sizeof(*sampler_list->descs));
    sampler_list->capacity = max_samplers;
    sampler_list->count    = 0;

    if(sampler_list->samplers == NULL || sampler_list->descs == NULL)
    {
        goto ERROR;
    }

    PSamplerDesc nearest_desc = {.mag_filter = NEAREST, .min_filter = NEAREST, .mipmap_mode = NEAREST, .address_mode = P_ADDRESS_MODE_REPEAT};
    create_sampler(pigment, &sampler_list->samplers[0], &nearest_desc);
    sampler_list->descs[0] = nearest_desc;
    sampler_list->count++;

    PSamplerDesc linear_desc = {.mag_filter = LINEAR, .min_filter = LINEAR, .mipmap_mode = LINEAR, .address_mode = P_ADDRESS_MODE_REPEAT};
    create_sampler(pigment, &sampler_list->samplers[1], &linear_desc);
    sampler_list->descs[1] = linear_desc;
    sampler_list->count++;

    return sampler_list;

ERROR:
    if(sampler_list != NULL)
    {
        free(sampler_list->samplers);
        free(sampler_list->descs);
    }
    free(sampler_list);
    return NULL;
}

void destroy_samplers(Pigment* pigment, PSamplerList* sampler_list)
{
    PDevice* device = pigment->device;
    if(sampler_list != NULL)
    {
        for(size_t i = 0; i < sampler_list->count; i++)
        {
            vkDestroySampler(device->logical_device, sampler_list->samplers[i].sampler, NULL);
        }
        free(sampler_list->samplers);
        free(sampler_list->descs);
        free(sampler_list);
    }
}

// Record into an existing command buffer

static void cmd_transition_image_layout(Pigment* pigment, VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels)
{
    VkImageMemoryBarrier2 barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .oldLayout           = old_layout,
        .newLayout           = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1}
    };

    if(old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_NONE;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    else if(old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    }
    else
    {
        PLOG_ERROR(pigment, "Unsupported layout transition (%d -> %d)!", old_layout, new_layout);
        return;
    }

    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier
    };

    vkCmdPipelineBarrier2(cmd, &dep);
}

static void cmd_copy_buffer_to_image(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkBufferImageCopy region = {
        .bufferOffset                    = 0,
        .bufferRowLength                 = 0,
        .bufferImageHeight               = 0,
        .imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.mipLevel       = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount     = 1,
        .imageOffset                     = {    0,      0, 0},
        .imageExtent                     = {width, height, 1}
    };

    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

static void cmd_generate_mipmaps(VkCommandBuffer cmd, VkImage image, int32_t image_width, int32_t image_height, uint32_t mip_levels)
{
    VkImageMemoryBarrier2 barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .image               = image,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };

    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier
    };

    int32_t mip_width  = image_width;
    int32_t mip_height = image_height;

    for(uint32_t i = 1; i < mip_levels; i++)
    {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcStageMask                  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask                 = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask                  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask                 = VK_ACCESS_2_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier2(cmd, &dep);

        VkOffset3D src_offsets[] = {
            {        0,          0, 0},
            {mip_width, mip_height, 1}
        };

        VkOffset3D dst_offsets[] = {
            {                                0,                                   0, 0},
            {mip_width > 1 ? mip_width / 2 : 1, mip_height > 1 ? mip_height / 2 : 1, 1}
        };

        VkImageBlit blit = {
            .srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .srcSubresource.mipLevel       = i - 1,
            .srcSubresource.baseArrayLayer = 0,
            .srcSubresource.layerCount     = 1,
            .dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .dstSubresource.mipLevel       = i,
            .dstSubresource.baseArrayLayer = 0,
            .dstSubresource.layerCount     = 1
        };

        memcpy(blit.srcOffsets, src_offsets, 2 * sizeof(*src_offsets));
        memcpy(blit.dstOffsets, dst_offsets, 2 * sizeof(*dst_offsets));

        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

        vkCmdPipelineBarrier2(cmd, &dep);

        if(mip_width > 1)
        {
            mip_width /= 2;
        }

        if(mip_height > 1)
        {
            mip_height /= 2;
        }
    }

    barrier.subresourceRange.baseMipLevel = mip_levels - 1;
    barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcStageMask                  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask                 = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask                  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask                 = VK_ACCESS_2_SHADER_READ_BIT;

    vkCmdPipelineBarrier2(cmd, &dep);
}

static int prepare_image_upload(Pigment* pigment, PImage** out_image, PBuffer** out_staging, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format)
{
    PDevice* device     = pigment->device;
    uint32_t pixel_size = pformat_pixel_size(format);
    if(pixel_size == 0)
    {
        PLOG_ERROR(pigment, "Unsupported PFormat (%d)!", format);
        return PIGMENT_ERROR;
    }

    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(device->physical_device, (VkFormat) format, &format_properties);
    if(!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        PLOG_ERROR(pigment, "Image format does not support linear blitting!");
        return PIGMENT_ERROR;
    }

    VkDeviceSize image_size = (uint64_t) (width * height * pixel_size);
    uint32_t mip_levels     = (uint32_t) (floor(log2(imax(width, height)))) + 1;

    PBufferDesc staging_desc = {
        .size   = (uint64_t) image_size,
        .usage  = P_BUFFER_USAGE_TRANSFER_SRC,
        .memory = P_MEMORY_HOST_VISIBLE,
    };

    *out_staging = pigment_create_buffer(pigment, &staging_desc);
    if(*out_staging == NULL)
    {
        return PIGMENT_ERROR;
    }

    memcpy((*out_staging)->mapped, pixels, (size_t) image_size);

    PImageDesc image_desc = {
        .width      = width,
        .height     = height,
        .format     = format,
        .usage      = P_IMAGE_USAGE_TRANSFER_SRC | P_IMAGE_USAGE_TRANSFER_DST | P_IMAGE_USAGE_SAMPLED,
        .mip_levels = mip_levels
    };

    *out_image = pigment_create_image(pigment, &image_desc);
    if(*out_image == NULL)
    {
        return PIGMENT_ERROR;
    }

    return PIGMENT_SUCCESS;
}

static int batch_record_uploads(Pigment* pigment, VkCommandBuffer cmd, PImage** out_images, PBuffer** stagings, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count)
{
    for(uint32_t i = 0; i < count; i++)
    {
        if(prepare_image_upload(pigment, &out_images[i], &stagings[i], pixels[i], widths[i], heights[i], formats[i]) != PIGMENT_SUCCESS)
        {
            return PIGMENT_ERROR;
        }
        cmd_transition_image_layout(pigment, cmd, out_images[i]->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, out_images[i]->mip_levels);
        cmd_copy_buffer_to_image(cmd, stagings[i]->buffer, out_images[i]->image, widths[i], heights[i]);
        cmd_generate_mipmaps(cmd, out_images[i]->image, (int32_t) widths[i], (int32_t) heights[i], out_images[i]->mip_levels);
    }
    return PIGMENT_SUCCESS;
}

static uint32_t batch_append_images(PImageList* list, PImage** images, uint32_t count)
{
    uint32_t start_slot = list->count;
    for(uint32_t i = 0; i < count; i++)
    {
        image_list_append(list, images[i]);
    }
    return start_slot;
}

static void batch_write_descriptors(Pigment* pigment, uint32_t start_slot, uint32_t count)
{
    VkDescriptorImageInfo* infos = malloc(count * sizeof(*infos));
    if(infos == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        infos[i].imageView   = pigment->images->images[start_slot + i]->image_view;
        infos[i].sampler     = VK_NULL_HANDLE;
    }

    for(uint32_t f = 0; f < pigment->config.max_frames_in_flight; f++)
    {
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = pigment->descriptor->descriptor_sets[f],
            .dstBinding      = 2,
            .dstArrayElement = start_slot,
            .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = count,
            .pImageInfo      = infos,
        };
        vkUpdateDescriptorSets(pigment->device->logical_device, 1, &write, 0, NULL);
    }
    free(infos);
}
