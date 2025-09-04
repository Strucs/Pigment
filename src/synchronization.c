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

#include "synchronization.h"
#include "structs.h"
VkSemaphore create_semaphore(VkDevice device);
VkFence create_fence(VkDevice device);

PSync* create_sync(PDevice* device, const uint32_t max_frame, const uint32_t swapchain_image_count)
{
    PSync* sync = NULL;

    sync = calloc(1, sizeof(*sync));
    if(sync == NULL)
    {
        goto ERROR;
    }

    sync->image_available_semaphores = malloc(max_frame * sizeof(*sync->image_available_semaphores));
    if(sync->image_available_semaphores == NULL)
    {
        goto ERROR;
    }

    sync->render_finished_semaphores = malloc(swapchain_image_count * sizeof(*sync->render_finished_semaphores));
    if(sync->render_finished_semaphores == NULL)
    {
        goto ERROR;
    }

    sync->in_flight_fences = malloc(max_frame * sizeof(*sync->in_flight_fences));
    if(sync->in_flight_fences == NULL)
    {
        goto ERROR;
    }

    for(size_t i = 0; i < max_frame; i++)
    {
        sync->image_available_semaphores[i] = create_semaphore(device->logical_device);
        sync->in_flight_fences[i]           = create_fence(device->logical_device);

        if(sync->image_available_semaphores[i] == NULL || sync->in_flight_fences[i] == NULL)
        {
            fprintf(stderr, "Failed to create synchronization objects\n");
            goto ERROR;
        }
    }

    for(size_t i = 0; i < swapchain_image_count; i++)
    {
        sync->render_finished_semaphores[i] = create_semaphore(device->logical_device);

        if(sync->render_finished_semaphores[i] == NULL)
        {
            fprintf(stderr, "Failed to create synchronization objects\n");
            goto ERROR;
        }
    }

    return sync;

ERROR:
    perror("create_sync");
    if(sync != NULL)
    {
        free(sync->in_flight_fences);
        free(sync->render_finished_semaphores);
        free(sync->image_available_semaphores);
        free(sync);
    }
    return NULL;
}
void destroy_sync(PSync* sync, PDevice* device, PSwapchain* swapchain, const uint32_t max_frame)
{
    if(sync == NULL || swapchain == NULL)
    {
        return;
    }

    for(size_t i = 0; i < max_frame; i++)
    {
        vkDestroySemaphore(device->logical_device, sync->image_available_semaphores[i], NULL);
        vkDestroyFence(device->logical_device, sync->in_flight_fences[i], NULL);
    }

    for(size_t i = 0; i < swapchain->image_count; i++)
    {
        vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], NULL);
    }

    free(sync->in_flight_fences);
    free(sync->render_finished_semaphores);
    free(sync->image_available_semaphores);
    free(sync);
}

VkSemaphore create_semaphore(VkDevice device)
{
    VkSemaphore semaphore;

    VkSemaphoreCreateInfo semaphore_create_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    if(vkCreateSemaphore(device, &semaphore_create_info, NULL, &semaphore) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create semaphore\n");
        return NULL;
    }

    return semaphore;
}

VkFence create_fence(VkDevice device)
{
    VkFence fence;

    VkFenceCreateInfo fence_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    if(vkCreateFence(device, &fence_create_info, NULL, &fence) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create fence\n");
        return NULL;
    }

    return fence;
}
