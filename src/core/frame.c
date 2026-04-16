/**
 * Copyright 2025 Angel-Leduc TA
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
#include "structs.h"
#include "synchronization.h"

extern PSwapchain* recreate_swapchain(PSwapchain* previous_swapchain, PDevice* device, PSurface* surface, PWindow* window);
extern void update_uniform_buffer(PUniformBuffers* buffers, PSwapchain* swapchain, PCamera* camera);
extern void cmd_begin_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index);
extern void cmd_end_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index);

bool begin_frame(PUniformBuffers* buffers, PSwapchain** swapchain, PSync** sync, PCommands* commands, PSurface* surface, PWindow* window, PDevice* device, PCamera* camera, uint32_t max_frame, uint32_t* out_image_index)
{
    if(window->framebuffer_resized)
    {
        vkDeviceWaitIdle(device->logical_device);

        window->framebuffer_resized = false;
        destroy_sync(*sync, device, *swapchain, max_frame);
        *swapchain = recreate_swapchain(*swapchain, device, surface, window);
        if(*swapchain == NULL)
        {
            fprintf(stderr, "Failed to recreate swap chain!\n");
            return false;
        }
        *sync = create_sync(device, max_frame, (*swapchain)->image_count);

        (*swapchain)->current_frame = 0;
        return false;
    }

    uint32_t current_frame = (*swapchain)->current_frame;

    vkWaitForFences(device->logical_device, 1, &(*sync)->in_flight_fences[current_frame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(device->logical_device, (*swapchain)->swapchain, UINT64_MAX, (*sync)->image_available_semaphores[current_frame], VK_NULL_HANDLE, out_image_index);

    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        window->framebuffer_resized = true;
        return false;
    }
    else if(result != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to acquire swapchain image!\n");
        return false;
    }

    update_uniform_buffer(buffers, *swapchain, camera);

    vkResetFences(device->logical_device, 1, &(*sync)->in_flight_fences[current_frame]);

    VkCommandBuffer cmd = commands->command_buffers[current_frame];

    if((result = vkResetCommandBuffer(cmd, 0)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to reset command buffer! (result: %d)\n", result);
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if((result = vkBeginCommandBuffer(cmd, &begin_info)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to begin command buffer! (result: %d)\n", result);
        return false;
    }

    cmd_begin_rendering(cmd, *swapchain, *out_image_index);

    VkViewport viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float) (*swapchain)->extent.width,
        .height   = (float) (*swapchain)->extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        {0, 0},
        (*swapchain)->extent
    };

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    return true;
}

void end_frame(PSwapchain** swapchain, PSync** sync, PCommands* commands, PDevice* device, uint32_t image_index, uint32_t max_frame)
{
    uint32_t current_frame = (*swapchain)->current_frame;
    VkCommandBuffer cmd    = commands->command_buffers[current_frame];

    cmd_end_rendering(cmd, *swapchain, image_index);

    VkResult result;
    if((result = vkEndCommandBuffer(cmd)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to record command buffer! (result: %d)\n", result);
        return;
    }

    VkSemaphore wait_semaphores[]      = {(*sync)->image_available_semaphores[current_frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signal_semaphores[]    = {(*sync)->render_finished_semaphores[image_index]};

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

    if((result = vkQueueSubmit(device->graphics_queue, 1, &submit_info, (*sync)->in_flight_fences[current_frame])) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to submit draw command buffer! (result: %d)\n", result);
        return;
    }

    VkSwapchainKHR swapchains[] = {(*swapchain)->swapchain};

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
        fprintf(stderr, "Failed to present swap chain image!\n");
    }

    uint32_t next_frame         = current_frame + 1;
    (*swapchain)->current_frame = next_frame * (next_frame < max_frame);
}

void pigment_draw(Pigment* pigment, PDrawCall* draw_cmds, uint32_t draw_cmd_count)
{
    if(pigment == NULL || draw_cmds == NULL || draw_cmd_count == 0)
    {
        return;
    }

    uint32_t current_frame = pigment->swapchain->current_frame;
    VkCommandBuffer cmd    = pigment->commands->command_buffers[current_frame];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pigment->pipeline->graphic_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pigment->pipeline->pipeline_layout, 0, 1, &pigment->descriptor->descriptor_sets[current_frame], 0, NULL);

    for(uint32_t i = 0; i < draw_cmd_count; i++)
    {
        PDrawCall* draw_call = &draw_cmds[i];
        if(draw_call->mesh == NULL)
        {
            continue;
        }

        PDrawPushConstants push = {0};
        glm_mat4_copy(draw_call->transform, push.world_matrix);
        push.vertex_buffer = draw_call->mesh->vertex_buffer_address;
        push.image_index   = draw_call->image_index;
        push.sampler_index = draw_call->sampler_index;

        vkCmdPushConstants(cmd, pigment->pipeline->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PDrawPushConstants), &push);

        vkCmdBindIndexBuffer(cmd, draw_call->mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, draw_call->index_count, 1, draw_call->first_index, 0, 0);
    }
}
