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

#include "frame.h"

#include "cmd_sync.h"
#include "commands.h"
#include "deletion.h"
#include "image.h"
#include "surface.h"
#include "synchronization.h"

#include "internal.h"

void pigment_wait_frame_ready(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return;
    }

    uint32_t current_frame = renderer->swapchain->current_frame;
    PSubmitHandle slot     = renderer->sync->per_slot_handle[current_frame];
    if(slot.queue == NULL || slot.value == 0 || slot.queue->timeline == VK_NULL_HANDLE)
    {
        return;
    }

    VkSemaphoreWaitInfo wait_info = {
        .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores    = &slot.queue->timeline,
        .pValues        = &slot.value,
    };
    vkWaitSemaphores(pigment->device->logical_device, &wait_info, UINT64_MAX);
}

PCommandBuffer* pigment_begin_frame(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return NULL;
    }

    drain_deletion_queue(pigment);

    if(renderer->needs_recreate)
    {
        return NULL;
    }

    PDevice* device        = pigment->device;
    uint32_t current_frame = renderer->swapchain->current_frame;

    VkResult result = vkAcquireNextImageKHR(device->logical_device, renderer->swapchain->swapchain, UINT64_MAX, renderer->sync->image_available_semaphores[current_frame], VK_NULL_HANDLE, &renderer->current_image_index);

    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreate_image_available_semaphore(pigment, renderer->sync, current_frame);
        renderer->needs_recreate = P_TRUE;
        return NULL;
    }
    else if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to acquire swapchain image!");
        return NULL;
    }

    PCommandBuffer* cmd = renderer->command_buffers[current_frame];

    if((result = vkResetCommandBuffer(cmd->buffer, 0)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to reset command buffer! (result: %d)", result);
        return NULL;
    }

    pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);
    return cmd;
}

uint32_t pigment_renderer_current_frame(PWindowRenderer* renderer)
{
    if(renderer == NULL || renderer->swapchain == NULL)
    {
        return 0;
    }
    return renderer->swapchain->current_frame;
}

PCommandBuffer* pigment_renderer_frame_cmd(PWindowRenderer* renderer)
{
    if(renderer == NULL || renderer->swapchain == NULL)
    {
        return NULL;
    }

    return renderer->command_buffers[renderer->swapchain->current_frame];
}

void pigment_begin_swapchain_pass(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return;
    }
    uint32_t image_index       = renderer->current_image_index;
    uint32_t current_frame_idx = renderer->swapchain->current_frame;
    PCommandBuffer* cmd        = renderer->command_buffers[current_frame_idx];
    PSwapchain* swapchain      = renderer->swapchain;
    PBool transparent          = renderer->desc.transparent;
    PBool multisample          = (swapchain->color_multisample != NULL);

    VkImageMemoryBarrier2 color_barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask       = VK_ACCESS_2_NONE,
        .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = swapchain->images[image_index],
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };

    VkDependencyInfo dep_color = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &color_barrier,
    };

    vkCmdPipelineBarrier2(cmd->buffer, &dep_color);

    if(multisample)
    {
        PImageBarrier color_multisample_barrier = {
            .image       = swapchain->color_multisample,
            .old_layout  = P_IMAGE_LAYOUT_UNDEFINED,
            .new_layout  = P_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .src         = {                       P_PIPELINE_STAGE_NONE,                       P_MEMORY_ACCESS_NONE},
            .dst         = {P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
            .mip_count   = 1,
            .layer_count = 1,
        };
        pigment_cmd_image_barriers(pigment, cmd, &color_multisample_barrier, 1);
    }

    PImageBarrier depth_barrier = {
        .image       = swapchain->depth,
        .old_layout  = P_IMAGE_LAYOUT_UNDEFINED,
        .new_layout  = P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
        .src         = { P_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,                                                     P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        .dst         = {P_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        .mip_count   = 1,
        .layer_count = 1,
    };

    pigment_cmd_image_barriers(pigment, cmd, &depth_barrier, 1);

    VkClearColorValue clear_color_value = {
        {0.0f, 0.0f, 0.0f, transparent ? 0.0f : 1.0f},
    };

    VkClearDepthStencilValue clear_depth_stencil_value = {pigment->config.depth_clear_value, 0};

    VkImageView color_view = multisample ? image_get_or_create_view(pigment, swapchain->color_multisample, &(PImageViewDesc) {0})->view
                                         : swapchain->image_views[image_index];

    VkRenderingAttachmentInfo color_attachment = {
        .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView          = color_view,
        .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp            = multisample ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue         = {.color = clear_color_value},
        .resolveMode        = multisample ? VK_RESOLVE_MODE_AVERAGE_BIT : VK_RESOLVE_MODE_NONE,
        .resolveImageView   = multisample ? swapchain->image_views[image_index] : VK_NULL_HANDLE,
        .resolveImageLayout = multisample ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
    };

    PImageView* depth_view = image_get_or_create_view(pigment, swapchain->depth, &(PImageViewDesc) {0});

    VkRenderingAttachmentInfo depth_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView   = depth_view->view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue  = {.depthStencil = clear_depth_stencil_value},
    };

    PBool has_stencil = (swapchain->depth->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;

    VkRenderingAttachmentInfo stencil_attachment = depth_attachment;

    VkRenderingInfoKHR rendering_info = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .renderArea           = {{0, 0}, swapchain->extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_attachment,
        .pDepthAttachment     = &depth_attachment,
        .pStencilAttachment   = has_stencil ? &stencil_attachment : NULL,
    };

    vkCmdBeginRendering(cmd->buffer, &rendering_info);

    VkViewport viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float) swapchain->extent.width,
        .height   = (float) swapchain->extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        {0, 0},
        swapchain->extent,
    };

    vkCmdSetViewportWithCount(cmd->buffer, 1, &viewport);
    vkCmdSetScissorWithCount(cmd->buffer, 1, &scissor);
}

void pigment_end_swapchain_pass(PWindowRenderer* renderer)
{
    if(renderer == NULL)
    {
        return;
    }

    uint32_t image_index   = renderer->current_image_index;
    uint32_t current_frame = renderer->swapchain->current_frame;
    VkCommandBuffer cmd    = renderer->command_buffers[current_frame]->buffer;

    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 barrier_to_present = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask       = VK_ACCESS_2_NONE,
        .oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = renderer->swapchain->images[image_index],
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };

    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier_to_present,
    };

    vkCmdPipelineBarrier2(cmd, &dep);
}

void pigment_begin_render_pass(Pigment* pigment, PCommandBuffer* cmd, const PRenderPassDesc* desc)
{
    if(pigment == NULL || cmd == NULL || desc == NULL)
    {
        return;
    }
    PBool has_ds_attachment = (desc->depth_attachment.image != NULL);
    PBool has_depth         = has_ds_attachment && (desc->depth_attachment.image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
    PBool has_stencil       = has_ds_attachment && (desc->depth_attachment.image->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
    if(desc->color_count == 0 && !has_ds_attachment)
    {
        return;
    }

    uint32_t width  = UINT32_MAX;
    uint32_t height = UINT32_MAX;
    for(uint32_t i = 0; i < desc->color_count; i++)
    {
        const PAttachmentRef* ref = &desc->color_attachments[i];

        uint32_t w = ref->image->width >> ref->mip_level;
        uint32_t h = ref->image->height >> ref->mip_level;
        if(w < width)
        {
            width = w;
        }
        if(h < height)
        {
            height = h;
        }
    }

    if(has_ds_attachment)
    {
        const PAttachmentRef* ref = &desc->depth_attachment;

        uint32_t w = ref->image->width >> ref->mip_level;
        uint32_t h = ref->image->height >> ref->mip_level;
        if(w < width)
        {
            width = w;
        }
        if(h < height)
        {
            height = h;
        }
    }

    if(width == 0 || width == UINT32_MAX)
    {
        width = 1;
    }

    if(height == 0 || height == UINT32_MAX)
    {
        height = 1;
    }

    uint32_t resolve_count = 0;
    for(uint32_t i = 0; i < desc->color_count; i++)
    {
        if(desc->color_attachments[i].resolve_image != NULL)
        {
            resolve_count++;
        }
    }
    PBool has_depth_resolve = has_ds_attachment && (desc->depth_attachment.resolve_image != NULL);

    uint32_t max_barrier_count = desc->color_count + resolve_count + (has_ds_attachment ? 1 : 0) + (has_depth_resolve ? 1 : 0);
    P_STACK_OR_HEAP(PImageBarrier, barriers, max_barrier_count);
    P_STACK_OR_HEAP(VkRenderingAttachmentInfo, color, desc->color_count);

    if(barriers == NULL || (desc->color_count > 0 && color == NULL))
    {
        goto FREE;
    }

    uint32_t barrier_count = 0;

    VkClearColorValue clear_color = {
        {desc->clear_color[0], desc->clear_color[1], desc->clear_color[2], desc->clear_color[3]}
    };

    for(uint32_t i = 0; i < desc->color_count; i++)
    {
        const PAttachmentRef* ref = &desc->color_attachments[i];
        PBool has_resolve         = (ref->resolve_image != NULL);

        PImage* img               = ref->image;
        PBool preserve_contents   = (ref->load_op == P_LOAD_OP_LOAD);
        barriers[barrier_count++] = (PImageBarrier) {
            .image       = img,
            .old_layout  = preserve_contents ? P_IMAGE_LAYOUT_COLOR_ATTACHMENT : P_IMAGE_LAYOUT_UNDEFINED,
            .new_layout  = P_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .src         = {preserve_contents ? P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : P_PIPELINE_STAGE_NONE,                                                                     preserve_contents ? P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : P_MEMORY_ACCESS_NONE},
            .dst         = {                                            P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, preserve_contents ? (P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | P_MEMORY_ACCESS_COLOR_ATTACHMENT_READ_BIT) : P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
            .base_mip    = ref->mip_level,
            .mip_count   = 1,
            .base_layer  = ref->base_layer,
            .layer_count = ref->layer_count,
        };

        PImageViewDesc view_desc = {
            .base_layer  = ref->base_layer,
            .layer_count = ref->layer_count,
            .base_mip    = ref->mip_level,
            .mip_count   = 1,
        };

        PImageView* view = image_get_or_create_view(pigment, img, &view_desc);

        VkImageView resolve_view_handle = VK_NULL_HANDLE;
        if(has_resolve)
        {
            barriers[barrier_count++] = (PImageBarrier) {
                .image       = ref->resolve_image,
                .old_layout  = P_IMAGE_LAYOUT_UNDEFINED,
                .new_layout  = P_IMAGE_LAYOUT_COLOR_ATTACHMENT,
                .src         = {                       P_PIPELINE_STAGE_NONE,                       P_MEMORY_ACCESS_NONE},
                .dst         = {P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
                .base_mip    = ref->resolve_mip_level,
                .mip_count   = 1,
                .base_layer  = ref->resolve_base_layer,
                .layer_count = ref->layer_count,
            };

            PImageViewDesc resolve_view_desc = {
                .base_layer  = ref->resolve_base_layer,
                .layer_count = ref->layer_count,
                .base_mip    = ref->resolve_mip_level,
                .mip_count   = 1,
            };
            PImageView* resolve_view = image_get_or_create_view(pigment, ref->resolve_image, &resolve_view_desc);
            resolve_view_handle      = resolve_view->view;
        }

        color[i] = (VkRenderingAttachmentInfo) {
            .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView          = view->view,
            .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp             = load_op_to_vk(ref->load_op),
            .storeOp            = store_op_to_vk(ref->store_op, has_resolve),
            .clearValue         = {.color = clear_color},
            .resolveMode        = has_resolve ? resolve_mode_to_vk(ref->resolve_mode, P_FALSE) : VK_RESOLVE_MODE_NONE,
            .resolveImageView   = resolve_view_handle,
            .resolveImageLayout = has_resolve ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        };
    }

    VkRenderingAttachmentInfo depth   = {0};
    VkRenderingAttachmentInfo stencil = {0};
    if(has_ds_attachment)
    {
        const PAttachmentRef* ref = &desc->depth_attachment;
        PImage* img               = ref->image;
        PBool preserve_depth      = (ref->load_op == P_LOAD_OP_LOAD);
        barriers[barrier_count++] = (PImageBarrier) {
            .image       = img,
            .old_layout  = preserve_depth ? P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT : P_IMAGE_LAYOUT_UNDEFINED,
            .new_layout  = P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
            .src         = { P_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,                                                     P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
            .dst         = {P_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
            .base_mip    = ref->mip_level,
            .mip_count   = 1,
            .base_layer  = ref->base_layer,
            .layer_count = ref->layer_count,
        };

        PImageViewDesc view_desc = {
            .base_layer  = ref->base_layer,
            .layer_count = ref->layer_count,
            .base_mip    = ref->mip_level,
            .mip_count   = 1,
        };
        PImageView* view = image_get_or_create_view(pigment, img, &view_desc);

        VkImageView depth_resolve_view_handle = VK_NULL_HANDLE;
        if(has_depth_resolve)
        {
            barriers[barrier_count++] = (PImageBarrier) {
                .image       = ref->resolve_image,
                .old_layout  = P_IMAGE_LAYOUT_UNDEFINED,
                .new_layout  = P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
                .src         = { P_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,                                                     P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
                .dst         = {P_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
                .base_mip    = ref->resolve_mip_level,
                .mip_count   = 1,
                .base_layer  = ref->resolve_base_layer,
                .layer_count = ref->layer_count,
            };

            PImageViewDesc resolve_view_desc = {
                .base_layer  = ref->resolve_base_layer,
                .layer_count = ref->layer_count,
                .base_mip    = ref->resolve_mip_level,
                .mip_count   = 1,
            };
            PImageView* resolve_view  = image_get_or_create_view(pigment, ref->resolve_image, &resolve_view_desc);
            depth_resolve_view_handle = resolve_view->view;
        }

        VkClearDepthStencilValue clear_depth_stencil_value = {desc->depth_clear_value, desc->stencil_clear_value};

        VkRenderingAttachmentInfo base_attachment = {
            .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView          = view->view,
            .imageLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .clearValue         = {.depthStencil = clear_depth_stencil_value},
            .resolveMode        = has_depth_resolve ? resolve_mode_to_vk(ref->resolve_mode, P_TRUE) : VK_RESOLVE_MODE_NONE,
            .resolveImageView   = depth_resolve_view_handle,
            .resolveImageLayout = has_depth_resolve ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if(has_depth)
        {
            depth         = base_attachment;
            depth.loadOp  = load_op_to_vk(ref->load_op);
            depth.storeOp = store_op_to_vk(ref->store_op, has_depth_resolve);
        }

        if(has_stencil)
        {
            stencil         = base_attachment;
            stencil.loadOp  = load_op_to_vk(ref->stencil_load_op);
            stencil.storeOp = store_op_to_vk(ref->stencil_store_op, has_depth_resolve);
        }
    }

    pigment_cmd_image_barriers(pigment, cmd, barriers, barrier_count);

    uint32_t layer_count = (desc->layer_count == 0) ? 1 : desc->layer_count;

    VkRenderingInfo rendering_info = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .renderArea           = {{0, 0}, {width, height}},
        .layerCount           = layer_count,
        .viewMask             = desc->view_mask,
        .colorAttachmentCount = desc->color_count,
        .pColorAttachments    = color,
        .pDepthAttachment     = has_depth ? &depth : NULL,
        .pStencilAttachment   = has_stencil ? &stencil : NULL,
    };
    vkCmdBeginRendering(cmd->buffer, &rendering_info);

    VkViewport viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float) width,
        .height   = (float) height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        {    0,      0},
        {width, height}
    };

    vkCmdSetViewportWithCount(cmd->buffer, 1, &viewport);
    vkCmdSetScissorWithCount(cmd->buffer, 1, &scissor);

FREE:
    P_STACK_OR_HEAP_FREE(pigment, barriers);
    P_STACK_OR_HEAP_FREE(pigment, color);
}

void pigment_end_render_pass(Pigment* pigment, PCommandBuffer* cmd, const PRenderPassDesc* desc)
{
    if(pigment == NULL || cmd == NULL || desc == NULL)
    {
        return;
    }

    vkCmdEndRendering(cmd->buffer);

    PBool has_ds_attachment = (desc->depth_attachment.image != NULL);
    uint32_t max_barriers   = desc->color_count * 2 + (has_ds_attachment ? 2 : 0);
    if(max_barriers == 0)
    {
        return;
    }

    P_STACK_OR_HEAP(PImageBarrier, barriers, max_barriers);
    if(barriers == NULL)
    {
        return;
    }

    uint32_t barrier_count = 0;

    for(uint32_t i = 0; i < desc->color_count; i++)
    {
        const PAttachmentRef* ref = &desc->color_attachments[i];

        PImageLayout image_target = pick_end_pass_layout(ref->final_layout, ref->image, P_IMAGE_LAYOUT_COLOR_ATTACHMENT);
        if(image_target != P_IMAGE_LAYOUT_COLOR_ATTACHMENT)
        {
            PPipelineStage dst_stage = 0;
            PMemoryAccess dst_access = 0;
            layout_to_dst_sync(image_target, &dst_stage, &dst_access);

            barriers[barrier_count++] = (PImageBarrier) {
                .image       = ref->image,
                .old_layout  = P_IMAGE_LAYOUT_COLOR_ATTACHMENT,
                .new_layout  = image_target,
                .src         = {P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
                .dst         = {                                   dst_stage,                                 dst_access},
                .base_mip    = ref->mip_level,
                .mip_count   = 1,
                .base_layer  = ref->base_layer,
                .layer_count = ref->layer_count,
            };
        }

        if(ref->resolve_image != NULL)
        {
            PImageLayout resolve_target = pick_end_pass_layout(ref->final_layout, ref->resolve_image, P_IMAGE_LAYOUT_COLOR_ATTACHMENT);
            if(resolve_target != P_IMAGE_LAYOUT_COLOR_ATTACHMENT)
            {
                PPipelineStage dst_stage = 0;
                PMemoryAccess dst_access = 0;
                layout_to_dst_sync(resolve_target, &dst_stage, &dst_access);

                barriers[barrier_count++] = (PImageBarrier) {
                    .image       = ref->resolve_image,
                    .old_layout  = P_IMAGE_LAYOUT_COLOR_ATTACHMENT,
                    .new_layout  = resolve_target,
                    .src         = {P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
                    .dst         = {                                   dst_stage,                                 dst_access},
                    .base_mip    = ref->resolve_mip_level,
                    .mip_count   = 1,
                    .base_layer  = ref->resolve_base_layer,
                    .layer_count = ref->layer_count,
                };
            }
        }
    }

    if(has_ds_attachment)
    {
        const PAttachmentRef* ref = &desc->depth_attachment;

        PImageLayout image_target = pick_end_pass_layout(ref->final_layout, ref->image, P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT);
        if(image_target != P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT)
        {
            PPipelineStage dst_stage = 0;
            PMemoryAccess dst_access = 0;
            layout_to_dst_sync(image_target, &dst_stage, &dst_access);

            barriers[barrier_count++] = (PImageBarrier) {
                .image       = ref->image,
                .old_layout  = P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
                .new_layout  = image_target,
                .src         = {P_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
                .dst         = {                               dst_stage,                                         dst_access},
                .base_mip    = ref->mip_level,
                .mip_count   = 1,
                .base_layer  = ref->base_layer,
                .layer_count = ref->layer_count,
            };
        }

        if(ref->resolve_image != NULL)
        {
            PImageLayout resolve_target = pick_end_pass_layout(ref->final_layout, ref->resolve_image, P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT);
            if(resolve_target != P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT)
            {
                PPipelineStage dst_stage = 0;
                PMemoryAccess dst_access = 0;
                layout_to_dst_sync(resolve_target, &dst_stage, &dst_access);

                barriers[barrier_count++] = (PImageBarrier) {
                    .image       = ref->resolve_image,
                    .old_layout  = P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
                    .new_layout  = resolve_target,
                    .src         = {P_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
                    .dst         = {                               dst_stage,                                         dst_access},
                    .base_mip    = ref->resolve_mip_level,
                    .mip_count   = 1,
                    .base_layer  = ref->resolve_base_layer,
                    .layer_count = ref->layer_count,
                };
            }
        }
    }

    pigment_cmd_image_barriers(pigment, cmd, barriers, barrier_count);

    P_STACK_OR_HEAP_FREE(pigment, barriers);
}

void pigment_end_recording_frame(Pigment* pigment, PWindowRenderer* renderer)
{
    uint32_t current_frame = renderer->swapchain->current_frame;
    VkCommandBuffer cmd    = renderer->command_buffers[current_frame]->buffer;

    VkResult result = vkEndCommandBuffer(cmd);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to end frame command buffer! (result: %d)", result);
    }
}

PSubmitHandle pigment_queue_submit_frame(Pigment* pigment, PWindowRenderer* renderer, PDeviceQueue* queue)
{
    if(pigment == NULL || renderer == NULL)
    {
        return (PSubmitHandle) {0};
    }

    if(queue == NULL)
    {
        queue = device_find_queue(pigment->device, P_QUEUE_GRAPHICS_BIT, 0);
    }

    if(queue == NULL || queue->timeline == VK_NULL_HANDLE)
    {
        PLOG_ERROR(pigment, "Frame submit queue unavailable, frame submit aborted.");
        return (PSubmitHandle) {0};
    }

    uint32_t current_frame    = renderer->swapchain->current_frame;
    uint32_t image_index      = renderer->current_image_index;
    PCommandBuffer* frame_cmd = renderer->command_buffers[current_frame];

    PSubmitHandle handle                           = {.queue = queue, .value = device_queue_acquire_value(queue)};
    renderer->sync->per_slot_handle[current_frame] = handle;
    atomic_store_explicit(&renderer->tracker.last_used[queue->slot], handle.value, memory_order_relaxed);

    stamp_uses_submit(&frame_cmd, 1, queue->slot, handle.value);

    VkSemaphoreSubmitInfo wait_info = {
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = renderer->sync->image_available_semaphores[current_frame],
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    VkSemaphoreSubmitInfo signal_infos[2] = {
        {
         .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .semaphore = renderer->sync->render_finished_semaphores[image_index],
         .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
         },
        {
         .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .semaphore = queue->timeline,
         .value     = handle.value,
         .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
         },
    };

    VkCommandBufferSubmitInfo cmd_info = {
        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame_cmd->buffer,
    };

    VkSubmitInfo2 submit_info = {
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount   = 1,
        .pWaitSemaphoreInfos      = &wait_info,
        .commandBufferInfoCount   = 1,
        .pCommandBufferInfos      = &cmd_info,
        .signalSemaphoreInfoCount = 2,
        .pSignalSemaphoreInfos    = signal_infos,
    };

    VkResult result = vkQueueSubmit2(queue->queue, 1, &submit_info, VK_NULL_HANDLE);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to submit draw command buffer! (result: %d)", result);
        return (PSubmitHandle) {0};
    }

    return handle;
}

void pigment_present(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return;
    }

    uint32_t max_frame     = pigment->config.max_frames_in_flight;
    uint32_t current_frame = renderer->swapchain->current_frame;
    uint32_t image_index   = renderer->current_image_index;

    VkSemaphore present_wait[]  = {renderer->sync->render_finished_semaphores[image_index]};
    VkSwapchainKHR swapchains[] = {renderer->swapchain->swapchain};

    VkSwapchainPresentFenceInfoEXT present_fence_info = {0};
    void* present_pnext                               = NULL;
    if(renderer->sync->present_fences != NULL)
    {
        VkFence fence = renderer->sync->present_fences[current_frame];
        vkWaitForFences(pigment->device->logical_device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(pigment->device->logical_device, 1, &fence);
        present_fence_info.sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
        present_fence_info.swapchainCount = 1;
        present_fence_info.pFences        = &renderer->sync->present_fences[current_frame];
        present_pnext                     = &present_fence_info;
    }

    VkPresentInfoKHR present_info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext              = present_pnext,
        .waitSemaphoreCount = sizeof(present_wait) / sizeof(present_wait[0]),
        .pWaitSemaphores    = present_wait,
        .swapchainCount     = sizeof(swapchains) / sizeof(swapchains[0]),
        .pSwapchains        = swapchains,
        .pImageIndices      = &image_index
    };

    VkResult result = vkQueuePresentKHR(renderer->swapchain->present_queue, &present_info);
    if(result != VK_SUCCESS && result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
    {
        PLOG_ERROR(pigment, "Failed to present swap chain image!");
    }

    uint32_t next_frame                = current_frame + 1;
    renderer->swapchain->current_frame = next_frame * (next_frame < max_frame);
}

void pigment_cmd_set_depth(Pigment* pigment, PCommandBuffer* cmd, PBool test, PBool write, PCompareOp op)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    vkCmdSetDepthTestEnable(cmd->buffer, test ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(cmd->buffer, write ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(cmd->buffer, (VkCompareOp) op);
}

void pigment_cmd_set_cull(Pigment* pigment, PCommandBuffer* cmd, PCullMode mode, PFrontFace face)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    vkCmdSetCullMode(cmd->buffer, (VkCullModeFlags) mode);
    vkCmdSetFrontFace(cmd->buffer, (VkFrontFace) face);
}

void pigment_cmd_set_stencil_test(Pigment* pigment, PCommandBuffer* cmd, PBool enable)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    vkCmdSetStencilTestEnable(cmd->buffer, enable ? VK_TRUE : VK_FALSE);
}

void pigment_cmd_set_stencil_op(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, PStencilOp fail_op, PStencilOp pass_op, PStencilOp depth_fail_op, PCompareOp compare_op)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    vkCmdSetStencilOp(cmd->buffer, (VkStencilFaceFlags) faces, (VkStencilOp) fail_op, (VkStencilOp) pass_op, (VkStencilOp) depth_fail_op, (VkCompareOp) compare_op);
}

void pigment_cmd_set_stencil_compare_mask(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, uint32_t mask)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    vkCmdSetStencilCompareMask(cmd->buffer, (VkStencilFaceFlags) faces, mask);
}

void pigment_cmd_set_stencil_write_mask(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, uint32_t mask)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    vkCmdSetStencilWriteMask(cmd->buffer, (VkStencilFaceFlags) faces, mask);
}

void pigment_cmd_set_stencil_reference(Pigment* pigment, PCommandBuffer* cmd, PStencilFaceFlags faces, uint32_t reference)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    vkCmdSetStencilReference(cmd->buffer, (VkStencilFaceFlags) faces, reference);
}

void pigment_cmd_set_viewport(Pigment* pigment, PCommandBuffer* cmd, const PViewport* viewports, uint32_t count)
{
    if(pigment == NULL || cmd == NULL || viewports == NULL || count == 0)
    {
        return;
    }
    P_STACK_OR_HEAP(VkViewport, vk_viewports, count);
    if(vk_viewports == NULL)
    {
        return;
    }
    for(uint32_t i = 0; i < count; i++)
    {
        vk_viewports[i] = (VkViewport) {
            .x        = viewports[i].x,
            .y        = viewports[i].y,
            .width    = viewports[i].width,
            .height   = viewports[i].height,
            .minDepth = viewports[i].min_depth,
            .maxDepth = viewports[i].max_depth,
        };
    }
    vkCmdSetViewportWithCount(cmd->buffer, count, vk_viewports);
    P_STACK_OR_HEAP_FREE(pigment, vk_viewports);
}

void pigment_cmd_set_scissor(Pigment* pigment, PCommandBuffer* cmd, const PScissor* scissors, uint32_t count)
{
    if(pigment == NULL || cmd == NULL || scissors == NULL || count == 0)
    {
        return;
    }
    P_STACK_OR_HEAP(VkRect2D, vk_scissors, count);
    if(vk_scissors == NULL)
    {
        return;
    }
    for(uint32_t i = 0; i < count; i++)
    {
        vk_scissors[i] = (VkRect2D) {
            .offset = {    scissors[i].x,      scissors[i].y},
            .extent = {scissors[i].width, scissors[i].height},
        };
    }
    vkCmdSetScissorWithCount(cmd->buffer, count, vk_scissors);
    P_STACK_OR_HEAP_FREE(pigment, vk_scissors);
}

void pigment_cmd_set_depth_bias(Pigment* pigment, PCommandBuffer* cmd, PBool enable, float constant, float clamp, float slope)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    vkCmdSetDepthBiasEnable(cmd->buffer, enable ? VK_TRUE : VK_FALSE);
    if(enable)
    {
        vkCmdSetDepthBias(cmd->buffer, constant, clamp, slope);
    }
}

void pigment_cmd_set_depth_bounds(Pigment* pigment, PCommandBuffer* cmd, PBool enable, float min, float max)
{
    if(cmd == NULL || pigment == NULL)
    {
        return;
    }
    if(!pigment->device->features[P_FEATURE_DEPTH_BOUNDS_TEST])
    {
        return;
    }
    vkCmdSetDepthBoundsTestEnable(cmd->buffer, enable ? VK_TRUE : VK_FALSE);
    if(enable)
    {
        vkCmdSetDepthBounds(cmd->buffer, min, max);
    }
}

void pigment_cmd_push_constants(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline, uint32_t offset, uint32_t size, const void* data)
{
    if(pigment == NULL || cmd == NULL || pipeline == NULL || pipeline->layout == NULL)
    {
        return;
    }
    PLayout* layout = pipeline->layout;
    vkCmdPushConstants(cmd->buffer, layout->layout, layout->push_stages, offset, size, data);
}

void pigment_cmd_draw(Pigment* pigment, PCommandBuffer* cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    vkCmdDraw(cmd->buffer, vertex_count, instance_count, first_vertex, first_instance);
}

void pigment_cmd_draw_indexed(Pigment* pigment, PCommandBuffer* cmd, PBuffer* index_buffer, PIndexType index_type, uint64_t index_buffer_offset, uint32_t first_index, uint32_t index_count, int32_t vertex_offset, uint32_t instance_count, uint32_t first_instance)
{
    if(pigment == NULL || cmd == NULL || index_buffer == NULL)
    {
        return;
    }
    pigment_cmd_use_buffer(pigment, cmd, index_buffer);
    VkIndexType vk_index_type = (index_type == P_INDEX_TYPE_UINT16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd->buffer, index_buffer->buffer, (VkDeviceSize) index_buffer_offset, vk_index_type);
    vkCmdDrawIndexed(cmd->buffer, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void pigment_cmd_draw_indirect(Pigment* pigment, PCommandBuffer* cmd, PBuffer* indirect_buffer, uint64_t indirect_offset, uint32_t draw_count, uint32_t stride)
{
    if(pigment == NULL || cmd == NULL || indirect_buffer == NULL)
    {
        return;
    }
    pigment_cmd_use_buffer(pigment, cmd, indirect_buffer);
    vkCmdDrawIndirect(cmd->buffer, indirect_buffer->buffer, (VkDeviceSize) indirect_offset, draw_count, stride);
}

void pigment_cmd_draw_indexed_indirect(Pigment* pigment, PCommandBuffer* cmd, PBuffer* index_buffer, PIndexType index_type, uint64_t index_offset, PBuffer* indirect_buffer, uint64_t indirect_offset, uint32_t draw_count, uint32_t stride)
{
    if(pigment == NULL || cmd == NULL || index_buffer == NULL || indirect_buffer == NULL)
    {
        return;
    }
    pigment_cmd_use_buffer(pigment, cmd, index_buffer);
    pigment_cmd_use_buffer(pigment, cmd, indirect_buffer);
    VkIndexType vk_index_type = (index_type == P_INDEX_TYPE_UINT16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd->buffer, index_buffer->buffer, (VkDeviceSize) index_offset, vk_index_type);
    vkCmdDrawIndexedIndirect(cmd->buffer, indirect_buffer->buffer, (VkDeviceSize) indirect_offset, draw_count, stride);
}

void pigment_cmd_draw_indirect_count(Pigment* pigment, PCommandBuffer* cmd, PBuffer* indirect_buffer, uint64_t indirect_offset, PBuffer* count_buffer, uint64_t count_offset, uint32_t max_draw_count, uint32_t stride)
{
    if(pigment == NULL || cmd == NULL || indirect_buffer == NULL || count_buffer == NULL)
    {
        return;
    }
    pigment_cmd_use_buffer(pigment, cmd, indirect_buffer);
    pigment_cmd_use_buffer(pigment, cmd, count_buffer);
    vkCmdDrawIndirectCount(cmd->buffer, indirect_buffer->buffer, (VkDeviceSize) indirect_offset, count_buffer->buffer, (VkDeviceSize) count_offset, max_draw_count, stride);
}

void pigment_cmd_draw_indexed_indirect_count(Pigment* pigment, PCommandBuffer* cmd, PBuffer* index_buffer, PIndexType index_type, uint64_t index_offset, PBuffer* indirect_buffer, uint64_t indirect_offset, PBuffer* count_buffer, uint64_t count_offset, uint32_t max_draw_count, uint32_t stride)
{
    if(pigment == NULL || cmd == NULL || index_buffer == NULL || indirect_buffer == NULL || count_buffer == NULL)
    {
        return;
    }
    pigment_cmd_use_buffer(pigment, cmd, index_buffer);
    pigment_cmd_use_buffer(pigment, cmd, indirect_buffer);
    pigment_cmd_use_buffer(pigment, cmd, count_buffer);
    VkIndexType vk_index_type = (index_type == P_INDEX_TYPE_UINT16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd->buffer, index_buffer->buffer, (VkDeviceSize) index_offset, vk_index_type);
    vkCmdDrawIndexedIndirectCount(cmd->buffer, indirect_buffer->buffer, (VkDeviceSize) indirect_offset, count_buffer->buffer, (VkDeviceSize) count_offset, max_draw_count, stride);
}
