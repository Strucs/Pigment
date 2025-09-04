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

#include "descriptor.h"

#include "structs.h"

int create_descriptor_pool(PDescriptor* descriptor, PTextureList* textures, PSamplerList* samplers, PDevice* device, uint32_t descriptor_count);
int create_descriptor_sets(PDescriptor* descriptor, PBuffers* buffers, PTextureList* textures, PSamplerList* samplers, PDevice* device, uint32_t descriptor_count);

PDescriptor* create_descriptor(PTextureList* textures, PSamplerList* samplers, PDevice* device)
{
    PDescriptor* descriptor = malloc(sizeof(*descriptor));
    if(descriptor == NULL)
    {
        goto ERROR;
    }

    VkDescriptorSetLayoutBinding uniform_buffer_set_layout_binding = {
        .binding            = 0,
        .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount    = 1,
        .pImmutableSamplers = NULL,
        .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT
    };

    VkDescriptorSetLayoutBinding sampler_set_layout_binding = {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .descriptorCount = samplers->sampler_number,
        .pImmutableSamplers = NULL,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutBinding image_set_layout_binding = {
        .binding            = 2,
        .descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .descriptorCount    = textures->texture_number,
        .pImmutableSamplers = NULL,
        .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT
    };

    VkDescriptorSetLayoutBinding descriptor_set_layout_binding[] = {
        uniform_buffer_set_layout_binding, 
        sampler_set_layout_binding,
        image_set_layout_binding
    };

    VkDescriptorBindingFlagsEXT descriptor_binding_flags[]       = {
        0,
        0,
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT descriptor_set_layout_binding_flags = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT,
        .bindingCount  = sizeof(descriptor_binding_flags) / sizeof(descriptor_binding_flags[0]),
        .pBindingFlags = descriptor_binding_flags,
    };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = sizeof(descriptor_set_layout_binding) / sizeof(descriptor_set_layout_binding[0]),
        .pBindings    = descriptor_set_layout_binding,
        .pNext        = &descriptor_set_layout_binding_flags
    };

    if(vkCreateDescriptorSetLayout(device->logical_device, &layout_info, NULL, &descriptor->descriptor_set_layout) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create descriptor set layout!\n");
        goto ERROR;
    }

    return descriptor;

ERROR:
    perror("create_descriptor");
    free(descriptor);
    return NULL;
}

int update_descriptor(PDescriptor* descriptor, PBuffers* buffers, PTextureList* textures, PSamplerList* samplers, PDevice* device, uint32_t descriptor_count)
{
    if(create_descriptor_pool(descriptor, textures, samplers, device, descriptor_count))
    {
        goto ERROR;
    }
    if(create_descriptor_sets(descriptor, buffers, textures, samplers, device, descriptor_count))
    {
        goto ERROR;
    }

    return PIGMENT_SUCCESS;

ERROR:
    perror("update_descriptor");
    return PIGMENT_ERROR;
}

void destroy_descriptor(PDescriptor* descriptor, PDevice* device)
{
    if(descriptor == NULL)
    {
        return;
    }
    vkDestroyDescriptorPool(device->logical_device, descriptor->descriptor_pool, NULL);
    vkDestroyDescriptorSetLayout(device->logical_device, descriptor->descriptor_set_layout, NULL);
}

VkDescriptorPoolSize create_descriptor_pool_size(VkDescriptorType type, uint32_t descriptor_count)
{
    VkDescriptorPoolSize pool_size = {
        .type            = type,
        .descriptorCount = descriptor_count
    };
    return pool_size;
}

int create_descriptor_pool(PDescriptor* descriptor, PTextureList* textures, PSamplerList* samplers, PDevice* device, uint32_t descriptor_count)
{
    VkDescriptorPoolSize pool_sizes[] = {
        create_descriptor_pool_size(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptor_count),
        create_descriptor_pool_size(VK_DESCRIPTOR_TYPE_SAMPLER, descriptor_count * samplers->sampler_number),
        create_descriptor_pool_size(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptor_count * textures->texture_number),
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]),
        .pPoolSizes    = pool_sizes,
        .maxSets       = descriptor_count
    };

    if(vkCreateDescriptorPool(device->logical_device, &pool_info, NULL, &(descriptor->descriptor_pool)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create descriptor pool!\n");
        return PIGMENT_ERROR;
    }

    return PIGMENT_SUCCESS;
}

int create_descriptor_sets(PDescriptor* descriptor, PBuffers* buffers, PTextureList* textures, PSamplerList* samplers, PDevice* device, uint32_t descriptor_count)
{
    VkDescriptorSetLayout* layouts = NULL;
    uint32_t* variable_desciptor_counts = NULL;
    VkDescriptorImageInfo* texture_infos = NULL;
    VkDescriptorImageInfo* sampler_infos = NULL;

    layouts = malloc(descriptor_count * sizeof(*layouts));
    if(layouts == NULL)
    {
        goto ERROR;
    }

    variable_desciptor_counts = malloc(descriptor_count * sizeof(*variable_desciptor_counts));
    if(variable_desciptor_counts == NULL)
    {
        goto ERROR;
    }

    for(size_t i = 0; i < descriptor_count; i++)
    {
        layouts[i] = descriptor->descriptor_set_layout;
        variable_desciptor_counts[i] = textures->texture_number;
    }

    VkDescriptorSetVariableDescriptorCountAllocateInfoEXT variable_desciptor_counts_alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
        .descriptorSetCount = descriptor_count,
        .pDescriptorCounts  = variable_desciptor_counts
    };

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = descriptor->descriptor_pool,
        .descriptorSetCount = descriptor_count,
        .pSetLayouts        = layouts,
        .pNext              = &variable_desciptor_counts_alloc_info
    };

    descriptor->descriptor_sets = malloc(descriptor_count * sizeof(*descriptor->descriptor_sets));
    if(descriptor->descriptor_sets == NULL)
    {
        goto ERROR;
    }

    texture_infos = malloc(textures->texture_number * sizeof(*texture_infos));
    if(texture_infos == NULL)
    {
        goto ERROR;
    }

    sampler_infos = malloc(samplers->sampler_number * sizeof(*sampler_infos));
    if(sampler_infos == NULL)
    {
        goto ERROR;
    }

    if(vkAllocateDescriptorSets(device->logical_device, &alloc_info, descriptor->descriptor_sets) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to allocate descriptor sets!\n");
        goto ERROR;
    }

    for(size_t i = 0; i < descriptor_count; i++)
    {

        VkWriteDescriptorSet descriptor_set_writes[3] = {0};

        VkDescriptorBufferInfo buffer_info = {
            .buffer = buffers->uniform_buffers[i],
            .offset = 0,
            .range  = sizeof(UniformBufferObject)
        };

        uint32_t descriptor_set_write_number = sizeof(descriptor_set_writes) / sizeof(descriptor_set_writes[0]);

        descriptor_set_writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_set_writes[0].dstSet          = descriptor->descriptor_sets[i];
        descriptor_set_writes[0].dstBinding      = 0;
        descriptor_set_writes[0].dstArrayElement = 0;
        descriptor_set_writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_set_writes[0].descriptorCount = 1;
        descriptor_set_writes[0].pBufferInfo     = &buffer_info;

        for(size_t s = 0; s < samplers->sampler_number; s++)
        {
            sampler_infos[s].sampler     = samplers->samplers[s].sampler;
            sampler_infos[s].imageView   = NULL;
            sampler_infos[s].imageLayout = 0;
        }

        descriptor_set_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_set_writes[1].dstSet = descriptor->descriptor_sets[i];
        descriptor_set_writes[1].dstBinding = 1;
        descriptor_set_writes[1].dstArrayElement = 0;
        descriptor_set_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        descriptor_set_writes[1].descriptorCount = samplers->sampler_number;
        descriptor_set_writes[1].pImageInfo = sampler_infos;

        for(size_t j = 0; j < textures->texture_number; j++)
        {
            texture_infos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            texture_infos[j].imageView   = textures->textures[j].image_view;
        }

        descriptor_set_writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_set_writes[2].dstSet          = descriptor->descriptor_sets[i];
        descriptor_set_writes[2].dstBinding      = 2;
        descriptor_set_writes[2].dstArrayElement = 0;
        descriptor_set_writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        descriptor_set_writes[2].descriptorCount = textures->texture_number;
        descriptor_set_writes[2].pImageInfo      = texture_infos;

        vkUpdateDescriptorSets(device->logical_device, descriptor_set_write_number, descriptor_set_writes, 0, NULL);
    }

    free(sampler_infos);
    free(texture_infos);
    free(variable_desciptor_counts);
    free(layouts);
    return PIGMENT_SUCCESS;

ERROR:
    perror("create_descriptor_sets");
    free(sampler_infos);
    free(texture_infos);
    free(descriptor->descriptor_sets);
    free(variable_desciptor_counts);
    free(layouts);

    return PIGMENT_ERROR;
}
