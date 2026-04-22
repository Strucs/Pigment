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

#include "structs.h"
#include "window.h"
#include "instance.h"
#include "device.h"
#include "frame.h"
#include "commands.h"
#include "synchronization.h"
#include "buffers.h"
#include "descriptor.h"
#include "texture.h"

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

    pigment->max_frames_in_flight = (config && config->max_frames_in_flight) ? config->max_frames_in_flight : PIGMENT_DEFAULT_MAX_FRAMES_IN_FLIGHT;
    pigment->max_images           = (config && config->max_images) ? config->max_images : PIGMENT_DEFAULT_MAX_IMAGES;
    pigment->max_samplers         = (config && config->max_samplers) ? config->max_samplers : PIGMENT_DEFAULT_MAX_SAMPLERS;

    pigment->window_capacity = PIGMENT_DEFAULT_WINDOW_CAPACITY;
    pigment->window_count    = 0;
    pigment->windows         = calloc(pigment->window_capacity, sizeof(*pigment->windows));
    pigment->renderers       = calloc(pigment->window_capacity, sizeof(*pigment->renderers));
    if(pigment->windows == NULL || pigment->renderers == NULL)
    {
        goto ERROR;
    }

    pigment->windows[0] = create_window(window_info);
    if(pigment->windows[0] == NULL)
    {
        goto ERROR;
    }
    pigment->renderers[0] = create_window_renderer(window_info);
    if(pigment->renderers[0] == NULL)
    {
        goto ERROR;
    }
    pigment->window_count = 1;

    pigment->instance = create_instance(app_info);
    if(pigment->instance == NULL)
    {
        goto ERROR;
    }
    setup_debug_messenger(pigment->instance);

    pigment->renderers[0]->surface = create_surface(pigment->instance, pigment->windows[0]);
    if(pigment->renderers[0]->surface == NULL)
    {
        goto ERROR;
    }
    pigment->device = create_device(pigment->instance, pigment->renderers[0]->surface);
    if(pigment->device == NULL)
    {
        goto ERROR;
    }
    pigment->command_pools = create_command_pools(pigment->device, pigment->renderers[0]->surface);
    if(pigment->command_pools == NULL)
    {
        goto ERROR;
    }
    pigment->images = create_images();
    if(pigment->images == NULL)
    {
        goto ERROR;
    }
    pigment->samplers = create_samplers(pigment->max_samplers, pigment->device);
    if(pigment->samplers == NULL)
    {
        goto ERROR;
    }

    add_default_image(pigment->images, pigment->command_pools, 0, pigment->device);

    pigment->descriptor = create_descriptor(pigment->max_samplers, pigment->max_images, pigment->device);
    if(pigment->descriptor == NULL)
    {
        goto ERROR;
    }
    pigment->buffers = create_uniform_buffers(pigment->device, pigment->max_frames_in_flight);
    if(pigment->buffers == NULL)
    {
        goto ERROR;
    }
    update_descriptor(pigment->descriptor, pigment->buffers, pigment->images, pigment->samplers, pigment->max_samplers, pigment->max_images, pigment->device, pigment->max_frames_in_flight);

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

    device_wait_idle(pigment->device);
    for(uint32_t i = 0; i < pigment->window_count; i++)
    {
        if(pigment->renderers && pigment->renderers[i] != NULL)
        {
            destroy_sync(pigment->renderers[i]->sync, pigment->device, pigment->renderers[i]->swapchain, pigment->max_frames_in_flight);
            destroy_command_buffers(pigment->renderers[i]->command_buffers, pigment->device, pigment->max_frames_in_flight);
            destroy_swapchain(pigment->renderers[i]->swapchain, pigment->device);
        }
    }
    destroy_uniform_buffers(pigment->buffers, pigment->device, pigment->max_frames_in_flight);
    destroy_descriptor(pigment->descriptor, pigment->device);
    destroy_images(pigment->images, pigment->device);
    destroy_samplers(pigment->samplers, pigment->device);
    destroy_command_pools(pigment->command_pools, pigment->device);
    destroy_device(pigment->device);
    for(uint32_t i = 0; i < pigment->window_count; i++)
    {
        if(pigment->renderers && pigment->renderers[i] != NULL)
        {
            destroy_surface(pigment->renderers[i]->surface, pigment->instance);
            free(pigment->renderers[i]);
        }
    }
    destroy_instance(pigment->instance);
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

    free(pigment);
}

void pigment_wait_idle(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return;
    }

    device_wait_idle(pigment->device);
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

bool pigment_begin_frame(Pigment* pigment, uint32_t window_index, PCamera* camera)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return false;
    }

    return begin_frame(pigment->buffers, pigment->renderers[window_index], pigment->device, camera, &pigment->renderers[window_index]->current_image_index);
}

void pigment_end_frame(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return;
    }

    end_frame(pigment->renderers[window_index], pigment->device, pigment->renderers[window_index]->current_image_index, pigment->max_frames_in_flight);
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

    renderer->swapchain = create_swapchain(framebuffer_width, framebuffer_height, window_info->preferred_present_mode, renderer->transparent_framebuffer, renderer->surface, pigment->device);
    if(renderer->swapchain == NULL)
    {
        return PIGMENT_ERROR;
    }

    create_image_views(renderer->swapchain, pigment->device);
    create_depth_resources(renderer->swapchain, pigment->device);

    renderer->command_buffers = create_command_buffers(pigment->command_pools, 0, pigment->device, pigment->max_frames_in_flight);
    if(renderer->command_buffers == NULL)
    {
        return PIGMENT_ERROR;
    }

    renderer->sync = create_sync(pigment->device, pigment->max_frames_in_flight, renderer->swapchain->image_count);
    if(renderer->sync == NULL)
    {
        return PIGMENT_ERROR;
    }

    return PIGMENT_SUCCESS;
}
