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

#include "lib/math.h"
#include "lib/stb_image.h"

extern int create_buffer(VkBuffer* buffer, VkDeviceMemory* buffer_memory, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device);
extern VkCommandBuffer start_single_usage_commands(VkCommandPool command_pool, PDevice* device);
extern void end_single_usage_commands(VkCommandBuffer* command_buffer, VkCommandPool command_pool, PDevice* device);
extern VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels, VkDevice device);
extern uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

int create_image(VkImage* image, VkDeviceMemory* image_memory, uint32_t width, uint32_t height, uint32_t mip_levels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device);
int create_texture_image(PTexture* texture, const char* texture_path, VkDeviceMemory* image_memory, PCommands* commands, PDevice* device);
void copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, VkCommandPool command_pool, PDevice* device);
int create_sampler(PSampler* sampler, FilteringMode filtering_mode, PDevice* device);
bool has_stencil_component(VkFormat format);
int transition_image_layout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels, VkCommandPool command_pool, PDevice* device);
int generate_mipmaps(VkImage image, VkFormat image_format, int32_t texture_width, int32_t texture_height, uint32_t mip_levels, VkCommandPool command_pool, PDevice* device);

void texture_list_append(PTextureList* texture_list, PTexture texture)
{
    if(texture_list->texture_number >= texture_list->texture_size)
    {
        texture_list->texture_size *= 2;
        texture_list->textures = realloc(texture_list->textures, texture_list->texture_size * sizeof(*(texture_list->textures)));
        if(texture_list->textures == NULL)
        {
            perror("realloc");
            return;
        }
    }
    texture_list->textures[texture_list->texture_number] = texture;
    texture_list->texture_number++;
}

PTextureList* create_textures(void)
{
    PTextureList* texture_list = calloc(1, sizeof(*texture_list));
    if(texture_list == NULL)
    {
        goto ERROR;
    }

    texture_list->textures = malloc(1 * sizeof(*texture_list->textures));
    if(texture_list->textures == NULL)
    {
        goto ERROR;
    }

    texture_list->texture_size = 1;

    return texture_list;

ERROR:
    perror("create_textures");
    free(texture_list);
    return NULL;
}

int add_texture(PTextureList* texture_list, const char* texture_path, PCommands* commands, PDevice* device)
{
    PTexture texture;

    if(create_texture_image(&texture, texture_path, &texture.image_memory, commands, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    texture.image_view = create_image_view(texture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, texture.mip_levels, device->logical_device);
    if(texture.image_view == NULL)
    {
        goto ERROR;
    }


    texture_list_append(texture_list, texture);

    return PIGMENT_SUCCESS;

ERROR:
    fprintf(stderr, "Failed to add a texture.\n");
    return PIGMENT_ERROR;
}

unsigned char* create_default_texture(int* texture_width, int* texture_height)
{
    *texture_width  = 2;
    *texture_height = 2;

    unsigned char* pixels = malloc((size_t) (*texture_width * *texture_height * 4) * sizeof(*pixels));
    if(pixels == NULL)
    {
        return NULL;
    }

    unsigned char magenta_rgba[] = {255, 0, 255, 255};
    unsigned char black_rgba[]   = {0, 0, 0, 255};

    for(int i = 0; i < *texture_width; i++)
    {
        for(int j = 0; j < *texture_height; j++)
        {
            if((i + j) % 2 == 0)
            {
                memcpy(&pixels[(*texture_height * i + j) * 4], magenta_rgba, 4 * sizeof(*pixels));
            }
            else
            {
                memcpy(&pixels[(*texture_height * i + j) * 4], black_rgba, 4 * sizeof(*pixels));
            }
        }
    }

    return pixels;
}

stbi_uc* load_texture_file(const char* texture_path, int* texture_width, int* texture_height)
{
    int texture_channels;
    stbi_uc* pixels = stbi_load(texture_path, texture_width, texture_height, &texture_channels, STBI_rgb_alpha);
    if(pixels == NULL)
    {
        fprintf(stderr, "Failed to load texture file!\n");
        return NULL;
    }

    return pixels;
}

int create_texture_image(PTexture* texture, const char* texture_path, VkDeviceMemory* image_memory, PCommands* commands, PDevice* device)
{
    int texture_width, texture_height;
    unsigned char* pixels;

    if(strncmp(texture_path, "default", 8) == 0)
    {
        pixels = create_default_texture(&texture_width, &texture_height);
    }
    else
    {
        pixels = load_texture_file(texture_path, &texture_width, &texture_height);
    }

    if(pixels == NULL)
    {
        goto ERROR;
    }

    VkDeviceSize image_size = (uint64_t) (texture_width * texture_height * 4);
    texture->mip_levels     = (uint32_t) (floor(log2(imax(texture_width, texture_height)))) + 1;

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;

    create_buffer(&staging_buffer, &staging_buffer_memory, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, device);

    void* data;
    vkMapMemory(device->logical_device, staging_buffer_memory, 0, image_size, 0, &data);
    memcpy(data, pixels, (size_t) image_size);
    vkUnmapMemory(device->logical_device, staging_buffer_memory);

    free(pixels);

    create_image(&texture->image, image_memory, (uint32_t) texture_width, (uint32_t) texture_height, texture->mip_levels, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, device);

    transition_image_layout(texture->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture->mip_levels, commands->command_pool, device);
    copy_buffer_to_image(staging_buffer, texture->image, (uint32_t) texture_width, (uint32_t) texture_height, commands->command_pool, device);

    vkDestroyBuffer(device->logical_device, staging_buffer, NULL);
    vkFreeMemory(device->logical_device, staging_buffer_memory, NULL);

    generate_mipmaps(texture->image, VK_FORMAT_R8G8B8A8_SRGB, texture_width, texture_height, texture->mip_levels, commands->command_pool, device);

    return PIGMENT_SUCCESS;

ERROR:
    fprintf(stderr, "Failed to create texture image!\n");
    return PIGMENT_ERROR;
}

int generate_mipmaps(VkImage image, VkFormat image_format, int32_t texture_width, int32_t texture_height, uint32_t mip_levels, VkCommandPool command_pool, PDevice* device)
{
    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(device->physical_device, image_format, &format_properties);

    VkCommandBuffer command_buffer = start_single_usage_commands(command_pool, device);

    if(!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        fprintf(stderr, "Texture image format does not support linear blitting!\n");
        return PIGMENT_ERROR;
    }

    VkImageMemoryBarrier barrier = {
        .sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .image                           = image,
        .srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount     = 1,
        .subresourceRange.levelCount     = 1
    };

    int32_t mip_width  = texture_width;
    int32_t mip_height = texture_height;

    for(uint32_t i = 1; i < mip_levels; i++)
    {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(
            command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            NULL,
            0,
            NULL,
            1,
            &barrier
        );

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

        vkCmdBlitImage(
            command_buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR
        );

        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            NULL,
            0,
            NULL,
            1,
            &barrier
        );

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
    barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &barrier
    );

    end_single_usage_commands(&command_buffer, command_pool, device);

    return PIGMENT_SUCCESS;
}

int create_sampler(PSampler* sampler, FilteringMode filtering_mode, PDevice* device)
{
    VkPhysicalDeviceProperties properties = {0};
    vkGetPhysicalDeviceProperties(device->physical_device, &properties);

    VkSamplerCreateInfo sampler_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter               = (VkFilter) filtering_mode,
        .minFilter               = (VkFilter) filtering_mode,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable        = VK_TRUE,
        .maxAnisotropy           = properties.limits.maxSamplerAnisotropy,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
        .compareEnable           = VK_FALSE,
        .compareOp               = VK_COMPARE_OP_ALWAYS,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .minLod                  = 0.0f,
        .maxLod                  = VK_LOD_CLAMP_NONE
    };

    if(vkCreateSampler(device->logical_device, &sampler_create_info, NULL, &sampler->sampler) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create texture sampler!\n");
        return PIGMENT_ERROR;
    }

    return PIGMENT_SUCCESS;
}

PSamplerList* create_samplers(PDevice* device)
{
    PSamplerList* sampler_list = calloc(1, sizeof(*sampler_list));
    if(sampler_list == NULL)
    {
        goto ERROR;
    }

    sampler_list->samplers = malloc(MAX_SAMPLERS * sizeof(*sampler_list->samplers));
    sampler_list->sampler_size = MAX_SAMPLERS;
    sampler_list->sampler_number = 0;

    // Add nearest sampler
    create_sampler(&sampler_list->samplers[0], NEAREST, device);
    sampler_list->sampler_number++;

    // Add linear sampler
    create_sampler(&sampler_list->samplers[1], LINEAR, device);
    sampler_list->sampler_number++;

    return sampler_list;

ERROR:
    perror("create_samplers");
    if(sampler_list != NULL)
    {
        free(sampler_list->samplers);
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
        free(sampler_list);
    }
}

void destroy_textures(PTextureList* texture_list, PDevice* device)
{
    if(texture_list != NULL)
    {
        for(size_t i = 0; i < texture_list->texture_number; i++)
        {
            vkDestroyImageView(device->logical_device, texture_list->textures[i].image_view, NULL);
            vkDestroyImage(device->logical_device, texture_list->textures[i].image, NULL);
            vkFreeMemory(device->logical_device, texture_list->textures[i].image_memory, NULL);
        }

        free(texture_list->textures);

        free(texture_list);
    }
}

int create_image(VkImage* image, VkDeviceMemory* image_memory, uint32_t width, uint32_t height, uint32_t mip_levels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device)
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

    if(vkCreateImage(device->logical_device, &image_create_info, NULL, image) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create image!\n");
        goto ERROR;
    }

    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(device->logical_device, *image, &memory_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memory_requirements.size,
        .memoryTypeIndex = find_memory_type(device->physical_device, memory_requirements.memoryTypeBits, properties)
    };

    if(vkAllocateMemory(device->logical_device, &alloc_info, NULL, image_memory) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to allocate image memory!\n");
        goto ERROR;
    }

    if(vkBindImageMemory(device->logical_device, *image, *image_memory, 0))
    {
        fprintf(stderr, "Failed to bind image memory!\n");
        goto ERROR;
    }

    return PIGMENT_SUCCESS;

ERROR:
    vkFreeMemory(device->logical_device, *image_memory, NULL);
    vkDestroyImage(device->logical_device, *image, NULL);
    return PIGMENT_ERROR;
}

int transition_image_layout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels, VkCommandPool command_pool, PDevice* device)
{
    VkCommandBuffer command_buffer = start_single_usage_commands(command_pool, device);

    VkImageMemoryBarrier barrier = {
        .sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout                       = old_layout,
        .newLayout                       = new_layout,
        .srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED,
        .image                           = image,
        .subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseMipLevel   = 0,
        .subresourceRange.levelCount     = mip_levels,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount     = 1
    };

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;

    if(new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        if(has_stencil_component(format))
        {
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    else
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    if(old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        source_stage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if(old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        source_stage      = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if(old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        source_stage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else
    {
        fprintf(stderr, "Unsupported layout transition!\n");
        goto ERROR;
    }

    vkCmdPipelineBarrier(
        command_buffer,
        source_stage,
        destination_stage,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &barrier
    );

    end_single_usage_commands(&command_buffer, command_pool, device);

    return PIGMENT_SUCCESS;

ERROR:
    return PIGMENT_ERROR;
}

void copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, VkCommandPool command_pool, PDevice* device)
{
    VkCommandBuffer command_buffer = start_single_usage_commands(command_pool, device);

    VkOffset3D image_offset = {0, 0, 0};
    VkExtent3D image_extent = {width, height, 1};

    VkBufferImageCopy region = {
        .bufferOffset                    = 0,
        .bufferRowLength                 = 0,
        .bufferImageHeight               = 0,
        .imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.mipLevel       = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount     = 1,
        .imageOffset                     = image_offset,
        .imageExtent                     = image_extent
    };

    vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    end_single_usage_commands(&command_buffer, command_pool, device);
}

bool has_stencil_component(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}
