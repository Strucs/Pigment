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

#include "pigment_vk.h"
#include "queue.h"

#include "internal.h"

static const VkQueueFlags QUEUE_USEFUL_FLAGS = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;

static const PQueueRequest DEFAULT_QUEUE_REQUESTS[] = {
    {.required = P_QUEUE_GRAPHICS_BIT,                                          .forbidden = 0, .priority = 1.0f},
    { .required = P_QUEUE_COMPUTE_BIT,                       .forbidden = P_QUEUE_GRAPHICS_BIT, .priority = 1.0f},
    {.required = P_QUEUE_TRANSFER_BIT, .forbidden = P_QUEUE_GRAPHICS_BIT | P_QUEUE_COMPUTE_BIT, .priority = 1.0f},
};

#define DEFAULT_QUEUE_REQUEST_COUNT (sizeof(DEFAULT_QUEUE_REQUESTS) / sizeof(DEFAULT_QUEUE_REQUESTS[0]))

typedef struct ResolvedQueue {
    uint32_t family;
    uint32_t queue_index_in_family;
    float priority;
} ResolvedQueue;

static PBool features10_supports(const VkPhysicalDeviceFeatures* req, const VkPhysicalDeviceFeatures* available);
static PBool features_chain_supports(const void* req, const void* available, size_t struct_size);
static void merge_features10_or(VkPhysicalDeviceFeatures* dst, const VkPhysicalDeviceFeatures* src, const VkPhysicalDeviceFeatures* available);
static void merge_features_chain_or(void* dst, const void* src, const void* available, size_t struct_size);

static PBool check_device_extensions_supported(Pigment* pigment, VkPhysicalDevice device, const ExtensionList* required);
static PBool is_suitable(Pigment* pigment, VkPhysicalDevice device, const ExtensionList* req_extensions, const PVkInitInfo* vk_init);
static PResult pick_physical_device(Pigment* pigment, PDevice* device, const ExtensionList* req_extensions, const PVkInitInfo* vk_init);
static PResult resolve_queue_requests(Pigment* pigment, const VkQueueFamilyProperties* queue_families, uint32_t queue_families_count, const PQueueRequest* requests, uint32_t request_count, ResolvedQueue* resolved, uint32_t* requested_per_family, PBool optional);
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
        .timelineSemaphore                         = VK_TRUE,
    };
}

static inline VkPhysicalDeviceVulkan13Features pigment_req_features_13(void)
{
    return (VkPhysicalDeviceVulkan13Features) {
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE,
        .maintenance4     = VK_TRUE,
    };
}

static inline uint32_t count_useful_bits(PQueueFlags flags)
{
    uint32_t count = 0;
    if(flags & P_QUEUE_GRAPHICS_BIT)
    {
        count++;
    }
    if(flags & P_QUEUE_COMPUTE_BIT)
    {
        count++;
    }
    if(flags & P_QUEUE_TRANSFER_BIT)
    {
        count++;
    }
    return count;
}

static inline PQueueFlags vk_to_pigment_queue_flags(VkQueueFlags flags)
{
    PQueueFlags out = 0;
    if(flags & VK_QUEUE_GRAPHICS_BIT)
    {
        out |= P_QUEUE_GRAPHICS_BIT;
    }
    if(flags & VK_QUEUE_COMPUTE_BIT)
    {
        out |= P_QUEUE_COMPUTE_BIT;
    }
    if(flags & VK_QUEUE_TRANSFER_BIT)
    {
        out |= P_QUEUE_TRANSFER_BIT;
    }
    return out;
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

static PBool check_device_extensions_supported(Pigment* pigment, VkPhysicalDevice device, const ExtensionList* required)
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
        available = P_NEW_ARRAY_FOR_COMMAND(pigment, available, count);
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

    P_FREE(pigment, available);
    return extensions_found;
}

static PBool is_suitable(Pigment* pigment, VkPhysicalDevice device, const ExtensionList* req_extensions, const PVkInitInfo* vk_init)
{
    PBool result = P_FALSE;

    uint32_t graphics_family = 0;
    if(!find_graphics_family(pigment, device, &graphics_family))
    {
        PLOG_TRACE(pigment, "Device rejected: no graphics queue family");
        goto FREE;
    }

    if(!check_device_extensions_supported(pigment, device, req_extensions))
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

    devices = P_NEW_ARRAY_FOR_COMMAND(pigment, devices, devices_count);
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

    P_FREE(pigment, devices);

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

PBool find_graphics_family(Pigment* pigment, VkPhysicalDevice device, uint32_t* out_family)
{
    uint32_t queue_families_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, NULL);
    if(queue_families_count == 0)
    {
        return P_FALSE;
    }

    VkQueueFamilyProperties* queue_families = P_NEW_ARRAY_FOR_COMMAND(pigment, queue_families, queue_families_count);
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

    P_FREE(pigment, queue_families);
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

    PDeviceQueue* best  = NULL;
    uint32_t best_score = UINT32_MAX;
    for(uint32_t i = 0; i < device->queue_count; i++)
    {
        PQueueFlags flags = device->queues[i].flags;
        if((flags & required) != required)
        {
            continue;
        }
        if((flags & forbidden) != 0)
        {
            continue;
        }

        uint32_t score = count_useful_bits(flags);

        // Prefer queues with the lowest score because the less bits set, the more specialized the queue is.
        if(score < best_score)
        {
            best_score = score;
            best       = &device->queues[i];
        }
    }
    return best;
}

PDeviceQueue* pigment_get_queue(Pigment* pigment, PQueueFlags required, PQueueFlags forbidden)
{
    if(pigment == NULL)
    {
        return NULL;
    }

    return device_find_queue(pigment->device, required, forbidden);
}

uint32_t pigment_get_queue_count(Pigment* pigment)
{
    if(pigment == NULL || pigment->device == NULL)
    {
        return 0;
    }

    return pigment->device->queue_count;
}

PDeviceQueue* pigment_get_queue_at(Pigment* pigment, uint32_t index)
{
    if(pigment == NULL || pigment->device == NULL || index >= pigment->device->queue_count)
    {
        return NULL;
    }

    return &pigment->device->queues[index];
}

static PResult resolve_queue_requests(Pigment* pigment, const VkQueueFamilyProperties* queue_families, uint32_t queue_families_count, const PQueueRequest* requests, uint32_t request_count, ResolvedQueue* resolved, uint32_t* requested_per_family, PBool optional)
{
    for(uint32_t req_idx = 0; req_idx < request_count; req_idx++)
    {
        const PQueueRequest* request = &requests[req_idx];
        uint32_t best_family         = UINT32_MAX;
        uint32_t best_score          = UINT32_MAX;

        for(uint32_t family_idx = 0; family_idx < queue_families_count; family_idx++)
        {
            VkQueueFlags vk_flags      = queue_families[family_idx].queueFlags & QUEUE_USEFUL_FLAGS;
            PQueueFlags family_pigment = vk_to_pigment_queue_flags(vk_flags);
            if((family_pigment & request->required) != request->required)
            {
                continue;
            }
            if((family_pigment & request->forbidden) != 0)
            {
                continue;
            }
            if(requested_per_family[family_idx] >= queue_families[family_idx].queueCount)
            {
                continue;
            }
            uint32_t score = count_useful_bits(family_pigment);

            // Prefer queues with the lowest score because the less bits set, the more specialized the queue is.
            if(score < best_score)
            {
                best_score  = score;
                best_family = family_idx;
            }
        }

        if(best_family == UINT32_MAX)
        {
            if(optional)
            {
                resolved[req_idx].family = UINT32_MAX;
                continue;
            }
            PLOG_ERROR(pigment, "Queue request %u (required=0x%x, forbidden=0x%x) could not be satisfied by this device", req_idx, request->required, request->forbidden);
            return PIGMENT_ERROR_VULKAN;
        }

        float priority                          = request->priority > 0.0f ? request->priority : 1.0f;
        resolved[req_idx].family                = best_family;
        resolved[req_idx].queue_index_in_family = requested_per_family[best_family]++;
        resolved[req_idx].priority              = priority;
    }

    return PIGMENT_SUCCESS;
}

static PResult create_logical_device(Pigment* pigment, PDevice* device)
{
    PResult result                          = PIGMENT_ERROR_OUT_OF_MEMORY;
    PInstance* instance                     = pigment->instance;
    const PVkInitInfo* vk_init              = (const PVkInitInfo*) pigment->config.extra;
    VkDeviceQueueCreateInfo* queue_infos    = NULL;
    uint32_t* requested_per_family          = NULL;
    float* priority_pool                    = NULL;
    ResolvedQueue* resolved                 = NULL;
    VkQueueFamilyProperties* queue_families = NULL;
    uint32_t queue_families_count           = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device, &queue_families_count, NULL);
    queue_families = P_NEW_ARRAY_FOR_COMMAND(pigment, queue_families, queue_families_count);
    if(queue_families == NULL)
    {
        goto ERROR;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device, &queue_families_count, queue_families);

    const PQueueRequest* user_requests = pigment->config.queue_requests;
    uint32_t request_count             = pigment->config.queue_request_count;
    PBool default_policy               = (user_requests == NULL || request_count == 0);
    if(default_policy)
    {
        user_requests = DEFAULT_QUEUE_REQUESTS;
        request_count = DEFAULT_QUEUE_REQUEST_COUNT;
    }

    requested_per_family = P_NEW_ARRAY_FOR_COMMAND(pigment, requested_per_family, queue_families_count);
    priority_pool        = P_NEW_ARRAY_FOR_COMMAND(pigment, priority_pool, request_count);
    resolved             = P_NEW_ARRAY_FOR_COMMAND(pigment, resolved, request_count);
    queue_infos          = P_NEW_ARRAY_FOR_COMMAND(pigment, queue_infos, queue_families_count);
    if(requested_per_family == NULL || priority_pool == NULL || resolved == NULL || queue_infos == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < queue_families_count; i++)
    {
        requested_per_family[i] = 0;
    }

    PResult resolve_result = resolve_queue_requests(pigment, queue_families, queue_families_count, user_requests, request_count, resolved, requested_per_family, default_policy);
    if(resolve_result != PIGMENT_SUCCESS)
    {
        result = resolve_result;
        goto ERROR;
    }

    uint32_t selected_count  = 0;
    uint32_t priority_offset = 0;
    for(uint32_t family_idx = 0; family_idx < queue_families_count; family_idx++)
    {
        if(requested_per_family[family_idx] == 0)
        {
            continue;
        }

        for(uint32_t req_idx = 0; req_idx < request_count; req_idx++)
        {
            if(resolved[req_idx].family == family_idx)
            {
                priority_pool[priority_offset + resolved[req_idx].queue_index_in_family] = resolved[req_idx].priority;
            }
        }

        queue_infos[selected_count] = (VkDeviceQueueCreateInfo) {
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = family_idx,
            .queueCount       = requested_per_family[family_idx],
            .pQueuePriorities = &priority_pool[priority_offset],
        };
        priority_offset += requested_per_family[family_idx];
        selected_count++;
    }

    VkPhysicalDeviceVulkan11Features available_11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features available_12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &available_11};
    VkPhysicalDeviceVulkan13Features available_13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &available_12};
    VkPhysicalDeviceFeatures2 available_2         = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &available_13};
    vkGetPhysicalDeviceFeatures2(device->physical_device, &available_2);

    device->features[P_FEATURE_DEPTH_BOUNDS_TEST]            = (PBool) available_2.features.depthBounds;
    device->features[P_FEATURE_WIREFRAME_RASTERIZATION]      = (PBool) available_2.features.fillModeNonSolid;
    device->features[P_FEATURE_MULTI_DRAW_INDIRECT]          = (PBool) available_2.features.multiDrawIndirect;
    device->features[P_FEATURE_DRAW_INDIRECT_FIRST_INSTANCE] = (PBool) available_2.features.drawIndirectFirstInstance;
    device->features[P_FEATURE_DRAW_INDIRECT_COUNT]          = (PBool) available_12.drawIndirectCount;

    VkPhysicalDeviceVulkan11Features vk11_features = {
        .sType     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .multiview = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features vk12_features = {
        .sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext             = &vk11_features,
        .drawIndirectCount = device->features[P_FEATURE_DRAW_INDIRECT_COUNT] ? VK_TRUE : VK_FALSE,
    };
    VkPhysicalDeviceVulkan13Features vk13_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vk12_features,
    };

    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain_maint1_features = {0};
    PBool enable_swapchain_maint1                                              = name_in_list((const char* const*) device->extensions->names, device->extensions->size, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
    if(enable_swapchain_maint1)
    {
        swapchain_maint1_features.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        swapchain_maint1_features.swapchainMaintenance1 = VK_TRUE;
        swapchain_maint1_features.pNext                 = &vk13_features;
    }

    VkPhysicalDeviceFeatures2 features = {
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .features = {
                     .fillModeNonSolid          = device->features[P_FEATURE_WIREFRAME_RASTERIZATION] ? VK_TRUE : VK_FALSE,
                     .depthBounds               = device->features[P_FEATURE_DEPTH_BOUNDS_TEST] ? VK_TRUE : VK_FALSE,
                     .multiDrawIndirect         = device->features[P_FEATURE_MULTI_DRAW_INDIRECT] ? VK_TRUE : VK_FALSE,
                     .drawIndirectFirstInstance = device->features[P_FEATURE_DRAW_INDIRECT_FIRST_INSTANCE] ? VK_TRUE : VK_FALSE,
                     },
        .pNext = enable_swapchain_maint1 ? (void*) &swapchain_maint1_features : (void*) &vk13_features,
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
    if(vkCreateDevice(device->physical_device, &create_info, &pigment->vk_alloc, &(device->logical_device)) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create logical device");
        goto ERROR;
    }
    volkLoadDevice(device->logical_device);

    uint32_t resolved_count = 0;
    for(uint32_t r = 0; r < request_count; r++)
    {
        if(resolved[r].family != UINT32_MAX)
        {
            resolved_count++;
        }
    }

    device->queues = P_NEW_ARRAY_FOR_OBJECT(pigment, device->queues, resolved_count);
    if(device->queues == NULL)
    {
        result = PIGMENT_ERROR_OUT_OF_MEMORY;
        goto ERROR;
    }
    device->queue_count = resolved_count;

    uint32_t queue_index = 0;
    for(uint32_t r = 0; r < request_count; r++)
    {
        if(resolved[r].family == UINT32_MAX)
        {
            continue;
        }
        PDeviceQueue* queue = &device->queues[queue_index];
        queue->family_index = resolved[r].family;
        queue->slot         = queue_index;
        queue->flags        = vk_to_pigment_queue_flags(queue_families[resolved[r].family].queueFlags & QUEUE_USEFUL_FLAGS);
        atomic_store_explicit(&queue->next_value, 0, memory_order_relaxed);
        vkGetDeviceQueue(device->logical_device, resolved[r].family, resolved[r].queue_index_in_family, &queue->queue);

        VkSemaphoreTypeCreateInfo type_info = {
            .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue  = 0,
        };
        VkSemaphoreCreateInfo sem_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &type_info,
        };
        if(vkCreateSemaphore(device->logical_device, &sem_info, &pigment->vk_alloc, &queue->timeline) != VK_SUCCESS)
        {
            PLOG_ERROR(pigment, "Failed to create timeline semaphore for queue family %u", resolved[r].family);
            for(uint32_t j = 0; j < queue_index; j++)
            {
                vkDestroySemaphore(device->logical_device, device->queues[j].timeline, &pigment->vk_alloc);
            }
            P_FREE(pigment, device->queues);
            device->queues = NULL;
            result         = PIGMENT_ERROR_VULKAN;
            goto ERROR;
        }
        queue_index++;
    }

    PLOG_DEBUG(pigment, "Resolved %u queue%s:", device->queue_count, device->queue_count > 1 ? "s" : "");
    for(uint32_t i = 0; i < device->queue_count; i++)
    {
        PDeviceQueue* queue = &device->queues[i];
        PLOG_DEBUG(pigment, "  [%u] family=%u flags=%s%s%s", i, queue->family_index, (queue->flags & P_QUEUE_GRAPHICS_BIT) ? "GRAPHICS " : "", (queue->flags & P_QUEUE_COMPUTE_BIT) ? "COMPUTE " : "", (queue->flags & P_QUEUE_TRANSFER_BIT) ? "TRANSFER" : "");
    }

    P_FREE(pigment, queue_infos);
    P_FREE(pigment, requested_per_family);
    P_FREE(pigment, priority_pool);
    P_FREE(pigment, resolved);
    P_FREE(pigment, queue_families);
    return PIGMENT_SUCCESS;

ERROR:
    P_FREE(pigment, queue_infos);
    P_FREE(pigment, requested_per_family);
    P_FREE(pigment, priority_pool);
    P_FREE(pigment, resolved);
    P_FREE(pigment, queue_families);
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

    PBool instance_has_surface_maint1 = name_in_list((const char* const*) pigment->instance->extensions->names, pigment->instance->extensions->size, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME)
                                        && name_in_list((const char* const*) pigment->instance->extensions->names, pigment->instance->extensions->size, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);

    const char* default_opt_extensions[1] = {0};
    uint32_t default_opt_count            = 0;
    if(instance_has_surface_maint1)
    {
        default_opt_extensions[default_opt_count++] = VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
    }

    device = P_NEW_FOR_OBJECT(pigment, device);
    if(device == NULL)
    {
        goto ERROR;
    }

    uint32_t max_req     = default_req_count + (vk_init != NULL ? vk_init->req_device_extensions_count : 0);
    req_extensions.names = P_NEW_ARRAY_FOR_COMMAND(pigment, req_extensions.names, max_req);
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
        available_extensions = P_NEW_ARRAY_FOR_COMMAND(pigment, available_extensions, available_extension_count);
        if(available_extensions == NULL)
        {
            goto ERROR;
        }
        vkEnumerateDeviceExtensionProperties(device->physical_device, NULL, &available_extension_count, available_extensions);
    }

    uint32_t max_final = req_extensions.size + default_opt_count + (vk_init != NULL ? vk_init->opt_device_extensions_count : 0);
    device->extensions = P_NEW_FOR_OBJECT(pigment, device->extensions);
    if(device->extensions == NULL)
    {
        goto ERROR;
    }
    device->extensions->names = P_NEW_ARRAY_FOR_OBJECT(pigment, device->extensions->names, max_final);
    if(device->extensions->names == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < req_extensions.size; i++)
    {
        device->extensions->names[device->extensions->size++] = req_extensions.names[i];
    }

    for(uint32_t i = 0; i < default_opt_count; i++)
    {
        const char* name = default_opt_extensions[i];
        if(!extension_available(available_extensions, available_extension_count, name))
        {
            PLOG_INFO(pigment, "Optional device extension not available, skipping: %s", name);
            continue;
        }
        if(!name_in_list((const char* const*) device->extensions->names, device->extensions->size, name))
        {
            device->extensions->names[device->extensions->size++] = name;
        }
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

    P_FREE(pigment, req_extensions.names);
    P_FREE(pigment, available_extensions);
    return device;

ERROR:
    P_FREE(pigment, req_extensions.names);
    P_FREE(pigment, available_extensions);
    if(device != NULL)
    {
        if(device->extensions != NULL)
        {
            P_FREE(pigment, device->extensions->names);
        }
        P_FREE(pigment, device->extensions);
        P_FREE(pigment, device);
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
    if(device->queues != NULL)
    {
        for(uint32_t i = 0; i < device->queue_count; i++)
        {
            if(device->queues[i].timeline != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device->logical_device, device->queues[i].timeline, &pigment->vk_alloc);
            }
        }
    }
    vkDestroyDevice(device->logical_device, &pigment->vk_alloc);
    if(device->extensions != NULL)
    {
        P_FREE(pigment, device->extensions->names);
    }
    P_FREE(pigment, device->extensions);
    P_FREE(pigment, device->queues);
    P_FREE(pigment, device);
}

void device_wait_idle(Pigment* pigment)
{
    if(pigment == NULL || pigment->device == NULL)
    {
        return;
    }
    vkDeviceWaitIdle(pigment->device->logical_device);
    drain_deletion_queue(pigment);
}

VkInstance pigment_vk_instance(Pigment* pigment)
{
    return (pigment != NULL && pigment->instance != NULL) ? pigment->instance->vulkan_instance : VK_NULL_HANDLE;
}

VkDevice pigment_vk_device(Pigment* pigment)
{
    return (pigment != NULL && pigment->device != NULL) ? pigment->device->logical_device : VK_NULL_HANDLE;
}

VkPhysicalDevice pigment_vk_physical_device(Pigment* pigment)
{
    return (pigment != NULL && pigment->device != NULL) ? pigment->device->physical_device : VK_NULL_HANDLE;
}

VkQueue pigment_vk_queue(PDeviceQueue* queue)
{
    return (queue != NULL) ? queue->queue : VK_NULL_HANDLE;
}

uint32_t pigment_queue_family(PDeviceQueue* queue)
{
    return (queue != NULL) ? queue->family_index : UINT32_MAX;
}
