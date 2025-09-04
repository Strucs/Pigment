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

#include "device.h"
#include "structs.h"

#include <vulkan/vulkan_core.h>
#define QUEUE_FAMILY_NUM 2

extern SwapChainSupportDetails* get_support_details(VkPhysicalDevice device, VkSurfaceKHR surface);
extern void destroy_support_details(SwapChainSupportDetails* details);

QueueFamilySet* create_queue_family_set(QueueFamilyIndices* indices);
void destroy_queue_family_set(QueueFamilySet* set);
void append_set(uint32_t* set, uint32_t* idx, uint32_t element);
QueueFamilyIndices* init_indice(void);
QueueFamilyIndices* find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);
bool queue_families_indices_completed(QueueFamilyIndices indices);
bool queue_families_indices_completed(QueueFamilyIndices indices);
bool check_device_extensions(VkPhysicalDevice device, ExtensionList requiered_extensions);
bool is_suitable(VkPhysicalDevice device, VkSurfaceKHR surface, ExtensionList requiered_extensions);
int pick_physical_device(PDevice* device, PInstance* instance, PSurface* surface);
int create_logical_device(PDevice* device, PInstance* instance, PSurface* surface);

void append_set(uint32_t* set, uint32_t* idx, uint32_t element)
{
    bool present = false;
    for(size_t i = 0; i < *idx; i++)
    {
        if(element == set[i])
        {
            present = true;
            break;
        }
    }

    if(!present)
    {
        set[*idx] = element;
        (*idx)++;
    }
}

QueueFamilySet* create_queue_family_set(QueueFamilyIndices* indices)
{
    QueueFamilySet* set = calloc(1, sizeof(*set));
    if(set == NULL)
    {
        goto ERROR;
    }
    uint32_t queues[QUEUE_FAMILY_NUM] = {0};

    if(indices->graphics_family.has_value)
    {
        append_set(queues, &set->size, indices->graphics_family.value);
    }

    if(indices->present_family.has_value)
    {
        append_set(queues, &set->size, indices->present_family.value);
    }

    set->set = malloc(set->size * sizeof(*set->set));
    if(set->set == NULL)
    {
        goto ERROR;
    }

    memcpy(set->set, queues, set->size * sizeof(*set->set));

    return set;

ERROR:
    if(set != NULL)
    {
        free(set->set);
        free(set);
    }
    return NULL;
}

void destroy_queue_family_set(QueueFamilySet* set)
{
    if(set != NULL)
    {
        free(set->set);
        free(set);
    }
}

QueueFamilyIndices* init_indices(void)
{
    QueueFamilyIndices* indices = calloc(1, sizeof(*indices));
    if(indices == NULL)
    {
        perror("init_indices");
        return NULL;
    }
    return indices;
}


bool queue_families_indices_completed(QueueFamilyIndices indices)
{
    return indices.graphics_family.has_value && indices.present_family.has_value;
}

bool check_device_extensions(VkPhysicalDevice device, ExtensionList requiered_extensions)
{
    uint32_t extensions_count;
    VkExtensionProperties* available_extensions;
    bool extensions_found;

    vkEnumerateDeviceExtensionProperties(device, NULL, &extensions_count, NULL);    // Store the number of layers in layers_count

    available_extensions = malloc(extensions_count * sizeof(*available_extensions));
    if(available_extensions == NULL)
    {
        goto ERROR;
    }

    vkEnumerateDeviceExtensionProperties(device, NULL, &extensions_count, available_extensions);    // Store all the layers in available_layers

    // Search if all requested layers are available
    for(size_t i = 0; i < requiered_extensions.size; i++)
    {
        extensions_found = false;

        for(size_t j = 0; j < extensions_count; j++)
        {
            if(strcmp(requiered_extensions.names[i], available_extensions[j].extensionName) == 0)
            {
                extensions_found = true;
                break;
            }
        }

        if(!extensions_found)
        {
            goto ERROR;
        }
    }

    free(available_extensions);

    return true;
    
ERROR:
    perror("check_device_extensions");
    free(available_extensions);
    return false;
}

bool is_suitable(VkPhysicalDevice device, VkSurfaceKHR surface, ExtensionList requiered_extensions)
{
    QueueFamilyIndices* indices;
    SwapChainSupportDetails* details;

    indices = find_queue_families(device, surface);
    if(indices == NULL)
    {
        goto ERROR;
    }

    bool is_completed = queue_families_indices_completed(*indices);

    bool extensions_supported = check_device_extensions(device, requiered_extensions);

    bool suitable_swap_chain = false;
    if(extensions_supported)
    {
        details = get_support_details(device, surface);
        if(details == NULL)
        {
            goto ERROR;
        }
        suitable_swap_chain = (details->formats_count != 0) && (details->present_modes_count != 0);
        destroy_support_details(details);
    }

    free(indices);
    indices = NULL;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT,
    };

    VkPhysicalDeviceFeatures2 available_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &descriptor_indexing_features
    };

    vkGetPhysicalDeviceFeatures2(device, &available_features);

    bool has_descriptor_indexing_features = available_features.features.shaderSampledImageArrayDynamicIndexing &&
                                            descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing && 
                                            descriptor_indexing_features.runtimeDescriptorArray &&
                                            descriptor_indexing_features.descriptorBindingVariableDescriptorCount;


    return is_completed && extensions_supported && suitable_swap_chain && available_features.features.samplerAnisotropy && has_descriptor_indexing_features;

ERROR:
    perror("is_suitable");
    free(indices);
    return false;
}

int pick_physical_device(PDevice* device, PInstance* instance, PSurface* surface)
{
    VkPhysicalDevice* devices;
    uint32_t devices_count;

    vkEnumeratePhysicalDevices(instance->vulkan_instance, &devices_count, NULL);

    if(devices_count == 0)
    {
        fprintf(stderr, "Failed to find GPUs compatible with Vulkan\n");
        goto ERROR;
    }

    devices = malloc(devices_count * sizeof(*devices));
    if(devices == NULL)
    {
        goto ERROR;
    }

    vkEnumeratePhysicalDevices(instance->vulkan_instance, &devices_count, devices);

    for(size_t i = 0; i < devices_count; i++)
    {
        if(is_suitable(devices[i], surface->surface, *device->extensions))
        {
            device->physical_device = devices[i];
            break;
        }
    }

    free(devices);

    if(device->physical_device == VK_NULL_HANDLE)
    {
        fprintf(stderr, "Failed to find a suitable GPU\n");
        goto ERROR;
    }

    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device->physical_device, &device_properties);

    printf("\033[1;33mGPU picked : %s\033[0m\n", device_properties.deviceName);

    return PIGMENT_SUCCESS;

ERROR:
    return PIGMENT_ERROR;
}

QueueFamilyIndices* find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    QueueFamilyIndices* indices;
    VkQueueFamilyProperties* queue_families;
    uint32_t queue_families_count;
    VkBool32 present_support;

    indices = init_indices();
    if (indices == NULL)
    {
        goto ERROR;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, NULL);

    queue_families = malloc(queue_families_count * sizeof(*queue_families));
    if(queue_families == NULL)
    {
        goto ERROR;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, queue_families);

    for(uint32_t i = 0; i < queue_families_count; i++)
    {
        if(queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices->graphics_family.has_value = true;
            indices->graphics_family.value     = i;
        }

        present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);

        if(present_support)
        {
            indices->present_family.has_value = true;
            indices->present_family.value     = i;
        }

        if(queue_families_indices_completed(*indices))
        {
            break;
        }
    }

    free(queue_families);

    return indices;

ERROR:
    perror("find_queue_families");
    free(indices);
    return NULL;
}

int create_logical_device(PDevice* device, PInstance* instance, PSurface* surface)
{
    QueueFamilyIndices* indices = NULL;
    VkDeviceQueueCreateInfo* queue_create_infos = NULL;
    QueueFamilySet* set = NULL;

    indices = find_queue_families(device->physical_device, surface->surface);
    if(indices == NULL)
    {
        goto ERROR;
    }

    set = create_queue_family_set(indices);
    if(set == NULL)
    {
        goto ERROR;
    }

    queue_create_infos = calloc(set->size, sizeof(*queue_create_infos));
    if(queue_create_infos == NULL)
    {
        goto ERROR;
    }

    float queue_priority = 1.0f;
    for(size_t i = 0; i < set->size; i++)
    {
        queue_create_infos[i].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[i].queueFamilyIndex = indices->graphics_family.value;
        queue_create_infos[i].queueCount       = 1;
        queue_create_infos[i].pQueuePriorities = &queue_priority;
    }

    VkPhysicalDeviceFeatures enabled_features = {
        .samplerAnisotropy                      = VK_TRUE,
        .shaderSampledImageArrayDynamicIndexing = VK_TRUE
    };

    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = {
        .sType                                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .runtimeDescriptorArray                    = VK_TRUE,
        .descriptorBindingVariableDescriptorCount  = VK_TRUE
    };

    VkPhysicalDeviceFeatures2 features = {
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .features = enabled_features,
        .pNext    = &descriptor_indexing_features
    };

    VkDeviceCreateInfo create_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos       = queue_create_infos,
        .queueCreateInfoCount    = 1,
        .pEnabledFeatures        = NULL,
        .pNext                   = &features,
        .enabledExtensionCount   = device->extensions->size,
        .ppEnabledExtensionNames = device->extensions->names
    };

    if(VLAYERS_ENABLED)
    {
        create_info.enabledLayerCount   = instance->layers->size;
        create_info.ppEnabledLayerNames = instance->layers->names;
    }
    else
    {
        create_info.enabledLayerCount = 0;
    }

    if(vkCreateDevice(device->physical_device, &create_info, NULL, &(device->logical_device)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create logical device\n");
        goto ERROR;
    }

    vkGetDeviceQueue(device->logical_device, indices->graphics_family.value, 0, &device->graphics_queue);
    vkGetDeviceQueue(device->logical_device, indices->present_family.value, 0, &device->present_queue);

    free(queue_create_infos);
    destroy_queue_family_set(set);
    free(indices);

    return PIGMENT_SUCCESS;

ERROR:
    free(queue_create_infos);
    destroy_queue_family_set(set);
    free(indices);
    return PIGMENT_ERROR;
}

PDevice* create_device(PInstance* instance, PSurface* surface)
{
    PDevice* device;

    device = calloc(1, sizeof(*device));
    if(device == NULL)
    {
        goto ERROR;
    }

    const char* extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MAINTENANCE1_EXTENSION_NAME,
        VK_KHR_MAINTENANCE3_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        #ifdef __APPLE__
        "VK_KHR_portability_subset"
        #endif
    };

    device->extensions = calloc(1, sizeof(*device->extensions));
    if(device->extensions == NULL)
    {
        goto ERROR;
    }

    device->extensions->size  = sizeof(extensions) / sizeof(extensions[0]);
    device->extensions->names = malloc(device->extensions->size * sizeof(*(device->extensions->names)));
    if(device->extensions->names == NULL)
    {
        goto ERROR;
    }

    memcpy(device->extensions->names, extensions, device->extensions->size * sizeof(*extensions));

    pick_physical_device(device, instance, surface);
    create_logical_device(device, instance, surface);
    return device;

ERROR:
    perror("create_device");
    if(device != NULL)
    {
        if(device->extensions != NULL)
        {
            free(device->extensions->names);
        }
        free(device->extensions);
        free(device);
    }
    return NULL;
}

void destroy_device(PDevice* device)
{
    if(device == NULL)
    {
        return;
    }
    vkDestroyDevice(device->logical_device, NULL);
    free(device->extensions);
    free(device);
}

void device_wait_idle(PDevice* device)
{
    vkDeviceWaitIdle(device->logical_device);
}
