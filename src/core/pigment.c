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

    pigment->window = create_window(window_info);
    if(pigment->window == NULL)
    {
        goto ERROR;
    }
    pigment->window_renderer = calloc(1, sizeof(*pigment->window_renderer));
    if(pigment->window_renderer == NULL)
    {
        goto ERROR;
    }
    pigment->instance = create_instance(app_info);
    if(pigment->instance == NULL)
    {
        goto ERROR;
    }
    setup_debug_messenger(pigment->instance);
    pigment->window_renderer->surface = create_surface(pigment->instance, pigment->window);
    if(pigment->window_renderer->surface == NULL)
    {
        goto ERROR;
    }
    pigment->device = create_device(pigment->instance, pigment->window_renderer->surface);
    if(pigment->device == NULL)
    {
        goto ERROR;
    }
    uint32_t framebuffer_width = 0, framebuffer_height = 0;
    get_framebuffer_size(pigment->window, &framebuffer_width, &framebuffer_height);
    pigment->window_renderer->swapchain = create_swapchain(framebuffer_width, framebuffer_height, window_info->preferred_present_mode, pigment->window_renderer->surface, pigment->device);
    if(pigment->window_renderer->swapchain == NULL)
    {
        goto ERROR;
    }
    create_image_views(pigment->window_renderer->swapchain, pigment->device);
    pigment->command_pools = create_command_pools(pigment->device, pigment->window_renderer->surface);
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
    create_depth_resources(pigment->window_renderer->swapchain, pigment->device);
    pigment->buffers = create_uniform_buffers(pigment->device, pigment->max_frames_in_flight);
    if(pigment->buffers == NULL)
    {
        goto ERROR;
    }
    update_descriptor(pigment->descriptor, pigment->buffers, pigment->images, pigment->samplers, pigment->max_samplers, pigment->max_images, pigment->device, pigment->max_frames_in_flight);
    pigment->window_renderer->command_buffers = create_command_buffers(pigment->command_pools, 0, pigment->device, pigment->max_frames_in_flight);
    if(pigment->window_renderer->command_buffers == NULL)
    {
        goto ERROR;
    }
    pigment->window_renderer->sync = create_sync(pigment->device, pigment->max_frames_in_flight, pigment->window_renderer->swapchain->image_count);
    if(pigment->window_renderer->sync == NULL)
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
    if(pigment->window_renderer != NULL)
    {
        destroy_sync(pigment->window_renderer->sync, pigment->device, pigment->window_renderer->swapchain, pigment->max_frames_in_flight);
        destroy_command_buffers(pigment->window_renderer->command_buffers, pigment->device, pigment->max_frames_in_flight);
        destroy_swapchain(pigment->window_renderer->swapchain, pigment->device);
    }
    destroy_uniform_buffers(pigment->buffers, pigment->device, pigment->max_frames_in_flight);
    destroy_descriptor(pigment->descriptor, pigment->device);
    destroy_images(pigment->images, pigment->device);
    destroy_samplers(pigment->samplers, pigment->device);
    destroy_command_pools(pigment->command_pools, pigment->device);
    destroy_device(pigment->device);
    if(pigment->window_renderer != NULL)
    {
        destroy_surface(pigment->window_renderer->surface, pigment->instance);
        free(pigment->window_renderer);
    }
    destroy_instance(pigment->instance);
    destroy_window(pigment->window);

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

void pigment_show_window(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return;
    }

    show_window(pigment->window);
}

bool pigment_should_run(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return false;
    }

    return !window_should_close(pigment->window);
}

void pigment_poll_events(void)
{
    poll_events();
}

void pigment_handle_inputs(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return;
    }

    handle_inputs(pigment->window);

    // TODO: replace with proper input API (migrating to SDL3)
    static bool v_was_pressed = false;
    bool v_pressed            = glfwGetKey(pigment->window->window, GLFW_KEY_V) == GLFW_PRESS;
    if(v_pressed && !v_was_pressed)
    {
        PPresentMode current = pigment->window->info->preferred_present_mode;
        PPresentMode next    = (current == P_PRESENT_MODE_MAILBOX) ? P_PRESENT_MODE_FIFO : P_PRESENT_MODE_MAILBOX;
        pigment_set_present_mode(pigment, next);
        printf("Present mode: %s\n", next == P_PRESENT_MODE_MAILBOX ? "MAILBOX" : "FIFO");
    }
    v_was_pressed = v_pressed;
}

void pigment_set_present_mode(Pigment* pigment, PPresentMode mode)
{
    if(pigment == NULL)
    {
        return;
    }

    pigment->window->info->preferred_present_mode = mode;
    pigment->window->framebuffer_resized          = true;
}

bool pigment_begin_frame(Pigment* pigment, PCamera* camera)
{
    if(pigment == NULL)
    {
        return false;
    }

    return begin_frame(pigment->buffers, pigment->window_renderer, pigment->window, pigment->device, camera, &pigment->window_renderer->current_image_index);
}

void pigment_end_frame(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return;
    }

    end_frame(pigment->window_renderer, pigment->device, pigment->window_renderer->current_image_index, pigment->max_frames_in_flight);
}

PWindowRenderer* pigment_get_window_renderer(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return NULL;
    }

    return pigment->window_renderer;
}
