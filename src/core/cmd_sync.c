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

#include "cmd_sync.h"
#include "internal.h"
#include "log_internal.h"

static VkPipelineStageFlags2 pipeline_stage_to_vk(PPipelineStage stages);
static VkAccessFlags2 memory_access_to_vk(PMemoryAccess access);

void pigment_cmd_image_barriers(Pigment* pigment, PCommandBuffer* cmd, const PImageBarrier* barriers, uint32_t count)
{
    if(pigment == NULL || cmd == NULL || barriers == NULL || count == 0)
    {
        return;
    }

    VkImageMemoryBarrier2* vk_barriers = calloc(count, sizeof(*vk_barriers));
    if(vk_barriers == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        const PImageBarrier* b = &barriers[i];

        uint32_t mip_count   = (b->mip_count == 0) ? VK_REMAINING_MIP_LEVELS : b->mip_count;
        uint32_t layer_count = (b->layer_count == 0) ? VK_REMAINING_ARRAY_LAYERS : b->layer_count;

        bool transfer  = (b->src_queue_family != b->dst_queue_family);
        vk_barriers[i] = (VkImageMemoryBarrier2) {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .oldLayout           = image_layout_to_vk(b->old_layout),
            .newLayout           = image_layout_to_vk(b->new_layout),
            .srcStageMask        = pipeline_stage_to_vk(b->src.stages),
            .srcAccessMask       = memory_access_to_vk(b->src.access),
            .dstStageMask        = pipeline_stage_to_vk(b->dst.stages),
            .dstAccessMask       = memory_access_to_vk(b->dst.access),
            .srcQueueFamilyIndex = transfer ? b->src_queue_family : VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = transfer ? b->dst_queue_family : VK_QUEUE_FAMILY_IGNORED,
            .image               = b->image->image,
            .subresourceRange    = {b->image->aspect, b->base_mip, mip_count, b->base_layer, layer_count},
        };
    }

    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = count,
        .pImageMemoryBarriers    = vk_barriers,
    };

    vkCmdPipelineBarrier2(cmd->buffer, &dep);

    free(vk_barriers);
}

void pigment_cmd_buffer_barriers(Pigment* pigment, PCommandBuffer* cmd, const PBufferBarrier* barriers, uint32_t count)
{
    if(pigment == NULL || cmd == NULL || barriers == NULL || count == 0)
    {
        return;
    }

    VkBufferMemoryBarrier2* vk_barriers = calloc(count, sizeof(*vk_barriers));
    if(vk_barriers == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        const PBufferBarrier* b = &barriers[i];

        bool transfer  = (b->src_queue_family != b->dst_queue_family);
        vk_barriers[i] = (VkBufferMemoryBarrier2) {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask        = pipeline_stage_to_vk(b->src.stages),
            .srcAccessMask       = memory_access_to_vk(b->src.access),
            .dstStageMask        = pipeline_stage_to_vk(b->dst.stages),
            .dstAccessMask       = memory_access_to_vk(b->dst.access),
            .srcQueueFamilyIndex = transfer ? b->src_queue_family : VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = transfer ? b->dst_queue_family : VK_QUEUE_FAMILY_IGNORED,
            .buffer              = b->buffer->buffer,
            .offset              = b->offset,
            .size                = (b->size == 0) ? VK_WHOLE_SIZE : b->size,
        };
    }

    VkDependencyInfo dep = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = count,
        .pBufferMemoryBarriers    = vk_barriers,
    };

    vkCmdPipelineBarrier2(cmd->buffer, &dep);

    free(vk_barriers);
}

void pigment_cmd_memory_barriers(Pigment* pigment, PCommandBuffer* cmd, const PMemoryBarrier* barriers, uint32_t count)
{
    if(pigment == NULL || cmd == NULL || barriers == NULL || count == 0)
    {
        return;
    }

    VkMemoryBarrier2* vk_barriers = calloc(count, sizeof(*vk_barriers));
    if(vk_barriers == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        const PMemoryBarrier* b = &barriers[i];

        vk_barriers[i] = (VkMemoryBarrier2) {
            .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask  = pipeline_stage_to_vk(b->src.stages),
            .srcAccessMask = memory_access_to_vk(b->src.access),
            .dstStageMask  = pipeline_stage_to_vk(b->dst.stages),
            .dstAccessMask = memory_access_to_vk(b->dst.access),
        };
    }

    VkDependencyInfo dep = {
        .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = count,
        .pMemoryBarriers    = vk_barriers,
    };

    vkCmdPipelineBarrier2(cmd->buffer, &dep);

    free(vk_barriers);
}

static VkPipelineStageFlags2 pipeline_stage_to_vk(PPipelineStage stages)
{
    VkPipelineStageFlags2 out = 0;
    if(stages & P_PIPELINE_STAGE_DRAW_INDIRECT_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    }
    if(stages & P_PIPELINE_STAGE_VERTEX_INPUT_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
    }
    if(stages & P_PIPELINE_STAGE_INDEX_INPUT_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    }
    if(stages & P_PIPELINE_STAGE_VERTEX_SHADER_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    }
    if(stages & P_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    }
    if(stages & P_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    }
    if(stages & P_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    }
    if(stages & P_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    }
    if(stages & P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    if(stages & P_PIPELINE_STAGE_TRANSFER_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    }
    if(stages & P_PIPELINE_STAGE_HOST_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_HOST_BIT;
    }
    if(stages & P_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    }
    if(stages & P_PIPELINE_STAGE_ALL_COMMANDS_BIT)
    {
        out |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
    return out;
}

static VkAccessFlags2 memory_access_to_vk(PMemoryAccess access)
{
    VkAccessFlags2 out = 0;
    if(access & P_MEMORY_ACCESS_INDIRECT_COMMAND_READ_BIT)
    {
        out |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    }
    if(access & P_MEMORY_ACCESS_INDEX_READ_BIT)
    {
        out |= VK_ACCESS_2_INDEX_READ_BIT;
    }
    if(access & P_MEMORY_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)
    {
        out |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if(access & P_MEMORY_ACCESS_UNIFORM_READ_BIT)
    {
        out |= VK_ACCESS_2_UNIFORM_READ_BIT;
    }
    if(access & P_MEMORY_ACCESS_SHADER_SAMPLED_READ_BIT)
    {
        out |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }
    if(access & P_MEMORY_ACCESS_SHADER_STORAGE_READ_BIT)
    {
        out |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    }
    if(access & P_MEMORY_ACCESS_SHADER_STORAGE_WRITE_BIT)
    {
        out |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    }
    if(access & P_MEMORY_ACCESS_COLOR_ATTACHMENT_READ_BIT)
    {
        out |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
    }
    if(access & P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
    {
        out |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if(access & P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT)
    {
        out |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    if(access & P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
    {
        out |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    if(access & P_MEMORY_ACCESS_TRANSFER_READ_BIT)
    {
        out |= VK_ACCESS_2_TRANSFER_READ_BIT;
    }
    if(access & P_MEMORY_ACCESS_TRANSFER_WRITE_BIT)
    {
        out |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if(access & P_MEMORY_ACCESS_HOST_READ_BIT)
    {
        out |= VK_ACCESS_2_HOST_READ_BIT;
    }
    if(access & P_MEMORY_ACCESS_HOST_WRITE_BIT)
    {
        out |= VK_ACCESS_2_HOST_WRITE_BIT;
    }
    return out;
}
