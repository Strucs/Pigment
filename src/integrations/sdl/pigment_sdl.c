/**
 * Copyright 2026 Angel-Leduc TA
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

#include "pigment_sdl.h"

#include <SDL3/SDL.h>

#define PIGMENT_PROP_SDL_METAL_VIEW "pigment.sdl.metal_view"

PWindowHandles pigment_sdl_get_window_handles(SDL_Window* window)
{
    PWindowHandles handles = {0};
    if(window == NULL)
    {
        return handles;
    }

    SDL_PropertiesID props = SDL_GetWindowProperties(window);

#ifdef SDL_PLATFORM_WIN32
    handles.type            = P_WINDOW_HANDLE_WIN32;
    handles.win32.hwnd      = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    handles.win32.hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, NULL);
#elif defined(SDL_PLATFORM_APPLE)
    SDL_MetalView metal_view = (SDL_MetalView) SDL_GetPointerProperty(props, PIGMENT_PROP_SDL_METAL_VIEW, NULL);
    if(metal_view == NULL)
    {
        metal_view = SDL_Metal_CreateView(window);
        SDL_SetPointerProperty(props, PIGMENT_PROP_SDL_METAL_VIEW, metal_view);
    }
    handles.type                 = P_WINDOW_HANDLE_METAL;
    handles.metal.ca_metal_layer = SDL_Metal_GetLayer(metal_view);
#elif defined(SDL_PLATFORM_LINUX)
    if(SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
    {
        handles.type            = P_WINDOW_HANDLE_WAYLAND;
        handles.wayland.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
        handles.wayland.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    }
    else
    {
        handles.type         = P_WINDOW_HANDLE_XLIB;
        handles.xlib.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
        handles.xlib.window  = (unsigned long) SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    }
#elif defined(SDL_PLATFORM_ANDROID)
    handles.type                    = P_WINDOW_HANDLE_ANDROID;
    handles.android.a_native_window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL);
#endif

    return handles;
}

static SDL_LogPriority severity_to_sdl(PigmentLogSeverity severity)
{
    switch(severity)
    {
        case PIGMENT_LOG_TRACE_BIT:
            return SDL_LOG_PRIORITY_TRACE;
        case PIGMENT_LOG_DEBUG_BIT:
            return SDL_LOG_PRIORITY_DEBUG;
        case PIGMENT_LOG_INFO_BIT:
            return SDL_LOG_PRIORITY_INFO;
        case PIGMENT_LOG_WARN_BIT:
            return SDL_LOG_PRIORITY_WARN;
        case PIGMENT_LOG_ERROR_BIT:
            return SDL_LOG_PRIORITY_ERROR;
        default:
            return SDL_LOG_PRIORITY_INFO;
    }
}

void pigment_sdl_log_callback(PigmentLogSeverity severity, PigmentLogType type, const PigmentLogRecord* record, void* user_data)
{
    (void) type;
    (void) user_data;

    SDL_LogPriority priority = severity_to_sdl(severity);

    if(record->message_id_name != NULL)
    {
        SDL_LogMessage(SDL_LOG_CATEGORY_GPU, priority, "[%s] %s", record->message_id_name, record->message);
    }
    else
    {
        SDL_LogMessage(SDL_LOG_CATEGORY_GPU, priority, "%s", record->message);
    }
}
