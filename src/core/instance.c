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

#include "instance.h"
#include "internal.h"
#include "pigment_vk.h"

static PBool layer_available(const VkLayerProperties* available, uint32_t count, const char* name);
static PResult build_instance_layers(Pigment* pigment, PInstance* instance, const PVkInitInfo* vk_init, const VkLayerProperties* available, uint32_t available_count);
static PResult build_instance_extensions(Pigment* pigment, PInstance* instance, const PVkInitInfo* vk_init, const VkExtensionProperties* available, uint32_t available_count);
static PigmentLogSeverity vk_severity_to_pigment(VkDebugUtilsMessageSeverityFlagBitsEXT severity);
static PigmentLogType vk_type_to_pigment(VkDebugUtilsMessageTypeFlagsEXT type);
static void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT* create_info, Pigment* pigment);
static VkResult create_debug_utils_messenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* p_create_info, const VkAllocationCallbacks* p_allocator, VkDebugUtilsMessengerEXT* p_debug_messenger);
static void destroy_debug_utils_messenger(VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger, const VkAllocationCallbacks* p_allocator);
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity_flags,
    VkDebugUtilsMessageTypeFlagsEXT type_flags,
    const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
    void* user_data
);

PInstance* create_instance(Pigment* pigment, PAppInfo* info)
{
    PInstance* instance                         = NULL;
    VkLayerProperties* available_layers         = NULL;
    VkExtensionProperties* available_extensions = NULL;
    uint32_t available_layer_count              = 0;
    uint32_t available_extension_count          = 0;

    instance = P_NEW_FOR_INSTANCE(pigment, instance);
    if(instance == NULL)
    {
        goto ERROR;
    }

    if(info == NULL)
    {
        PLOG_WARN(pigment, "PAppInfo is NULL. Using default values.");
    }

    VkResult volk_result = volkInitialize();
    if(volk_result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Vulkan loader not found (result: %d)", volk_result);
        goto ERROR;
    }

    const PVkInitInfo* vk_init = (const PVkInitInfo*) pigment->config.extra;

    vkEnumerateInstanceLayerProperties(&available_layer_count, NULL);
    if(available_layer_count > 0)
    {
        available_layers = P_NEW_ARRAY_FOR_COMMAND(pigment, available_layers, available_layer_count);
        if(available_layers == NULL)
        {
            goto ERROR;
        }
        vkEnumerateInstanceLayerProperties(&available_layer_count, available_layers);
    }

    vkEnumerateInstanceExtensionProperties(NULL, &available_extension_count, NULL);
    if(available_extension_count > 0)
    {
        available_extensions = P_NEW_ARRAY_FOR_COMMAND(pigment, available_extensions, available_extension_count);
        if(available_extensions == NULL)
        {
            goto ERROR;
        }
        vkEnumerateInstanceExtensionProperties(NULL, &available_extension_count, available_extensions);
    }

    if(pigment->config.validation_enabled)
    {
        PBool layer_ok       = layer_available(available_layers, available_layer_count, "VK_LAYER_KHRONOS_validation");
        PBool debug_utils_ok = extension_available(available_extensions, available_extension_count, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if(!layer_ok)
        {
            PLOG_WARN(pigment, "VK_LAYER_KHRONOS_validation not installed. Disabling validation.");
        }
        if(!debug_utils_ok)
        {
            PLOG_WARN(pigment, "VK_EXT_debug_utils not available. Disabling validation.");
        }
        if(!layer_ok || !debug_utils_ok)
        {
            pigment->config.validation_enabled     = P_FALSE;
            pigment->config.best_practices_enabled = P_FALSE;
        }
    }

    if(build_instance_layers(pigment, instance, vk_init, available_layers, available_layer_count) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    if(build_instance_extensions(pigment, instance, vk_init, available_extensions, available_extension_count) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    VkApplicationInfo app_info  = {0};
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = info != NULL ? info->app_name : "Unnamed";
    app_info.applicationVersion = info != NULL ? info->app_version : PIGMENT_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName        = "Pigment";
    app_info.engineVersion      = VK_MAKE_VERSION(0, 0, 3);
    app_info.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info = {0};
    create_info.sType                = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo     = &app_info;
#ifdef __APPLE__
    create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    create_info.enabledExtensionCount   = instance->extensions->size;
    create_info.ppEnabledExtensionNames = instance->extensions->names;
    create_info.enabledLayerCount       = instance->layers->size;
    create_info.ppEnabledLayerNames     = instance->layers->size > 0 ? instance->layers->names : NULL;

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {0};
    VkValidationFeatureEnableEXT enabled_features[]      = {VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT};
    VkValidationFeaturesEXT validation_features          = {
        .sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .enabledValidationFeatureCount = 1,
        .pEnabledValidationFeatures    = enabled_features,
    };

    if(pigment->config.validation_enabled)
    {
        populate_debug_messenger_create_info(&debug_create_info, pigment);
        if(pigment->config.best_practices_enabled)
        {
            debug_create_info.pNext = &validation_features;
        }
        create_info.pNext = &debug_create_info;
    }
    else
    {
        create_info.pNext = NULL;
    }

    if(vk_init != NULL && vk_init->instance_pnext_chain != NULL)
    {
        pigment_vk_append_pnext(&create_info, vk_init->instance_pnext_chain);
    }

    VkResult result;
    if((result = vkCreateInstance(&create_info, &pigment->vk_alloc, &(instance->vulkan_instance))) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create an instance. (result: %d)", result);
        goto ERROR;
    }

    volkLoadInstance(instance->vulkan_instance);

    P_FREE(pigment, available_layers);
    P_FREE(pigment, available_extensions);

    return instance;

ERROR:
    P_FREE(pigment, available_layers);
    P_FREE(pigment, available_extensions);
    if(instance != NULL)
    {
        if(instance->layers != NULL)
        {
            P_FREE(pigment, instance->layers->names);
        }
        P_FREE(pigment, instance->layers);
        if(instance->extensions != NULL)
        {
            P_FREE(pigment, instance->extensions->names);
        }
        P_FREE(pigment, instance->extensions);
        P_FREE(pigment, instance);
    }
    return NULL;
}

static PResult build_instance_layers(Pigment* pigment, PInstance* instance, const PVkInitInfo* vk_init, const VkLayerProperties* available, uint32_t available_count)
{
    instance->layers = P_NEW_FOR_INSTANCE(pigment, instance->layers);
    if(instance->layers == NULL)
    {
        goto ERROR;
    }

    uint32_t max_layers = 0;
    if(pigment->config.validation_enabled)
    {
        max_layers++;
    }

    if(vk_init != NULL)
    {
        max_layers += vk_init->req_instance_layers_count;
        max_layers += vk_init->opt_instance_layers_count;
    }

    if(max_layers == 0)
    {
        return PIGMENT_SUCCESS;
    }

    instance->layers->names = P_NEW_ARRAY_FOR_INSTANCE(pigment, instance->layers->names, max_layers);
    if(instance->layers->names == NULL)
    {
        goto ERROR;
    }

    if(pigment->config.validation_enabled)
    {
        instance->layers->names[instance->layers->size++] = "VK_LAYER_KHRONOS_validation";
    }

    if(vk_init == NULL)
    {
        return PIGMENT_SUCCESS;
    }

    for(uint32_t i = 0; i < vk_init->req_instance_layers_count; i++)
    {
        const char* name = vk_init->req_instance_layers[i];
        if(!layer_available(available, available_count, name))
        {
            PLOG_ERROR(pigment, "Required instance layer not available: %s", name);
            goto ERROR;
        }
        if(!name_in_list((const char* const*) instance->layers->names, instance->layers->size, name))
        {
            instance->layers->names[instance->layers->size++] = name;
        }
    }

    for(uint32_t i = 0; i < vk_init->opt_instance_layers_count; i++)
    {
        const char* name = vk_init->opt_instance_layers[i];
        if(!layer_available(available, available_count, name))
        {
            PLOG_WARN(pigment, "Optional instance layer not available, skipping: %s", name);
            continue;
        }
        if(!name_in_list((const char* const*) instance->layers->names, instance->layers->size, name))
        {
            instance->layers->names[instance->layers->size++] = name;
        }
    }

    return PIGMENT_SUCCESS;

ERROR:
    if(instance->layers != NULL)
    {
        P_FREE(pigment, instance->layers->names);
    }
    P_FREE(pigment, instance->layers);
    instance->layers = NULL;
    return PIGMENT_ERROR_OUT_OF_MEMORY;
}

static PResult build_instance_extensions(Pigment* pigment, PInstance* instance, const PVkInitInfo* vk_init, const VkExtensionProperties* available, uint32_t available_count)
{
    const char** names = NULL;
    uint32_t count     = 0;

    instance->extensions = P_NEW_FOR_INSTANCE(pigment, instance->extensions);
    if(instance->extensions == NULL)
    {
        goto ERROR;
    }

    const char* surface_extensions[4] = {0};
    uint32_t surface_extension_count  = 0;

    surface_extensions[surface_extension_count++] = "VK_KHR_surface";
#ifdef _WIN32
    surface_extensions[surface_extension_count++] = "VK_KHR_win32_surface";
#elif defined(__APPLE__)
    surface_extensions[surface_extension_count++] = "VK_EXT_metal_surface";
#elif defined(__ANDROID__)
    surface_extensions[surface_extension_count++] = "VK_KHR_android_surface";
#elif defined(__linux__)
    if(getenv("WAYLAND_DISPLAY") != NULL)
    {
        surface_extensions[surface_extension_count++] = "VK_KHR_wayland_surface";
    }
    else
    {
        surface_extensions[surface_extension_count++] = "VK_KHR_xcb_surface";
        surface_extensions[surface_extension_count++] = "VK_KHR_xlib_surface";
    }
#endif

    uint32_t max_extensions = surface_extension_count;
#ifdef __APPLE__
    max_extensions++;
#endif
    if(pigment->config.validation_enabled)
    {
        max_extensions++;
    }

    max_extensions += 3;

    if(vk_init != NULL)
    {
        max_extensions += vk_init->req_instance_extensions_count;
        max_extensions += vk_init->opt_instance_extensions_count;
    }

    names = P_NEW_ARRAY_FOR_INSTANCE(pigment, names, max_extensions);
    if(names == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < surface_extension_count; i++)
    {
        if(extension_available(available, available_count, surface_extensions[i]))
        {
            names[count++] = surface_extensions[i];
        }
    }

#ifdef __APPLE__
    if(extension_available(available, available_count, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) && !name_in_list((const char* const*) names, count, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        names[count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    }
#endif
    if(pigment->config.validation_enabled && !name_in_list((const char* const*) names, count, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        names[count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
    if(extension_available(available, available_count, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME)
       && !name_in_list((const char* const*) names, count, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
    {
        names[count++] = VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME;
    }
    if(extension_available(available, available_count, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)
       && extension_available(available, available_count, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME))
    {
        if(!name_in_list((const char* const*) names, count, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME))
        {
            names[count++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
        }
        if(!name_in_list((const char* const*) names, count, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME))
        {
            names[count++] = VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
        }
    }

    if(vk_init != NULL)
    {
        for(uint32_t i = 0; i < vk_init->req_instance_extensions_count; i++)
        {
            const char* name = vk_init->req_instance_extensions[i];
            if(!extension_available(available, available_count, name))
            {
                PLOG_ERROR(pigment, "Required instance extension not available: %s", name);
                goto ERROR;
            }
            if(!name_in_list((const char* const*) names, count, name))
            {
                names[count++] = name;
            }
        }

        for(uint32_t i = 0; i < vk_init->opt_instance_extensions_count; i++)
        {
            const char* name = vk_init->opt_instance_extensions[i];
            if(!extension_available(available, available_count, name))
            {
                PLOG_WARN(pigment, "Optional instance extension not available, skipping: %s", name);
                continue;
            }
            if(!name_in_list((const char* const*) names, count, name))
            {
                names[count++] = name;
            }
        }
    }

    instance->extensions->names = names;
    instance->extensions->size  = count;
    names                       = NULL;
    return PIGMENT_SUCCESS;

ERROR:
    P_FREE(pigment, names);
    P_FREE(pigment, instance->extensions);
    instance->extensions = NULL;
    return PIGMENT_ERROR_OUT_OF_MEMORY;
}

void destroy_instance(Pigment* pigment)
{
    PInstance* instance = pigment->instance;
    if(instance == NULL)
    {
        return;
    }
    if(pigment->config.validation_enabled)
    {
        destroy_debug_utils_messenger(instance->vulkan_instance, instance->debug_messenger, &pigment->vk_alloc);
    }
    vkDestroyInstance(instance->vulkan_instance, &pigment->vk_alloc);
    P_FREE(pigment, instance->layers->names);
    P_FREE(pigment, instance->layers);
    P_FREE(pigment, instance->extensions->names);
    P_FREE(pigment, instance->extensions);
    P_FREE(pigment, instance);
}

static PBool layer_available(const VkLayerProperties* available, uint32_t count, const char* name)
{
    for(uint32_t i = 0; i < count; i++)
    {
        if(strcmp(available[i].layerName, name) == 0)
        {
            return P_TRUE;
        }
    }
    return P_FALSE;
}

static PigmentLogSeverity vk_severity_to_pigment(VkDebugUtilsMessageSeverityFlagBitsEXT severity)
{
    if(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        return PIGMENT_LOG_ERROR_BIT;
    }
    if(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        return PIGMENT_LOG_WARN_BIT;
    }
    if(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        return PIGMENT_LOG_INFO_BIT;
    }
    return PIGMENT_LOG_TRACE_BIT;
}

static PigmentLogType vk_type_to_pigment(VkDebugUtilsMessageTypeFlagsEXT type)
{
    PigmentLogType out = 0;
    if(type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
    {
        out |= PIGMENT_LOG_TYPE_GENERAL_BIT;
    }
    if(type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
    {
        out |= PIGMENT_LOG_TYPE_VALIDATION_BIT;
    }
    if(type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
    {
        out |= PIGMENT_LOG_TYPE_PERFORMANCE_BIT;
    }
    return out;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity_flags,
    VkDebugUtilsMessageTypeFlagsEXT type_flags,
    const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
    void* user_data
)
{
    Pigment* pigment = (Pigment*) user_data;
    if(pigment == NULL || pigment->log == NULL)
    {
        return VK_FALSE;
    }

    PigmentLogSeverity sev = vk_severity_to_pigment(severity_flags);
    PigmentLogType type    = vk_type_to_pigment(type_flags);

    if(pigment_log_should_dispatch(pigment, sev, type))
    {
        pigment_log_dispatch(pigment, sev, type, p_callback_data->pMessageIdName, p_callback_data->messageIdNumber, "%s", p_callback_data->pMessage);
    }

    return VK_FALSE;
}

static void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT* create_info, Pigment* pigment)
{
    create_info->sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info->messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info->pfnUserCallback = debug_callback;
    create_info->flags           = 0;
    create_info->pNext           = NULL;
    create_info->pUserData       = pigment;
}

void setup_debug_messenger(Pigment* pigment)
{
    if(!pigment->config.validation_enabled)
    {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT create_info;
    populate_debug_messenger_create_info(&create_info, pigment);

    PInstance* instance = pigment->instance;
    VkResult result;
    if((result = create_debug_utils_messenger(instance->vulkan_instance, &create_info, &pigment->vk_alloc, &instance->debug_messenger)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to set up debug messenger! (result: %d)", result);
    }
}

static VkResult create_debug_utils_messenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* p_create_info, const VkAllocationCallbacks* p_allocator, VkDebugUtilsMessengerEXT* p_debug_messenger)
{
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if(func != NULL)
    {
        return func(instance, p_create_info, p_allocator, p_debug_messenger);
    }
    else
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void destroy_debug_utils_messenger(VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger, const VkAllocationCallbacks* p_allocator)
{
    if(debug_messenger == NULL)
    {
        return;
    }

    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if(func != NULL)
    {
        func(instance, debug_messenger, p_allocator);
    }
}
