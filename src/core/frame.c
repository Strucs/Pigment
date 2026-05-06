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
#include "image.h"
#include "internal.h"
#include "surface.h"
#include "synchronization.h"
#include "log_internal.h"

#include <stdlib.h>

PBool begin_frame(Pigment* pigment, PWindowRenderer* renderer, uint32_t* out_image_index)
{
    PDevice* device = pigment->device;
    if(renderer->needs_recreate)
    {
        renderer->needs_recreate = P_FALSE;

        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical_device, renderer->surface->surface, &caps);
        if(caps.currentExtent.width != 0xFFFFFFFF)
        {
            renderer->desc.width  = caps.currentExtent.width;
            renderer->desc.height = caps.currentExtent.height;
        }
        if(renderer->desc.width == 0 || renderer->desc.height == 0)
        {
            renderer->needs_recreate = P_TRUE;
            return P_FALSE;
        }

        uint32_t old_image_count = renderer->swapchain->image_count;
        if(recreate_swapchain(pigment, renderer) != PIGMENT_SUCCESS)
        {
            renderer->needs_recreate = P_TRUE;
            return P_FALSE;
        }
        if(old_image_count != renderer->swapchain->image_count)
        {
            resize_render_finished_semaphores(pigment, renderer->sync, old_image_count, renderer->swapchain->image_count);
        }

        PSwapchainResizeEvent event = {
            .renderer = renderer,
            .width    = renderer->swapchain->extent.width,
            .height   = renderer->swapchain->extent.height,
        };
        dispatch_swapchain_resize(pigment, &event);

        return P_FALSE;
    }

    uint32_t current_frame = renderer->swapchain->current_frame;

    VkResult result = vkAcquireNextImageKHR(device->logical_device, renderer->swapchain->swapchain, UINT64_MAX, renderer->sync->image_available_semaphores[current_frame], VK_NULL_HANDLE, out_image_index);

    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreate_image_available_semaphore(pigment, renderer->sync, current_frame);
        renderer->needs_recreate = P_TRUE;
        return P_FALSE;
    }
    else if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to acquire swapchain image!");
        return P_FALSE;
    }

    vkResetFences(device->logical_device, 1, &renderer->sync->in_flight_fences[current_frame]);

    VkCommandBuffer cmd = renderer->command_buffers[current_frame]->buffer;

    if((result = vkResetCommandBuffer(cmd, 0)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to reset command buffer! (result: %d)", result);
        return P_FALSE;
    }

    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if((result = vkBeginCommandBuffer(cmd, &begin_info)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to begin command buffer! (result: %d)", result);
        return P_FALSE;
    }

    return P_TRUE;
}

void begin_swapchain_pass(Pigment* pigment, PWindowRenderer* renderer, uint32_t image_index)
{
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

void end_swapchain_pass(PWindowRenderer* renderer, uint32_t image_index)
{
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

    uint32_t max_barrier_count       = desc->color_count + resolve_count + (has_ds_attachment ? 1 : 0) + (has_depth_resolve ? 1 : 0);
    PImageBarrier* barriers          = malloc(max_barrier_count * sizeof(*barriers));
    VkRenderingAttachmentInfo* color = (desc->color_count > 0) ? malloc(desc->color_count * sizeof(*color)) : NULL;

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
        barriers[barrier_count++] = (PImageBarrier) {
            .image       = img,
            .old_layout  = P_IMAGE_LAYOUT_UNDEFINED,
            .new_layout  = P_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .src         = {                       P_PIPELINE_STAGE_NONE,                       P_MEMORY_ACCESS_NONE},
            .dst         = {P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
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
            .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp            = has_resolve ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
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
        barriers[barrier_count++] = (PImageBarrier) {
            .image       = img,
            .old_layout  = P_IMAGE_LAYOUT_UNDEFINED,
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

        VkRenderingAttachmentInfo attachment = {
            .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView          = view->view,
            .imageLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp            = has_depth_resolve ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue         = {.depthStencil = clear_depth_stencil_value},
            .resolveMode        = has_depth_resolve ? resolve_mode_to_vk(ref->resolve_mode, P_TRUE) : VK_RESOLVE_MODE_NONE,
            .resolveImageView   = depth_resolve_view_handle,
            .resolveImageLayout = has_depth_resolve ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if(has_depth)
        {
            depth = attachment;
        }

        if(has_stencil)
        {
            stencil = attachment;
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
    free(barriers);
    free(color);
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

    PImageBarrier* barriers = malloc(max_barriers * sizeof(*barriers));
    if(barriers == NULL)
    {
        return;
    }

    uint32_t barrier_count = 0;

    for(uint32_t i = 0; i < desc->color_count; i++)
    {
        const PAttachmentRef* ref = &desc->color_attachments[i];

        if(ref->image->vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT)
        {
            barriers[barrier_count++] = (PImageBarrier) {
                .image       = ref->image,
                .old_layout  = P_IMAGE_LAYOUT_COLOR_ATTACHMENT,
                .new_layout  = P_IMAGE_LAYOUT_SHADER_READ_ONLY,
                .src         = {P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
                .dst         = {        P_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,    P_MEMORY_ACCESS_SHADER_SAMPLED_READ_BIT},
                .base_mip    = ref->mip_level,
                .mip_count   = 1,
                .base_layer  = ref->base_layer,
                .layer_count = ref->layer_count,
            };
        }

        if(ref->resolve_image != NULL && (ref->resolve_image->vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT))
        {
            barriers[barrier_count++] = (PImageBarrier) {
                .image       = ref->resolve_image,
                .old_layout  = P_IMAGE_LAYOUT_COLOR_ATTACHMENT,
                .new_layout  = P_IMAGE_LAYOUT_SHADER_READ_ONLY,
                .src         = {P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
                .dst         = {        P_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,    P_MEMORY_ACCESS_SHADER_SAMPLED_READ_BIT},
                .base_mip    = ref->resolve_mip_level,
                .mip_count   = 1,
                .base_layer  = ref->resolve_base_layer,
                .layer_count = ref->layer_count,
            };
        }
    }

    if(has_ds_attachment)
    {
        const PAttachmentRef* ref = &desc->depth_attachment;

        if(ref->image->vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT)
        {
            barriers[barrier_count++] = (PImageBarrier) {
                .image       = ref->image,
                .old_layout  = P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
                .new_layout  = P_IMAGE_LAYOUT_SHADER_READ_ONLY,
                .src         = {P_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
                .dst         = {    P_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,            P_MEMORY_ACCESS_SHADER_SAMPLED_READ_BIT},
                .base_mip    = ref->mip_level,
                .mip_count   = 1,
                .base_layer  = ref->base_layer,
                .layer_count = ref->layer_count,
            };
        }

        if(ref->resolve_image != NULL && (ref->resolve_image->vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT))
        {
            barriers[barrier_count++] = (PImageBarrier) {
                .image       = ref->resolve_image,
                .old_layout  = P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
                .new_layout  = P_IMAGE_LAYOUT_SHADER_READ_ONLY,
                .src         = {P_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
                .dst         = {    P_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,            P_MEMORY_ACCESS_SHADER_SAMPLED_READ_BIT},
                .base_mip    = ref->resolve_mip_level,
                .mip_count   = 1,
                .base_layer  = ref->resolve_base_layer,
                .layer_count = ref->layer_count,
            };
        }
    }

    pigment_cmd_image_barriers(pigment, cmd, barriers, barrier_count);

    free(barriers);
}

void end_frame(Pigment* pigment, PWindowRenderer* renderer, uint32_t image_index, uint32_t max_frame)
{
    PDevice* device        = pigment->device;
    uint32_t current_frame = renderer->swapchain->current_frame;
    VkCommandBuffer cmd    = renderer->command_buffers[current_frame]->buffer;

    VkResult result;
    if((result = vkEndCommandBuffer(cmd)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to record command buffer! (result: %d)", result);
        return;
    }

    VkSemaphore wait_semaphores[]      = {renderer->sync->image_available_semaphores[current_frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signal_semaphores[]    = {renderer->sync->render_finished_semaphores[image_index]};

    VkSubmitInfo submit_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = sizeof(wait_semaphores) / sizeof(wait_semaphores[0]),
        .pWaitSemaphores      = wait_semaphores,
        .pWaitDstStageMask    = wait_stages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmd,
        .signalSemaphoreCount = sizeof(signal_semaphores) / sizeof(signal_semaphores[0]),
        .pSignalSemaphores    = signal_semaphores
    };

    if((result = vkQueueSubmit(device_find_queue(device, P_QUEUE_GRAPHICS_BIT, 0)->queue, 1, &submit_info, renderer->sync->in_flight_fences[current_frame])) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to submit draw command buffer! (result: %d)", result);
        return;
    }

    VkSwapchainKHR swapchains[] = {renderer->swapchain->swapchain};

    VkPresentInfoKHR present_info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = sizeof(signal_semaphores) / sizeof(signal_semaphores[0]),
        .pWaitSemaphores    = signal_semaphores,
        .swapchainCount     = sizeof(swapchains) / sizeof(swapchains[0]),
        .pSwapchains        = swapchains,
        .pImageIndices      = &image_index
    };

    result = vkQueuePresentKHR(renderer->swapchain->present_queue, &present_info);
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

void pigment_cmd_set_viewport(Pigment* pigment, PCommandBuffer* cmd, float x, float y, float width, float height, float min_depth, float max_depth)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    VkViewport vp = {
        .x        = x,
        .y        = y,
        .width    = width,
        .height   = height,
        .minDepth = min_depth,
        .maxDepth = max_depth,
    };
    vkCmdSetViewportWithCount(cmd->buffer, 1, &vp);
}

void pigment_cmd_set_scissor(Pigment* pigment, PCommandBuffer* cmd, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }
    VkRect2D rect = {
        .offset = {    x,      y},
        .extent = {width, height},
    };
    vkCmdSetScissorWithCount(cmd->buffer, 1, &rect);
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
    VkIndexType vk_index_type = (index_type == P_INDEX_TYPE_UINT16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd->buffer, index_buffer->buffer, (VkDeviceSize) index_buffer_offset, vk_index_type);
    vkCmdDrawIndexed(cmd->buffer, index_count, instance_count, first_index, vertex_offset, first_instance);
}
