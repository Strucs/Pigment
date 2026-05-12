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

#ifdef _WIN32
    #define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
    #define VK_USE_PLATFORM_METAL_EXT
#elif defined(__ANDROID__)
    #define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(__linux__)
    #define VK_USE_PLATFORM_XLIB_KHR
    #define VK_USE_PLATFORM_XCB_KHR
    #define VK_USE_PLATFORM_WAYLAND_KHR
#endif

#include "native_surface.h"
#include "internal.h"

#define LOAD_SURFACE_FUNC(instance, name)                                                         \
    PFN_##name name##_func = (PFN_##name) vkGetInstanceProcAddr((instance), #name);               \
    if(name##_func == NULL)                                                                       \
    {                                                                                             \
        PLOG_ERROR(pigment, #name " not available (extension not enabled at vkCreateInstance?)"); \
        return VK_NULL_HANDLE;                                                                    \
    }

VkSurfaceKHR create_vk_surface_from_handles(Pigment* pigment, const PWindowHandles* handles)
{
    if(pigment == NULL || handles == NULL)
    {
        return VK_NULL_HANDLE;
    }

    VkInstance instance  = pigment->instance->vulkan_instance;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result      = VK_ERROR_EXTENSION_NOT_PRESENT;

    switch(handles->type)
    {
#ifdef VK_USE_PLATFORM_WIN32_KHR
        case P_WINDOW_HANDLE_WIN32:
            {
                LOAD_SURFACE_FUNC(instance, vkCreateWin32SurfaceKHR);
                VkWin32SurfaceCreateInfoKHR info = {
                    .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                    .hinstance = (HINSTANCE) handles->win32.hinstance,
                    .hwnd      = (HWND) handles->win32.hwnd,
                };
                result = vkCreateWin32SurfaceKHR_func(instance, &info, &pigment->vk_alloc, &surface);
                break;
            }
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR
        case P_WINDOW_HANDLE_XLIB:
            {
                LOAD_SURFACE_FUNC(instance, vkCreateXlibSurfaceKHR);
                VkXlibSurfaceCreateInfoKHR info = {
                    .sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                    .dpy    = (Display*) handles->xlib.display,
                    .window = (Window) handles->xlib.window,
                };
                result = vkCreateXlibSurfaceKHR_func(instance, &info, &pigment->vk_alloc, &surface);
                break;
            }
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
        case P_WINDOW_HANDLE_XCB:
            {
                LOAD_SURFACE_FUNC(instance, vkCreateXcbSurfaceKHR);
                VkXcbSurfaceCreateInfoKHR info = {
                    .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
                    .connection = (xcb_connection_t*) handles->xcb.connection,
                    .window     = (xcb_window_t) handles->xcb.window,
                };
                result = vkCreateXcbSurfaceKHR_func(instance, &info, &pigment->vk_alloc, &surface);
                break;
            }
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
        case P_WINDOW_HANDLE_WAYLAND:
            {
                LOAD_SURFACE_FUNC(instance, vkCreateWaylandSurfaceKHR);
                VkWaylandSurfaceCreateInfoKHR info = {
                    .sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
                    .display = (struct wl_display*) handles->wayland.display,
                    .surface = (struct wl_surface*) handles->wayland.surface,
                };
                result = vkCreateWaylandSurfaceKHR_func(instance, &info, &pigment->vk_alloc, &surface);
                break;
            }
#endif

#ifdef VK_USE_PLATFORM_METAL_EXT
        case P_WINDOW_HANDLE_METAL:
            {
                LOAD_SURFACE_FUNC(instance, vkCreateMetalSurfaceEXT);
                VkMetalSurfaceCreateInfoEXT info = {
                    .sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
                    .pLayer = (const CAMetalLayer*) handles->metal.ca_metal_layer,
                };
                result = vkCreateMetalSurfaceEXT_func(instance, &info, &pigment->vk_alloc, &surface);
                break;
            }
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
        case P_WINDOW_HANDLE_ANDROID:
            {
                LOAD_SURFACE_FUNC(instance, vkCreateAndroidSurfaceKHR);
                VkAndroidSurfaceCreateInfoKHR info = {
                    .sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
                    .window = (struct ANativeWindow*) handles->android.a_native_window,
                };
                result = vkCreateAndroidSurfaceKHR_func(instance, &info, &pigment->vk_alloc, &surface);
                break;
            }
#endif

        default:
            PLOG_ERROR(pigment, "Unsupported window handle type %d on this platform", (int) handles->type);
            return VK_NULL_HANDLE;
    }

    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create Vulkan surface from native handles (result: %d)", result);
        return VK_NULL_HANDLE;
    }

    return surface;
}
