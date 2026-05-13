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

    sync->image_available_semaphores = P_NEW_ARRAY_FOR_OBJECT(pigment, sync->image_available_semaphores, max_frame);
    if(sync->image_available_semaphores == NULL)
    {
        goto ERROR;
    }

    sync->render_finished_semaphores = P_NEW_ARRAY_FOR_OBJECT(pigment, sync->render_finished_semaphores, swapchain_image_count);
    if(sync->render_finished_semaphores == NULL)
    {
        goto ERROR;
    }

    sync->per_slot_handle = P_NEW_ARRAY_FOR_OBJECT(pigment, sync->per_slot_handle, max_frame);
    if(sync->per_slot_handle == NULL)
    {
        goto ERROR;
    }

    if(pigment_has_swapchain_maintenance1(pigment))
    {
        sync->present_fences = P_NEW_ARRAY_FOR_OBJECT(pigment, sync->present_fences, max_frame);
        if(sync->present_fences == NULL)
        {
            goto ERROR;
        }
    }

    for(size_t i = 0; i < max_frame; i++)
    {
        sync->image_available_semaphores[i] = create_semaphore(pigment);

        if(sync->image_available_semaphores[i] == NULL)
        {
            PLOG_ERROR(pigment, "Failed to create synchronization objects");
            goto ERROR;
        }

        if(sync->present_fences != NULL)
        {
            sync->present_fences[i] = create_fence(pigment);
            if(sync->present_fences[i] == NULL)
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
        if(sync->image_available_semaphores != NULL)
        {
            for(size_t i = 0; i < max_frame; i++)
            {
                vkDestroySemaphore(device->logical_device, sync->image_available_semaphores[i], &pigment->vk_alloc);
            }
        }
        if(sync->render_finished_semaphores != NULL)
        {
            for(size_t i = 0; i < swapchain_image_count; i++)
            {
                vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], &pigment->vk_alloc);
            }
        }
        if(sync->present_fences != NULL)
        {
            for(size_t i = 0; i < max_frame; i++)
            {
                if(sync->present_fences[i] != NULL)
                {
                    vkDestroyFence(device->logical_device, sync->present_fences[i], &pigment->vk_alloc);
                }
            }
            P_FREE(pigment, sync->present_fences);
        }
        P_FREE(pigment, sync->per_slot_handle);
        P_FREE(pigment, sync->render_finished_semaphores);
        P_FREE(pigment, sync->image_available_semaphores);
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
        vkDestroySemaphore(device->logical_device, sync->image_available_semaphores[i], &pigment->vk_alloc);
        if(sync->present_fences != NULL)
        {
            vkDestroyFence(device->logical_device, sync->present_fences[i], &pigment->vk_alloc);
        }
    }

    for(size_t i = 0; i < swapchain->image_count; i++)
    {
        vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], &pigment->vk_alloc);
    }

    P_FREE(pigment, sync->present_fences);
    P_FREE(pigment, sync->per_slot_handle);
    P_FREE(pigment, sync->render_finished_semaphores);
    P_FREE(pigment, sync->image_available_semaphores);
    P_FREE(pigment, sync);
}

PResult recreate_image_available_semaphore(Pigment* pigment, PSync* sync, uint32_t index)
{
    PDevice* device   = pigment->device;
    VkSemaphore fresh = create_semaphore(pigment);
    if(fresh == NULL)
    {
        return PIGMENT_ERROR_VULKAN;
    }

    vkDestroySemaphore(device->logical_device, sync->image_available_semaphores[index], &pigment->vk_alloc);
    sync->image_available_semaphores[index] = fresh;

    return PIGMENT_SUCCESS;
}

PResult recreate_render_finished_semaphores(Pigment* pigment, PSync* sync, uint32_t old_count, uint32_t new_count)
{
    PResult result              = PIGMENT_ERROR_OUT_OF_MEMORY;
    PDevice* device             = pigment->device;
    VkSemaphore* new_semaphores = P_NEW_ARRAY_FOR_OBJECT(pigment, new_semaphores, new_count);
    if(new_semaphores == NULL)
    {
        goto ERROR;
    }

    result = PIGMENT_ERROR_VULKAN;
    for(uint32_t i = 0; i < new_count; i++)
    {
        new_semaphores[i] = create_semaphore(pigment);
        if(new_semaphores[i] == NULL)
        {
            goto ERROR;
        }
    }

    for(uint32_t i = 0; i < old_count; i++)
    {
        vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], &pigment->vk_alloc);
    }

    P_FREE(pigment, sync->render_finished_semaphores);
    sync->render_finished_semaphores = new_semaphores;

    return PIGMENT_SUCCESS;

ERROR:
    PLOG_ERROR(pigment, "Failed to recreate render_finished_semaphores, keeping previous ones");
    if(new_semaphores == NULL)
    {
        return result;
    }

    for(uint32_t i = 0; i < new_count; i++)
    {
        if(new_semaphores[i] != NULL)
        {
            vkDestroySemaphore(device->logical_device, new_semaphores[i], &pigment->vk_alloc);
        }
    }
    P_FREE(pigment, new_semaphores);

    return result;
}

static VkSemaphore create_semaphore(Pigment* pigment)
{
    VkSemaphore semaphore;

    VkSemaphoreCreateInfo semaphore_create_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkResult result;
    if((result = vkCreateSemaphore(pigment->device->logical_device, &semaphore_create_info, &pigment->vk_alloc, &semaphore)) != VK_SUCCESS)
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

    VkResult result;
    if((result = vkCreateFence(pigment->device->logical_device, &fence_create_info, &pigment->vk_alloc, &fence)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create fence (result: %d)", result);
        return NULL;
    }

    return fence;
}
