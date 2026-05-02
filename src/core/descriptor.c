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

#include "descriptor.h"
#include "internal.h"
#include "log_internal.h"

#define PIGMENT_DESCRIPTOR_POOL_INITIAL_CAPACITY 4

static VkDescriptorType to_vk_descriptor_type(PDescriptorType type);
static VkShaderStageFlags to_vk_shader_stages(PShaderStageFlags stages);
static VkDescriptorBindingFlags to_vk_binding_flags(PDescriptorBindingFlags flags);
static int pool_append_set(PDescriptorPool* pool, PDescriptorSet* set);
static VkImageLayout resolve_image_layout(PDescriptorType type, PImageDescriptorLayout override);

PDescriptorSetLayout* pigment_create_descriptor_set_layout(Pigment* pigment, const PDescriptorSetLayoutDesc* desc)
{
    if(pigment == NULL || desc == NULL || desc->binding_count == 0)
    {
        return NULL;
    }

    PDescriptorSetLayout* layout            = calloc(1, sizeof(*layout));
    VkDescriptorSetLayoutBinding* bindings  = calloc(desc->binding_count, sizeof(*bindings));
    VkDescriptorBindingFlags* binding_flags = calloc(desc->binding_count, sizeof(*binding_flags));

    if(layout == NULL || bindings == NULL || binding_flags == NULL)
    {
        goto ERROR;
    }

    bool needs_update_after_bind = false;
    for(uint32_t i = 0; i < desc->binding_count; i++)
    {
        const PDescriptorBinding* b = &desc->bindings[i];

        bindings[i] = (VkDescriptorSetLayoutBinding) {
            .binding         = b->binding,
            .descriptorType  = to_vk_descriptor_type(b->type),
            .descriptorCount = b->count,
            .stageFlags      = to_vk_shader_stages(b->stages),
        };

        binding_flags[i] = to_vk_binding_flags(b->flags);
        if(b->flags & P_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
        {
            needs_update_after_bind = true;
        }
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount  = desc->binding_count,
        .pBindingFlags = binding_flags,
    };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = desc->binding_count,
        .pBindings    = bindings,
        .flags        = needs_update_after_bind ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT : 0,
        .pNext        = &flags_info,
    };

    VkResult result = vkCreateDescriptorSetLayout(pigment->device->logical_device, &layout_info, NULL, &layout->layout);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create descriptor set layout (result: %d)", result);
        goto ERROR;
    }

    free(bindings);
    free(binding_flags);
    return layout;

ERROR:
    free(bindings);
    free(binding_flags);
    free(layout);
    return NULL;
}

void pigment_destroy_descriptor_set_layout(Pigment* pigment, PDescriptorSetLayout* layout)
{
    if(pigment == NULL || layout == NULL)
    {
        return;
    }
    vkDestroyDescriptorSetLayout(pigment->device->logical_device, layout->layout, NULL);
    free(layout);
}

PDescriptorPool* pigment_create_descriptor_pool(Pigment* pigment, const PDescriptorPoolDesc* desc)
{
    if(pigment == NULL || desc == NULL || desc->pool_size_count == 0 || desc->max_sets == 0)
    {
        return NULL;
    }

    PDescriptorPool* pool       = calloc(1, sizeof(*pool));
    VkDescriptorPoolSize* sizes = calloc(desc->pool_size_count, sizeof(*sizes));
    if(pool == NULL || sizes == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < desc->pool_size_count; i++)
    {
        sizes[i] = (VkDescriptorPoolSize) {
            .type            = to_vk_descriptor_type(desc->pool_sizes[i].type),
            .descriptorCount = desc->pool_sizes[i].count,
        };
    }

    VkDescriptorPoolCreateInfo pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = desc->pool_size_count,
        .pPoolSizes    = sizes,
        .maxSets       = desc->max_sets,
        .flags         = desc->allow_update_after_bind ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : 0,
    };

    VkResult result = vkCreateDescriptorPool(pigment->device->logical_device, &pool_info, NULL, &pool->pool);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create descriptor pool (result: %d)", result);
        goto ERROR;
    }

    free(sizes);
    return pool;

ERROR:
    free(sizes);
    free(pool);
    return NULL;
}

void pigment_destroy_descriptor_pool(Pigment* pigment, PDescriptorPool* pool)
{
    if(pigment == NULL || pool == NULL)
    {
        return;
    }
    vkDestroyDescriptorPool(pigment->device->logical_device, pool->pool, NULL);
    for(uint32_t i = 0; i < pool->set_count; i++)
    {
        free(pool->sets[i]);
    }
    free(pool->sets);
    free(pool);
}

PDescriptorSet* pigment_allocate_descriptor_set(Pigment* pigment, PDescriptorPool* pool, PDescriptorSetLayout* layout, uint32_t variable_count)
{
    if(pigment == NULL || pool == NULL || layout == NULL)
    {
        return NULL;
    }

    PDescriptorSet* set = calloc(1, sizeof(*set));
    if(set == NULL)
    {
        return NULL;
    }

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_desciptor_counts_alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts  = &variable_count,
    };

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = pool->pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &layout->layout,
        .pNext              = (variable_count > 0) ? &variable_desciptor_counts_alloc_info : NULL,
    };

    VkResult result = vkAllocateDescriptorSets(pigment->device->logical_device, &alloc_info, &set->set);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to allocate descriptor set (result: %d)", result);
        free(set);
        return NULL;
    }

    if(pool_append_set(pool, set) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to append descriptor set to pool");
        free(set);
        return NULL;
    }

    return set;
}

void pigment_write_descriptors(Pigment* pigment, const PDescriptorWrite* writes, uint32_t write_count)
{
    if(pigment == NULL || writes == NULL || write_count == 0)
    {
        return;
    }

    VkWriteDescriptorSet* vk_writes       = calloc(write_count, sizeof(*vk_writes));
    VkDescriptorImageInfo** image_infos   = calloc(write_count, sizeof(*image_infos));
    VkDescriptorBufferInfo** buffer_infos = calloc(write_count, sizeof(*buffer_infos));
    if(vk_writes == NULL || image_infos == NULL || buffer_infos == NULL)
    {
        goto FREE;
    }

    for(uint32_t i = 0; i < write_count; i++)
    {
        const PDescriptorWrite* write = &writes[i];

        vk_writes[i] = (VkWriteDescriptorSet) {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = write->set->set,
            .dstBinding      = write->binding,
            .dstArrayElement = write->array_element,
            .descriptorType  = to_vk_descriptor_type(write->type),
            .descriptorCount = write->count,
        };

        switch(write->type)
        {
            case P_DESCRIPTOR_TYPE_SAMPLER:
            case P_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case P_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                {
                    image_infos[i] = calloc(write->count, sizeof(VkDescriptorImageInfo));
                    if(image_infos[i] == NULL)
                    {
                        goto FREE;
                    }

                    for(uint32_t j = 0; j < write->count; j++)
                    {
                        const PDescriptorImageInfo* info = &write->image_infos[j];
                        image_infos[i][j].sampler        = (info->sampler != NULL) ? info->sampler->sampler : VK_NULL_HANDLE;
                        image_infos[i][j].imageView      = (info->image != NULL) ? info->image->image_view : VK_NULL_HANDLE;
                        image_infos[i][j].imageLayout    = resolve_image_layout(write->type, info->layout);
                    }

                    vk_writes[i].pImageInfo = image_infos[i];

                    break;
                }
            case P_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case P_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                {
                    buffer_infos[i] = calloc(write->count, sizeof(VkDescriptorBufferInfo));
                    if(buffer_infos[i] == NULL)
                    {
                        goto FREE;
                    }

                    for(uint32_t j = 0; j < write->count; j++)
                    {
                        const PDescriptorBufferInfo* info = &write->buffer_infos[j];
                        buffer_infos[i][j].buffer         = info->buffer->buffer;
                        buffer_infos[i][j].offset         = info->offset;
                        buffer_infos[i][j].range          = (info->range == 0) ? VK_WHOLE_SIZE : info->range;
                    }

                    vk_writes[i].pBufferInfo = buffer_infos[i];

                    break;
                }
        }
    }

    vkUpdateDescriptorSets(pigment->device->logical_device, write_count, vk_writes, 0, NULL);

FREE:
    if(image_infos != NULL)
    {
        for(uint32_t i = 0; i < write_count; i++)
        {
            free(image_infos[i]);
        }
        free(image_infos);
    }

    if(buffer_infos != NULL)
    {
        for(uint32_t i = 0; i < write_count; i++)
        {
            free(buffer_infos[i]);
        }
        free(buffer_infos);
    }
    free(vk_writes);
}

void pigment_cmd_bind_descriptor_set(Pigment* pigment, uint32_t window_index, PPipeline* pipeline, uint32_t set_index, PDescriptorSet* set)
{
    if(pigment == NULL || pipeline == NULL || set == NULL || window_index >= pigment->window_count)
    {
        return;
    }

    PWindowRenderer* renderer = pigment->renderers[window_index];
    VkCommandBuffer cmd       = renderer->command_buffers->buffers[renderer->swapchain->current_frame];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout->layout, set_index, 1, &set->set, 0, NULL);
}

static VkDescriptorType to_vk_descriptor_type(PDescriptorType type)
{
    switch(type)
    {
        case P_DESCRIPTOR_TYPE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case P_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case P_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case P_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case P_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

static VkShaderStageFlags to_vk_shader_stages(PShaderStageFlags stages)
{
    VkShaderStageFlags out = 0;
    if(stages & P_SHADER_STAGE_VERTEX_BIT)
    {
        out |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if(stages & P_SHADER_STAGE_FRAGMENT_BIT)
    {
        out |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if(stages & P_SHADER_STAGE_COMPUTE_BIT)
    {
        out |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return out;
}

static VkDescriptorBindingFlags to_vk_binding_flags(PDescriptorBindingFlags flags)
{
    VkDescriptorBindingFlags out = 0;
    if(flags & P_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT)
    {
        out |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    }
    if(flags & P_DESCRIPTOR_BINDING_VARIABLE_COUNT_BIT)
    {
        out |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    }
    if(flags & P_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
    {
        out |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    }
    return out;
}

static int pool_append_set(PDescriptorPool* pool, PDescriptorSet* set)
{
    if(pool->set_count >= pool->set_capacity)
    {
        uint32_t new_capacity    = (pool->set_capacity == 0) ? PIGMENT_DESCRIPTOR_POOL_INITIAL_CAPACITY : pool->set_capacity * 2;
        PDescriptorSet** new_ptr = realloc(pool->sets, new_capacity * sizeof(*new_ptr));
        if(new_ptr == NULL)
        {
            return PIGMENT_ERROR;
        }

        pool->sets         = new_ptr;
        pool->set_capacity = new_capacity;
    }
    pool->sets[pool->set_count++] = set;

    return PIGMENT_SUCCESS;
}

static VkImageLayout resolve_image_layout(PDescriptorType type, PImageDescriptorLayout override)
{
    switch(override)
    {
        case P_IMAGE_DESCRIPTOR_LAYOUT_SHADER_READ_ONLY:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case P_IMAGE_DESCRIPTOR_LAYOUT_GENERAL:
            return VK_IMAGE_LAYOUT_GENERAL;
        case P_IMAGE_DESCRIPTOR_LAYOUT_DEPTH_READ_ONLY:
            return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        case P_IMAGE_DESCRIPTOR_LAYOUT_DEPTH_STENCIL_READ_ONLY:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case P_IMAGE_DESCRIPTOR_LAYOUT_AUTO:
        default:
            return (type == P_DESCRIPTOR_TYPE_STORAGE_IMAGE) ? VK_IMAGE_LAYOUT_GENERAL
                                                             : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
}
