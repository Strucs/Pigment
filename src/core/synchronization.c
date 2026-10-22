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

#include "synchronization.h"

#include "internal.h"
#include "structs.h"

static VkSemaphore create_semaphore(Pigment* pigment);
static VkFence create_fence(Pigment* pigment);

PSync* create_sync(Pigment* pigment, const uint32_t max_frame, const uint32_t swapchain_image_count)
{
    PDevice* device = pigment->device;
    PSync* sync     = NULL;

    sync = P_NEW_FOR_OBJECT(pigment, sync);
    if(sync == NULL)
    {
        goto ERROR;
    }

    sync->acquisitions = P_NEW_ARRAY_FOR_OBJECT(pigment, sync->acquisitions, max_frame);
    if(sync->acquisitions == NULL)
    {
        goto ERROR;
    }

    sync->render_finished_semaphores = P_NEW_ARRAY_FOR_OBJECT(pigment, sync->render_finished_semaphores, swapchain_image_count);
    if(sync->render_finished_semaphores == NULL)
    {
        goto ERROR;
    }

    if(pigment_has_swapchain_maintenance1(pigment))
    {
        sync->presentations = P_NEW_ARRAY_FOR_OBJECT(pigment, sync->presentations, max_frame);
        if(sync->presentations == NULL)
        {
            goto ERROR;
        }
    }

    for(size_t i = 0; i < max_frame; i++)
    {
        sync->acquisitions[i].semaphore = create_semaphore(pigment);

        if(sync->acquisitions[i].semaphore == NULL)
        {
            PLOG_ERROR(pigment, "Failed to create synchronization objects");
            goto ERROR;
        }

        sync->acquisitions[i].fence = create_fence(pigment);
        if(sync->acquisitions[i].fence == NULL)
        {
            PLOG_ERROR(pigment, "Failed to create synchronization objects");
            goto ERROR;
        }

        if(sync->presentations != NULL)
        {
            sync->presentations[i].fence = create_fence(pigment);
            if(sync->presentations[i].fence == NULL)
            {
                PLOG_ERROR(pigment, "Failed to create synchronization objects");
                goto ERROR;
            }
        }
    }

    for(size_t i = 0; i < swapchain_image_count; i++)
    {
        sync->render_finished_semaphores[i] = create_semaphore(pigment);

        if(sync->render_finished_semaphores[i] == NULL)
        {
            PLOG_ERROR(pigment, "Failed to create synchronization objects");
            goto ERROR;
        }
    }

    return sync;

ERROR:
    if(sync != NULL)
    {
        if(sync->acquisitions != NULL)
        {
            for(size_t i = 0; i < max_frame; i++)
            {
                vkDestroySemaphore(device->logical_device, sync->acquisitions[i].semaphore, &pigment->vk_alloc);
                vkDestroyFence(device->logical_device, sync->acquisitions[i].fence, &pigment->vk_alloc);
            }
        }
        if(sync->render_finished_semaphores != NULL)
        {
            for(size_t i = 0; i < swapchain_image_count; i++)
            {
                vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], &pigment->vk_alloc);
            }
        }
        if(sync->presentations != NULL)
        {
            for(size_t i = 0; i < max_frame; i++)
            {
                if(sync->presentations[i].fence != NULL)
                {
                    vkDestroyFence(device->logical_device, sync->presentations[i].fence, &pigment->vk_alloc);
                }
            }
            P_FREE(pigment, sync->presentations);
        }

        P_FREE(pigment, sync->render_finished_semaphores);
        P_FREE(pigment, sync->acquisitions);
        P_FREE(pigment, sync);
    }
    return NULL;
}

void destroy_sync(Pigment* pigment, PSync* sync, PSwapchain* swapchain, const uint32_t max_frame)
{
    if(sync == NULL || swapchain == NULL)
    {
        return;
    }
    PDevice* device = pigment->device;

    for(size_t i = 0; i < max_frame; i++)
    {
        vkDestroySemaphore(device->logical_device, sync->acquisitions[i].semaphore, &pigment->vk_alloc);
        vkDestroyFence(device->logical_device, sync->acquisitions[i].fence, &pigment->vk_alloc);
        if(sync->presentations != NULL)
        {
            vkDestroyFence(device->logical_device, sync->presentations[i].fence, &pigment->vk_alloc);
        }
    }

    for(size_t i = 0; i < swapchain->image_count; i++)
    {
        vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], &pigment->vk_alloc);
    }

    P_FREE(pigment, sync->presentations);
    P_FREE(pigment, sync->render_finished_semaphores);
    P_FREE(pigment, sync->acquisitions);
    P_FREE(pigment, sync);
}

PResult wait_acquisition(Pigment* pigment, PSync* sync, uint32_t slot, uint64_t timeout_ns)
{
    PAcquireSync* acquisition = &sync->acquisitions[slot];
    if(!acquisition->pending)
    {
        return PIGMENT_SUCCESS;
    }

    VkResult result = vkWaitForFences(pigment->device->logical_device, 1, &acquisition->fence, VK_TRUE, timeout_ns);
    if(result == VK_TIMEOUT)
    {
        return timeout_ns == 0 ? PIGMENT_NOT_READY : PIGMENT_TIMEOUT;
    }

    if(result == VK_ERROR_DEVICE_LOST)
    {
        acquisition->pending = P_FALSE;
    }

    if(device_result(pigment, result) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to wait for acquisition (result: %d)", result);
        return device_result(pigment, result);
    }

    acquisition->pending = P_FALSE;
    return pigment_device_lost(pigment) ? PIGMENT_ERROR_DEVICE_LOST : PIGMENT_SUCCESS;
}

static VkSemaphore create_semaphore(Pigment* pigment)
{
    VkSemaphore semaphore;

    VkSemaphoreCreateInfo semaphore_create_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkResult result = vkCreateSemaphore(pigment->device->logical_device, &semaphore_create_info, &pigment->vk_alloc, &semaphore);
    if(device_result(pigment, result) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create semaphore (result: %d)", result);
        return NULL;
    }

    return semaphore;
}

static VkFence create_fence(Pigment* pigment)
{
    VkFence fence;

    VkFenceCreateInfo fence_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    VkResult result = vkCreateFence(pigment->device->logical_device, &fence_create_info, &pigment->vk_alloc, &fence);
    if(device_result(pigment, result) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create fence (result: %d)", result);
        return NULL;
    }

    return fence;
}
