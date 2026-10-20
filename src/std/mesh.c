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

#include "mesh.h"

#include "transfert.h"
#include "std_internal.h"

#include "pigment/pigment.h"

#include "pigment/alloc.h"

PMeshBuffers* pigment_std_cmd_upload_mesh(Pigment* pigment, PCommandBuffer* cmd, const PMeshUploadDesc* desc)
{
    if(pigment == NULL || cmd == NULL || desc == NULL || desc->vertices == NULL || desc->vertices_size == 0
       || desc->indices == NULL || desc->index_count == 0
       || !shared_queues_valid(pigment, desc->shared_queues, desc->shared_queue_count))
    {
        return NULL;
    }

    PMeshBuffers* mesh = P_NEW_FOR_OBJECT(pigment, mesh);
    if(mesh == NULL)
    {
        return NULL;
    }

    PBufferDesc vertex_buffer_desc = {
        .size               = (uint64_t) desc->vertices_size,
        .usage              = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS | P_BUFFER_USAGE_TRANSFER_DST,
        .memory             = {.required = P_MEMORY_DEVICE_LOCAL_BIT},
        .shared_queues      = desc->shared_queues,
        .shared_queue_count = desc->shared_queue_count,
    };
    mesh->vertex_buffer = pigment_create_buffer(pigment, &vertex_buffer_desc);
    if(mesh->vertex_buffer == NULL)
    {
        goto ERROR;
    }

    PBufferDesc index_buffer_desc = {
        .size               = (uint64_t) desc->index_count * sizeof(*desc->indices),
        .usage              = P_BUFFER_USAGE_INDEX | P_BUFFER_USAGE_TRANSFER_DST,
        .memory             = {.required = P_MEMORY_DEVICE_LOCAL_BIT},
        .shared_queues      = desc->shared_queues,
        .shared_queue_count = desc->shared_queue_count,
    };
    mesh->index_buffer = pigment_create_buffer(pigment, &index_buffer_desc);
    if(mesh->index_buffer == NULL)
    {
        goto ERROR;
    }

    PBufferUploadDesc uploads[] = {
        {.dst = mesh->vertex_buffer, .data = desc->vertices, .size = vertex_buffer_desc.size},
        { .dst = mesh->index_buffer,  .data = desc->indices,  .size = index_buffer_desc.size},
    };
    if(pigment_std_cmd_buffer_upload(pigment, cmd, uploads, sizeof(uploads) / sizeof(uploads[0])) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    mesh->index_count = desc->index_count;
    mesh->index_type  = P_INDEX_TYPE_UINT32;
    return mesh;

ERROR:
    pigment_std_destroy_mesh(pigment, mesh);
    return NULL;
}

PMeshBuffers* pigment_std_upload_mesh(Pigment* pigment, PCommandPool* pool, PDeviceQueue* queue, const PMeshUploadDesc* desc, PSubmitHandle* out_handle)
{
    if(out_handle != NULL)
    {
        *out_handle = (PSubmitHandle) {0};
    }

    if(pigment == NULL || pool == NULL || desc == NULL)
    {
        return NULL;
    }

    if(queue == NULL)
    {
        queue = pigment_get_queue(pigment, P_QUEUE_GRAPHICS_BIT);
    }

    if(!shared_queues_valid(pigment, &queue, 1)
       || !shared_queues_valid(pigment, desc->shared_queues, desc->shared_queue_count))
    {
        return NULL;
    }

    if(desc->shared_queue_count > 0)
    {
        PBool found = P_FALSE;
        for(uint32_t i = 0; i < desc->shared_queue_count; i++)
        {
            found |= pigment_queue_family(desc->shared_queues[i]) == pigment_queue_family(queue);
        }

        if(!found)
        {
            PLOG_ERROR(pigment, "Mesh shared queues must include the upload family.");
            return NULL;
        }
    }

    PCommandBuffer* cmd = NULL;
    if(pigment_create_command_buffers(pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd) != PIGMENT_SUCCESS)
    {
        return NULL;
    }

    pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
    PMeshBuffers* mesh = pigment_std_cmd_upload_mesh(pigment, cmd, desc);
    pigment_end_recording(pigment, cmd);

    PSubmit submit = {.queue = queue, .cmds = &cmd, .cmd_count = 1};
    if(mesh != NULL && pigment_queue_submit(pigment, &submit, 1, out_handle) != PIGMENT_SUCCESS)
    {
        pigment_std_destroy_mesh(pigment, mesh);
        mesh = NULL;
    }

    pigment_destroy_command_buffers(pigment, &cmd, 1);

    return mesh;
}

void pigment_std_destroy_mesh(Pigment* pigment, PMeshBuffers* mesh)
{
    if(pigment == NULL || mesh == NULL)
    {
        return;
    }
    pigment_destroy_buffer(pigment, mesh->vertex_buffer);
    pigment_destroy_buffer(pigment, mesh->index_buffer);
    P_FREE(pigment, mesh);
}

uint64_t pigment_std_mesh_vertex_address(PMeshBuffers* mesh)
{
    if(mesh == NULL)
    {
        return 0;
    }
    return (uint64_t) pigment_buffer_address(mesh->vertex_buffer);
}

void pigment_std_mesh_use(Pigment* pigment, PCommandBuffer* cmd, PMeshBuffers* mesh)
{
    if(pigment == NULL || cmd == NULL || mesh == NULL || mesh->vertex_buffer == NULL)
    {
        return;
    }
    pigment_cmd_use_buffer(pigment, cmd, mesh->vertex_buffer);
}
