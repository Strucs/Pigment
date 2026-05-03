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

#include "bindless.h"
#include "buffers.h"
#include "image.h"
#include "internal.h"
#include "log_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct PImageList {
    PImage** images;
    uint32_t count;
    uint32_t capacity;
} PImageList;

typedef struct PSamplerList {
    PSampler** samplers;
    PSamplerDesc* descs;
    uint32_t count;
    uint32_t capacity;
} PSamplerList;

struct PStdBindless {
    PDescriptorSetLayout* layout;
    PDescriptorPool* pool;
    PDescriptorSet** sets;
    uint32_t set_count;

    PImageList images;
    PImageList cubemaps;
    PSamplerList samplers;
};

static int image_list_append(PImageList* image_list, PImage* image);
static void cmd_transition_image_layout(Pigment* pigment, VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels, uint32_t layer_count);
static void cmd_copy_buffer_to_image(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layer_count, uint64_t layer_size_bytes);
static void cmd_generate_mipmaps(VkCommandBuffer cmd, VkImage image, int32_t image_width, int32_t image_height, uint32_t mip_levels, uint32_t layer_count);
static int prepare_layered_image_upload(Pigment* pigment, PImage** out_image, PBuffer** out_staging, const unsigned char* const* layer_data, uint32_t width, uint32_t height, uint32_t layer_count, PFormat format, PImageType type);
static int batch_record_uploads(Pigment* pigment, VkCommandBuffer cmd, PImage** out_images, PBuffer** stagings, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count);
static uint32_t batch_append_images(PImageList* list, PImage** images, uint32_t count);
static int add_image_from_pixels(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format, PCommandPool* pool);
static int add_default_image(Pigment* pigment, PStdBindless* bindless, PCommandPool* pool);
static int sampler_list_init(Pigment* pigment, PSamplerList* sampler_list, uint32_t max_samplers);
static void sampler_list_destroy(Pigment* pigment, PSamplerList* sampler_list);
static int image_list_init(PImageList* image_list);
static void image_list_destroy(Pigment* pigment, PImageList* image_list);
static void write_sampler_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PSampler* sampler);
static void write_cubemap_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PImage* image);
static void write_image_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PImage* image);
static void batch_write_descriptors(Pigment* pigment, PStdBindless* bindless, uint32_t start_slot, uint32_t count);

static inline int imax(int a, int b)
{
    return a > b ? a : b;
}

PStdBindless* pigment_std_create_bindless(Pigment* pigment, uint32_t max_images, uint32_t max_samplers, uint32_t max_cubemaps)
{
    if(pigment == NULL || max_images == 0 || max_samplers == 0)
    {
        return NULL;
    }

    PStdBindless* bindless = calloc(1, sizeof(*bindless));
    if(bindless == NULL)
    {
        return NULL;
    }

    PDescriptorBinding bindings[] = {
        {
         .binding = 0,
         .type    = P_DESCRIPTOR_TYPE_SAMPLER,
         .count   = max_samplers,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
         },
        {
         .binding = 1,
         .type    = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count   = (max_cubemaps == 0) ? 1 : max_cubemaps,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
         },
        {
         .binding = 2,
         .type    = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count   = max_images,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | P_DESCRIPTOR_BINDING_VARIABLE_COUNT_BIT,
         },
    };

    PDescriptorSetLayoutDesc layout_desc = {
        .bindings      = bindings,
        .binding_count = sizeof(bindings) / sizeof(bindings[0]),
    };

    bindless->layout = pigment_create_descriptor_set_layout(pigment, &layout_desc);
    if(bindless->layout == NULL)
    {
        goto ERROR;
    }

    uint32_t frames = pigment->config.max_frames_in_flight;

    PDescriptorPoolSize pool_sizes[] = {
        {      .type  = P_DESCRIPTOR_TYPE_SAMPLER,
         .count = frames * max_samplers               },
        {.type  = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count = frames * (max_images + max_cubemaps)},
    };

    PDescriptorPoolDesc pool_desc = {
        .pool_sizes      = pool_sizes,
        .pool_size_count = sizeof(pool_sizes) / sizeof(pool_sizes[0]),
        .max_sets        = frames,
    };

    bindless->pool = pigment_create_descriptor_pool(pigment, &pool_desc);
    if(bindless->pool == NULL)
    {
        goto ERROR;
    }

    bindless->sets = calloc(frames, sizeof(*bindless->sets));
    if(bindless->sets == NULL)
    {
        goto ERROR;
    }

    bindless->set_count = frames;

    for(uint32_t i = 0; i < frames; i++)
    {
        bindless->sets[i] = pigment_allocate_descriptor_set(pigment, bindless->pool, bindless->layout, max_images);
        if(bindless->sets[i] == NULL)
        {
            goto ERROR;
        }
    }

    if(image_list_init(&bindless->images) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    if(image_list_init(&bindless->cubemaps) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    if(sampler_list_init(pigment, &bindless->samplers, max_samplers) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    PCommandPool* pool = pigment_default_pool(pigment);
    if(pool == NULL)
    {
        goto ERROR;
    }

    if(add_default_image(pigment, bindless, pool) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    PDescriptorImageInfo* sampler_infos = calloc(bindless->samplers.count, sizeof(*sampler_infos));
    if(sampler_infos == NULL)
    {
        goto ERROR;
    }

    PDescriptorImageInfo image_info = {
        .image = bindless->images.images[0]
    };

    for(uint32_t i = 0; i < bindless->samplers.count; i++)
    {
        sampler_infos[i].sampler = bindless->samplers.samplers[i];
    }

    for(uint32_t f = 0; f < bindless->set_count; f++)
    {
        PDescriptorWrite writes[2] = {
            {
             .set           = bindless->sets[f],
             .binding       = 0,
             .array_element = 0,
             .type          = P_DESCRIPTOR_TYPE_SAMPLER,
             .count         = bindless->samplers.count,
             .image_infos   = sampler_infos,
             },
            {
             .set           = bindless->sets[f],
             .binding       = 2,
             .array_element = 0,
             .type          = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
             .count         = 1,
             .image_infos   = &image_info,
             },
        };
        pigment_write_descriptors(pigment, writes, sizeof(writes) / sizeof(writes[0]));
    }
    free(sampler_infos);

    return bindless;

ERROR:
    pigment_std_destroy_bindless(pigment, bindless);
    return NULL;
}

void pigment_std_destroy_bindless(Pigment* pigment, PStdBindless* bindless)
{
    if(bindless == NULL)
    {
        return;
    }
    image_list_destroy(pigment, &bindless->images);
    image_list_destroy(pigment, &bindless->cubemaps);
    sampler_list_destroy(pigment, &bindless->samplers);
    free(bindless->sets);
    pigment_destroy_descriptor_pool(pigment, bindless->pool);
    pigment_destroy_descriptor_set_layout(pigment, bindless->layout);
    free(bindless);
}

uint32_t pigment_std_upload_image(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format)
{
    if(pigment == NULL || bindless == NULL || pixels == NULL || width == 0 || height == 0)
    {
        return 0;
    }

    PCommandPool* pool = pigment_default_pool(pigment);
    if(pool == NULL)
    {
        return 0;
    }

    uint32_t slot = bindless->images.count;

    if(add_image_from_pixels(pigment, bindless, pixels, width, height, format, pool) != PIGMENT_SUCCESS)
    {
        return 0;
    }

    write_image_descriptor(pigment, bindless, slot, bindless->images.images[slot]);

    return slot;
}

uint32_t pigment_std_upload_image_batch(Pigment* pigment, PStdBindless* bindless, const unsigned char** pixels, const uint32_t* widths, const uint32_t* heights, const PFormat* formats, uint32_t count)
{
    if(pigment == NULL || bindless == NULL || pixels == NULL || count == 0)
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
        start_slot = batch_append_images(&bindless->images, new_images, count);
        batch_write_descriptors(pigment, bindless, start_slot, count);
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

uint32_t pigment_std_add_sampler(Pigment* pigment, PStdBindless* bindless, const PSamplerDesc* desc)
{
    if(pigment == NULL || bindless == NULL || desc == NULL)
    {
        return 0;
    }

    PSamplerList* samplers = &bindless->samplers;

    for(uint32_t i = 0; i < samplers->count; i++)
    {
        if(memcmp(&samplers->descs[i], desc, sizeof(*desc)) == 0)
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

    PSampler* sampler = pigment_create_sampler(pigment, desc);
    if(sampler == NULL)
    {
        return 0;
    }
    samplers->samplers[slot] = sampler;
    samplers->descs[slot]    = *desc;
    samplers->count++;

    write_sampler_descriptor(pigment, bindless, slot, sampler);
    return slot;
}

uint32_t pigment_std_upload_cubemap(Pigment* pigment, PStdBindless* bindless, const unsigned char* faces[6], uint32_t face_width, uint32_t face_height, PFormat format)
{
    if(pigment == NULL || bindless == NULL || faces == NULL || face_width == 0 || face_height == 0)
    {
        return 0;
    }
    PCommandPool* pool = pigment_default_pool(pigment);
    if(pool == NULL)
    {
        return 0;
    }

    PBuffer* staging = NULL;
    PImage* image    = NULL;
    if(prepare_layered_image_upload(pigment, &image, &staging, faces, face_width, face_height, 6, format, P_IMAGE_TYPE_CUBE) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    uint64_t face_size = (uint64_t) face_width * face_height * pigment_format_pixel_size(format);

    VkCommandBuffer cmd = start_single_usage_commands(pigment, pool->pool);
    cmd_transition_image_layout(pigment, cmd, image->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, image->mip_levels, 6);
    cmd_copy_buffer_to_image(cmd, staging->buffer, image->image, face_width, face_height, 6, face_size);
    cmd_generate_mipmaps(cmd, image->image, (int32_t) face_width, (int32_t) face_height, image->mip_levels, 6);
    end_single_usage_commands(pigment, &cmd, pool->pool);
    pigment_destroy_buffer(pigment, staging);

    uint32_t slot = bindless->cubemaps.count;
    if(image_list_append(&bindless->cubemaps, image) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    write_cubemap_descriptor(pigment, bindless, slot, image);
    return slot;

ERROR:
    pigment_destroy_buffer(pigment, staging);
    pigment_destroy_image(pigment, image);
    return 0;
}

PDescriptorSetLayout* pigment_std_bindless_layout(PStdBindless* bindless)
{
    return (bindless != NULL) ? bindless->layout : NULL;
}

PDescriptorSet* pigment_std_bindless_set(Pigment* pigment, PStdBindless* bindless, uint32_t window_index)
{
    if(pigment == NULL || bindless == NULL || window_index >= pigment->window_count)
    {
        return NULL;
    }
    uint32_t current_frame = pigment->renderers[window_index]->swapchain->current_frame;
    if(current_frame >= bindless->set_count)
    {
        return NULL;
    }

    return bindless->sets[current_frame];
}

static int image_list_init(PImageList* image_list)
{
    image_list->images = calloc(1, sizeof(*image_list->images));
    if(image_list->images == NULL)
    {
        return PIGMENT_ERROR;
    }

    image_list->capacity = 1;
    image_list->count    = 0;

    return PIGMENT_SUCCESS;
}

static void image_list_destroy(Pigment* pigment, PImageList* image_list)
{
    for(uint32_t i = 0; i < image_list->count; i++)
    {
        pigment_destroy_image(pigment, image_list->images[i]);
    }

    free(image_list->images);
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

    image_list->images[image_list->count++] = image;

    return PIGMENT_SUCCESS;
}

static int sampler_list_init(Pigment* pigment, PSamplerList* sampler_list, uint32_t max_samplers)
{
    sampler_list->samplers = calloc(max_samplers, sizeof(*sampler_list->samplers));
    sampler_list->descs    = calloc(max_samplers, sizeof(*sampler_list->descs));
    sampler_list->capacity = max_samplers;
    sampler_list->count    = 0;

    if(sampler_list->samplers == NULL || sampler_list->descs == NULL)
    {
        free(sampler_list->samplers);
        free(sampler_list->descs);
        return PIGMENT_ERROR;
    }

    PSamplerDesc nearest_desc = {
        .mag_filter     = P_FILTERING_MODE_NEAREST,
        .min_filter     = P_FILTERING_MODE_NEAREST,
        .mipmap_mode    = P_FILTERING_MODE_NEAREST,
        .address_mode   = P_ADDRESS_MODE_REPEAT,
        .max_anisotropy = 16.0f,
    };
    sampler_list->samplers[0] = pigment_create_sampler(pigment, &nearest_desc);
    sampler_list->descs[0]    = nearest_desc;
    sampler_list->count++;

    PSamplerDesc linear_desc = {
        .mag_filter     = P_FILTERING_MODE_LINEAR,
        .min_filter     = P_FILTERING_MODE_LINEAR,
        .mipmap_mode    = P_FILTERING_MODE_LINEAR,
        .address_mode   = P_ADDRESS_MODE_REPEAT,
        .max_anisotropy = 16.0f,
    };
    sampler_list->samplers[1] = pigment_create_sampler(pigment, &linear_desc);
    sampler_list->descs[1]    = linear_desc;
    sampler_list->count++;

    return PIGMENT_SUCCESS;
}

static void sampler_list_destroy(Pigment* pigment, PSamplerList* sampler_list)
{
    for(uint32_t i = 0; i < sampler_list->count; i++)
    {
        pigment_destroy_sampler(pigment, sampler_list->samplers[i]);
    }

    free(sampler_list->samplers);
    free(sampler_list->descs);
}

static int add_image_from_pixels(Pigment* pigment, PStdBindless* bindless, const unsigned char* pixels, uint32_t width, uint32_t height, PFormat format, PCommandPool* pool)
{
    PBuffer* staging = NULL;
    PImage* image    = NULL;

    if(pool == NULL)
    {
        goto ERROR;
    }

    if(prepare_layered_image_upload(pigment, &image, &staging, &pixels, width, height, 1, format, P_IMAGE_TYPE_2D) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    VkCommandBuffer cmd = start_single_usage_commands(pigment, pool->pool);
    cmd_transition_image_layout(pigment, cmd, image->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, image->mip_levels, 1);
    cmd_copy_buffer_to_image(cmd, staging->buffer, image->image, width, height, 1, 0);
    cmd_generate_mipmaps(cmd, image->image, (int32_t) width, (int32_t) height, image->mip_levels, 1);
    end_single_usage_commands(pigment, &cmd, pool->pool);

    pigment_destroy_buffer(pigment, staging);

    if(image_list_append(&bindless->images, image) != PIGMENT_SUCCESS)
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

static int add_default_image(Pigment* pigment, PStdBindless* bindless, PCommandPool* pool)
{
    unsigned char white[] = {255, 255, 255, 255};
    return add_image_from_pixels(pigment, bindless, white, 1, 1, P_FORMAT_R8G8B8A8_UNORM, pool);
}

static void cmd_transition_image_layout(Pigment* pigment, VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels, uint32_t layer_count)
{
    VkImageMemoryBarrier2 barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .oldLayout           = old_layout,
        .newLayout           = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, layer_count}
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

static void cmd_copy_buffer_to_image(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layer_count, uint64_t layer_size_bytes)
{
    VkBufferImageCopy* regions = malloc(layer_count * sizeof(*regions));
    if(regions == NULL)
    {
        return;
    }
    for(uint32_t i = 0; i < layer_count; i++)
    {
        regions[i] = (VkBufferImageCopy) {
            .bufferOffset                    = (VkDeviceSize) i * layer_size_bytes,
            .imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .imageSubresource.mipLevel       = 0,
            .imageSubresource.baseArrayLayer = i,
            .imageSubresource.layerCount     = 1,
            .imageExtent                     = {width, height, 1},
        };
    }
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layer_count, regions);
    free(regions);
}

static void cmd_generate_mipmaps(VkCommandBuffer cmd, VkImage image, int32_t image_width, int32_t image_height, uint32_t mip_levels, uint32_t layer_count)
{
    VkImageMemoryBarrier2 barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .image               = image,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layer_count}
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
            .srcSubresource.layerCount     = layer_count,
            .dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .dstSubresource.mipLevel       = i,
            .dstSubresource.baseArrayLayer = 0,
            .dstSubresource.layerCount     = layer_count
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

static int prepare_layered_image_upload(Pigment* pigment, PImage** out_image, PBuffer** out_staging, const unsigned char* const* layer_data, uint32_t width, uint32_t height, uint32_t layer_count, PFormat format, PImageType type)
{
    uint32_t pixel_size = pigment_format_pixel_size(format);
    if(pixel_size == 0)
    {
        PLOG_ERROR(pigment, "Unsupported PFormat (%d)!", format);
        return PIGMENT_ERROR;
    }

    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(pigment->device->physical_device, (VkFormat) format, &format_properties);
    if(!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        PLOG_ERROR(pigment, "Image format does not support linear blitting!");
        return PIGMENT_ERROR;
    }

    uint64_t layer_size = (uint64_t) width * height * pixel_size;
    uint64_t total_size = layer_size * layer_count;
    uint32_t mip_levels = (uint32_t) (floor(log2(imax(width, height)))) + 1;

    PBufferDesc staging_desc = {
        .size   = total_size,
        .usage  = P_BUFFER_USAGE_TRANSFER_SRC,
        .memory = P_MEMORY_HOST_VISIBLE,
    };

    *out_staging = pigment_create_buffer(pigment, &staging_desc);
    if(*out_staging == NULL)
    {
        return PIGMENT_ERROR;
    }

    unsigned char* mapped = (unsigned char*) pigment_buffer_mapped(*out_staging);
    for(uint32_t i = 0; i < layer_count; i++)
    {
        memcpy(mapped + i * layer_size, layer_data[i], (size_t) layer_size);
    }

    PImageDesc image_desc = {
        .width        = width,
        .height       = height,
        .array_layers = layer_count,
        .format       = format,
        .usage        = P_IMAGE_USAGE_TRANSFER_SRC | P_IMAGE_USAGE_TRANSFER_DST | P_IMAGE_USAGE_SAMPLED,
        .mip_levels   = mip_levels,
        .type         = type,
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
        cmd_transition_image_layout(pigment, cmd, out_images[i]->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, out_images[i]->mip_levels, 1);
        cmd_copy_buffer_to_image(cmd, stagings[i]->buffer, out_images[i]->image, widths[i], heights[i], 1, 0);
        cmd_generate_mipmaps(cmd, out_images[i]->image, (int32_t) widths[i], (int32_t) heights[i], out_images[i]->mip_levels, 1);
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

static void write_sampler_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PSampler* sampler)
{
    PDescriptorImageInfo info = {.sampler = sampler};
    for(uint32_t i = 0; i < bindless->set_count; i++)
    {
        PDescriptorWrite write = {
            .set           = bindless->sets[i],
            .binding       = 0,
            .array_element = slot,
            .type          = P_DESCRIPTOR_TYPE_SAMPLER,
            .count         = 1,
            .image_infos   = &info,
        };
        pigment_write_descriptors(pigment, &write, 1);
    }
}

static void write_image_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PImage* image)
{
    PDescriptorImageInfo info = {
        .image = image
    };

    for(uint32_t i = 0; i < bindless->set_count; i++)
    {
        PDescriptorWrite write = {
            .set           = bindless->sets[i],
            .binding       = 2,
            .array_element = slot,
            .type          = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .count         = 1,
            .image_infos   = &info,
        };
        pigment_write_descriptors(pigment, &write, 1);
    }
}

static void write_cubemap_descriptor(Pigment* pigment, PStdBindless* bindless, uint32_t slot, PImage* image)
{
    PDescriptorImageInfo info = {.image = image};
    for(uint32_t i = 0; i < bindless->set_count; i++)
    {
        PDescriptorWrite write = {
            .set           = bindless->sets[i],
            .binding       = 1,
            .array_element = slot,
            .type          = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .count         = 1,
            .image_infos   = &info,
        };
        pigment_write_descriptors(pigment, &write, 1);
    }
}

static void batch_write_descriptors(Pigment* pigment, PStdBindless* bindless, uint32_t start_slot, uint32_t count)
{
    PDescriptorImageInfo* infos = malloc(count * sizeof(*infos));
    if(infos == NULL)
    {
        return;
    }
    for(uint32_t i = 0; i < count; i++)
    {
        infos[i].sampler = NULL;
        infos[i].image   = bindless->images.images[start_slot + i];
        infos[i].layout  = P_IMAGE_DESCRIPTOR_LAYOUT_AUTO;
    }
    for(uint32_t f = 0; f < bindless->set_count; f++)
    {
        PDescriptorWrite write = {
            .set           = bindless->sets[f],
            .binding       = 2,
            .array_element = start_slot,
            .type          = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .count         = count,
            .image_infos   = infos,
        };

        pigment_write_descriptors(pigment, &write, 1);
    }
    free(infos);
}
