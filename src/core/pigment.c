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
#include "window.h"
#include "instance.h"
#include "device.h"
#include "frame.h"
#include "commands.h"
#include "synchronization.h"
#include "buffers.h"
#include "descriptor.h"
#include "texture.h"
#include "pipeline.h"

#define PIGMENT_DEFAULT_WINDOW_CAPACITY 4

static PWindowRenderer* create_window_renderer(PWindowInfo* window_info);
static int init_window_renderer_resources(Pigment* pigment, uint32_t window_index);

Pigment* init_pigment(PAppInfo* app_info, PWindowInfo* window_info, PigmentConfig* config)
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
    pigment->config.max_images             = (config && config->max_images) ? config->max_images : PIGMENT_DEFAULT_MAX_IMAGES;
    pigment->config.max_samplers           = (config && config->max_samplers) ? config->max_samplers : PIGMENT_DEFAULT_MAX_SAMPLERS;
    pigment->config.validation_enabled     = config && config->enable_validation;
    pigment->config.best_practices_enabled = config && config->enable_best_practices;
    pigment->config.depth_clear_value      = config ? config->depth_clear_value : 0.0f;
    pigment->config.extra                  = config ? config->extra : NULL;

    pigment->window_capacity = PIGMENT_DEFAULT_WINDOW_CAPACITY;
    pigment->window_count    = 0;
    pigment->windows         = calloc(pigment->window_capacity, sizeof(*pigment->windows));
    pigment->renderers       = calloc(pigment->window_capacity, sizeof(*pigment->renderers));
    if(pigment->windows == NULL || pigment->renderers == NULL)
    {
        goto ERROR;
    }

    PWindow* window = create_window(pigment, window_info);
    if(window == NULL)
    {
        goto ERROR;
    }
    pigment->windows[0] = window;

    PWindowRenderer* renderer = create_window_renderer(window_info);
    if(renderer == NULL)
    {
        goto ERROR;
    }
    pigment->renderers[0] = renderer;

    pigment->window_count = 1;

    pigment->instance = create_instance(pigment, app_info);
    if(pigment->instance == NULL)
    {
        goto ERROR;
    }
    setup_debug_messenger(pigment);

    pigment->renderers[0]->surface = create_surface(pigment, pigment->windows[0]);
    if(pigment->renderers[0]->surface == NULL)
    {
        goto ERROR;
    }
    pigment->device = create_device(pigment, pigment->renderers[0]->surface);
    if(pigment->device == NULL)
    {
        goto ERROR;
    }

    const PVkInitInfo* vk_init = (const PVkInitInfo*) pigment->config.extra;
    if(vk_init != NULL && vk_init->allocator != NULL)
    {
        pigment->allocator      = vk_init->allocator;
        pigment->owns_allocator = false;
    }
    else
    {
        pigment->allocator      = pigment_vk_create_default_allocator(pigment, NULL);
        pigment->owns_allocator = true;
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
    pigment->images = create_images();
    if(pigment->images == NULL)
    {
        goto ERROR;
    }
    pigment->samplers = create_samplers(pigment, pigment->config.max_samplers);
    if(pigment->samplers == NULL)
    {
        goto ERROR;
    }

    add_default_image(pigment, pigment->images, pigment_default_pool(pigment));

    pigment->descriptor = create_descriptor(pigment, pigment->config.max_samplers, pigment->config.max_images);
    if(pigment->descriptor == NULL)
    {
        goto ERROR;
    }
    update_descriptor(pigment, pigment->descriptor, pigment->images, pigment->samplers, pigment->config.max_samplers, pigment->config.max_images, pigment->config.max_frames_in_flight);

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

    if(init_window_renderer_resources(pigment, 0) != PIGMENT_SUCCESS)
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
    for(uint32_t i = 0; i < pigment->window_count; i++)
    {
        if(pigment->renderers && pigment->renderers[i] != NULL)
        {
            destroy_sync(pigment, pigment->renderers[i]->sync, pigment->renderers[i]->swapchain, pigment->config.max_frames_in_flight);
            destroy_command_buffers(pigment, pigment->renderers[i]->command_buffers, pigment->config.max_frames_in_flight);
            destroy_swapchain(pigment, pigment->renderers[i]->swapchain);
        }
    }
    destroy_pipeline_list(pigment, pigment->pipelines);
    destroy_layout_list(pigment, pigment->layouts);
    destroy_descriptor(pigment, pigment->descriptor);
    destroy_images(pigment, pigment->images);
    destroy_samplers(pigment, pigment->samplers);
    destroy_command_pools(pigment, pigment->command_pools);
    if(pigment->owns_allocator)
    {
        pigment_vk_destroy_allocator(pigment->allocator);
    }
    destroy_device(pigment);
    for(uint32_t i = 0; i < pigment->window_count; i++)
    {
        if(pigment->renderers && pigment->renderers[i] != NULL)
        {
            destroy_surface(pigment, pigment->renderers[i]->surface);
            free(pigment->renderers[i]);
        }
    }
    destroy_instance(pigment);
    for(uint32_t i = 0; i < pigment->window_count; i++)
    {
        if(pigment->windows && pigment->windows[i] != NULL)
        {
            destroy_window(pigment->windows[i]);
        }
    }
    free(pigment->windows);
    free(pigment->renderers);

    SDL_Quit();

    pigment_log_destroy(pigment);
    free(pigment);
    pigment = NULL;
}

void pigment_wait_idle(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return;
    }

    device_wait_idle(pigment);
}

void pigment_show_window(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return;
    }

    show_window(pigment->windows[window_index]);
}

bool pigment_should_run(Pigment* pigment)
{
    if(pigment == NULL || pigment->window_count == 0)
    {
        return false;
    }

    for(uint32_t i = 0; i < pigment->window_count; i++)
    {
        if(!window_should_close(pigment->windows[i]))
        {
            return true;
        }
    }
    return false;
}

void pigment_wait_frame_ready(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return;
    }

    PWindowRenderer* renderer = pigment->renderers[window_index];
    uint32_t current_frame    = renderer->swapchain->current_frame;
    vkWaitForFences(pigment->device->logical_device, 1, &renderer->sync->in_flight_fences[current_frame], VK_TRUE, UINT64_MAX);
}

void pigment_set_present_mode(Pigment* pigment, uint32_t window_index, PPresentMode mode)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return;
    }

    PWindowRenderer* renderer                                    = pigment->renderers[window_index];
    renderer->requested_present_mode                             = mode;
    renderer->framebuffer_resized                                = true;
    pigment->windows[window_index]->info->preferred_present_mode = mode;
}

bool pigment_begin_frame(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return false;
    }

    return begin_frame(pigment, pigment->renderers[window_index], &pigment->renderers[window_index]->current_image_index);
}

void pigment_end_frame(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return;
    }

    end_frame(pigment, pigment->renderers[window_index], pigment->renderers[window_index]->current_image_index, pigment->config.max_frames_in_flight);
}

void pigment_begin_swapchain_pass(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return;
    }

    PWindowRenderer* renderer = pigment->renderers[window_index];
    begin_swapchain_pass(pigment, renderer, renderer->current_image_index);
}

void pigment_end_swapchain_pass(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return;
    }

    PWindowRenderer* renderer = pigment->renderers[window_index];
    end_swapchain_pass(renderer, renderer->current_image_index);
}

bool pigment_supports(Pigment* pigment, PFeature feature)
{
    if(pigment == NULL || pigment->device == NULL)
    {
        return false;
    }
    if((unsigned) feature >= (unsigned) P_FEATURE_COUNT)
    {
        return false;
    }
    return pigment->device->features[feature];
}

PWindow* pigment_get_window(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return NULL;
    }

    return pigment->windows[window_index];
}

PWindowRenderer* pigment_get_window_renderer(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return NULL;
    }

    return pigment->renderers[window_index];
}

uint32_t pigment_window_count(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return 0;
    }

    return pigment->window_count;
}

static PWindowRenderer* create_window_renderer(PWindowInfo* window_info)
{
    PWindowRenderer* renderer = calloc(1, sizeof(*renderer));
    if(renderer == NULL)
    {
        return NULL;
    }

    renderer->framebuffer_resized     = false;
    renderer->pending_width           = (uint32_t) window_info->width;
    renderer->pending_height          = (uint32_t) window_info->height;
    renderer->requested_present_mode  = window_info->preferred_present_mode;
    renderer->transparent_framebuffer = (window_info->flags & P_WINDOW_FLAG_TRANSPARENT) != 0;

    return renderer;
}

static int init_window_renderer_resources(Pigment* pigment, uint32_t window_index)
{
    PWindow* window           = pigment->windows[window_index];
    PWindowRenderer* renderer = pigment->renderers[window_index];
    PWindowInfo* window_info  = window->info;

    uint32_t framebuffer_width = 0, framebuffer_height = 0;
    get_framebuffer_size(window, &framebuffer_width, &framebuffer_height);

    renderer->swapchain = create_swapchain(pigment, framebuffer_width, framebuffer_height, window_info->preferred_present_mode, renderer->transparent_framebuffer, renderer->surface);
    if(renderer->swapchain == NULL)
    {
        return PIGMENT_ERROR;
    }

    create_image_views(pigment, renderer->swapchain);
    create_depth_resources(pigment, renderer->swapchain);

    renderer->command_buffers = create_command_buffers(pigment, pigment_default_pool(pigment), pigment->config.max_frames_in_flight);
    if(renderer->command_buffers == NULL)
    {
        return PIGMENT_ERROR;
    }

    renderer->sync = create_sync(pigment, pigment->config.max_frames_in_flight, renderer->swapchain->image_count);
    if(renderer->sync == NULL)
    {
        return PIGMENT_ERROR;
    }

    return PIGMENT_SUCCESS;
}
