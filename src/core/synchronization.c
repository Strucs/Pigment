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
#include "structs.h"
#include "log_internal.h"

static VkSemaphore create_semaphore(Pigment* pigment);

PSync* create_sync(Pigment* pigment, const uint32_t max_frame, const uint32_t swapchain_image_count)
{
    PDevice* device = pigment->device;
    PSync* sync     = NULL;

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

    sync->per_slot_value = calloc(max_frame, sizeof(*sync->per_slot_value));
    if(sync->per_slot_value == NULL)
    {
        goto ERROR;
    }

    for(size_t i = 0; i < max_frame; i++)
    {
        sync->image_available_semaphores[i] = create_semaphore(pigment);

        if(sync->image_available_semaphores[i] == NULL)
        {
            PLOG_ERROR(pigment, "Failed to create synchronization objects");
            goto ERROR;
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
                vkDestroySemaphore(device->logical_device, sync->image_available_semaphores[i], NULL);
            }
        }
        if(sync->render_finished_semaphores != NULL)
        {
            for(size_t i = 0; i < swapchain_image_count; i++)
            {
                vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], NULL);
            }
        }
        free(sync->per_slot_value);
        free(sync->render_finished_semaphores);
        free(sync->image_available_semaphores);
        free(sync);
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
        vkDestroySemaphore(device->logical_device, sync->image_available_semaphores[i], NULL);
    }

    for(size_t i = 0; i < swapchain->image_count; i++)
    {
        vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], NULL);
    }

    free(sync->per_slot_value);
    free(sync->render_finished_semaphores);
    free(sync->image_available_semaphores);
    free(sync);
}

PResult recreate_image_available_semaphore(Pigment* pigment, PSync* sync, uint32_t index)
{
    PDevice* device   = pigment->device;
    VkSemaphore fresh = create_semaphore(pigment);
    if(fresh == NULL)
    {
        return PIGMENT_ERROR_VULKAN;
    }

    vkDestroySemaphore(device->logical_device, sync->image_available_semaphores[index], NULL);
    sync->image_available_semaphores[index] = fresh;

    return PIGMENT_SUCCESS;
}

PResult recreate_render_finished_semaphores(Pigment* pigment, PSync* sync, uint32_t old_count, uint32_t new_count)
{
    PResult result              = PIGMENT_ERROR_OUT_OF_MEMORY;
    PDevice* device             = pigment->device;
    VkSemaphore* new_semaphores = calloc(new_count, sizeof(*new_semaphores));
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
        vkDestroySemaphore(device->logical_device, sync->render_finished_semaphores[i], NULL);
    }

    free(sync->render_finished_semaphores);
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
            vkDestroySemaphore(device->logical_device, new_semaphores[i], NULL);
        }
    }
    free(new_semaphores);

    return result;
}

static VkSemaphore create_semaphore(Pigment* pigment)
{
    VkSemaphore semaphore;

    VkSemaphoreCreateInfo semaphore_create_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkResult result;
    if((result = vkCreateSemaphore(pigment->device->logical_device, &semaphore_create_info, NULL, &semaphore)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create semaphore (result: %d)", result);
        return NULL;
    }

    return semaphore;
}
