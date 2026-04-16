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

#include "texture.h"
#include "structs.h"

extern int create_buffer(VkBuffer* buffer, VkDeviceMemory* buffer_memory, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device);
extern VkCommandBuffer start_single_usage_commands(VkCommandPool command_pool, PDevice* device);
extern void end_single_usage_commands(VkCommandBuffer* command_buffer, VkCommandPool command_pool, PDevice* device);
extern VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels, VkDevice device);
extern uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

int create_vk_image(VkImage* image, VkDeviceMemory* image_memory, uint32_t width, uint32_t height, uint32_t mip_levels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device);

static int image_list_append(PImageList* image_list, PImage image);
static int create_image(PImage* image, const unsigned char* pixels, uint32_t width, uint32_t height, PCommands* commands, PDevice* device);
static int create_sampler(PSampler* sampler, PSamplerDesc* desc, PDevice* device);
static void cmd_transition_image_layout(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels);
static void cmd_copy_buffer_to_image(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
static void cmd_generate_mipmaps(VkCommandBuffer cmd, VkImage image, int32_t image_width, int32_t image_height, uint32_t mip_levels);
static int prepare_image_upload(PImage* image, VkBuffer* staging_buffer, VkDeviceMemory* staging_memory, const unsigned char* pixels, uint32_t width, uint32_t height, PDevice* device);
static int batch_record_uploads(VkCommandBuffer cmd, PImage* out_images, VkBuffer* stagings, VkDeviceMemory* staging_mems, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, uint32_t count, PDevice* device);
static uint32_t batch_append_images(PImageList* list, PImage* images, uint32_t count, PDevice* device);
static void batch_write_descriptors(Pigment* pigment, uint32_t start_slot, uint32_t count);


static inline int imax(int a, int b)
{
    return a > b ? a : b;
}

static int image_list_append(PImageList* image_list, PImage image)
{
    if(image_list->image_number >= image_list->image_size)
    {
        uint32_t new_size = image_list->image_size * 2;
        PImage* new_ptr = realloc(image_list->images, new_size * sizeof(PImage));

        if(new_ptr == NULL)
        {
            perror("image_list_append");
            return PIGMENT_ERROR;
        }

        image_list->images     = new_ptr;
        image_list->image_size = new_size;
    }

    image_list->images[image_list->image_number] = image;
    image_list->image_number++;

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

    image_list->image_size = 1;

    return image_list;

ERROR:
    perror("create_images");
    free(image_list);
    return NULL;
}

void destroy_images(PImageList* image_list, PDevice* device)
{
    if(image_list != NULL)
    {
        for(size_t i = 0; i < image_list->image_number; i++)
        {
            vkDestroyImageView(device->logical_device, image_list->images[i].image_view, NULL);
            vkDestroyImage(device->logical_device, image_list->images[i].image, NULL);
            vkFreeMemory(device->logical_device, image_list->images[i].image_memory, NULL);
        }

        free(image_list->images);

        free(image_list);
    }
}

uint32_t pigment_upload_image(Pigment* pigment, const unsigned char* pixels, uint32_t width, uint32_t height)
{
    if(pigment == NULL || pixels == NULL || width == 0 || height == 0)
    {
        return 0;
    }

    PImageList* images = pigment->images;
    uint32_t slot          = images->image_number;

    if(add_image_from_pixels(images, pixels, width, height, pigment->commands, pigment->device) != PIGMENT_SUCCESS)
    {
        return 0;
    }

    VkDescriptorImageInfo image_info = {
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView   = images->images[slot].image_view,
    };

    for(uint32_t i = 0; i < pigment->max_frames_in_flight; i++)
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

uint32_t pigment_upload_image_batch(Pigment* pigment, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, uint32_t count)
{
    if(pigment == NULL || pixels == NULL || count == 0)
    {
        return 0;
    }

    VkBuffer* stagings           = calloc(count, sizeof(*stagings));
    VkDeviceMemory* staging_mems = calloc(count, sizeof(*staging_mems));
    PImage* new_images       = calloc(count, sizeof(*new_images));
    uint32_t start_slot          = 0;

    if(stagings == NULL || staging_mems == NULL || new_images == NULL)
    {
        goto FREE;
    }

    VkCommandBuffer cmd = start_single_usage_commands(pigment->commands->command_pool, pigment->device);
    int result          = batch_record_uploads(cmd, new_images, stagings, staging_mems, pixels, widths, heights, count, pigment->device);
    end_single_usage_commands(&cmd, pigment->commands->command_pool, pigment->device);

    if(result == PIGMENT_SUCCESS)
    {
        start_slot = batch_append_images(pigment->images, new_images, count, pigment->device);
        batch_write_descriptors(pigment, start_slot, count);
    }

FREE:
    if(stagings != NULL && staging_mems != NULL)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            vkDestroyBuffer(pigment->device->logical_device, stagings[i], NULL);
            vkFreeMemory(pigment->device->logical_device, staging_mems[i], NULL);
        }
    }
    free(stagings);
    free(staging_mems);
    free(new_images);
    return start_slot;
}

int add_image_from_pixels(PImageList* image_list, const unsigned char* pixels, uint32_t width, uint32_t height, PCommands* commands, PDevice* device)
{
    PImage image = {0};

    if(create_image(&image, pixels, width, height, commands, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    image.image_view = create_image_view(image.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, image.mip_levels, device->logical_device);
    if(image.image_view == NULL)
    {
        goto ERROR;
    }

    if(image_list_append(image_list, image) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    return PIGMENT_SUCCESS;

ERROR:
    vkDestroyImageView(device->logical_device, image.image_view, NULL);
    vkDestroyImage(device->logical_device, image.image, NULL);
    vkFreeMemory(device->logical_device, image.image_memory, NULL);
    fprintf(stderr, "Failed to add image from pixels.\n");
    return PIGMENT_ERROR;
}

int add_default_image(PImageList* image_list, PCommands* commands, PDevice* device)
{
    unsigned char white[] = {255, 255, 255, 255};
    return add_image_from_pixels(image_list, white, 1, 1, commands, device);
}

static int create_image(PImage* image, const unsigned char* pixels, uint32_t width, uint32_t height, PCommands* commands, PDevice* device)
{
    VkBuffer staging_buffer              = VK_NULL_HANDLE;
    VkDeviceMemory staging_buffer_memory = VK_NULL_HANDLE;

    if(prepare_image_upload(image, &staging_buffer, &staging_buffer_memory, pixels, width, height, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    VkCommandBuffer cmd = start_single_usage_commands(commands->command_pool, device);
    cmd_transition_image_layout(cmd, image->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, image->mip_levels);
    cmd_copy_buffer_to_image(cmd, staging_buffer, image->image, width, height);
    cmd_generate_mipmaps(cmd, image->image, (int32_t) width, (int32_t) height, image->mip_levels);
    end_single_usage_commands(&cmd, commands->command_pool, device);

    vkDestroyBuffer(device->logical_device, staging_buffer, NULL);
    vkFreeMemory(device->logical_device, staging_buffer_memory, NULL);

    return PIGMENT_SUCCESS;

ERROR:
    vkDestroyBuffer(device->logical_device, staging_buffer, NULL);
    vkFreeMemory(device->logical_device, staging_buffer_memory, NULL);
    fprintf(stderr, "Failed to create image image!\n");
    return PIGMENT_ERROR;
}

static int create_sampler(PSampler* sampler, PSamplerDesc* desc, PDevice* device)
{
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
        fprintf(stderr, "Failed to create image sampler! (result: %d)\n", result);
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

    for(uint32_t i = 0; i < samplers->sampler_number; i++)
    {
        // Check if a sampler with the same description already exists and return its slot if found
        if(samplers->descs[i].mag_filter == desc->mag_filter && samplers->descs[i].min_filter == desc->min_filter && samplers->descs[i].mipmap_mode == desc->mipmap_mode && samplers->descs[i].address_mode == desc->address_mode)
        {
            return i;
        }
    }

    uint32_t slot = samplers->sampler_number;

    if(slot >= samplers->sampler_size)
    {
        fprintf(stderr, "Max samplers reached (%u)\n", samplers->sampler_size);
        return 0;
    }

    if(create_sampler(&samplers->samplers[slot], desc, pigment->device) != PIGMENT_SUCCESS)
    {
        return 0;
    }
    samplers->descs[slot] = *desc;
    samplers->sampler_number++;

    VkDescriptorImageInfo sampler_info = {
        .sampler = samplers->samplers[slot].sampler,
    };

    for(uint32_t i = 0; i < pigment->max_frames_in_flight; i++)
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

PSamplerList* create_samplers(uint32_t max_samplers, PDevice* device)
{
    PSamplerList* sampler_list = calloc(1, sizeof(*sampler_list));
    if(sampler_list == NULL)
    {
        goto ERROR;
    }

    sampler_list->samplers       = malloc(max_samplers * sizeof(*sampler_list->samplers));
    sampler_list->descs          = malloc(max_samplers * sizeof(*sampler_list->descs));
    sampler_list->sampler_size   = max_samplers;
    sampler_list->sampler_number = 0;

    if(sampler_list->samplers == NULL || sampler_list->descs == NULL)
    {
        goto ERROR;
    }

    PSamplerDesc nearest_desc = {.mag_filter = NEAREST, .min_filter = NEAREST, .mipmap_mode = NEAREST, .address_mode = P_ADDRESS_MODE_REPEAT};
    create_sampler(&sampler_list->samplers[0], &nearest_desc, device);
    sampler_list->descs[0] = nearest_desc;
    sampler_list->sampler_number++;

    PSamplerDesc linear_desc = {.mag_filter = LINEAR, .min_filter = LINEAR, .mipmap_mode = LINEAR, .address_mode = P_ADDRESS_MODE_REPEAT};
    create_sampler(&sampler_list->samplers[1], &linear_desc, device);
    sampler_list->descs[1] = linear_desc;
    sampler_list->sampler_number++;

    return sampler_list;

ERROR:
    perror("create_samplers");
    if(sampler_list != NULL)
    {
        free(sampler_list->samplers);
        free(sampler_list->descs);
    }
    free(sampler_list);
    return NULL;
}

void destroy_samplers(PSamplerList* sampler_list, PDevice* device)
{
    if(sampler_list != NULL)
    {
        for(size_t i = 0; i < sampler_list->sampler_number; i++)
        {
            vkDestroySampler(device->logical_device, sampler_list->samplers[i].sampler, NULL);
        }
        free(sampler_list->samplers);
        free(sampler_list->descs);
        free(sampler_list);
    }
}

int create_vk_image(VkImage* image, VkDeviceMemory* image_memory, uint32_t width, uint32_t height, uint32_t mip_levels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device)
{
    VkImageCreateInfo image_create_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .extent.width  = width,
        .extent.height = height,
        .extent.depth  = 1,
        .mipLevels     = mip_levels,
        .arrayLayers   = 1,
        .format        = format,
        .tiling        = tiling,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage         = usage,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE
    };

    VkResult result;
    if((result = vkCreateImage(device->logical_device, &image_create_info, NULL, image)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create image! (result: %d)\n", result);
        goto ERROR;
    }

    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(device->logical_device, *image, &memory_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memory_requirements.size,
        .memoryTypeIndex = find_memory_type(device->physical_device, memory_requirements.memoryTypeBits, properties)
    };

    if((result = vkAllocateMemory(device->logical_device, &alloc_info, NULL, image_memory)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to allocate image memory! (result: %d)\n", result);
        goto ERROR;
    }

    if((result = vkBindImageMemory(device->logical_device, *image, *image_memory, 0)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to bind image memory! (result: %d)\n", result);
        goto ERROR;
    }

    return PIGMENT_SUCCESS;

ERROR:
    vkFreeMemory(device->logical_device, *image_memory, NULL);
    vkDestroyImage(device->logical_device, *image, NULL);
    return PIGMENT_ERROR;
}

// Record into an existing command buffer

static void cmd_transition_image_layout(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels)
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
        fprintf(stderr, "Unsupported layout transition (%d -> %d)!\n", old_layout, new_layout);
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

static int prepare_image_upload(PImage* image, VkBuffer* staging_buffer, VkDeviceMemory* staging_memory, const unsigned char* pixels, uint32_t width, uint32_t height, PDevice* device)
{
    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(device->physical_device, VK_FORMAT_R8G8B8A8_SRGB, &format_properties);
    if(!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        fprintf(stderr, "Image format does not support linear blitting!\n");
        return PIGMENT_ERROR;
    }

    VkDeviceSize image_size = (uint64_t) (width * height * 4);
    image->mip_levels     = (uint32_t) (floor(log2(imax(width, height)))) + 1;

    if(create_buffer(staging_buffer, staging_memory, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, device) != PIGMENT_SUCCESS)
    {
        return PIGMENT_ERROR;
    }

    void* data;
    if(vkMapMemory(device->logical_device, *staging_memory, 0, image_size, 0, &data) != VK_SUCCESS)
    {
        return PIGMENT_ERROR;
    }
    memcpy(data, pixels, (size_t) image_size);
    vkUnmapMemory(device->logical_device, *staging_memory);

    if(create_vk_image(&image->image, &image->image_memory, width, height, image->mip_levels, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, device) != PIGMENT_SUCCESS)
    {
        return PIGMENT_ERROR;
    }

    return PIGMENT_SUCCESS;
}

static int batch_record_uploads(VkCommandBuffer cmd, PImage* out_images, VkBuffer* stagings, VkDeviceMemory* staging_mems, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, uint32_t count, PDevice* device)
{
    for(uint32_t i = 0; i < count; i++)
    {
        if(prepare_image_upload(&out_images[i], &stagings[i], &staging_mems[i], pixels[i], widths[i], heights[i], device) != PIGMENT_SUCCESS)
        {
            return PIGMENT_ERROR;
        }
        cmd_transition_image_layout(cmd, out_images[i].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, out_images[i].mip_levels);
        cmd_copy_buffer_to_image(cmd, stagings[i], out_images[i].image, widths[i], heights[i]);
        cmd_generate_mipmaps(cmd, out_images[i].image, (int32_t) widths[i], (int32_t) heights[i], out_images[i].mip_levels);
    }
    return PIGMENT_SUCCESS;
}

static uint32_t batch_append_images(PImageList* list, PImage* images, uint32_t count, PDevice* device)
{
    uint32_t start_slot = list->image_number;
    for(uint32_t i = 0; i < count; i++)
    {
        images[i].image_view = create_image_view(images[i].image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, images[i].mip_levels, device->logical_device);
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
        infos[i].imageView   = pigment->images->images[start_slot + i].image_view;
        infos[i].sampler     = VK_NULL_HANDLE;
    }

    for(uint32_t f = 0; f < pigment->max_frames_in_flight; f++)
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