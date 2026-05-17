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

#include "transfert.h"

#include "pigment/pigment.h"

#include "internal.h"

#include <string.h>

static PResult prepare_image_upload(Pigment* pigment, PImage** out_image, PBuffer** out_staging, const PImageUploadDesc* upload);
static void record_image_upload(Pigment* pigment, PCommandBuffer* cmd, PImage* image, PBuffer* staging, const PImageUploadDesc* upload);
static PResult host_copy_image(Pigment* pigment, PImage** out_image, const PImageUploadDesc* upload);

static inline uint32_t desc_layer_count(const PImageUploadDesc* upload)
{
    return upload->layer_count ? upload->layer_count : 1;
}

static inline uint32_t desc_depth(const PImageUploadDesc* upload)
{
    return upload->depth ? upload->depth : 1;
}

PResult pigment_std_buffer_upload(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, const PBufferUploadDesc* uploads, uint32_t count, PSubmitHandle* out_handle)
{
    if(pigment == NULL || pool == NULL || uploads == NULL || count == 0)
    {
        return PIGMENT_ERROR;
    }

    if(out_handle != NULL)
    {
        *out_handle = (PSubmitHandle) {0};
    }

    for(uint32_t i = 0; i < count; i++)
    {
        const PBufferUploadDesc* upload = &uploads[i];
        if(upload->dst == NULL || upload->data == NULL || upload->size == 0)
        {
            PLOG_ERROR(pigment, "Buffer upload %u is invalid (NULL dst, NULL data, or zero size).", i);
            return PIGMENT_ERROR;
        }
        if(upload->offset + upload->size > pigment_buffer_size(upload->dst))
        {
            PLOG_ERROR(pigment, "Buffer upload %u writes out of bounds (offset %llu + size %llu > buffer size %llu).", i, (unsigned long long) upload->offset, (unsigned long long) upload->size, (unsigned long long) pigment_buffer_size(upload->dst));
            return PIGMENT_ERROR;
        }
    }

    PBuffer** stagings = P_NEW_ARRAY_FOR_COMMAND(pigment, stagings, count);
    if(stagings == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    P_STACK_OR_HEAP(PBufferBarrier, barriers, count);
    if(barriers == NULL)
    {
        P_FREE(pigment, stagings);
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    PCommandBuffer* cmd    = NULL;
    PResult result         = PIGMENT_SUCCESS;
    uint32_t barrier_count = 0;

    for(uint32_t i = 0; i < count; i++)
    {
        const PBufferUploadDesc* upload = &uploads[i];

        void* dst_mapped = pigment_buffer_mapped(upload->dst);
        if(dst_mapped != NULL)
        {
            memcpy((unsigned char*) dst_mapped + upload->offset, upload->data, (size_t) upload->size);
            pigment_buffer_flush(pigment, upload->dst, upload->offset, upload->size);
            continue;
        }

        PBufferDesc staging_desc = {
            .size   = upload->size,
            .usage  = P_BUFFER_USAGE_TRANSFER_SRC,
            .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT | P_MEMORY_DEVICE_LOCAL_BIT},
        };

        stagings[i] = pigment_create_buffer(pigment, &staging_desc);
        if(stagings[i] == NULL)
        {
            result = PIGMENT_ERROR_OUT_OF_MEMORY;
            break;
        }

        memcpy(pigment_buffer_mapped(stagings[i]), upload->data, (size_t) upload->size);
        pigment_buffer_flush(pigment, stagings[i], 0, upload->size);

        if(cmd == NULL)
        {
            if(pigment_create_command_buffers(pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd) != PIGMENT_SUCCESS)
            {
                result = PIGMENT_ERROR;
                break;
            }
            pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
        }

        PBufferCopy region = {.src_offset = 0, .dst_offset = upload->offset, .size = upload->size};
        pigment_cmd_copy_buffer(pigment, cmd, stagings[i], upload->dst, &region, 1);

        barriers[barrier_count++] = (PBufferBarrier) {
            .buffer = upload->dst,
            .src    = {    P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
            .dst    = {P_PIPELINE_STAGE_ALL_COMMANDS_BIT,    P_MEMORY_ACCESS_MEMORY_READ_BIT},
            .offset = upload->offset,
            .size   = upload->size,
        };
    }

    PSubmitHandle handle = {0};
    if(cmd != NULL)
    {
        if(result == PIGMENT_SUCCESS)
        {
            pigment_cmd_buffer_barriers(pigment, cmd, barriers, barrier_count);
        }

        pigment_end_recording(pigment, cmd);
        if(result == PIGMENT_SUCCESS)
        {
            result = pigment_queue_submit(pigment, &(PSubmit) {.queue = queue, .cmds = &cmd, .cmd_count = 1}, 1, &handle);
        }
        pigment_destroy_command_buffers(pigment, &cmd, 1);
    }

    for(uint32_t i = 0; i < count; i++)
    {
        pigment_destroy_buffer(pigment, stagings[i]);
    }
    P_FREE(pigment, stagings);
    P_STACK_OR_HEAP_FREE(pigment, barriers);

    if(result != PIGMENT_SUCCESS)
    {
        return result;
    }

    if(out_handle != NULL)
    {
        *out_handle = handle;
    }
    return PIGMENT_SUCCESS;
}

static PResult prepare_image_upload(Pigment* pigment, PImage** out_image, PBuffer** out_staging, const PImageUploadDesc* upload)
{
    uint32_t pixel_size = pigment_format_pixel_size(upload->format);
    if(pixel_size == 0)
    {
        PLOG_ERROR(pigment, "Unsupported PFormat (%d)!", upload->format);
        return PIGMENT_ERROR;
    }

    uint32_t layer_count = desc_layer_count(upload);
    uint32_t depth       = desc_depth(upload);
    uint32_t mip_levels  = 1;
    if(upload->flags & P_IMAGE_UPLOAD_MIPMAPS)
    {
        uint32_t max_dim = upload->width;
        if(upload->height > max_dim)
        {
            max_dim = upload->height;
        }
        if(depth > max_dim)
        {
            max_dim = depth;
        }
        while(max_dim > 1)
        {
            max_dim >>= 1;
            mip_levels++;
        }
    }

    if(mip_levels > 1 && !pigment_format_supports_linear_blit(pigment, upload->format))
    {
        PLOG_ERROR(pigment, "Image format does not support linear blitting (needed for mip generation)!");
        return PIGMENT_ERROR;
    }

    uint64_t layer_size = (uint64_t) upload->width * upload->height * depth * pixel_size;
    uint64_t total_size = layer_size * layer_count;

    PBufferDesc staging_desc = {
        .size   = total_size,
        .usage  = P_BUFFER_USAGE_TRANSFER_SRC,
        .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT | P_MEMORY_DEVICE_LOCAL_BIT},
    };

    *out_staging = pigment_create_buffer(pigment, &staging_desc);
    if(*out_staging == NULL)
    {
        return PIGMENT_ERROR_VULKAN;
    }

    unsigned char* mapped = (unsigned char*) pigment_buffer_mapped(*out_staging);
    for(uint32_t i = 0; i < layer_count; i++)
    {
        memcpy(mapped + i * layer_size, upload->layers[i], (size_t) layer_size);
    }
    pigment_buffer_flush(pigment, *out_staging, 0, total_size);

    PImageDesc image_desc = {
        .width              = upload->width,
        .height             = upload->height,
        .depth              = depth,
        .array_layers       = layer_count,
        .format             = upload->format,
        .usage              = P_IMAGE_USAGE_TRANSFER_SRC | P_IMAGE_USAGE_TRANSFER_DST | P_IMAGE_USAGE_SAMPLED,
        .mip_levels         = mip_levels,
        .type               = upload->type,
        .shared_queues      = upload->shared_queues,
        .shared_queue_count = upload->shared_queue_count,
    };

    *out_image = pigment_create_image(pigment, &image_desc);
    if(*out_image == NULL)
    {
        return PIGMENT_ERROR_VULKAN;
    }

    return PIGMENT_SUCCESS;
}

static void record_image_upload(Pigment* pigment, PCommandBuffer* cmd, PImage* image, PBuffer* staging, const PImageUploadDesc* upload)
{
    uint32_t layer_count = desc_layer_count(upload);
    uint32_t depth       = desc_depth(upload);
    uint64_t layer_size  = (uint64_t) upload->width * upload->height * depth * pigment_format_pixel_size(upload->format);

    PImageBarrier to_dst = {
        .image       = image,
        .old_layout  = P_IMAGE_LAYOUT_UNDEFINED,
        .new_layout  = P_IMAGE_LAYOUT_TRANSFER_DST,
        .src         = {        P_PIPELINE_STAGE_NONE,               P_MEMORY_ACCESS_NONE},
        .dst         = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
        .layer_count = layer_count,
    };
    pigment_cmd_image_barriers(pigment, cmd, &to_dst, 1);

    P_STACK_OR_HEAP(PBufferImageCopy, regions, layer_count);
    if(regions == NULL)
    {
        return;
    }
    for(uint32_t i = 0; i < layer_count; i++)
    {
        regions[i] = (PBufferImageCopy) {
            .buffer_offset    = (uint64_t) i * layer_size,
            .base_array_layer = i,
            .layer_count      = 1,
            .extent_w         = upload->width,
            .extent_h         = upload->height,
            .extent_d         = depth,
        };
    }
    pigment_cmd_copy_buffer_to_image(pigment, cmd, staging, image, P_IMAGE_LAYOUT_TRANSFER_DST, regions, layer_count);
    P_STACK_OR_HEAP_FREE(pigment, regions);
}

static PResult host_copy_image(Pigment* pigment, PImage** out_image, const PImageUploadDesc* upload)
{
    *out_image = NULL;

    uint32_t layer_count = desc_layer_count(upload);
    uint32_t depth       = desc_depth(upload);

    PImageDesc image_desc = {
        .width              = upload->width,
        .height             = upload->height,
        .depth              = depth,
        .array_layers       = layer_count,
        .format             = upload->format,
        .usage              = P_IMAGE_USAGE_SAMPLED | P_IMAGE_USAGE_HOST_TRANSFER | P_IMAGE_USAGE_TRANSFER_DST,
        .mip_levels         = 1,
        .type               = upload->type,
        .shared_queues      = upload->shared_queues,
        .shared_queue_count = upload->shared_queue_count,
    };

    PImage* image = pigment_create_image(pigment, &image_desc);
    if(image == NULL)
    {
        return PIGMENT_ERROR;
    }

    pigment_image_host_transition(pigment, &(PHostImageTransition) {.image = image, .old_layout = P_IMAGE_LAYOUT_UNDEFINED, .new_layout = P_IMAGE_LAYOUT_GENERAL}, 1);

    P_STACK_OR_HEAP(PHostImageCopy, regions, layer_count);
    if(regions == NULL)
    {
        pigment_destroy_image(pigment, image);
        return PIGMENT_ERROR;
    }

    for(uint32_t i = 0; i < layer_count; i++)
    {
        regions[i] = (PHostImageCopy) {
            .host_pointer     = (void*) upload->layers[i],
            .base_array_layer = i,
            .layer_count      = 1,
            .extent_w         = upload->width,
            .extent_h         = upload->height,
            .extent_d         = depth,
        };
    }

    pigment_image_write(pigment, image, P_IMAGE_LAYOUT_GENERAL, regions, layer_count);
    P_STACK_OR_HEAP_FREE(pigment, regions);

    pigment_image_host_transition(pigment, &(PHostImageTransition) {.image = image, .old_layout = P_IMAGE_LAYOUT_GENERAL, .new_layout = P_IMAGE_LAYOUT_TRANSFER_DST}, 1);

    *out_image = image;
    return PIGMENT_SUCCESS;
}

PResult pigment_std_image_upload(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, const PImageUploadDesc* uploads, uint32_t count, PImage** out_images, PSubmitHandle* out_handle)
{
    if(pigment == NULL || pool == NULL || uploads == NULL || out_images == NULL || count == 0)
    {
        return PIGMENT_ERROR;
    }

    if(out_handle != NULL)
    {
        *out_handle = (PSubmitHandle) {0};
    }
    for(uint32_t i = 0; i < count; i++)
    {
        out_images[i] = NULL;
    }

    PBuffer** stagings = P_NEW_ARRAY_FOR_COMMAND(pigment, stagings, count);
    if(stagings == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    PBool host_copy     = pigment_supports(pigment, P_FEATURE_HOST_IMAGE_COPY) && pigment_memory_budget(pigment, P_MEMORY_DEVICE_LOCAL_BIT | P_MEMORY_HOST_VISIBLE_BIT) > 0;
    PCommandBuffer* cmd = NULL;
    PResult result      = PIGMENT_SUCCESS;

    for(uint32_t i = 0; i < count; i++)
    {
        const PImageUploadDesc* upload = &uploads[i];

        if(host_copy && !(upload->flags & P_IMAGE_UPLOAD_MIPMAPS))
        {
            if(host_copy_image(pigment, &out_images[i], upload) == PIGMENT_SUCCESS)
            {
                continue;
            }
            host_copy = P_FALSE;
            PLOG_TRACE(pigment, "Host image copy failed for image %u, using staging for the rest of the batch.", i);
        }

        result = prepare_image_upload(pigment, &out_images[i], &stagings[i], upload);
        if(result != PIGMENT_SUCCESS)
        {
            break;
        }

        if(cmd == NULL)
        {
            if(pigment_create_command_buffers(pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd) != PIGMENT_SUCCESS)
            {
                result = PIGMENT_ERROR;
                break;
            }
            pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
        }

        record_image_upload(pigment, cmd, out_images[i], stagings[i], upload);
    }

    PSubmitHandle handle = {0};
    if(cmd != NULL)
    {
        pigment_end_recording(pigment, cmd);
        if(result == PIGMENT_SUCCESS)
        {
            result = pigment_queue_submit(pigment, &(PSubmit) {.queue = queue, .cmds = &cmd, .cmd_count = 1}, 1, &handle);
        }
        pigment_destroy_command_buffers(pigment, &cmd, 1);
    }

    for(uint32_t i = 0; i < count; i++)
    {
        pigment_destroy_buffer(pigment, stagings[i]);
    }
    P_FREE(pigment, stagings);

    if(result != PIGMENT_SUCCESS)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            pigment_destroy_image(pigment, out_images[i]);
            out_images[i] = NULL;
        }
        return result;
    }

    if(out_handle != NULL)
    {
        *out_handle = handle;
    }
    return PIGMENT_SUCCESS;
}

PResult pigment_std_image_finalize(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, PImage* const* images, const PImageUploadDesc* uploads, uint32_t count, PSubmitHandle* out_handle)
{
    if(pigment == NULL || pool == NULL || images == NULL || uploads == NULL || count == 0)
    {
        return PIGMENT_ERROR;
    }

    if(out_handle != NULL)
    {
        *out_handle = (PSubmitHandle) {0};
    }

    PCommandBuffer* cmd = NULL;
    if(pigment_create_command_buffers(pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd) != PIGMENT_SUCCESS)
    {
        return PIGMENT_ERROR;
    }

    pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
    for(uint32_t i = 0; i < count; i++)
    {
        pigment_cmd_generate_mipmaps(pigment, cmd, images[i], 0, desc_layer_count(&uploads[i]), P_IMAGE_LAYOUT_SHADER_READ_ONLY);
    }
    pigment_end_recording(pigment, cmd);

    PSubmitHandle handle = {0};
    PResult result       = pigment_queue_submit(pigment, &(PSubmit) {.queue = queue, .cmds = &cmd, .cmd_count = 1}, 1, &handle);
    pigment_destroy_command_buffers(pigment, &cmd, 1);

    if(result == PIGMENT_SUCCESS && out_handle != NULL)
    {
        *out_handle = handle;
    }
    return result;
}
