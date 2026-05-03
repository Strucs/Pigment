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
#include "image.h"
#include "internal.h"
#include "surface.h"
#include "synchronization.h"
#include "window.h"
#include "log_internal.h"

#include <stdlib.h>

static VkCommandBuffer current_cmd(Pigment* pigment, uint32_t window_index);

bool begin_frame(Pigment* pigment, PWindowRenderer* renderer, uint32_t* out_image_index)
{
    PDevice* device = pigment->device;
    if(renderer->framebuffer_resized)
    {
        renderer->framebuffer_resized = false;

        uint32_t framebuffer_width  = renderer->pending_width;
        uint32_t framebuffer_height = renderer->pending_height;
        PPresentMode present_mode   = renderer->requested_present_mode;

        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical_device, renderer->surface->surface, &caps);
        if(caps.currentExtent.width != 0xFFFFFFFF)
        {
            framebuffer_width  = caps.currentExtent.width;
            framebuffer_height = caps.currentExtent.height;
        }
        if(framebuffer_width == 0 || framebuffer_height == 0)
        {
            renderer->framebuffer_resized = true;
            return false;
        }

        uint32_t old_image_count = renderer->swapchain->image_count;
        if(recreate_swapchain(pigment, renderer, framebuffer_width, framebuffer_height, present_mode) != PIGMENT_SUCCESS)
        {
            renderer->framebuffer_resized = true;
            return false;
        }
        if(old_image_count != renderer->swapchain->image_count)
        {
            resize_render_finished_semaphores(pigment, renderer->sync, old_image_count, renderer->swapchain->image_count);
        }

        for(uint32_t i = 0; i < pigment->window_count; i++)
        {
            if(pigment->renderers[i] == renderer)
            {
                pigment_image_resize_tracked(pigment, i);
                break;
            }
        }

        renderer->swapchain->current_frame = 0;
        return false;
    }

    uint32_t current_frame = renderer->swapchain->current_frame;

    VkResult result = vkAcquireNextImageKHR(device->logical_device, renderer->swapchain->swapchain, UINT64_MAX, renderer->sync->image_available_semaphores[current_frame], VK_NULL_HANDLE, out_image_index);

    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreate_image_available_semaphore(pigment, renderer->sync, current_frame);
        renderer->framebuffer_resized = true;
        return false;
    }
    else if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to acquire swapchain image!");
        return false;
    }

    vkResetFences(device->logical_device, 1, &renderer->sync->in_flight_fences[current_frame]);

    VkCommandBuffer cmd = renderer->command_buffers->buffers[current_frame];

    if((result = vkResetCommandBuffer(cmd, 0)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to reset command buffer! (result: %d)", result);
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if((result = vkBeginCommandBuffer(cmd, &begin_info)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to begin command buffer! (result: %d)", result);
        return false;
    }

    return true;
}

void begin_swapchain_pass(Pigment* pigment, PWindowRenderer* renderer, uint32_t image_index)
{
    uint32_t current_frame = renderer->swapchain->current_frame;
    VkCommandBuffer cmd    = renderer->command_buffers->buffers[current_frame];

    cmd_begin_rendering(pigment, cmd, renderer->swapchain, image_index, renderer->transparent_framebuffer);

    VkViewport viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float) renderer->swapchain->extent.width,
        .height   = (float) renderer->swapchain->extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        {0, 0},
        renderer->swapchain->extent
    };

    vkCmdSetViewportWithCount(cmd, 1, &viewport);
    vkCmdSetScissorWithCount(cmd, 1, &scissor);
}

void end_swapchain_pass(PWindowRenderer* renderer, uint32_t image_index)
{
    uint32_t current_frame = renderer->swapchain->current_frame;
    VkCommandBuffer cmd    = renderer->command_buffers->buffers[current_frame];

    cmd_end_rendering(cmd, renderer->swapchain, image_index);
}

void pigment_begin_render_pass(Pigment* pigment, uint32_t window_index, const PRenderPassDesc* desc)
{
    if(pigment == NULL || desc == NULL)
    {
        return;
    }
    if(desc->color_count == 0 && desc->depth_attachment == NULL)
    {
        return;
    }

    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE)
    {
        return;
    }

    uint32_t width;
    uint32_t height;
    if(desc->color_count > 0)
    {
        width  = desc->color_attachments[0]->width;
        height = desc->color_attachments[0]->height;
    }
    else
    {
        width  = desc->depth_attachment->width;
        height = desc->depth_attachment->height;
    }

    uint32_t barrier_count           = desc->color_count + (desc->depth_attachment != NULL ? 1 : 0);
    VkImageMemoryBarrier2* barriers  = malloc(barrier_count * sizeof(*barriers));
    VkRenderingAttachmentInfo* color = (desc->color_count > 0) ? malloc(desc->color_count * sizeof(*color)) : NULL;

    if(barriers == NULL || (desc->color_count > 0 && color == NULL))
    {
        PLOG_ERROR(pigment, "Failed to allocate render pass attachments");
        goto FREE;
    }

    VkClearColorValue clear_color = {
        {desc->clear_color[0], desc->clear_color[1], desc->clear_color[2], desc->clear_color[3]}
    };

    for(uint32_t i = 0; i < desc->color_count; i++)
    {
        PImage* img = desc->color_attachments[i];
        barriers[i] = (VkImageMemoryBarrier2) {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask       = VK_ACCESS_2_NONE,
            .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = img->image,
            .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, img->mip_levels, 0, 1},
        };

        color[i] = (VkRenderingAttachmentInfo) {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView   = img->image_view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue  = {.color = clear_color},
        };
    }

    VkRenderingAttachmentInfo depth = {0};
    if(desc->depth_attachment != NULL)
    {
        PImage* img                 = desc->depth_attachment;
        barriers[desc->color_count] = (VkImageMemoryBarrier2) {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask        = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = img->image,
            .subresourceRange    = {img->aspect, 0, img->mip_levels, 0, 1},
        };

        VkClearDepthStencilValue clear_depth_stencil_value = {pigment->config.depth_clear_value, 0};

        depth = (VkRenderingAttachmentInfo) {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView   = img->image_view,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue  = {.depthStencil = clear_depth_stencil_value},
        };
    }

    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = barrier_count,
        .pImageMemoryBarriers    = barriers,
    };

    vkCmdPipelineBarrier2(cmd, &dep);

    VkRenderingInfo rendering_info = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .renderArea           = {{0, 0}, {width, height}},
        .layerCount           = 1,
        .colorAttachmentCount = desc->color_count,
        .pColorAttachments    = color,
        .pDepthAttachment     = (desc->depth_attachment != NULL) ? &depth : NULL,
    };
    vkCmdBeginRendering(cmd, &rendering_info);

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

    vkCmdSetViewportWithCount(cmd, 1, &viewport);
    vkCmdSetScissorWithCount(cmd, 1, &scissor);

FREE:
    free(barriers);
    free(color);
}

void pigment_end_render_pass(Pigment* pigment, uint32_t window_index, const PRenderPassDesc* desc)
{
    if(pigment == NULL || desc == NULL)
    {
        return;
    }

    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE)
    {
        return;
    }

    vkCmdEndRendering(cmd);

    uint32_t max_barriers = desc->color_count + (desc->depth_attachment != NULL ? 1 : 0);
    if(max_barriers == 0)
    {
        return;
    }

    VkImageMemoryBarrier2* barriers = malloc(max_barriers * sizeof(*barriers));
    if(barriers == NULL)
    {
        return;
    }

    uint32_t barrier_count = 0;

    for(uint32_t i = 0; i < desc->color_count; i++)
    {
        PImage* img = desc->color_attachments[i];
        if(!(img->vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT))
        {
            continue;
        }
        barriers[barrier_count++] = (VkImageMemoryBarrier2) {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = img->image,
            .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, img->mip_levels, 0, 1},
        };
    }

    if(desc->depth_attachment != NULL && (desc->depth_attachment->vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        PImage* img               = desc->depth_attachment;
        barriers[barrier_count++] = (VkImageMemoryBarrier2) {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = img->image,
            .subresourceRange    = {img->aspect, 0, img->mip_levels, 0, 1},
        };
    }

    if(barrier_count > 0)
    {
        VkDependencyInfo dep = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = barrier_count,
            .pImageMemoryBarriers    = barriers,
        };

        vkCmdPipelineBarrier2(cmd, &dep);
    }

    free(barriers);
}

void end_frame(Pigment* pigment, PWindowRenderer* renderer, uint32_t image_index, uint32_t max_frame)
{
    PDevice* device        = pigment->device;
    uint32_t current_frame = renderer->swapchain->current_frame;
    VkCommandBuffer cmd    = renderer->command_buffers->buffers[current_frame];

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

    if((result = vkQueueSubmit(device->graphics_queue, 1, &submit_info, renderer->sync->in_flight_fences[current_frame])) != VK_SUCCESS)
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

    result = vkQueuePresentKHR(device->present_queue, &present_info);
    if(result != VK_SUCCESS && result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
    {
        PLOG_ERROR(pigment, "Failed to present swap chain image!");
    }

    uint32_t next_frame                = current_frame + 1;
    renderer->swapchain->current_frame = next_frame * (next_frame < max_frame);
}

static VkCommandBuffer current_cmd(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return VK_NULL_HANDLE;
    }
    PWindowRenderer* renderer = pigment->renderers[window_index];
    return renderer->command_buffers->buffers[renderer->swapchain->current_frame];
}

void pigment_cmd_set_depth(Pigment* pigment, uint32_t window_index, bool test, bool write, PCompareOp op)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE)
    {
        return;
    }
    vkCmdSetDepthTestEnable(cmd, test ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(cmd, write ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, (VkCompareOp) op);
}

void pigment_cmd_set_cull(Pigment* pigment, uint32_t window_index, PCullMode mode, PFrontFace face)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE)
    {
        return;
    }
    vkCmdSetCullMode(cmd, (VkCullModeFlags) mode);
    vkCmdSetFrontFace(cmd, (VkFrontFace) face);
}

void pigment_cmd_set_stencil_test(Pigment* pigment, uint32_t window_index, bool enable)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE)
    {
        return;
    }
    vkCmdSetStencilTestEnable(cmd, enable ? VK_TRUE : VK_FALSE);
}

void pigment_cmd_set_viewport(Pigment* pigment, uint32_t window_index, float x, float y, float width, float height, float min_depth, float max_depth)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE)
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
    vkCmdSetViewportWithCount(cmd, 1, &vp);
}

void pigment_cmd_set_scissor(Pigment* pigment, uint32_t window_index, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE)
    {
        return;
    }
    VkRect2D rect = {
        .offset = {    x,      y},
        .extent = {width, height},
    };
    vkCmdSetScissorWithCount(cmd, 1, &rect);
}

void pigment_cmd_set_depth_bias(Pigment* pigment, uint32_t window_index, bool enable, float constant, float clamp, float slope)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE)
    {
        return;
    }
    vkCmdSetDepthBiasEnable(cmd, enable ? VK_TRUE : VK_FALSE);
    if(enable)
    {
        vkCmdSetDepthBias(cmd, constant, clamp, slope);
    }
}

void pigment_cmd_set_depth_bounds(Pigment* pigment, uint32_t window_index, bool enable, float min, float max)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE)
    {
        return;
    }
    if(!pigment->device->features[P_FEATURE_DEPTH_BOUNDS_TEST])
    {
        return;
    }
    vkCmdSetDepthBoundsTestEnable(cmd, enable ? VK_TRUE : VK_FALSE);
    if(enable)
    {
        vkCmdSetDepthBounds(cmd, min, max);
    }
}

void pigment_cmd_push_constants(Pigment* pigment, uint32_t window_index, PPipeline* pipeline, uint32_t offset, uint32_t size, const void* data)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE || pipeline == NULL || pipeline->layout == NULL)
    {
        return;
    }
    PLayout* layout = pipeline->layout;
    vkCmdPushConstants(cmd, layout->layout, layout->push_stages, offset, size, data);
}

void pigment_cmd_draw(Pigment* pigment, uint32_t window_index, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE)
    {
        return;
    }
    vkCmdDraw(cmd, vertex_count, instance_count, first_vertex, first_instance);
}

void pigment_cmd_draw_indexed(Pigment* pigment, uint32_t window_index, PBuffer* index_buffer, PIndexType index_type, uint64_t index_buffer_offset, uint32_t first_index, uint32_t index_count, int32_t vertex_offset, uint32_t instance_count, uint32_t first_instance)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE || index_buffer == NULL)
    {
        return;
    }
    VkIndexType vk_index_type = (index_type == P_INDEX_TYPE_UINT16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd, index_buffer->buffer, (VkDeviceSize) index_buffer_offset, vk_index_type);
    vkCmdDrawIndexed(cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
}
