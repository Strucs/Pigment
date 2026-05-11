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
#include "pigment.h"

#include <string.h>

PResult pigment_std_buffer_upload(Pigment* pigment, PCommandPool* pool, PBuffer* dst, const PBufferUploadDesc* desc, PSubmitHandle* out_handle)
{
    if(pigment == NULL || pool == NULL || dst == NULL || desc == NULL || desc->data == NULL || desc->size == 0)
    {
        return PIGMENT_ERROR;
    }

    if(desc->offset + desc->size > pigment_buffer_size(dst))
    {
        return PIGMENT_ERROR;
    }

    if(out_handle != NULL)
    {
        *out_handle = (PSubmitHandle) {0};
    }

    void* dst_mapped = pigment_buffer_mapped(dst);
    if(dst_mapped != NULL)
    {
        memcpy((unsigned char*) dst_mapped + desc->offset, desc->data, (size_t) desc->size);
        pigment_buffer_flush(pigment, dst, desc->offset, desc->size);
        return PIGMENT_SUCCESS;
    }

    PBufferDesc staging_desc = {
        .size   = desc->size,
        .usage  = P_BUFFER_USAGE_TRANSFER_SRC,
        .memory = P_MEMORY_HOST_UPLOAD,
    };

    PBuffer* staging = pigment_create_buffer(pigment, &staging_desc);
    if(staging == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    memcpy(pigment_buffer_mapped(staging), desc->data, (size_t) desc->size);
    pigment_buffer_flush(pigment, staging, 0, desc->size);

    PCommandBuffer* cmd = pigment_create_command_buffer(pigment, pool);
    if(cmd == NULL)
    {
        pigment_destroy_buffer(pigment, staging);
        return PIGMENT_ERROR;
    }
    pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    PBufferCopy region = {.src_offset = 0, .dst_offset = desc->offset, .size = desc->size};
    pigment_cmd_copy_buffer(pigment, cmd, staging, dst, &region, 1);

    PBufferBarrier barrier = {
        .buffer = dst,
        .src    = {    P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
        .dst    = {P_PIPELINE_STAGE_ALL_COMMANDS_BIT,    P_MEMORY_ACCESS_MEMORY_READ_BIT},
        .offset = desc->offset,
        .size   = desc->size,
    };
    pigment_cmd_buffer_barriers(pigment, cmd, &barrier, 1);

    pigment_end_recording(pigment, cmd);

    PSubmitHandle handle = pigment_queue_submit(pigment, &cmd, 1);

    pigment_destroy_command_buffer(pigment, cmd);
    pigment_destroy_buffer(pigment, staging);

    if(out_handle != NULL)
    {
        *out_handle = handle;
    }

    return PIGMENT_SUCCESS;
}
