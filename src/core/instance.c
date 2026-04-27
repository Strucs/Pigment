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
#include "structs.h"
#include "log_internal.h"

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
    VkApplicationInfo app_info       = {0};
    VkInstanceCreateInfo create_info = {0};
    PInstance* instance;

    instance = calloc(1, sizeof(*instance));
    if(instance == NULL)
    {
        goto ERROR;
    }

    if(info == NULL)
    {
        PLOG_WARN(pigment, "PAppInfo is NULL. Using default values.");
    }

    const char* layers[] = {
        "VK_LAYER_KHRONOS_validation",
    };

    instance->layers = calloc(1, sizeof(*(instance->layers)));
    if(instance->layers == NULL)
    {
        goto ERROR;
    }

    instance->layers->size  = sizeof(layers) / sizeof(layers[0]);
    instance->layers->names = malloc(instance->layers->size * sizeof(*(instance->layers->names)));
    if(instance->layers->names == NULL)
    {
        goto ERROR;
    }

    memcpy(instance->layers->names, layers, instance->layers->size * sizeof(*layers));

    VkResult volk_result = volkInitialize();
    if(volk_result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Vulkan loader not found (result: %d)", volk_result);
        goto ERROR;
    }

    if(pigment->config.validation_enabled && !check_layers(pigment, instance->layers))
    {
        PLOG_WARN(pigment, "Validation layers requested but VK_LAYER_KHRONOS_validation is not installed. Continuing without validation.");
        pigment->config.validation_enabled     = false;
        pigment->config.best_practices_enabled = false;
    }

    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = info != NULL ? info->app_name : "Unnamed";
    app_info.applicationVersion = info != NULL ? info->app_version : PIGMENT_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName        = "Pigment";
    app_info.engineVersion      = VK_MAKE_VERSION(0, 0, 3);
    app_info.apiVersion         = VK_API_VERSION_1_3;

    if(get_extensions(pigment, instance) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    create_info.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
#ifdef __APPLE__
    create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    create_info.enabledExtensionCount   = instance->extensions->size;
    create_info.ppEnabledExtensionNames = instance->extensions->names;

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {0};
    VkValidationFeatureEnableEXT enabled_features[]      = {VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT};
    VkValidationFeaturesEXT validation_features          = {
        .sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .enabledValidationFeatureCount = 1,
        .pEnabledValidationFeatures    = enabled_features,
    };

    if(pigment->config.validation_enabled)
    {
        create_info.enabledLayerCount   = instance->layers->size;
        create_info.ppEnabledLayerNames = instance->layers->names;

        populate_debug_messenger_create_info(&debug_create_info, pigment);
        if(pigment->config.best_practices_enabled)
        {
            debug_create_info.pNext = &validation_features;
        }
        create_info.pNext = &debug_create_info;
    }
    else
    {
        create_info.enabledLayerCount = 0;
        create_info.pNext             = NULL;
    }

    VkResult result;
    if((result = vkCreateInstance(&create_info, NULL, &(instance->vulkan_instance))) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create an instance. (result: %d)", result);
        goto ERROR;
    }

    volkLoadInstance(instance->vulkan_instance);

    return instance;

ERROR:
    if(instance != NULL)
    {
        if(instance->layers != NULL)
        {
            free(instance->layers->names);
        }
        free(instance->layers);
        free(instance);
    }
    return NULL;
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
        destroy_debug_utils_messenger(instance->vulkan_instance, instance->debug_messenger, NULL);
    }
    vkDestroyInstance(instance->vulkan_instance, NULL);
    free(instance->layers->names);
    free(instance->layers);
    free(instance->extensions->names);
    free(instance->extensions);
    free(instance);
}

int get_extensions(Pigment* pigment, PInstance* instance)
{
    Uint32 sdl_extensions_count       = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);

    if(sdl_extensions == NULL)
    {
        PLOG_ERROR(pigment, "SDL_Vulkan_GetInstanceExtensions: %s", SDL_GetError());
        goto ERROR;
    }

    instance->extensions = malloc(sizeof(*instance->extensions));
    if(instance->extensions == NULL)
    {
        goto ERROR;
    }

    bool want_debug_utils = pigment->config.validation_enabled;
    uint32_t extra_count  = 0;
#ifdef __APPLE__
    extra_count++;
#endif
    if(want_debug_utils)
    {
        extra_count++;
    }

    instance->extensions->size  = sdl_extensions_count + extra_count;
    instance->extensions->names = malloc(instance->extensions->size * sizeof(*instance->extensions->names));
    if(instance->extensions->names == NULL)
    {
        goto ERROR;
    }

    memcpy(instance->extensions->names, sdl_extensions, sdl_extensions_count * sizeof(*sdl_extensions));

    uint32_t idx = sdl_extensions_count;
#ifdef __APPLE__
    instance->extensions->names[idx++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#endif
    if(want_debug_utils)
    {
        instance->extensions->names[idx++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

    return PIGMENT_SUCCESS;

ERROR:
    if(instance->extensions != NULL)
    {
        free(instance->extensions->names);
    }
    free(instance->extensions);
    return PIGMENT_ERROR;
}

bool check_layers(Pigment* pigment, LayerList* requested_layers)
{
    uint32_t layers_count;
    VkLayerProperties* available_layers;
    bool layer_found;

    vkEnumerateInstanceLayerProperties(&layers_count, NULL);    // Store the number of layers in layers_count

    available_layers = malloc(layers_count * sizeof(*available_layers));
    if(available_layers == NULL)
    {
        return false;
    }

    vkEnumerateInstanceLayerProperties(&layers_count, available_layers);    // Store all the layers in available_layers

    // Search if all requested layers are available
    for(size_t i = 0; i < requested_layers->size; i++)
    {
        layer_found = false;

        for(size_t j = 0; j < layers_count; j++)
        {
            PLOG_TRACE(pigment, "Available layer: %s", available_layers[j].layerName);
            if(strcmp(requested_layers->names[i], available_layers[j].layerName) == 0)
            {
                layer_found = true;
                break;
            }
        }

        if(!layer_found)
        {
            free(available_layers);
            return false;
        }
    }

    free(available_layers);

    return true;
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
    if((result = create_debug_utils_messenger(instance->vulkan_instance, &create_info, NULL, &instance->debug_messenger)) != VK_SUCCESS)
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
