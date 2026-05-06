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

#include "device.h"
#include "internal.h"
#include "log_internal.h"
#include "pigment_vk.h"

#include <vulkan/vulkan_core.h>

static PBool features10_supports(const VkPhysicalDeviceFeatures* req, const VkPhysicalDeviceFeatures* available);
static PBool features_chain_supports(const void* req, const void* available, size_t struct_size);
static void merge_features10_or(VkPhysicalDeviceFeatures* dst, const VkPhysicalDeviceFeatures* src, const VkPhysicalDeviceFeatures* available);
static void merge_features_chain_or(void* dst, const void* src, const void* available, size_t struct_size);

static PBool check_device_extensions_supported(VkPhysicalDevice device, const ExtensionList* required);
static PBool is_suitable(Pigment* pigment, VkPhysicalDevice device, const ExtensionList* req_extensions, const PVkInitInfo* vk_init);
static PResult pick_physical_device(Pigment* pigment, PDevice* device, const ExtensionList* req_extensions, const PVkInitInfo* vk_init);
static PResult create_logical_device(Pigment* pigment, PDevice* device);

static inline VkPhysicalDeviceFeatures pigment_req_features(void)
{
    return (VkPhysicalDeviceFeatures) {
        .samplerAnisotropy                      = VK_TRUE,
        .shaderSampledImageArrayDynamicIndexing = VK_TRUE,
    };
}

static inline VkPhysicalDeviceVulkan12Features pigment_req_features_12(void)
{
    return (VkPhysicalDeviceVulkan12Features) {
        .sType                                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing                        = VK_TRUE,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingVariableDescriptorCount  = VK_TRUE,
        .descriptorBindingPartiallyBound           = VK_TRUE,
        .runtimeDescriptorArray                    = VK_TRUE,
        .bufferDeviceAddress                       = VK_TRUE,
    };
}

static inline VkPhysicalDeviceVulkan13Features pigment_req_features_13(void)
{
    return (VkPhysicalDeviceVulkan13Features) {
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE,
    };
}

static PBool features10_supports(const VkPhysicalDeviceFeatures* req, const VkPhysicalDeviceFeatures* available)
{
    if(req == NULL)
    {
        return P_TRUE;
    }
    const VkBool32* req_fields       = (const VkBool32*) req;
    const VkBool32* available_fields = (const VkBool32*) available;
    const size_t count               = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
    for(size_t i = 0; i < count; i++)
    {
        if(req_fields[i] && !available_fields[i])
        {
            return P_FALSE;
        }
    }
    return P_TRUE;
}

static PBool features_chain_supports(const void* req, const void* available, size_t struct_size)
{
    if(req == NULL)
    {
        return P_TRUE;
    }
    const size_t header              = sizeof(VkBaseOutStructure);
    const VkBool32* req_fields       = (const VkBool32*) ((const char*) req + header);
    const VkBool32* available_fields = (const VkBool32*) ((const char*) available + header);
    const size_t count               = (struct_size - header) / sizeof(VkBool32);
    for(size_t i = 0; i < count; i++)
    {
        if(req_fields[i] && !available_fields[i])
        {
            return P_FALSE;
        }
    }
    return P_TRUE;
}

static void merge_features10_or(VkPhysicalDeviceFeatures* dst, const VkPhysicalDeviceFeatures* src, const VkPhysicalDeviceFeatures* available)
{
    if(src == NULL)
    {
        return;
    }
    VkBool32* dst_fields             = (VkBool32*) dst;
    const VkBool32* src_fields       = (const VkBool32*) src;
    const VkBool32* available_fields = available != NULL ? (const VkBool32*) available : NULL;
    const size_t count               = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
    for(size_t i = 0; i < count; i++)
    {
        if(src_fields[i] && (available_fields == NULL || available_fields[i]))
        {
            dst_fields[i] = VK_TRUE;
        }
    }
}

static void merge_features_chain_or(void* dst, const void* src, const void* available, size_t struct_size)
{
    if(src == NULL)
    {
        return;
    }
    const size_t header              = sizeof(VkBaseOutStructure);
    VkBool32* dst_fields             = (VkBool32*) ((char*) dst + header);
    const VkBool32* src_fields       = (const VkBool32*) ((const char*) src + header);
    const VkBool32* available_fields = available != NULL ? (const VkBool32*) ((const char*) available + header) : NULL;
    const size_t count               = (struct_size - header) / sizeof(VkBool32);
    for(size_t i = 0; i < count; i++)
    {
        if(src_fields[i] && (available_fields == NULL || available_fields[i]))
        {
            dst_fields[i] = VK_TRUE;
        }
    }
}

static PBool check_device_extensions_supported(VkPhysicalDevice device, const ExtensionList* required)
{
    uint32_t count                   = 0;
    VkExtensionProperties* available = NULL;

    vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    if(count == 0 && required->size > 0)
    {
        return P_FALSE;
    }

    if(count > 0)
    {
        available = malloc(count * sizeof(*available));
        if(available == NULL)
        {
            return P_FALSE;
        }
        vkEnumerateDeviceExtensionProperties(device, NULL, &count, available);
    }

    PBool extensions_found = P_TRUE;
    for(size_t i = 0; i < required->size; i++)
    {
        if(!extension_available(available, count, required->names[i]))
        {
            extensions_found = P_FALSE;
            break;
        }
    }

    free(available);
    return extensions_found;
}

static PBool is_suitable(Pigment* pigment, VkPhysicalDevice device, const ExtensionList* req_extensions, const PVkInitInfo* vk_init)
{
    PBool result = P_FALSE;

    uint32_t graphics_family = 0;
    if(!find_graphics_family(device, &graphics_family))
    {
        PLOG_TRACE(pigment, "Device rejected: no graphics queue family");
        goto FREE;
    }

    if(!check_device_extensions_supported(device, req_extensions))
    {
        PLOG_TRACE(pigment, "Device rejected: missing required extensions");
        goto FREE;
    }

    VkPhysicalDeviceVulkan11Features available_11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features available_12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &available_11};
    VkPhysicalDeviceVulkan13Features available_13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &available_12};
    VkPhysicalDeviceFeatures2 available_2         = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &available_13};
    vkGetPhysicalDeviceFeatures2(device, &available_2);

    const VkPhysicalDeviceFeatures req         = pigment_req_features();
    const VkPhysicalDeviceVulkan12Features r12 = pigment_req_features_12();
    const VkPhysicalDeviceVulkan13Features r13 = pigment_req_features_13();
    if(!features10_supports(&req, &available_2.features) || !features_chain_supports(&r12, &available_12, sizeof(available_12)) || !features_chain_supports(&r13, &available_13, sizeof(available_13)))
    {
        PLOG_TRACE(pigment, "Device rejected: missing core Pigment features");
        goto FREE;
    }

    if(vk_init != NULL)
    {
        if(!features10_supports(vk_init->req_features, &available_2.features))
        {
            PLOG_TRACE(pigment, "Device rejected: missing user req features (1.0)");
            goto FREE;
        }
        if(!features_chain_supports(vk_init->req_features_11, &available_11, sizeof(available_11)))
        {
            PLOG_TRACE(pigment, "Device rejected: missing user req features (1.1)");
            goto FREE;
        }
        if(!features_chain_supports(vk_init->req_features_12, &available_12, sizeof(available_12)))
        {
            PLOG_TRACE(pigment, "Device rejected: missing user req features (1.2)");
            goto FREE;
        }
        if(!features_chain_supports(vk_init->req_features_13, &available_13, sizeof(available_13)))
        {
            PLOG_TRACE(pigment, "Device rejected: missing user req features (1.3)");
            goto FREE;
        }
    }

    result = P_TRUE;

FREE:
    return result;
}

static PResult pick_physical_device(Pigment* pigment, PDevice* device, const ExtensionList* req_extensions, const PVkInitInfo* vk_init)
{
    VkPhysicalDevice* devices = NULL;
    uint32_t devices_count    = 0;

    vkEnumeratePhysicalDevices(pigment->instance->vulkan_instance, &devices_count, NULL);

    if(devices_count == 0)
    {
        PLOG_ERROR(pigment, "Failed to find GPUs compatible with Vulkan");
        return PIGMENT_ERROR_VULKAN;
    }

    devices = malloc(devices_count * sizeof(*devices));
    if(devices == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    vkEnumeratePhysicalDevices(pigment->instance->vulkan_instance, &devices_count, devices);

    for(size_t i = 0; i < devices_count; i++)
    {
        if(is_suitable(pigment, devices[i], req_extensions, vk_init))
        {
            device->physical_device = devices[i];
            break;
        }
    }

    free(devices);

    if(device->physical_device == VK_NULL_HANDLE)
    {
        PLOG_ERROR(pigment, "Failed to find a suitable GPU");
        return PIGMENT_ERROR_VULKAN;
    }

    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device->physical_device, &device_properties);

    PLOG_INFO(pigment, "GPU picked: %s", device_properties.deviceName);

    return PIGMENT_SUCCESS;
}

PBool find_graphics_family(VkPhysicalDevice device, uint32_t* out_family)
{
    uint32_t queue_families_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, NULL);
    if(queue_families_count == 0)
    {
        return P_FALSE;
    }

    VkQueueFamilyProperties* queue_families = malloc(queue_families_count * sizeof(*queue_families));
    if(queue_families == NULL)
    {
        return P_FALSE;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, queue_families);

    PBool found = P_FALSE;
    for(uint32_t i = 0; i < queue_families_count; i++)
    {
        if(queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            *out_family = i;
            found       = P_TRUE;
            break;
        }
    }

    free(queue_families);
    return found;
}

PBool device_supports_surface(VkPhysicalDevice device, uint32_t family_index, VkSurfaceKHR surface)
{
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, family_index, surface, &supported);
    return supported ? P_TRUE : P_FALSE;
}

PDeviceQueue* device_find_queue(PDevice* device, PQueueFlags required, PQueueFlags forbidden)
{
    if(device == NULL)
    {
        return NULL;
    }

    for(uint32_t i = 0; i < device->queue_count; i++)
    {
        PQueueFlags flags = device->queues[i].flags;
        if((flags & required) == required && (flags & forbidden) == 0)
        {
            return &device->queues[i];
        }
    }

    return NULL;
}

static PResult create_logical_device(Pigment* pigment, PDevice* device)
{
    PResult result                          = PIGMENT_ERROR_OUT_OF_MEMORY;
    PInstance* instance                     = pigment->instance;
    const PVkInitInfo* vk_init              = (const PVkInitInfo*) pigment->config.extra;
    VkDeviceQueueCreateInfo* queue_infos    = NULL;
    uint32_t* selected_families             = NULL;
    VkQueueFamilyProperties* queue_families = NULL;
    uint32_t queue_families_count           = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device, &queue_families_count, NULL);
    queue_families = malloc(queue_families_count * sizeof(*queue_families));
    if(queue_families == NULL)
    {
        goto ERROR;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device, &queue_families_count, queue_families);

    selected_families = malloc(queue_families_count * sizeof(*selected_families));
    queue_infos       = calloc(queue_families_count, sizeof(*queue_infos));
    if(selected_families == NULL || queue_infos == NULL)
    {
        goto ERROR;
    }

    static const VkQueueFlags useful_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    uint32_t selected_count                = 0;

    for(uint32_t i = 0; i < queue_families_count; i++)
    {
        if((queue_families[i].queueFlags & useful_flags) != 0)
        {
            selected_families[selected_count++] = i;
        }
    }

    float queue_priority = 1.0f;
    for(uint32_t i = 0; i < selected_count; i++)
    {
        queue_infos[i].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_infos[i].queueFamilyIndex = selected_families[i];
        queue_infos[i].queueCount       = 1;
        queue_infos[i].pQueuePriorities = &queue_priority;
    }

    VkPhysicalDeviceVulkan11Features available_11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features available_12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &available_11};
    VkPhysicalDeviceVulkan13Features available_13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &available_12};
    VkPhysicalDeviceFeatures2 available_2         = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &available_13};
    vkGetPhysicalDeviceFeatures2(device->physical_device, &available_2);

    device->features[P_FEATURE_DEPTH_BOUNDS_TEST]       = (PBool) available_2.features.depthBounds;
    device->features[P_FEATURE_WIREFRAME_RASTERIZATION] = (PBool) available_2.features.fillModeNonSolid;

    VkPhysicalDeviceVulkan11Features vk11_features = {
        .sType     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .multiview = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features vk12_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vk11_features,
    };
    VkPhysicalDeviceVulkan13Features vk13_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vk12_features,
    };
    VkPhysicalDeviceFeatures2 features = {
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .features = {
                     .fillModeNonSolid = device->features[P_FEATURE_WIREFRAME_RASTERIZATION] ? VK_TRUE : VK_FALSE,
                     .depthBounds      = device->features[P_FEATURE_DEPTH_BOUNDS_TEST] ? VK_TRUE : VK_FALSE,
                     },
        .pNext = &vk13_features,
    };

    const VkPhysicalDeviceFeatures req         = pigment_req_features();
    const VkPhysicalDeviceVulkan12Features r12 = pigment_req_features_12();
    const VkPhysicalDeviceVulkan13Features r13 = pigment_req_features_13();
    merge_features10_or(&features.features, &req, NULL);
    merge_features_chain_or(&vk12_features, &r12, NULL, sizeof(vk12_features));
    merge_features_chain_or(&vk13_features, &r13, NULL, sizeof(vk13_features));

    if(vk_init != NULL)
    {
        merge_features10_or(&features.features, vk_init->req_features, NULL);
        merge_features10_or(&features.features, vk_init->opt_features, &available_2.features);

        merge_features_chain_or(&vk11_features, vk_init->req_features_11, NULL, sizeof(vk11_features));
        merge_features_chain_or(&vk11_features, vk_init->opt_features_11, &available_11, sizeof(vk11_features));

        merge_features_chain_or(&vk12_features, vk_init->req_features_12, NULL, sizeof(vk12_features));
        merge_features_chain_or(&vk12_features, vk_init->opt_features_12, &available_12, sizeof(vk12_features));

        merge_features_chain_or(&vk13_features, vk_init->req_features_13, NULL, sizeof(vk13_features));
        merge_features_chain_or(&vk13_features, vk_init->opt_features_13, &available_13, sizeof(vk13_features));
    }

    VkDeviceCreateInfo create_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos       = queue_infos,
        .queueCreateInfoCount    = selected_count,
        .pEnabledFeatures        = NULL,
        .pNext                   = &features,
        .enabledExtensionCount   = device->extensions->size,
        .ppEnabledExtensionNames = device->extensions->names
    };

    if(vk_init != NULL && vk_init->device_pnext_chain != NULL)
    {
        pigment_vk_append_pnext(&create_info, vk_init->device_pnext_chain);
    }

    if(pigment->config.validation_enabled)
    {
        create_info.enabledLayerCount   = instance->layers->size;
        create_info.ppEnabledLayerNames = instance->layers->names;
    }
    else
    {
        create_info.enabledLayerCount = 0;
    }

    result = PIGMENT_ERROR_VULKAN;
    if(vkCreateDevice(device->physical_device, &create_info, NULL, &(device->logical_device)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create logical device");
        goto ERROR;
    }
    volkLoadDevice(device->logical_device);

    device->queues = malloc(selected_count * sizeof(*device->queues));
    if(device->queues == NULL)
    {
        result = PIGMENT_ERROR_OUT_OF_MEMORY;
        goto ERROR;
    }
    device->queue_count = selected_count;
    for(uint32_t i = 0; i < selected_count; i++)
    {
        device->queues[i].family_index = selected_families[i];
        device->queues[i].flags        = (PQueueFlags) (queue_families[selected_families[i]].queueFlags & useful_flags);
        vkGetDeviceQueue(device->logical_device, selected_families[i], 0, &device->queues[i].queue);
    }

    free(queue_infos);
    free(selected_families);
    free(queue_families);
    return PIGMENT_SUCCESS;

ERROR:
    free(queue_infos);
    free(selected_families);
    free(queue_families);
    return result;
}

PDevice* create_device(Pigment* pigment)
{
    PDevice* device                             = NULL;
    ExtensionList req_extensions                = {0};
    VkExtensionProperties* available_extensions = NULL;
    uint32_t available_extension_count          = 0;
    const PVkInitInfo* vk_init                  = (const PVkInitInfo*) pigment->config.extra;

    static const char* default_req_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#ifdef __APPLE__
        "VK_KHR_portability_subset",
#endif
    };
    const uint32_t default_req_count = sizeof(default_req_extensions) / sizeof(default_req_extensions[0]);

    device = calloc(1, sizeof(*device));
    if(device == NULL)
    {
        goto ERROR;
    }

    uint32_t max_req     = default_req_count + (vk_init != NULL ? vk_init->req_device_extensions_count : 0);
    req_extensions.names = malloc(max_req * sizeof(*req_extensions.names));
    if(req_extensions.names == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < default_req_count; i++)
    {
        req_extensions.names[req_extensions.size++] = default_req_extensions[i];
    }
    if(vk_init != NULL)
    {
        for(uint32_t i = 0; i < vk_init->req_device_extensions_count; i++)
        {
            const char* name = vk_init->req_device_extensions[i];
            if(!name_in_list((const char* const*) req_extensions.names, req_extensions.size, name))
            {
                req_extensions.names[req_extensions.size++] = name;
            }
        }
    }

    if(pick_physical_device(pigment, device, &req_extensions, vk_init) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    vkEnumerateDeviceExtensionProperties(device->physical_device, NULL, &available_extension_count, NULL);
    if(available_extension_count > 0)
    {
        available_extensions = malloc(available_extension_count * sizeof(*available_extensions));
        if(available_extensions == NULL)
        {
            goto ERROR;
        }
        vkEnumerateDeviceExtensionProperties(device->physical_device, NULL, &available_extension_count, available_extensions);
    }

    uint32_t max_final = req_extensions.size + (vk_init != NULL ? vk_init->opt_device_extensions_count : 0);
    device->extensions = calloc(1, sizeof(*device->extensions));
    if(device->extensions == NULL)
    {
        goto ERROR;
    }
    device->extensions->names = malloc(max_final * sizeof(*device->extensions->names));
    if(device->extensions->names == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < req_extensions.size; i++)
    {
        device->extensions->names[device->extensions->size++] = req_extensions.names[i];
    }

    if(vk_init != NULL)
    {
        for(uint32_t i = 0; i < vk_init->opt_device_extensions_count; i++)
        {
            const char* name = vk_init->opt_device_extensions[i];
            if(!extension_available(available_extensions, available_extension_count, name))
            {
                PLOG_WARN(pigment, "Optional device extension not available, skipping: %s", name);
                continue;
            }
            if(!name_in_list((const char* const*) device->extensions->names, device->extensions->size, name))
            {
                device->extensions->names[device->extensions->size++] = name;
            }
        }
    }

    if(create_logical_device(pigment, device) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    free(req_extensions.names);
    free(available_extensions);
    return device;

ERROR:
    free(req_extensions.names);
    free(available_extensions);
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

void destroy_device(Pigment* pigment)
{
    PDevice* device = pigment->device;
    if(device == NULL)
    {
        return;
    }
    vkDestroyDevice(device->logical_device, NULL);
    if(device->extensions != NULL)
    {
        free(device->extensions->names);
    }
    free(device->extensions);
    free(device->queues);
    free(device);
}

void device_wait_idle(Pigment* pigment)
{
    if(pigment == NULL || pigment->device == NULL)
    {
        return;
    }
    vkDeviceWaitIdle(pigment->device->logical_device);
}
