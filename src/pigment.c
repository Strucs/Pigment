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

#include "pigment.h"

#include "structs.h"
#include "window.h"
#include "instance.h"
#include "device.h"
#include "surface.h"
#include "frame.h"
#include "pipeline.h"
#include "commands.h"
#include "synchronization.h"
#include "vertex.h"
#include "buffers.h"
#include "descriptor.h"
#include "texture.h"
#include "models.h"
#include "camera.h"
#include "time.h"

Pigment* init_pigment(PAppInfo* app_info, PWindowInfo* window_info, PModel* model, TexturesToLoad* textures_to_load, StringArray* texture_paths, uint32_t max_frame_in_flight)
{
    Pigment* pigment = malloc(sizeof(*pigment));
    if(pigment == NULL)
    {
        return NULL;
    }

    pigment->max_frames_in_flight = max_frame_in_flight;
    pigment->model = model;

    pigment->vertex_description = create_vertex_description();
    if(pigment->vertex_description == NULL)
    {
        goto ERROR;
    }

    pigment->window = create_window(window_info);
    if(pigment->window == NULL)
    {
        goto ERROR;
    }
    pigment->instance = create_instance(app_info);
    if(pigment->instance == NULL)
    {
        goto ERROR;
    }
    setup_debug_messenger(pigment->instance);
    pigment->surface   = create_surface(pigment->instance, pigment->window);
    if(pigment->surface == NULL)
    {
        goto ERROR;
    }
    pigment->device    = create_device(pigment->instance, pigment->surface);
    if(pigment->device == NULL)
    {
        goto ERROR;
    }
    pigment->swapchain = create_swapchain(pigment->device, pigment->surface, pigment->window);
    if(pigment->swapchain == NULL)
    {
        goto ERROR;
    }
    create_image_views(pigment->swapchain, pigment->device);
    pigment->render_pass = create_render_pass(pigment->swapchain, pigment->device);
    if(pigment->render_pass == NULL)
    {
        goto ERROR;
    }
    pigment->commands    = create_commands(pigment->device, pigment->surface);
    if(pigment->commands == NULL)
    {
        goto ERROR;
    }
    pigment->textures    = create_textures();
    if(pigment->textures == NULL)
    {
        goto ERROR;
    }
    pigment->samplers    = create_samplers(pigment->device);
    if(pigment->samplers == NULL)
    {
        goto ERROR;
    }

    load_all_textures(pigment->textures, textures_to_load, texture_paths, pigment->commands, pigment->device);

    pigment->descriptor = create_descriptor(pigment->textures, pigment->samplers, pigment->device);
    if(pigment->descriptor == NULL)
    {
        goto ERROR;
    }
    pigment->pipeline   = create_graphic_pipeline(pigment->render_pass, pigment->descriptor, pigment->device, pigment->vertex_description);
    if(pigment->pipeline == NULL)
    {
        goto ERROR;
    }
    create_depth_resources(pigment->swapchain, pigment->commands, pigment->device);
    create_framebuffers(pigment->swapchain, pigment->render_pass, pigment->device);
    pigment->buffers = create_buffers(pigment->model, pigment->device, pigment->commands, pigment->max_frames_in_flight);
    if(pigment->buffers == NULL)
    {
        goto ERROR;
    }
    update_descriptor(pigment->descriptor, pigment->buffers, pigment->textures, pigment->samplers, pigment->device, pigment->max_frames_in_flight);
    update_commands(pigment->commands, pigment->device, pigment->max_frames_in_flight);
    pigment->sync = create_sync(pigment->device, pigment->max_frames_in_flight, pigment->swapchain->image_count);
    if(pigment->sync == NULL)
    {
        goto ERROR;
    }

    destroy_model(pigment->model);

    pigment->camera = create_camera();
    if(pigment->camera == NULL)
    {
        goto ERROR;
    }

    add_camera_to_window(pigment->camera, pigment->window);

    set_mouse_handler(pigment->window);

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

    destroy_camera(pigment->camera);
    destroy_sync(pigment->sync, pigment->device, pigment->swapchain, pigment->max_frames_in_flight);
    destroy_swapchain(pigment->swapchain, pigment->device);
    destroy_buffers(pigment->buffers, pigment->device, pigment->max_frames_in_flight);
    destroy_descriptor(pigment->descriptor, pigment->device);
    destroy_pipeline(pigment->pipeline, pigment->device);
    destroy_textures(pigment->textures, pigment->device);
    destroy_samplers(pigment->samplers, pigment->device);
    destroy_commands(pigment->commands, pigment->device, pigment->max_frames_in_flight);
    destroy_vertex_description(pigment->vertex_description);
    destroy_render_pass(pigment->render_pass, pigment->device);
    destroy_device(pigment->device);
    destroy_surface(pigment->surface, pigment->instance);
    destroy_instance(pigment->instance);
    destroy_window(pigment->window);

    free(pigment);
}

void pigment_run(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return;
    }

    while(!window_should_close(pigment->window))
    {
        poll_events();
        handle_inputs(pigment->window);
        pigment_draw_frame(pigment);
    }

    device_wait_idle(pigment->device);
}

void pigment_draw_frame(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return;
    }

    draw_frame(pigment->buffers, &(pigment->swapchain), &(pigment->sync), pigment->commands, pigment->descriptor, pigment->pipeline, pigment->surface, pigment->window, pigment->render_pass, pigment->device, pigment->max_frames_in_flight);
}
