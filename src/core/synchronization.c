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

static VkSemaphore create_semaphore(VkDevice device);
static VkFence create_fence(VkDevice device);

PSync* create_sync(PDevice* device, const uint32_t max_frame, const uint32_t swapchain_image_count)
{
    PSync* sync = NULL;

    sync = calloc(1, sizeof(*sync));
    if(sync == NULL)
    {
        goto ERROR;
    }

    sync->image_available_semaphores = calloc(max_frame, sizeof(*sync->image_available_semaphores));
    if(sync->image_available_semaphores == NULL)
    {
        goto ERROR;
    }

    sync->render_finished_semaphores = calloc(swapchain_image_count, sizeof(*sync->render_finished_semaphores));
    if(sync->render_finished_semaphores == NULL)
    {
        goto ERROR;
    }

    sync->in_flight_fences = calloc(max_frame, sizeof(*sync->in_flight_fences));
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
        if(sync->image_available_semaphores != NULL)
        {
            for(size_t i = 0; i < max_frame; i++)
            {
                vkDestroySemaphore(device->logical_device, sync->image_available_semaphores[i], NULL);
            }
        }
        if(sync->in_flight_fences != NULL)
        {
            for(size_t i = 0; i < max_frame; i++)
            {
                vkDestroyFence(device->logical_device, sync->in_flight_fences[i], NULL);
            }
        }
        if(sync->render_finished_semaphores != NULL)
        {
            for(size_t i = 0; i < swapchain_image_count; i++)
            {
                vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], NULL);
            }
        }
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

int resize_render_finished_semaphores(PSync* sync, uint32_t old_count, uint32_t new_count, PDevice* device)
{
    VkSemaphore* new_semaphores = calloc(new_count, sizeof(*new_semaphores));
    if(new_semaphores == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < new_count; i++)
    {
        new_semaphores[i] = create_semaphore(device->logical_device);
        if(new_semaphores[i] == NULL)
        {
            goto ERROR;
        }
    }

    for(uint32_t i = 0; i < old_count; i++)
    {
        vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], NULL);
    }

    free(sync->render_finished_semaphores);
    sync->render_finished_semaphores = new_semaphores;

    return PIGMENT_SUCCESS;

ERROR:
    fprintf(stderr, "Failed to resize render_finished_semaphores, keeping previous ones\n");
    if(new_semaphores == NULL)
    {
        return PIGMENT_ERROR;
    }

    for(uint32_t i = 0; i < new_count; i++)
    {
        if(new_semaphores[i] != NULL)
        {
            vkDestroySemaphore(device->logical_device, new_semaphores[i], NULL);
        }
    }
    free(new_semaphores);

    return PIGMENT_ERROR;
}

static VkSemaphore create_semaphore(VkDevice device)
{
    VkSemaphore semaphore;

    VkSemaphoreCreateInfo semaphore_create_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkResult result;
    if((result = vkCreateSemaphore(device, &semaphore_create_info, NULL, &semaphore)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create semaphore (result: %d)\n", result);
        return NULL;
    }

    return semaphore;
}

static VkFence create_fence(VkDevice device)
{
    VkFence fence;

    VkFenceCreateInfo fence_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    VkResult result;
    if((result = vkCreateFence(device, &fence_create_info, NULL, &fence)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create fence (result: %d)\n", result);
        return NULL;
    }

    return fence;
}
