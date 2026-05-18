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

#include "deletion.h"

#include "internal.h"

#define PIGMENT_DESCRIPTOR_POOL_INITIAL_CAPACITY 4

typedef struct PDescriptorSetBatch {
    PDescriptorPool* pool;
    VkDescriptorSet* vk_sets;
    PDescriptorSet** wrappers;
    uint32_t count;
} PDescriptorSetBatch;

static void destroy_descriptor_pool_immediate(Pigment* pigment, void* resource);
static void destroy_descriptor_sets_immediate(Pigment* pigment, void* resource);
static VkDescriptorType to_vk_descriptor_type(PDescriptorType type);
static VkShaderStageFlags to_vk_shader_stages(PShaderStageFlags stages);
static VkDescriptorBindingFlags to_vk_binding_flags(PDescriptorBindingFlags flags);
static PResult pool_append_set(Pigment* pigment, PDescriptorPool* pool, PDescriptorSet* set);
static void pool_remove_set(PDescriptorPool* pool, PDescriptorSet* set);
static VkImageLayout resolve_image_layout(PDescriptorType type, PImageDescriptorLayout override);

PDescriptorSetLayout* pigment_create_descriptor_set_layout(Pigment* pigment, const PDescriptorSetLayoutDesc* desc)
{
    if(pigment == NULL || desc == NULL || desc->binding_count == 0)
    {
        return NULL;
    }

    PDescriptorSetLayout* layout            = P_NEW_FOR_OBJECT(pigment, layout);
    VkDescriptorSetLayoutBinding* bindings  = P_NEW_ARRAY_FOR_COMMAND(pigment, bindings, desc->binding_count);
    VkDescriptorBindingFlags* binding_flags = P_NEW_ARRAY_FOR_COMMAND(pigment, binding_flags, desc->binding_count);

    if(layout == NULL || bindings == NULL || binding_flags == NULL)
    {
        goto ERROR;
    }

    PBool needs_update_after_bind = P_FALSE;
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
            needs_update_after_bind = P_TRUE;
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

    VkResult result = vkCreateDescriptorSetLayout(pigment->device->logical_device, &layout_info, &pigment->vk_alloc, &layout->layout);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create descriptor set layout (result: %d)", result);
        goto ERROR;
    }

    set_object_name(pigment->device->logical_device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t) layout->layout, desc->name);

    P_FREE(pigment, bindings);
    P_FREE(pigment, binding_flags);
    return layout;

ERROR:
    P_FREE(pigment, bindings);
    P_FREE(pigment, binding_flags);
    P_FREE(pigment, layout);
    return NULL;
}

void pigment_destroy_descriptor_set_layout(Pigment* pigment, PDescriptorSetLayout* layout)
{
    if(pigment == NULL || layout == NULL)
    {
        return;
    }

    vkDestroyDescriptorSetLayout(pigment->device->logical_device, layout->layout, &pigment->vk_alloc);
    P_FREE(pigment, layout);
}

PDescriptorPool* pigment_create_descriptor_pool(Pigment* pigment, const PDescriptorPoolDesc* desc)
{
    if(pigment == NULL || desc == NULL || desc->pool_size_count == 0 || desc->max_sets == 0)
    {
        return NULL;
    }

    PDescriptorPool* pool       = P_NEW_FOR_OBJECT(pigment, pool);
    VkDescriptorPoolSize* sizes = P_NEW_ARRAY_FOR_COMMAND(pigment, sizes, desc->pool_size_count);
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

    VkDescriptorPoolCreateFlags vk_flags = 0;
    if(desc->allow_update_after_bind)
    {
        vk_flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    }
    if(desc->allow_free_set)
    {
        vk_flags |= VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    }

    VkDescriptorPoolCreateInfo pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = desc->pool_size_count,
        .pPoolSizes    = sizes,
        .maxSets       = desc->max_sets,
        .flags         = vk_flags,
    };

    VkResult result = vkCreateDescriptorPool(pigment->device->logical_device, &pool_info, &pigment->vk_alloc, &pool->pool);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create descriptor pool (result: %d)", result);
        goto ERROR;
    }

    pool->allow_free_set = desc->allow_free_set;

    set_object_name(pigment->device->logical_device, VK_OBJECT_TYPE_DESCRIPTOR_POOL, (uint64_t) pool->pool, desc->name);

    P_FREE(pigment, sizes);
    return pool;

ERROR:
    P_FREE(pigment, sizes);
    P_FREE(pigment, pool);
    return NULL;
}

void pigment_destroy_descriptor_pool(Pigment* pigment, PDescriptorPool* pool)
{
    if(pigment == NULL || pool == NULL)
    {
        return;
    }

    pigment_defer_destroy(pigment, destroy_descriptor_pool_immediate, pool);
}

static void destroy_descriptor_pool_immediate(Pigment* pigment, void* resource)
{
    PDescriptorPool* pool = (PDescriptorPool*) resource;
    vkDestroyDescriptorPool(pigment->device->logical_device, pool->pool, &pigment->vk_alloc);
    for(uint32_t i = 0; i < pool->set_count; i++)
    {
        pigment_resource_tracker_destroy(pigment, &pool->sets[i]->tracker);
        P_FREE(pigment, pool->sets[i]);
    }
    P_FREE(pigment, pool->sets);
    P_FREE(pigment, pool);
}

void pigment_reset_descriptor_pool(Pigment* pigment, PDescriptorPool* pool)
{
    if(pigment == NULL || pool == NULL)
    {
        return;
    }

    VkResult result = vkResetDescriptorPool(pigment->device->logical_device, pool->pool, 0);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to reset descriptor pool (result: %d)", result);
        return;
    }

    for(uint32_t i = 0; i < pool->set_count; i++)
    {
        pigment_resource_tracker_destroy(pigment, &pool->sets[i]->tracker);
        P_FREE(pigment, pool->sets[i]);
    }
    pool->set_count = 0;
}

static void destroy_descriptor_sets_immediate(Pigment* pigment, void* resource)
{
    PDescriptorSetBatch* batch = (PDescriptorSetBatch*) resource;
    vkFreeDescriptorSets(pigment->device->logical_device, batch->pool->pool, batch->count, batch->vk_sets);
    for(uint32_t i = 0; i < batch->count; i++)
    {
        pigment_resource_tracker_destroy(pigment, &batch->wrappers[i]->tracker);
        P_FREE(pigment, batch->wrappers[i]);
    }
    P_FREE(pigment, batch->vk_sets);
    P_FREE(pigment, batch->wrappers);
    P_FREE(pigment, batch);
}

void pigment_destroy_descriptor_sets(Pigment* pigment, PDescriptorSet** sets, uint32_t count)
{
    if(pigment == NULL || sets == NULL || count == 0)
    {
        return;
    }

    PDescriptorPool* pool = sets[0]->source_pool;
    if(!pool->allow_free_set)
    {
        PLOG_ERROR(pigment, "Source pool was not created with allow_free_set.");
        return;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        if(sets[i] == NULL || sets[i]->source_pool != pool)
        {
            PLOG_ERROR(pigment, "pigment_destroy_descriptor_sets: all sets must come from the same pool.");
            return;
        }
    }

    PDescriptorSetBatch* batch = P_NEW_FOR_OBJECT(pigment, batch);
    if(batch == NULL)
    {
        return;
    }
    batch->vk_sets  = P_NEW_ARRAY_FOR_OBJECT(pigment, batch->vk_sets, count);
    batch->wrappers = P_NEW_ARRAY_FOR_OBJECT(pigment, batch->wrappers, count);
    if(batch->vk_sets == NULL || batch->wrappers == NULL)
    {
        P_FREE(pigment, batch->vk_sets);
        P_FREE(pigment, batch->wrappers);
        P_FREE(pigment, batch);
        return;
    }
    batch->pool  = pool;
    batch->count = count;

    uint32_t queue_count      = pigment->device->queue_count;
    PResourceTracker combined = {0};
    if(pigment_resource_tracker_init(pigment, &combined) != PIGMENT_SUCCESS)
    {
        for(uint32_t i = 0; i < count; i++)
        {
            batch->vk_sets[i]  = sets[i]->set;
            batch->wrappers[i] = sets[i];
            pool_remove_set(pool, sets[i]);
        }
        pigment_defer_destroy(pigment, destroy_descriptor_sets_immediate, batch);
        return;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        _Atomic uint64_t* table = sets[i]->tracker.last_used;
        if(table != NULL)
        {
            for(uint32_t q = 0; q < queue_count; q++)
            {
                uint64_t value   = atomic_load_explicit(&table[q], memory_order_relaxed);
                uint64_t current = atomic_load_explicit(&combined.last_used[q], memory_order_relaxed);
                if(value > current)
                {
                    atomic_store_explicit(&combined.last_used[q], value, memory_order_relaxed);
                }
            }
        }
        batch->vk_sets[i]  = sets[i]->set;
        batch->wrappers[i] = sets[i];
        pool_remove_set(pool, sets[i]);
    }

    pigment_defer_destroy_tracked(pigment, destroy_descriptor_sets_immediate, batch, &combined);
    pigment_resource_tracker_destroy(pigment, &combined);
}

PResult pigment_create_descriptor_sets(Pigment* pigment, PDescriptorPool* pool, const PDescriptorSetAllocate* allocs, uint32_t count, PDescriptorSet** out_sets)
{
    if(pigment == NULL || pool == NULL || allocs == NULL || count == 0 || out_sets == NULL)
    {
        return PIGMENT_ERROR;
    }

    PResult status          = PIGMENT_ERROR;
    uint32_t temp_allocated = 0;
    PBool has_variable      = P_FALSE;

    P_STACK_OR_HEAP(VkDescriptorSetLayout, vk_layouts, count);
    P_STACK_OR_HEAP(uint32_t, variable_counts, count);
    P_STACK_OR_HEAP(VkDescriptorSet, vk_sets, count);
    P_STACK_OR_HEAP(PDescriptorSet*, temp_sets, count);
    if(vk_layouts == NULL || variable_counts == NULL || vk_sets == NULL || temp_sets == NULL)
    {
        status = PIGMENT_ERROR_OUT_OF_MEMORY;
        goto FREE;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        if(allocs[i].layout == NULL)
        {
            PLOG_ERROR(pigment, "pigment_create_descriptor_sets: allocs[%u].layout is NULL", i);
            goto FREE;
        }
        vk_layouts[i]      = allocs[i].layout->layout;
        variable_counts[i] = allocs[i].variable_count;
        if(allocs[i].variable_count > 0)
        {
            has_variable = P_TRUE;
        }

        temp_sets[i] = P_NEW_FOR_OBJECT(pigment, temp_sets[i]);
        if(temp_sets[i] == NULL)
        {
            status = PIGMENT_ERROR_OUT_OF_MEMORY;
            goto FREE;
        }

        if(pigment_resource_tracker_init(pigment, &temp_sets[i]->tracker) != PIGMENT_SUCCESS)
        {
            P_FREE(pigment, temp_sets[i]);
            temp_sets[i] = NULL;
            status       = PIGMENT_ERROR_OUT_OF_MEMORY;
            goto FREE;
        }
        temp_allocated++;
    }

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = count,
        .pDescriptorCounts  = variable_counts,
    };

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = pool->pool,
        .descriptorSetCount = count,
        .pSetLayouts        = vk_layouts,
        .pNext              = has_variable ? &variable_alloc_info : NULL,
    };

    VkResult result = vkAllocateDescriptorSets(pigment->device->logical_device, &alloc_info, vk_sets);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to allocate descriptor sets (count=%u, result: %d)", count, result);
        status = PIGMENT_ERROR_VULKAN;
        goto FREE;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        temp_sets[i]->set         = vk_sets[i];
        temp_sets[i]->source_pool = pool;
        set_object_name(pigment->device->logical_device, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t) vk_sets[i], allocs[i].name);

        if(pool_append_set(pigment, pool, temp_sets[i]) != PIGMENT_SUCCESS)
        {
            PLOG_ERROR(pigment, "Failed to append descriptor set to pool");
            for(uint32_t j = 0; j < i; j++)
            {
                pool_remove_set(pool, temp_sets[j]);
            }
            vkFreeDescriptorSets(pigment->device->logical_device, pool->pool, count, vk_sets);
            status = PIGMENT_ERROR_OUT_OF_MEMORY;
            goto FREE;
        }

        out_sets[i] = temp_sets[i];
    }

    temp_allocated = 0;

    status = PIGMENT_SUCCESS;

FREE:
    if(status != PIGMENT_SUCCESS)
    {
        for(uint32_t i = 0; i < temp_allocated; i++)
        {
            pigment_resource_tracker_destroy(pigment, &temp_sets[i]->tracker);
            P_FREE(pigment, temp_sets[i]);
        }
    }
    P_STACK_OR_HEAP_FREE(pigment, temp_sets);
    P_STACK_OR_HEAP_FREE(pigment, vk_sets);
    P_STACK_OR_HEAP_FREE(pigment, variable_counts);
    P_STACK_OR_HEAP_FREE(pigment, vk_layouts);
    return status;
}

void pigment_update_descriptors(Pigment* pigment, const PDescriptorWrite* writes, uint32_t write_count, const PDescriptorCopy* copies, uint32_t copy_count)
{
    if(pigment == NULL)
    {
        return;
    }
    if(write_count == 0 && copy_count == 0)
    {
        return;
    }
    if(write_count > 0 && writes == NULL)
    {
        return;
    }
    if(copy_count > 0 && copies == NULL)
    {
        return;
    }

    VkWriteDescriptorSet* vk_writes       = (write_count > 0) ? P_NEW_ARRAY_FOR_COMMAND(pigment, vk_writes, write_count) : NULL;
    VkDescriptorImageInfo** image_infos   = (write_count > 0) ? P_NEW_ARRAY_FOR_COMMAND(pigment, image_infos, write_count) : NULL;
    VkDescriptorBufferInfo** buffer_infos = (write_count > 0) ? P_NEW_ARRAY_FOR_COMMAND(pigment, buffer_infos, write_count) : NULL;
    VkCopyDescriptorSet* vk_copies        = (copy_count > 0) ? P_NEW_ARRAY_FOR_COMMAND(pigment, vk_copies, copy_count) : NULL;
    if((write_count > 0 && (vk_writes == NULL || image_infos == NULL || buffer_infos == NULL)) || (copy_count > 0 && vk_copies == NULL))
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
            case P_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            case P_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                {
                    image_infos[i] = P_NEW_ARRAY_FOR_COMMAND(pigment, image_infos[i], write->count);
                    if(image_infos[i] == NULL)
                    {
                        goto FREE;
                    }

                    for(uint32_t j = 0; j < write->count; j++)
                    {
                        const PDescriptorImageInfo* info = &write->image_infos[j];
                        image_infos[i][j].sampler        = (info->sampler != NULL) ? info->sampler->sampler : VK_NULL_HANDLE;

                        VkImageView vk_view = VK_NULL_HANDLE;
                        if(info->image != NULL)
                        {
                            PImageView* view = image_get_or_create_view(pigment, info->image, &(PImageViewDesc) {0});
                            if(view != NULL)
                            {
                                vk_view = view->view;
                            }
                        }

                        image_infos[i][j].imageView   = vk_view;
                        image_infos[i][j].imageLayout = resolve_image_layout(write->type, info->layout);
                    }

                    vk_writes[i].pImageInfo = image_infos[i];

                    break;
                }
            case P_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case P_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                {
                    buffer_infos[i] = P_NEW_ARRAY_FOR_COMMAND(pigment, buffer_infos[i], write->count);
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

    if(vk_copies != NULL)
    {
        for(uint32_t i = 0; i < copy_count; i++)
        {
            const PDescriptorCopy* copy = &copies[i];
            vk_copies[i]                = (VkCopyDescriptorSet) {
                .sType           = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
                .srcSet          = copy->src->set,
                .srcBinding      = copy->src_binding,
                .srcArrayElement = copy->src_array_element,
                .dstSet          = copy->dst->set,
                .dstBinding      = copy->dst_binding,
                .dstArrayElement = copy->dst_array_element,
                .descriptorCount = copy->count,
            };
        }
    }

    vkUpdateDescriptorSets(pigment->device->logical_device, write_count, vk_writes, copy_count, vk_copies);

FREE:
    if(image_infos != NULL)
    {
        for(uint32_t i = 0; i < write_count; i++)
        {
            P_FREE(pigment, image_infos[i]);
        }
        P_FREE(pigment, image_infos);
    }

    if(buffer_infos != NULL)
    {
        for(uint32_t i = 0; i < write_count; i++)
        {
            P_FREE(pigment, buffer_infos[i]);
        }
        P_FREE(pigment, buffer_infos);
    }
    P_FREE(pigment, vk_writes);
    P_FREE(pigment, vk_copies);
}

void pigment_cmd_bind_descriptor_sets(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline, uint32_t first_set, PDescriptorSet* const* sets, uint32_t set_count, const uint32_t* dynamic_offsets, uint32_t dynamic_offset_count)
{
    if(pigment == NULL || cmd == NULL || pipeline == NULL || sets == NULL || set_count == 0)
    {
        return;
    }

    P_STACK_OR_HEAP(VkDescriptorSet, vk_sets, set_count);
    if(vk_sets == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < set_count; i++)
    {
        if(sets[i] == NULL)
        {
            P_STACK_OR_HEAP_FREE(pigment, vk_sets);
            return;
        }
        pigment_cmd_use(pigment, cmd, &sets[i]->tracker);
        vk_sets[i] = sets[i]->set;
    }

    vkCmdBindDescriptorSets(cmd->buffer, (VkPipelineBindPoint) pipeline->bind_point, pipeline->layout->layout, first_set, set_count, vk_sets, dynamic_offset_count, dynamic_offsets);

    P_STACK_OR_HEAP_FREE(pigment, vk_sets);
}

static VkDescriptorType to_vk_descriptor_type(PDescriptorType type)
{
    switch(type)
    {
        case P_DESCRIPTOR_TYPE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case P_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case P_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
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
    if(flags & P_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT)
    {
        out |= VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    }
    return out;
}

static PResult pool_append_set(Pigment* pigment, PDescriptorPool* pool, PDescriptorSet* set)
{
    PResult res = P_ARRAY_RESERVE_OBJECT(pigment, pool->sets, pool->set_count, pool->set_capacity, 1, PIGMENT_DESCRIPTOR_POOL_INITIAL_CAPACITY);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
    }
    pool->sets[pool->set_count++] = set;

    return PIGMENT_SUCCESS;
}

static void pool_remove_set(PDescriptorPool* pool, PDescriptorSet* set)
{
    for(uint32_t i = 0; i < pool->set_count; i++)
    {
        if(pool->sets[i] == set)
        {
            pool->sets[i] = pool->sets[--pool->set_count];
            return;
        }
    }
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

VkDescriptorSet pigment_vk_descriptor_set(PDescriptorSet* set)
{
    return (set != NULL) ? set->set : VK_NULL_HANDLE;
}

VkDescriptorSetLayout pigment_vk_descriptor_set_layout(PDescriptorSetLayout* layout)
{
    return (layout != NULL) ? layout->layout : VK_NULL_HANDLE;
}

VkDescriptorPool pigment_vk_descriptor_pool(PDescriptorPool* pool)
{
    return (pool != NULL) ? pool->pool : VK_NULL_HANDLE;
}
