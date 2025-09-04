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

#include "instance.h"
#include "structs.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT* create_info);
VkResult create_debug_utils_messenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* p_create_info, const VkAllocationCallbacks* p_allocator, VkDebugUtilsMessengerEXT* p_debug_messenger);
void destroy_debug_utils_messenger(VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger, const VkAllocationCallbacks* p_allocator);
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity_flags __attribute__((unused)),
    VkDebugUtilsMessageTypeFlagsEXT type_flags __attribute__((unused)),
    const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
    void* user_data __attribute__((unused))
);

PInstance* create_instance(PAppInfo* info)
{
    VkApplicationInfo app_info       = {0};
    VkInstanceCreateInfo create_info = {0};
    PInstance* instance;

    instance = calloc(1, sizeof(*instance));
    if(instance == NULL)
    {
        goto ERROR;
    }

    if (info == NULL) {
        fprintf(stderr, "[Warning] Pigment: PAppInfo is NULL. Using default values.\n");
    }

    const char* layers[] = {
        "VK_LAYER_KHRONOS_validation",
        //"VK_LAYER_LUNARG_monitor"
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

    if(VLAYERS_ENABLED && !check_layers(instance->layers))
    {
        fprintf(stderr, "Validation layers unavailable.\n");
        goto ERROR;
    }

    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = info != NULL ? info->app_name : "Unnamed";
    app_info.applicationVersion = info != NULL ? info->app_version : PIGMENT_MAKE_VERSION(0,0,1);
    app_info.pEngineName        = "Pigment";
    app_info.engineVersion      = VK_MAKE_VERSION(0, 0, 3);
    app_info.apiVersion         = VK_API_VERSION_1_2;

    if(get_extensions(instance) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo        = &app_info;
    create_info.flags   = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    create_info.enabledExtensionCount   = instance->extensions->size;
    create_info.ppEnabledExtensionNames = instance->extensions->names;

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {0};
    if(VLAYERS_ENABLED)
    {
        create_info.enabledLayerCount   = instance->layers->size;
        create_info.ppEnabledLayerNames = instance->layers->names;

        populate_debug_messenger_create_info(&debug_create_info);
        create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debug_create_info;
    }
    else
    {
        create_info.enabledLayerCount = 0;
        create_info.pNext             = NULL;
    }

    if(vkCreateInstance(&create_info, NULL, &(instance->vulkan_instance)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create an instance\n");
        goto ERROR;
    }

    return instance;

ERROR:
    perror("create_instance");
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

void destroy_instance(PInstance* instance)
{
    if(instance == NULL)
    {
        return;
    }
    if(VLAYERS_ENABLED)
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

int get_extensions(PInstance* instance)
{
    const char** glfw_extensions;
    uint32_t glfw_extensions_count;

    glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);

    instance->extensions = malloc(sizeof(*instance->extensions));
    if(instance->extensions == NULL)
    {
        goto ERROR;
    }

    instance->extensions->size = glfw_extensions_count + 2;

    if(VLAYERS_ENABLED)
    {
        instance->extensions->size++;

        instance->extensions->names = malloc(instance->extensions->size * sizeof(*instance->extensions->names));
        if(instance->extensions->names == NULL)
        {
            goto ERROR;
        }

        memcpy(instance->extensions->names, glfw_extensions, glfw_extensions_count * sizeof(*glfw_extensions));
        instance->extensions->names[instance->extensions->size - 3] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        instance->extensions->names[instance->extensions->size - 2] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
        instance->extensions->names[instance->extensions->size - 1] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        return PIGMENT_SUCCESS;
    }
    instance->extensions->names = malloc(instance->extensions->size * sizeof(*instance->extensions->names));
    if(instance->extensions->names == NULL)
    {
        goto ERROR;
    }
    memcpy(instance->extensions->names, glfw_extensions, glfw_extensions_count * sizeof(*glfw_extensions));
    instance->extensions->names[instance->extensions->size - 2] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    instance->extensions->names[instance->extensions->size - 1] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

    return PIGMENT_SUCCESS;

ERROR:
    perror("get_extensions");
    if(instance->extensions != NULL)
    {
        free(instance->extensions->names);
    }
    free(instance->extensions);
    return PIGMENT_ERROR;
}

bool check_layers(LayerList* requested_layers)
{
    uint32_t layers_count;
    VkLayerProperties* available_layers;
    bool layer_found;

    vkEnumerateInstanceLayerProperties(&layers_count, NULL);    // Store the number of layers in layers_count

    available_layers = malloc(layers_count * sizeof(*available_layers));
    if(available_layers == NULL)
    {
        perror("check_layers");
        return false;
    }

    vkEnumerateInstanceLayerProperties(&layers_count, available_layers);    // Store all the layers in available_layers

    // Search if all requested layers are available
    for(size_t i = 0; i < requested_layers->size; i++)
    {
        layer_found = false;

        for(size_t j = 0; j < layers_count; j++)
        {
            printf("%s\n", available_layers[j].layerName);
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

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity_flags __attribute__((unused)),
    VkDebugUtilsMessageTypeFlagsEXT type_flags __attribute__((unused)),
    const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
    void* user_data __attribute__((unused))
)
{
    fprintf(stderr, "Validation Layer: %s\n", p_callback_data->pMessage);

    return VK_FALSE;
}

void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT* create_info)
{
    create_info->sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info->messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info->pfnUserCallback = debug_callback;
    create_info->flags           = 0;
    create_info->pNext           = NULL;
    create_info->pUserData       = NULL;
}

void setup_debug_messenger(PInstance* instance)
{
    if(!VLAYERS_ENABLED)
    {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT create_info;
    populate_debug_messenger_create_info(&create_info);

    if(create_debug_utils_messenger(instance->vulkan_instance, &create_info, NULL, &instance->debug_messenger) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to set up debug messenger!\n");
    }
}

VkResult create_debug_utils_messenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* p_create_info, const VkAllocationCallbacks* p_allocator, VkDebugUtilsMessengerEXT* p_debug_messenger)
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
