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
#define QUEUE_FAMILY_NUM 2

static QueueFamilySet* create_queue_family_set(QueueFamilyIndices* indices);
static void destroy_queue_family_set(QueueFamilySet* set);
static void append_set(uint32_t* set, uint32_t* idx, uint32_t element);
static QueueFamilyIndices* init_indices(void);
static bool queue_families_indices_completed(QueueFamilyIndices indices);

static bool features10_supports(const VkPhysicalDeviceFeatures* req, const VkPhysicalDeviceFeatures* available);
static bool features_chain_supports(const void* req, const void* available, size_t struct_size);
static void merge_features10_or(VkPhysicalDeviceFeatures* dst, const VkPhysicalDeviceFeatures* src, const VkPhysicalDeviceFeatures* available);
static void merge_features_chain_or(void* dst, const void* src, const void* available, size_t struct_size);

static bool check_device_extensions_supported(VkPhysicalDevice device, const ExtensionList* required);
static bool is_suitable(Pigment* pigment, VkPhysicalDevice device, VkSurfaceKHR surface, const ExtensionList* req_extensions, const PVkInitInfo* vk_init);
static int pick_physical_device(Pigment* pigment, PDevice* device, PSurface* surface, const ExtensionList* req_extensions, const PVkInitInfo* vk_init);
static int create_logical_device(Pigment* pigment, PDevice* device, PSurface* surface);

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

static void append_set(uint32_t* set, uint32_t* idx, uint32_t element)
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

static QueueFamilySet* create_queue_family_set(QueueFamilyIndices* indices)
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

static void destroy_queue_family_set(QueueFamilySet* set)
{
    if(set != NULL)
    {
        free(set->set);
        free(set);
    }
}

static QueueFamilyIndices* init_indices(void)
{
    QueueFamilyIndices* indices = calloc(1, sizeof(*indices));
    if(indices == NULL)
    {
        return NULL;
    }
    return indices;
}

static bool queue_families_indices_completed(QueueFamilyIndices indices)
{
    return indices.graphics_family.has_value && indices.present_family.has_value;
}

static bool features10_supports(const VkPhysicalDeviceFeatures* req, const VkPhysicalDeviceFeatures* available)
{
    if(req == NULL)
    {
        return true;
    }
    const VkBool32* req_fields       = (const VkBool32*) req;
    const VkBool32* available_fields = (const VkBool32*) available;
    const size_t count               = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
    for(size_t i = 0; i < count; i++)
    {
        if(req_fields[i] && !available_fields[i])
        {
            return false;
        }
    }
    return true;
}

static bool features_chain_supports(const void* req, const void* available, size_t struct_size)
{
    if(req == NULL)
    {
        return true;
    }
    const size_t header              = sizeof(VkBaseOutStructure);
    const VkBool32* req_fields       = (const VkBool32*) ((const char*) req + header);
    const VkBool32* available_fields = (const VkBool32*) ((const char*) available + header);
    const size_t count               = (struct_size - header) / sizeof(VkBool32);
    for(size_t i = 0; i < count; i++)
    {
        if(req_fields[i] && !available_fields[i])
        {
            return false;
        }
    }
    return true;
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

static bool check_device_extensions_supported(VkPhysicalDevice device, const ExtensionList* required)
{
    uint32_t count                   = 0;
    VkExtensionProperties* available = NULL;

    vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    if(count == 0 && required->size > 0)
    {
        return false;
    }

    if(count > 0)
    {
        available = malloc(count * sizeof(*available));
        if(available == NULL)
        {
            return false;
        }
        vkEnumerateDeviceExtensionProperties(device, NULL, &count, available);
    }

    bool extensions_found = true;
    for(size_t i = 0; i < required->size; i++)
    {
        if(!extension_available(available, count, required->names[i]))
        {
            extensions_found = false;
            break;
        }
    }

    free(available);
    return extensions_found;
}

static bool is_suitable(Pigment* pigment, VkPhysicalDevice device, VkSurfaceKHR surface, const ExtensionList* req_extensions, const PVkInitInfo* vk_init)
{
    QueueFamilyIndices* indices      = NULL;
    SwapChainSupportDetails* details = NULL;
    bool result                      = false;

    indices = find_queue_families(device, surface);
    if(indices == NULL)
    {
        goto FREE;
    }

    if(!queue_families_indices_completed(*indices))
    {
        PLOG_TRACE(pigment, "Device rejected: incomplete queue families");
        goto FREE;
    }

    if(!check_device_extensions_supported(device, req_extensions))
    {
        PLOG_TRACE(pigment, "Device rejected: missing required extensions");
        goto FREE;
    }

    details = get_support_details(device, surface);
    if(details == NULL)
    {
        goto FREE;
    }
    if(details->formats_count == 0 || details->present_modes_count == 0)
    {
        PLOG_TRACE(pigment, "Device rejected: no surface formats or present modes");
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

    result = true;

FREE:
    free(indices);
    if(details != NULL)
    {
        destroy_support_details(details);
    }
    return result;
}

static int pick_physical_device(Pigment* pigment, PDevice* device, PSurface* surface, const ExtensionList* req_extensions, const PVkInitInfo* vk_init)
{
    VkPhysicalDevice* devices = NULL;
    uint32_t devices_count    = 0;

    vkEnumeratePhysicalDevices(pigment->instance->vulkan_instance, &devices_count, NULL);

    if(devices_count == 0)
    {
        PLOG_ERROR(pigment, "Failed to find GPUs compatible with Vulkan");
        return PIGMENT_ERROR;
    }

    devices = malloc(devices_count * sizeof(*devices));
    if(devices == NULL)
    {
        return PIGMENT_ERROR;
    }

    vkEnumeratePhysicalDevices(pigment->instance->vulkan_instance, &devices_count, devices);

    for(size_t i = 0; i < devices_count; i++)
    {
        if(is_suitable(pigment, devices[i], surface->surface, req_extensions, vk_init))
        {
            device->physical_device = devices[i];
            break;
        }
    }

    free(devices);

    if(device->physical_device == VK_NULL_HANDLE)
    {
        PLOG_ERROR(pigment, "Failed to find a suitable GPU");
        return PIGMENT_ERROR;
    }

    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device->physical_device, &device_properties);

    PLOG_INFO(pigment, "GPU picked: %s", device_properties.deviceName);

    return PIGMENT_SUCCESS;
}

QueueFamilyIndices* find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    QueueFamilyIndices* indices;
    VkQueueFamilyProperties* queue_families;
    uint32_t queue_families_count;
    VkBool32 present_support;

    indices = init_indices();
    if(indices == NULL)
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
    free(indices);
    return NULL;
}

static int create_logical_device(Pigment* pigment, PDevice* device, PSurface* surface)
{
    PInstance* instance                         = pigment->instance;
    QueueFamilyIndices* indices                 = NULL;
    VkDeviceQueueCreateInfo* queue_create_infos = NULL;
    QueueFamilySet* set                         = NULL;
    const PVkInitInfo* vk_init                  = (const PVkInitInfo*) pigment->config.extra;

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

    VkPhysicalDeviceVulkan11Features available_11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features available_12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &available_11};
    VkPhysicalDeviceVulkan13Features available_13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &available_12};
    VkPhysicalDeviceFeatures2 available_2         = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &available_13};
    vkGetPhysicalDeviceFeatures2(device->physical_device, &available_2);

    device->features[P_FEATURE_DEPTH_BOUNDS_TEST]       = (bool) available_2.features.depthBounds;
    device->features[P_FEATURE_WIREFRAME_RASTERIZATION] = (bool) available_2.features.fillModeNonSolid;

    VkPhysicalDeviceVulkan11Features vk11_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
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
        .pQueueCreateInfos       = queue_create_infos,
        .queueCreateInfoCount    = 1,
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

    if(vkCreateDevice(device->physical_device, &create_info, NULL, &(device->logical_device)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create logical device");
        goto ERROR;
    }
    volkLoadDevice(device->logical_device);

    vkGetDeviceQueue(device->logical_device, indices->graphics_family.value, 0, &device->graphics_queue);
    vkGetDeviceQueue(device->logical_device, indices->present_family.value, 0, &device->present_queue);

    device->graphics_family_index = indices->graphics_family.value;
    device->present_family_index  = indices->present_family.value;

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

PDevice* create_device(Pigment* pigment, PSurface* surface)
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

    if(pick_physical_device(pigment, device, surface, &req_extensions, vk_init) != PIGMENT_SUCCESS)
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

    if(create_logical_device(pigment, device, surface) != PIGMENT_SUCCESS)
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
