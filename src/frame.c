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
extern void update_uniform_buffer(PBuffers* buffers, PSwapchain* swapchain, PCamera* camera);
extern void record_commands(VkCommandBuffer command_buffer, PPipeline* pipeline, PSwapchain* swapchain, uint32_t image_index, PBuffers* buffers, PDescriptor* descriptor);

void draw_frame(PBuffers* buffers, PSwapchain** swapchain, PSync** sync, PCommands* commands, PDescriptor* descriptor, PPipeline* pipeline, PSurface* surface, PWindow* window, PDevice* device, const uint32_t max_frame)
{
    if(window->framebuffer_resized)
    {
        vkDeviceWaitIdle(device->logical_device);

        window->framebuffer_resized = false;
        destroy_sync(*sync, device, (*swapchain), max_frame);
        *swapchain = recreate_swapchain(*swapchain, device, surface, window);
        if(*swapchain == NULL)
        {
            fprintf(stderr, "Failed to recreate swap chain!\n");
            return;
        }
        *sync = create_sync(device, max_frame, (*swapchain)->image_count);
        (*swapchain)->current_frame = 0;
    }

    if(*swapchain == NULL)
    {
        fprintf(stderr, "Failed to present swap chain image!\n");
        return;
    }

    uint32_t current_frame = (*swapchain)->current_frame;
    uint32_t image_index;
    VkResult result;

    vkWaitForFences(device->logical_device, 1, &((*sync)->in_flight_fences[current_frame]), VK_TRUE, UINT64_MAX);

    result = vkAcquireNextImageKHR(device->logical_device, (*swapchain)->swapchain, UINT64_MAX, (*sync)->image_available_semaphores[current_frame], VK_NULL_HANDLE, &image_index);

    if(result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        window->framebuffer_resized = true;
        return;
    }
    else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        fprintf(stderr, "Failed to acquire swapchain image!\n");
        return;
    }

    update_uniform_buffer(buffers, *swapchain, window->camera);

    vkResetFences(device->logical_device, 1, &((*sync)->in_flight_fences[current_frame]));

    vkResetCommandBuffer(commands->command_buffers[current_frame], 0);
    record_commands(commands->command_buffers[current_frame], pipeline, *swapchain, image_index, buffers, descriptor);

    VkSemaphore wait_semaphores[]      = {(*sync)->image_available_semaphores[current_frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSemaphore signal_semaphores[]    = {(*sync)->render_finished_semaphores[image_index]};

    VkSubmitInfo submit_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = sizeof(wait_semaphores) / sizeof(wait_semaphores[0]),
        .pWaitSemaphores      = wait_semaphores,
        .pWaitDstStageMask    = wait_stages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &commands->command_buffers[current_frame],
        .signalSemaphoreCount = sizeof(signal_semaphores) / sizeof(signal_semaphores[0]),
        .pSignalSemaphores    = signal_semaphores
    };

    if((result = vkQueueSubmit(device->graphics_queue, 1, &submit_info, ((*sync)->in_flight_fences[current_frame]))) != VK_SUCCESS)
    {
        printf("%i\n", result);
        fprintf(stderr, "Failed to submit draw command buffer!\n");
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

    vkQueuePresentKHR(device->present_queue, &present_info);

    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        window->framebuffer_resized = true;
    }
    else if(result != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to present swap chain image!\n");
        return;
    }

    current_frame = current_frame + 1;
    current_frame = current_frame * (current_frame < max_frame);

    (*swapchain)->current_frame = current_frame;
}
