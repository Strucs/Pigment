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

#include "pigment.h"

#include "device.h"
#include "instance.h"

#include "internal.h"

#include <string.h>

Pigment* init_pigment(PAppInfo* app_info, PigmentConfig* config)
{
    PAllocator cpu_alloc = (config != NULL && config->allocator != NULL) ? *config->allocator : pigment_default_allocator;

    Pigment* pigment = cpu_alloc.alloc(cpu_alloc.user_data, sizeof(*pigment), _Alignof(Pigment), P_ALLOC_SCOPE_INSTANCE);
    if(pigment == NULL)
    {
        return NULL;
    }
    memset(pigment, 0, sizeof(*pigment));
    atomic_init(&pigment->device_lost, P_FALSE);
    pigment->cpu_allocator = cpu_alloc;

    if(pigment_log_init(pigment) != PIGMENT_SUCCESS)
    {
        cpu_alloc.free(cpu_alloc.user_data, pigment);
        return NULL;
    }

    if(config != NULL && config->loggers != NULL)
    {
        for(uint32_t i = 0; i < config->logger_count; i++)
        {
            pigment_logger_create(pigment, &config->loggers[i]);
        }
    }

    pigment->config.max_frames_in_flight   = (config && config->max_frames_in_flight) ? config->max_frames_in_flight : PIGMENT_DEFAULT_MAX_FRAMES_IN_FLIGHT;
    pigment->config.validation_enabled     = config && config->enable_validation;
    pigment->config.best_practices_enabled = config && config->enable_best_practices;
    pigment->config.depth_clear_value      = config ? config->depth_clear_value : 0.0f;
    pigment->config.queue_requests         = config ? config->queue_requests : NULL;
    pigment->config.queue_request_count    = config ? config->queue_request_count : 0;
    pigment->config.extra                  = config ? config->extra : NULL;

    pigment->instance = create_instance(pigment, app_info);
    if(pigment->instance == NULL)
    {
        goto ERROR;
    }
    setup_debug_messenger(pigment);

    pigment->device = create_device(pigment);
    if(pigment->device == NULL)
    {
        goto ERROR;
    }

    if(init_gpu_allocator(pigment) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    pigment->swapchain_callbacks = create_swapchain_callback_list(pigment);
    if(pigment->swapchain_callbacks == NULL)
    {
        goto ERROR;
    }

    pigment->deletions = create_deletion_queue(pigment);
    if(pigment->deletions == NULL)
    {
        goto ERROR;
    }

    return pigment;

ERROR:
    destroy_pigment(pigment);

    return NULL;
}

void destroy_pigment(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return;
    }

    PResult result = device_wait_idle(pigment);
    if(result != PIGMENT_SUCCESS && result != PIGMENT_ERROR_DEVICE_LOST)
    {
        PLOG_ERROR(pigment, "Device wait idle failed during destroy (result: %d). Continuing.", result);
    }

    destroy_deletion_queue(pigment, pigment->deletions);

    destroy_swapchain_callback_list(pigment, pigment->swapchain_callbacks);
    destroy_gpu_allocator(pigment);
    destroy_device(pigment);
    destroy_instance(pigment);

    pigment_log_destroy(pigment);

    PAllocator cpu_alloc = pigment->cpu_allocator;
    cpu_alloc.free(cpu_alloc.user_data, pigment);
}

PResult pigment_wait_idle(Pigment* pigment)
{
    PResult result = device_wait_idle(pigment);
    if(result != PIGMENT_SUCCESS && result != PIGMENT_ERROR_DEVICE_LOST)
    {
        return result;
    }
    uint64_t destroyed_count;
    do
    {
        drain_deletion_queue(pigment, P_DELETION_ALREADY_IDLE, &destroyed_count);
    }
    while(destroyed_count != 0);

    return pigment_device_lost(pigment) ? PIGMENT_ERROR_DEVICE_LOST : PIGMENT_SUCCESS;
}

PBool pigment_supports(Pigment* pigment, PFeature feature)
{
    if(pigment == NULL || pigment->device == NULL)
    {
        return P_FALSE;
    }
    if((unsigned) feature >= (unsigned) P_FEATURE_COUNT)
    {
        return P_FALSE;
    }
    return pigment->device->features[feature];
}
