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

#include "window.h"
#include "pigment_sdl.h"
#include "internal.h"
#include "log_internal.h"

static uint32_t find_index_by_window_id(Pigment* pigment, SDL_WindowID id);

PWindow* create_window(Pigment* pigment, PWindowInfo* window_info)
{
    PWindow* window = calloc(1, sizeof(*window));
    if(window == NULL)
    {
        goto ERROR;
    }

    if(window_info->title == NULL)
    {
        PLOG_ERROR(pigment, "Window title cannot be NULL!");
        goto ERROR;
    }

    if(!SDL_Init(SDL_INIT_VIDEO))
    {
        PLOG_ERROR(pigment, "SDL_Init: %s", SDL_GetError());
        goto ERROR;
    }

    window->should_close = false;

    SDL_WindowFlags sdl_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN;
    PWindowFlags pflags       = window_info->flags;
    if(pflags & P_WINDOW_FLAG_RESIZABLE)
    {
        sdl_flags |= SDL_WINDOW_RESIZABLE;
    }
    if(pflags & P_WINDOW_FLAG_BORDERLESS)
    {
        sdl_flags |= SDL_WINDOW_BORDERLESS;
    }
    if(pflags & P_WINDOW_FLAG_FULLSCREEN)
    {
        sdl_flags |= SDL_WINDOW_FULLSCREEN;
    }
    if(pflags & P_WINDOW_FLAG_MAXIMIZED)
    {
        sdl_flags |= SDL_WINDOW_MAXIMIZED;
    }
    if(pflags & P_WINDOW_FLAG_MINIMIZED)
    {
        sdl_flags |= SDL_WINDOW_MINIMIZED;
    }
    if(pflags & P_WINDOW_FLAG_ALWAYS_ON_TOP)
    {
        sdl_flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    }
    if(pflags & P_WINDOW_FLAG_HIGH_DPI)
    {
        sdl_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if(pflags & P_WINDOW_FLAG_TRANSPARENT)
    {
        sdl_flags |= SDL_WINDOW_TRANSPARENT;
    }
    if(pflags & P_WINDOW_FLAG_NOT_FOCUSABLE)
    {
        sdl_flags |= SDL_WINDOW_NOT_FOCUSABLE;
    }

    window->window = SDL_CreateWindow(
        window_info->title,
        window_info->width,
        window_info->height,
        sdl_flags
    );

    if(window->window == NULL)
    {
        PLOG_ERROR(pigment, "SDL_CreateWindow: %s", SDL_GetError());
        goto ERROR;
    }

    window->info = window_info;

    return window;

ERROR:
    PLOG_ERROR(pigment, "Failed to create window!");
    free(window);
    return NULL;
}

void destroy_window(PWindow* window)
{
    if(window == NULL)
    {
        return;
    }
    SDL_DestroyWindow(window->window);
    free(window);
}

bool window_should_close(PWindow* window)
{
    return window->should_close;
}

void show_window(PWindow* window)
{
    SDL_ShowWindow(window->window);
}

void get_framebuffer_size(PWindow* window, uint32_t* out_width, uint32_t* out_height)
{
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window->window, &width, &height);
    *out_width  = (uint32_t) width;
    *out_height = (uint32_t) height;
}

const char* const* window_get_vk_instance_extensions(Pigment* pigment, uint32_t* extension_count)
{
    *extension_count              = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(extension_count);
    if(extensions == NULL)
    {
        PLOG_ERROR(pigment, "SDL_Vulkan_GetInstanceExtensions: %s", SDL_GetError());
        return NULL;
    }
    return extensions;
}

bool window_create_vk_surface(Pigment* pigment, PWindow* window, VkSurfaceKHR* out_surface)
{
    if(!SDL_Vulkan_CreateSurface(window->window, pigment->instance->vulkan_instance, NULL, out_surface))
    {
        PLOG_ERROR(pigment, "SDL_Vulkan_CreateSurface: %s", SDL_GetError());
        return false;
    }
    return true;
}

static uint32_t find_index_by_window_id(Pigment* pigment, SDL_WindowID id)
{
    for(uint32_t i = 0; i < pigment->window_count; i++)
    {
        if(SDL_GetWindowID(pigment->windows[i]->window) == id)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

void pigment_handle_sdl_event(Pigment* pigment, const SDL_Event* event)
{
    if(pigment == NULL || event == NULL)
    {
        return;
    }

    switch(event->type)
    {
        case SDL_EVENT_QUIT:
            {
                for(uint32_t i = 0; i < pigment->window_count; i++)
                {
                    pigment->windows[i]->should_close = true;
                }
                break;
            }
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            {
                uint32_t i = find_index_by_window_id(pigment, event->window.windowID);
                if(i != UINT32_MAX)
                {
                    pigment->windows[i]->should_close = true;
                }
                break;
            }
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                uint32_t i = find_index_by_window_id(pigment, event->window.windowID);
                if(i != UINT32_MAX)
                {
                    PWindowRenderer* renderer         = pigment->renderers[i];
                    renderer->pending_width           = (uint32_t) event->window.data1;
                    renderer->pending_height          = (uint32_t) event->window.data2;
                    renderer->framebuffer_resized     = true;
                    pigment->windows[i]->info->width  = event->window.data1;
                    pigment->windows[i]->info->height = event->window.data2;
                }
                break;
            }
        default:
            break;
    }
}

SDL_Window* pigment_get_sdl_window(Pigment* pigment, uint32_t window_index)
{
    if(pigment == NULL || window_index >= pigment->window_count)
    {
        return NULL;
    }
    return pigment->windows[window_index]->window;
}
