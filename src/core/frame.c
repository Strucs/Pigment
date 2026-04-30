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
#include "internal.h"
#include "surface.h"
#include "synchronization.h"
#include "window.h"
#include "log_internal.h"

static VkCommandBuffer current_cmd(Pigment* pigment, uint32_t window_index);

bool begin_frame(Pigment* pigment, PUniformBuffers* buffers, PWindowRenderer* renderer, PCamera* camera, uint32_t* out_image_index)
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

    update_uniform_buffer(buffers, renderer->swapchain, camera);

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

void begin_swapchain_pass(PWindowRenderer* renderer, uint32_t image_index)
{
    uint32_t current_frame = renderer->swapchain->current_frame;
    VkCommandBuffer cmd    = renderer->command_buffers->buffers[current_frame];

    cmd_begin_rendering(cmd, renderer->swapchain, image_index, renderer->transparent_framebuffer);

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

void pigment_cmd_draw_indexed(Pigment* pigment, uint32_t window_index, PMeshBuffers* mesh, uint64_t index_buffer_offset, uint32_t first_index, uint32_t index_count, int32_t vertex_offset, uint32_t instance_count, uint32_t first_instance)
{
    VkCommandBuffer cmd = current_cmd(pigment, window_index);
    if(cmd == VK_NULL_HANDLE || mesh == NULL)
    {
        return;
    }
    vkCmdBindIndexBuffer(cmd, mesh->index_buffer, (VkDeviceSize) index_buffer_offset, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
}
