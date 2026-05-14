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

#include "sampler.h"
#include "structs.h"
#include "commands.h"
#include "deletion.h"
#include "internal.h"

static void destroy_sampler_immediate(Pigment* pigment, void* resource);

PSampler* pigment_create_sampler(Pigment* pigment, const PSamplerDesc* desc)
{
    if(pigment == NULL || desc == NULL)
    {
        return NULL;
    }

    PSampler* sampler = P_NEW_FOR_OBJECT(pigment, sampler);
    if(sampler == NULL)
    {
        return NULL;
    }

    if(pigment_resource_tracker_init(pigment, &sampler->tracker) != PIGMENT_SUCCESS)
    {
        P_FREE(pigment, sampler);
        return NULL;
    }

    PDevice* device                       = pigment->device;
    VkPhysicalDeviceProperties properties = {0};
    vkGetPhysicalDeviceProperties(device->physical_device, &properties);

    float anisotropy = desc->max_anisotropy;
    if(anisotropy > properties.limits.maxSamplerAnisotropy)
    {
        anisotropy = properties.limits.maxSamplerAnisotropy;
    }

    VkSamplerAddressMode vk_address_mode    = (VkSamplerAddressMode) desc->address_mode;
    VkSamplerCreateInfo sampler_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter               = (VkFilter) desc->mag_filter,
        .minFilter               = (VkFilter) desc->min_filter,
        .addressModeU            = vk_address_mode,
        .addressModeV            = vk_address_mode,
        .addressModeW            = vk_address_mode,
        .anisotropyEnable        = (anisotropy > 0.0f) ? VK_TRUE : VK_FALSE,
        .maxAnisotropy           = anisotropy,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
        .compareEnable           = desc->compare_enable ? VK_TRUE : VK_FALSE,
        .compareOp               = (VkCompareOp) desc->compare_op,
        .mipmapMode              = (VkSamplerMipmapMode) desc->mipmap_mode,
        .minLod                  = 0.0f,
        .maxLod                  = VK_LOD_CLAMP_NONE,
    };

    VkResult result = vkCreateSampler(device->logical_device, &sampler_create_info, &pigment->vk_alloc, &sampler->sampler);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create sampler! (result: %d)", result);
        pigment_resource_tracker_destroy(pigment, &sampler->tracker);
        P_FREE(pigment, sampler);
        return NULL;
    }

    set_object_name(device->logical_device, VK_OBJECT_TYPE_SAMPLER, (uint64_t) sampler->sampler, desc->name);

    return sampler;
}

void pigment_destroy_sampler(Pigment* pigment, PSampler* sampler)
{
    if(pigment == NULL || sampler == NULL)
    {
        return;
    }

    pigment_defer_destroy_tracked(pigment, destroy_sampler_immediate, sampler, &sampler->tracker);
}

static void destroy_sampler_immediate(Pigment* pigment, void* resource)
{
    PSampler* sampler = (PSampler*) resource;
    vkDestroySampler(pigment->device->logical_device, sampler->sampler, &pigment->vk_alloc);
    pigment_resource_tracker_destroy(pigment, &sampler->tracker);
    P_FREE(pigment, sampler);
}

VkSampler pigment_vk_sampler(PSampler* sampler)
{
    return (sampler != NULL) ? sampler->sampler : VK_NULL_HANDLE;
}
