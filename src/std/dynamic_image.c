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

#include "dynamic_image.h"

#include "std_internal.h"

#include "pigment/pigment.h"

#include "internal_alloc.h"
#include "log_internal.h"

struct PDynamicImage {
    PImage* image;
    PBuffer** stagings;
    uint32_t ring_size;
    uint32_t ring_index;
    uint64_t upload_size;
    uint32_t width;
    uint32_t height;
    uint32_t block_width;
    uint32_t block_height;
    PBool initialized;
};

PDynamicImage* pigment_std_create_dynamic_image(Pigment* pigment, const PDynamicImageDesc* desc)
{
    if(pigment == NULL || desc == NULL || desc->width == 0 || desc->height == 0)
    {
        return NULL;
    }

    uint64_t upload_size = pigment_format_image_size(desc->format, desc->width, desc->height);
    if(upload_size == 0)
    {
        PLOG_ERROR(pigment, "Unsupported PFormat (%d)!", desc->format);
        return NULL;
    }

    PDynamicImage* dynamic_image = P_NEW_FOR_OBJECT(pigment, dynamic_image);
    if(dynamic_image == NULL)
    {
        return NULL;
    }

    PFormatInfo format_info     = pigment_format_info(desc->format);
    dynamic_image->ring_size    = desc->ring_size ? desc->ring_size : 2;
    dynamic_image->upload_size  = upload_size;
    dynamic_image->width        = desc->width;
    dynamic_image->height       = desc->height;
    dynamic_image->block_width  = format_info.block_width;
    dynamic_image->block_height = format_info.block_height;

    PImageDesc image_desc = {
        .width              = desc->width,
        .height             = desc->height,
        .format             = desc->format,
        .usage              = P_IMAGE_USAGE_SAMPLED | P_IMAGE_USAGE_TRANSFER_DST,
        .type               = P_IMAGE_TYPE_2D,
        .shared_queues      = desc->shared_queues,
        .shared_queue_count = desc->shared_queue_count,
        .name               = desc->name,
    };
    dynamic_image->image = pigment_create_image(pigment, &image_desc);
    if(dynamic_image->image == NULL)
    {
        goto ERROR;
    }

    dynamic_image->stagings = P_NEW_ARRAY_FOR_OBJECT(pigment, dynamic_image->stagings, dynamic_image->ring_size);
    if(dynamic_image->stagings == NULL)
    {
        goto ERROR;
    }

    PBufferDesc staging_desc = {
        .size   = upload_size,
        .usage  = P_BUFFER_USAGE_TRANSFER_SRC,
        .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT | P_MEMORY_DEVICE_LOCAL_BIT},
    };

    for(uint32_t i = 0; i < dynamic_image->ring_size; i++)
    {
        dynamic_image->stagings[i] = pigment_create_buffer(pigment, &staging_desc);
        if(dynamic_image->stagings[i] == NULL)
        {
            goto ERROR;
        }
    }

    return dynamic_image;

ERROR:
    pigment_std_destroy_dynamic_image(pigment, dynamic_image);
    return NULL;
}

void pigment_std_destroy_dynamic_image(Pigment* pigment, PDynamicImage* dynamic_image)
{
    if(pigment == NULL || dynamic_image == NULL)
    {
        return;
    }

    if(dynamic_image->stagings != NULL)
    {
        for(uint32_t i = 0; i < dynamic_image->ring_size; i++)
        {
            pigment_destroy_buffer(pigment, dynamic_image->stagings[i]);
        }
        P_FREE(pigment, dynamic_image->stagings);
    }

    pigment_destroy_image(pigment, dynamic_image->image);
    P_FREE(pigment, dynamic_image);
}

PResult pigment_std_update_dynamic_image(Pigment* pigment, PCommandBuffer* cmd, PDynamicImage* dynamic_image, const void* pixels, uint64_t size)
{
    if(pigment == NULL || cmd == NULL || dynamic_image == NULL || pixels == NULL)
    {
        return PIGMENT_ERROR;
    }

    if(size < dynamic_image->upload_size)
    {
        PLOG_ERROR(pigment, "Dynamic image update buffer too small (got %llu, need %llu).", (unsigned long long) size, (unsigned long long) dynamic_image->upload_size);
        return PIGMENT_ERROR;
    }

    PBuffer* staging = dynamic_image->stagings[dynamic_image->ring_index];
    pigment_buffer_wait(pigment, staging);

    memcpy(pigment_buffer_mapped(staging), pixels, (size_t) dynamic_image->upload_size);
    pigment_buffer_flush(pigment, staging, 0, dynamic_image->upload_size);

    PImageBarrier to_dst = {
        .image       = dynamic_image->image,
        .old_layout  = P_IMAGE_LAYOUT_UNDEFINED,
        .new_layout  = P_IMAGE_LAYOUT_TRANSFER_DST,
        .src         = {        P_PIPELINE_STAGE_NONE,               P_MEMORY_ACCESS_NONE},
        .dst         = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
        .layer_count = 1,
    };

    if(dynamic_image->initialized)
    {
        to_dst.old_layout = P_IMAGE_LAYOUT_SHADER_READ_ONLY;
        to_dst.src.stages = P_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        to_dst.src.access = P_MEMORY_ACCESS_SHADER_SAMPLED_READ_BIT;
    }

    pigment_cmd_image_barriers(pigment, cmd, &to_dst, 1);

    PBufferImageCopy region = {
        .buffer_offset       = 0,
        .buffer_row_length   = block_align(dynamic_image->width, dynamic_image->block_width),
        .buffer_image_height = block_align(dynamic_image->height, dynamic_image->block_height),
        .mip_level           = 0,
        .base_array_layer    = 0,
        .layer_count         = 1,
        .extent_w            = dynamic_image->width,
        .extent_h            = dynamic_image->height,
        .extent_d            = 1,
    };
    pigment_cmd_copy_buffer_to_image(pigment, cmd, staging, dynamic_image->image, P_IMAGE_LAYOUT_TRANSFER_DST, &region, 1);

    PImageBarrier to_sampled = {
        .image       = dynamic_image->image,
        .old_layout  = P_IMAGE_LAYOUT_TRANSFER_DST,
        .new_layout  = P_IMAGE_LAYOUT_SHADER_READ_ONLY,
        .src         = {    P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
        .dst         = {P_PIPELINE_STAGE_ALL_COMMANDS_BIT,    P_MEMORY_ACCESS_MEMORY_READ_BIT},
        .layer_count = 1,
    };
    pigment_cmd_image_barriers(pigment, cmd, &to_sampled, 1);

    dynamic_image->initialized = P_TRUE;

    uint32_t next_index       = dynamic_image->ring_index + 1;
    dynamic_image->ring_index = next_index * (next_index < dynamic_image->ring_size);
    return PIGMENT_SUCCESS;
}

PImage* pigment_std_dynamic_image_get(PDynamicImage* dynamic_image)
{
    return dynamic_image != NULL ? dynamic_image->image : NULL;
}
