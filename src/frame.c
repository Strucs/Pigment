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

extern VkFormat find_depth_format(VkPhysicalDevice physical_device);
extern PSwapchain* recreate_swapchain(PSwapchain* previous_swapchain, PCommands* commands, PDevice* device, PSurface* surface, PWindow* window, PRenderPass* render_pass);
extern void update_uniform_buffer(PBuffers* buffers, PSwapchain* swapchain, PCamera* camera);
extern void record_commands(VkCommandBuffer command_buffer, PPipeline* pipeline, PSwapchain* swapchain, PRenderPass* render_pass, uint32_t image_index, PBuffers* buffers, PDescriptor* descriptor);


PRenderPass* create_render_pass(PSwapchain* swapchain, PDevice* device)
{
    PRenderPass* render_pass = malloc(sizeof(*render_pass));
    if(render_pass == NULL)
    {
        perror("create_render_pass");
        return NULL;
    }

    VkAttachmentDescription color_attachment_description = {
        .format         = swapchain->image_format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    VkAttachmentDescription depth_attachment_description = {
        .format         = find_depth_format(device->physical_device),
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference color_attachment_reference = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference depth_attachment_reference = {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass_description = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &color_attachment_reference,
        .pDepthStencilAttachment = &depth_attachment_reference
    };

    VkSubpassDependency dependency = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = 0,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    };

    VkAttachmentDescription attachments_description[] = {color_attachment_description, depth_attachment_description};

    VkRenderPassCreateInfo render_pass_create_info = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = sizeof(attachments_description) / sizeof(attachments_description[0]),
        .pAttachments    = attachments_description,
        .subpassCount    = 1,
        .pSubpasses      = &subpass_description,
        .dependencyCount = 1,
        .pDependencies   = &dependency
    };

    if(vkCreateRenderPass(device->logical_device, &render_pass_create_info, NULL, &(render_pass->render_pass)) != VK_SUCCESS)
    {
        fprintf(stderr, "failed to create render pass!\n");
        free(render_pass);
        return NULL;
    }

    return render_pass;
}

void destroy_render_pass(PRenderPass* render_pass, PDevice* device)
{
    if(render_pass == NULL)
    {
        return;
    }
    vkDestroyRenderPass(device->logical_device, render_pass->render_pass, NULL);
    free(render_pass);
}

void draw_frame(PBuffers* buffers, PSwapchain** swapchain, PSync** sync, PCommands* commands, PDescriptor* descriptor, PPipeline* pipeline, PSurface* surface, PWindow* window, PRenderPass* render_pass, PDevice* device, const uint32_t max_frame)
{
    if(window->framebuffer_resized)
    {
        vkDeviceWaitIdle(device->logical_device);

        window->framebuffer_resized = false;
        destroy_sync(*sync, device, (*swapchain), max_frame);
        *swapchain = recreate_swapchain(*swapchain, commands, device, surface, window, render_pass);
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
    record_commands(commands->command_buffers[current_frame], pipeline, *swapchain, render_pass, image_index, buffers, descriptor);

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
