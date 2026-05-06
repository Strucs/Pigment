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

#include "internal.h"
#include "log_internal.h"
#include "instance.h"
#include "device.h"
#include "frame.h"
#include "commands.h"
#include "buffers.h"
#include "pipeline.h"

Pigment* init_pigment(PAppInfo* app_info, PigmentConfig* config)
{
    Pigment* pigment = calloc(1, sizeof(*pigment));
    if(pigment == NULL)
    {
        return NULL;
    }

    if(pigment_log_init(pigment) != PIGMENT_SUCCESS)
    {
        free(pigment);
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

    const PVkInitInfo* vk_init = (const PVkInitInfo*) pigment->config.extra;
    if(vk_init != NULL && vk_init->allocator != NULL)
    {
        pigment->allocator      = vk_init->allocator;
        pigment->owns_allocator = P_FALSE;
    }
    else
    {
        pigment->allocator      = pigment_vk_create_default_allocator(pigment, NULL);
        pigment->owns_allocator = P_TRUE;
    }

    if(pigment->allocator == NULL)
    {
        goto ERROR;
    }
    pigment->command_pools = create_command_pools(pigment);
    if(pigment->command_pools == NULL)
    {
        goto ERROR;
    }

    pigment->layouts = create_layout_list();
    if(pigment->layouts == NULL)
    {
        goto ERROR;
    }

    pigment->pipelines = create_pipeline_list();
    if(pigment->pipelines == NULL)
    {
        goto ERROR;
    }

    pigment->resize_callbacks = create_resize_callback_list();
    if(pigment->resize_callbacks == NULL)
    {
        goto ERROR;
    }

    pigment->renderers = create_renderer_list();
    if(pigment->renderers == NULL)
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

    device_wait_idle(pigment);

    destroy_renderer_list(pigment, pigment->renderers);

    destroy_pipeline_list(pigment, pigment->pipelines);
    destroy_layout_list(pigment, pigment->layouts);
    destroy_resize_callback_list(pigment->resize_callbacks);
    destroy_command_pools(pigment, pigment->command_pools);
    if(pigment->owns_allocator)
    {
        pigment_vk_destroy_allocator(pigment->allocator);
    }
    destroy_device(pigment);
    destroy_instance(pigment);

    pigment_log_destroy(pigment);
    free(pigment);
}

void pigment_wait_idle(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return;
    }

    device_wait_idle(pigment);
}

void pigment_wait_frame_ready(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return;
    }

    uint32_t current_frame = renderer->swapchain->current_frame;
    vkWaitForFences(pigment->device->logical_device, 1, &renderer->sync->in_flight_fences[current_frame], VK_TRUE, UINT64_MAX);
}

PCommandBuffer* pigment_begin_frame(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return NULL;
    }

    if(!begin_frame(pigment, renderer, &renderer->current_image_index))
    {
        return NULL;
    }

    return renderer->command_buffers[renderer->swapchain->current_frame];
}

void pigment_end_frame(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return;
    }

    end_frame(pigment, renderer, renderer->current_image_index, pigment->config.max_frames_in_flight);
}

uint32_t pigment_renderer_current_frame(PWindowRenderer* renderer)
{
    if(renderer == NULL || renderer->swapchain == NULL)
    {
        return 0;
    }

    return renderer->swapchain->current_frame;
}

PCommandBuffer* pigment_renderer_frame_cmd(PWindowRenderer* renderer)
{
    if(renderer == NULL || renderer->swapchain == NULL)
    {
        return NULL;
    }
    return renderer->command_buffers[renderer->swapchain->current_frame];
}

void pigment_begin_swapchain_pass(Pigment* pigment, PWindowRenderer* renderer)
{
    if(pigment == NULL || renderer == NULL)
    {
        return;
    }

    begin_swapchain_pass(pigment, renderer, renderer->current_image_index);
}

void pigment_end_swapchain_pass(PWindowRenderer* renderer)
{
    if(renderer == NULL)
    {
        return;
    }

    end_swapchain_pass(renderer, renderer->current_image_index);
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
