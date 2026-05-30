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

#include "pipeline.h"

#include "deletion.h"

#include "internal.h"
#include "structs.h"

static PPipelineBuild* pipeline_build_from_desc(Pigment* pigment, const PPipelineDesc* desc);
static void pipeline_build_destroy(Pigment* pigment, PPipelineBuild* build);
static VkShaderModule create_shader_module(Pigment* pigment, const uint32_t* code, uint32_t shader_size);
static PResult configure_shader_stage_create_info(Pigment* pigment, VkShaderModule shader_module, VkShaderStageFlagBits stage, const char* entry_point, const PSpecializationInfo* specialization, VkSpecializationMapEntry** out_vk_entries, VkSpecializationInfo* out_vk_spec_info, VkPipelineShaderStageCreateInfo* out_stage);
static VkPipelineVertexInputStateCreateInfo configure_vertex_input_state_create_info(const VkVertexInputBindingDescription* bindings, uint32_t binding_count, const VkVertexInputAttributeDescription* attributes, uint32_t attribute_count);
static VkPipelineInputAssemblyStateCreateInfo configure_input_assembly_state_create_info(PTopology topology);
static VkPipelineViewportStateCreateInfo configure_viewport_state_create_info(void);
static VkPipelineRasterizationStateCreateInfo configure_rasterizer_state_create_info(PPolygonMode polygon_mode);
static VkPipelineMultisampleStateCreateInfo configure_multisampling_state_create_info(PSampleCount sample_count, PBool sample_shading_enable, float min_sample_shading);
static VkPipelineDepthStencilStateCreateInfo configure_depth_stencil_state_create_info(void);
static VkPipelineColorBlendAttachmentState configure_color_blend_attachment_state_create_info(PBlendMode mode);
static VkPipelineColorBlendStateCreateInfo configure_color_blend_state_create_info(VkPipelineColorBlendAttachmentState* attachments, uint32_t attachment_count);
static VkPipelineDynamicStateCreateInfo configure_dynamic_state_create_info(VkDynamicState* dynamic_states, uint32_t dynamic_states_size);
static void destroy_pipeline_immediate(Pigment* pigment, void* resource);
static void destroy_layout_immediate(Pigment* pigment, void* resource);
static PResult build_specialization(Pigment* pigment, const PSpecializationInfo* in, VkSpecializationMapEntry** out_entries, VkSpecializationInfo* out_info);

static PPipelineBuild* pipeline_build_from_desc(Pigment* pigment, const PPipelineDesc* desc)
{
    if(pigment == NULL || desc == NULL || desc->vertex_shader == NULL)
    {
        return NULL;
    }

    PPipelineBuild* build = P_NEW_FOR_OBJECT(pigment, build);
    if(build == NULL)
    {
        return NULL;
    }

    build->vertex_module = create_shader_module(pigment, desc->vertex_shader, desc->vertex_shader_size);
    if(build->vertex_module == NULL)
    {
        goto ERROR;
    }
    if(configure_shader_stage_create_info(pigment, build->vertex_module, VK_SHADER_STAGE_VERTEX_BIT, "main", desc->vertex_specialization, &build->vertex_spec_entries, &build->vertex_spec_info, &build->shader_stages[build->shader_stage_count++]) != PIGMENT_SUCCESS)
    {
        goto ERROR;
    }

    if(desc->fragment_shader != NULL)
    {
        build->fragment_module = create_shader_module(pigment, desc->fragment_shader, desc->fragment_shader_size);
        if(build->fragment_module == NULL)
        {
            goto ERROR;
        }
        if(configure_shader_stage_create_info(pigment, build->fragment_module, VK_SHADER_STAGE_FRAGMENT_BIT, "main", desc->fragment_specialization, &build->fragment_spec_entries, &build->fragment_spec_info, &build->shader_stages[build->shader_stage_count++]) != PIGMENT_SUCCESS)
        {
            goto ERROR;
        }
    }

    if(desc->blend_modes != NULL && desc->blend_mode_count != desc->color_format_count)
    {
        PLOG_ERROR(pigment, "pigment_pipeline_build_from_desc: blend_mode_count (%u) must equal color_format_count (%u) when blend_modes is not NULL.", desc->blend_mode_count, desc->color_format_count);
        goto ERROR;
    }

    build->color_format_count = desc->color_format_count;
    if(desc->color_format_count > 0)
    {
        build->color_formats = P_NEW_ARRAY_FOR_OBJECT(pigment, build->color_formats, desc->color_format_count);
        if(build->color_formats == NULL)
        {
            goto ERROR;
        }
        for(uint32_t i = 0; i < desc->color_format_count; i++)
        {
            build->color_formats[i] = (VkFormat) desc->color_formats[i];
        }

        build->blend_attachment_count = desc->color_format_count;
        build->blend_attachments      = P_NEW_ARRAY_FOR_OBJECT(pigment, build->blend_attachments, desc->color_format_count);
        if(build->blend_attachments == NULL)
        {
            goto ERROR;
        }
        for(uint32_t i = 0; i < desc->color_format_count; i++)
        {
            PBlendMode mode             = (desc->blend_modes != NULL) ? desc->blend_modes[i] : P_BLEND_MODE_OPAQUE;
            build->blend_attachments[i] = configure_color_blend_attachment_state_create_info(mode);
        }
    }

    if(desc->vertex_binding_count > 0 && desc->vertex_bindings != NULL)
    {
        build->vk_vertex_bindings = P_NEW_ARRAY_FOR_OBJECT(pigment, build->vk_vertex_bindings, desc->vertex_binding_count);
        if(build->vk_vertex_bindings == NULL)
        {
            goto ERROR;
        }
        for(uint32_t i = 0; i < desc->vertex_binding_count; i++)
        {
            build->vk_vertex_bindings[i] = (VkVertexInputBindingDescription) {
                .binding   = desc->vertex_bindings[i].binding,
                .stride    = desc->vertex_bindings[i].stride,
                .inputRate = (desc->vertex_bindings[i].input_rate == P_VERTEX_INPUT_RATE_INSTANCE) ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX,
            };
        }
        build->vk_vertex_binding_count = desc->vertex_binding_count;
    }

    if(desc->vertex_attribute_count > 0 && desc->vertex_attributes != NULL)
    {
        build->vk_vertex_attributes = P_NEW_ARRAY_FOR_OBJECT(pigment, build->vk_vertex_attributes, desc->vertex_attribute_count);
        if(build->vk_vertex_attributes == NULL)
        {
            goto ERROR;
        }
        for(uint32_t i = 0; i < desc->vertex_attribute_count; i++)
        {
            build->vk_vertex_attributes[i] = (VkVertexInputAttributeDescription) {
                .location = desc->vertex_attributes[i].location,
                .binding  = desc->vertex_attributes[i].binding,
                .format   = (VkFormat) desc->vertex_attributes[i].format,
                .offset   = desc->vertex_attributes[i].offset,
            };
        }
        build->vk_vertex_attribute_count = desc->vertex_attribute_count;
    }

    build->vertex_input   = configure_vertex_input_state_create_info(build->vk_vertex_bindings, build->vk_vertex_binding_count, build->vk_vertex_attributes, build->vk_vertex_attribute_count);
    build->input_assembly = configure_input_assembly_state_create_info(desc->topology);
    build->viewport       = configure_viewport_state_create_info();
    build->rasterizer     = configure_rasterizer_state_create_info(desc->polygon_mode);
    build->multisample    = configure_multisampling_state_create_info(desc->sample_count != 0 ? desc->sample_count : P_SAMPLE_COUNT_1, desc->sample_shading_enable, desc->min_sample_shading);
    build->depth_stencil  = configure_depth_stencil_state_create_info();
    build->color_blend    = configure_color_blend_state_create_info(build->blend_attachments, build->blend_attachment_count);

    static const VkDynamicState base_dynamic_state_list[] = {
        VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
        VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
        VK_DYNAMIC_STATE_STENCIL_OP,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
        VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE
    };
    const uint32_t base_count = (uint32_t) (sizeof(base_dynamic_state_list) / sizeof(base_dynamic_state_list[0]));

    // Optional dynamic states depending on optional device features.
    const uint32_t optional_count = pigment->device->features[P_FEATURE_DEPTH_BOUNDS_TEST] ? 2 : 0;

    build->dynamic_state_count = base_count + optional_count;
    build->dynamic_state_list  = P_NEW_ARRAY_FOR_OBJECT(pigment, build->dynamic_state_list, build->dynamic_state_count);
    if(build->dynamic_state_list == NULL)
    {
        goto ERROR;
    }
    memcpy(build->dynamic_state_list, base_dynamic_state_list, sizeof(base_dynamic_state_list));
    if(pigment->device->features[P_FEATURE_DEPTH_BOUNDS_TEST])
    {
        build->dynamic_state_list[base_count + 0] = VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE;
        build->dynamic_state_list[base_count + 1] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;
    }
    build->dynamic = configure_dynamic_state_create_info(build->dynamic_state_list, build->dynamic_state_count);

    if(desc->layout == NULL)
    {
        PLOG_ERROR(pigment, "PPipelineDesc.layout is NULL");
        goto ERROR;
    }
    build->layout = desc->layout;
    build->name   = desc->name;

    build->rendering = (VkPipelineRenderingCreateInfoKHR) {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .viewMask                = desc->view_mask,
        .colorAttachmentCount    = build->color_format_count,
        .pColorAttachmentFormats = build->color_formats,
        .depthAttachmentFormat   = format_has_depth(desc->depth_format) ? (VkFormat) desc->depth_format : VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = format_has_stencil(desc->depth_format) ? (VkFormat) desc->depth_format : VK_FORMAT_UNDEFINED,
    };

    return build;

ERROR:
    pipeline_build_destroy(pigment, build);
    return NULL;
}

static void pipeline_build_destroy(Pigment* pigment, PPipelineBuild* build)
{
    if(pigment == NULL || build == NULL)
    {
        return;
    }

    VkDevice device = pigment->device->logical_device;

    if(build->vertex_module != NULL)
    {
        vkDestroyShaderModule(device, build->vertex_module, &pigment->vk_alloc);
    }
    if(build->fragment_module != NULL)
    {
        vkDestroyShaderModule(device, build->fragment_module, &pigment->vk_alloc);
    }

    P_FREE(pigment, build->color_formats);
    P_FREE(pigment, build->blend_attachments);
    P_FREE(pigment, build->dynamic_state_list);
    P_FREE(pigment, build->vertex_spec_entries);
    P_FREE(pigment, build->fragment_spec_entries);
    P_FREE(pigment, build->vk_vertex_bindings);
    P_FREE(pigment, build->vk_vertex_attributes);
    P_FREE(pigment, build);
}

static PResult build_specialization(Pigment* pigment, const PSpecializationInfo* in, VkSpecializationMapEntry** out_entries, VkSpecializationInfo* out_info)
{
    *out_entries = P_NEW_ARRAY_FOR_OBJECT(pigment, *out_entries, in->entry_count);
    if(*out_entries == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    for(uint32_t e = 0; e < in->entry_count; e++)
    {
        (*out_entries)[e] = (VkSpecializationMapEntry) {
            .constantID = in->entries[e].constant_id,
            .offset     = in->entries[e].offset,
            .size       = (size_t) in->entries[e].size,
        };
    }

    *out_info = (VkSpecializationInfo) {
        .mapEntryCount = in->entry_count,
        .pMapEntries   = *out_entries,
        .dataSize      = (size_t) in->data_size,
        .pData         = in->data,
    };

    return PIGMENT_SUCCESS;
}

PResult pigment_create_graphic_pipelines(Pigment* pigment, PPipelineCache* cache, const PPipelineDesc* descs, uint32_t count, PPipeline** out)
{
    if(pigment == NULL || descs == NULL || out == NULL || count == 0)
    {
        return PIGMENT_ERROR;
    }

    PResult status               = PIGMENT_ERROR_OUT_OF_MEMORY;
    uint32_t builds_done         = 0;
    uint32_t temp_pipelines_done = 0;

    P_STACK_OR_HEAP(PPipelineBuild*, builds, count);
    P_STACK_OR_HEAP(VkGraphicsPipelineCreateInfo, pipeline_create_infos, count);
    P_STACK_OR_HEAP(VkPipeline, vk_pipelines, count);
    P_STACK_OR_HEAP(PPipeline*, temp_pipelines, count);
    if(builds == NULL || pipeline_create_infos == NULL || vk_pipelines == NULL || temp_pipelines == NULL)
    {
        goto FREE;
    }

    VkDevice device = pigment->device->logical_device;

    for(uint32_t i = 0; i < count; i++)
    {
        builds[i] = pipeline_build_from_desc(pigment, &descs[i]);
        if(builds[i] == NULL)
        {
            goto FREE;
        }
        builds_done++;

        temp_pipelines[i] = P_NEW_FOR_OBJECT(pigment, temp_pipelines[i]);
        if(temp_pipelines[i] == NULL)
        {
            goto FREE;
        }

        if(pigment_resource_tracker_init(pigment, &temp_pipelines[i]->tracker) != PIGMENT_SUCCESS)
        {
            P_FREE(pigment, temp_pipelines[i]);
            temp_pipelines[i] = NULL;
            goto FREE;
        }

        temp_pipelines_done++;

        pipeline_create_infos[i] = (VkGraphicsPipelineCreateInfo) {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext               = &builds[i]->rendering,
            .stageCount          = builds[i]->shader_stage_count,
            .pStages             = builds[i]->shader_stages,
            .pVertexInputState   = &builds[i]->vertex_input,
            .pInputAssemblyState = &builds[i]->input_assembly,
            .pViewportState      = &builds[i]->viewport,
            .pRasterizationState = &builds[i]->rasterizer,
            .pMultisampleState   = &builds[i]->multisample,
            .pDepthStencilState  = &builds[i]->depth_stencil,
            .pColorBlendState    = &builds[i]->color_blend,
            .pDynamicState       = &builds[i]->dynamic,
            .layout              = builds[i]->layout->layout,
            .renderPass          = VK_NULL_HANDLE,
            .basePipelineHandle  = VK_NULL_HANDLE,
        };
    }

    status                   = PIGMENT_ERROR_VULKAN;
    VkPipelineCache vk_cache = (cache != NULL) ? cache->cache : VK_NULL_HANDLE;
    VkResult result          = vkCreateGraphicsPipelines(device, vk_cache, count, pipeline_create_infos, &pigment->vk_alloc, vk_pipelines);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create graphics pipelines! (result: %d)", result);
        for(uint32_t i = 0; i < count; i++)
        {
            if(vk_pipelines[i] != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, vk_pipelines[i], &pigment->vk_alloc);
            }
        }
        goto FREE;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        temp_pipelines[i]->pipeline   = vk_pipelines[i];
        temp_pipelines[i]->layout     = builds[i]->layout;
        temp_pipelines[i]->bind_point = P_PIPELINE_BIND_POINT_GRAPHICS;
        out[i]                        = temp_pipelines[i];

        set_object_name(device, VK_OBJECT_TYPE_PIPELINE, (uint64_t) vk_pipelines[i], builds[i]->name);
    }

    temp_pipelines_done = 0;
    status              = PIGMENT_SUCCESS;

FREE:
    if(status != PIGMENT_SUCCESS)
    {
        for(uint32_t i = 0; i < temp_pipelines_done; i++)
        {
            pigment_resource_tracker_destroy(pigment, &temp_pipelines[i]->tracker);
            P_FREE(pigment, temp_pipelines[i]);
        }
    }
    for(uint32_t i = 0; i < builds_done; i++)
    {
        pipeline_build_destroy(pigment, builds[i]);
    }
    P_STACK_OR_HEAP_FREE(pigment, temp_pipelines);
    P_STACK_OR_HEAP_FREE(pigment, vk_pipelines);
    P_STACK_OR_HEAP_FREE(pigment, pipeline_create_infos);
    P_STACK_OR_HEAP_FREE(pigment, builds);

    return status;
}

PResult pigment_create_compute_pipelines(Pigment* pigment, PPipelineCache* cache, const PComputePipelineDesc* descs, uint32_t count, PPipeline** out)
{
    if(pigment == NULL || descs == NULL || out == NULL || count == 0)
    {
        return PIGMENT_ERROR;
    }

    PResult status               = PIGMENT_ERROR_OUT_OF_MEMORY;
    uint32_t modules_done        = 0;
    uint32_t temp_pipelines_done = 0;

    P_STACK_OR_HEAP(VkShaderModule, modules, count);
    P_STACK_OR_HEAP(VkSpecializationMapEntry*, spec_entry_arrays, count);
    P_STACK_OR_HEAP(VkSpecializationInfo, spec_infos, count);
    P_STACK_OR_HEAP(VkComputePipelineCreateInfo, pipeline_create_infos, count);
    P_STACK_OR_HEAP(VkPipeline, vk_pipelines, count);
    P_STACK_OR_HEAP(PPipeline*, temp_pipelines, count);
    if(modules == NULL || spec_entry_arrays == NULL || spec_infos == NULL || pipeline_create_infos == NULL || vk_pipelines == NULL || temp_pipelines == NULL)
    {
        goto FREE;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        modules[i]           = VK_NULL_HANDLE;
        spec_entry_arrays[i] = NULL;
        temp_pipelines[i]    = NULL;
    }

    VkDevice device = pigment->device->logical_device;

    for(uint32_t i = 0; i < count; i++)
    {
        const PComputePipelineDesc* desc = &descs[i];
        if(desc->layout == NULL || desc->compute_shader == NULL)
        {
            PLOG_ERROR(pigment, "pigment_create_compute_pipelines: desc[%u] missing layout or compute_shader", i);
            status = PIGMENT_ERROR;
            goto FREE;
        }

        modules[i] = create_shader_module(pigment, desc->compute_shader, desc->compute_shader_size);
        if(modules[i] == VK_NULL_HANDLE)
        {
            status = PIGMENT_ERROR_VULKAN;
            goto FREE;
        }
        modules_done++;

        temp_pipelines[i] = P_NEW_FOR_OBJECT(pigment, temp_pipelines[i]);
        if(temp_pipelines[i] == NULL)
        {
            goto FREE;
        }

        if(pigment_resource_tracker_init(pigment, &temp_pipelines[i]->tracker) != PIGMENT_SUCCESS)
        {
            P_FREE(pigment, temp_pipelines[i]);
            temp_pipelines[i] = NULL;
            goto FREE;
        }
        temp_pipelines_done++;

        pipeline_create_infos[i] = (VkComputePipelineCreateInfo) {
            .sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .layout             = desc->layout->layout,
            .basePipelineHandle = VK_NULL_HANDLE,
        };

        if(configure_shader_stage_create_info(pigment, modules[i], VK_SHADER_STAGE_COMPUTE_BIT, "main", desc->specialization, &spec_entry_arrays[i], &spec_infos[i], &pipeline_create_infos[i].stage) != PIGMENT_SUCCESS)
        {
            goto FREE;
        }
    }

    status                   = PIGMENT_ERROR_VULKAN;
    VkPipelineCache vk_cache = (cache != NULL) ? cache->cache : VK_NULL_HANDLE;
    VkResult result          = vkCreateComputePipelines(device, vk_cache, count, pipeline_create_infos, &pigment->vk_alloc, vk_pipelines);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create compute pipelines! (result: %d)", result);
        for(uint32_t i = 0; i < count; i++)
        {
            if(vk_pipelines[i] != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, vk_pipelines[i], &pigment->vk_alloc);
            }
        }
        goto FREE;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        temp_pipelines[i]->pipeline   = vk_pipelines[i];
        temp_pipelines[i]->layout     = descs[i].layout;
        temp_pipelines[i]->bind_point = P_PIPELINE_BIND_POINT_COMPUTE;
        out[i]                        = temp_pipelines[i];

        set_object_name(device, VK_OBJECT_TYPE_PIPELINE, (uint64_t) vk_pipelines[i], descs[i].name);
    }

    temp_pipelines_done = 0;
    status              = PIGMENT_SUCCESS;

FREE:
    if(status != PIGMENT_SUCCESS)
    {
        for(uint32_t i = 0; i < temp_pipelines_done; i++)
        {
            if(temp_pipelines[i] != NULL)
            {
                pigment_resource_tracker_destroy(pigment, &temp_pipelines[i]->tracker);
                P_FREE(pigment, temp_pipelines[i]);
            }
        }
    }

    for(uint32_t i = 0; i < count; i++)
    {
        if(spec_entry_arrays[i] != NULL)
        {
            P_FREE(pigment, spec_entry_arrays[i]);
        }
    }

    for(uint32_t i = 0; i < modules_done; i++)
    {
        if(modules[i] != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(pigment->device->logical_device, modules[i], &pigment->vk_alloc);
        }
    }

    P_STACK_OR_HEAP_FREE(pigment, temp_pipelines);
    P_STACK_OR_HEAP_FREE(pigment, vk_pipelines);
    P_STACK_OR_HEAP_FREE(pigment, pipeline_create_infos);
    P_STACK_OR_HEAP_FREE(pigment, spec_infos);
    P_STACK_OR_HEAP_FREE(pigment, spec_entry_arrays);
    P_STACK_OR_HEAP_FREE(pigment, modules);

    return status;
}

void pigment_destroy_pipeline(Pigment* pigment, PPipeline* pipeline)
{
    if(pigment == NULL || pipeline == NULL)
    {
        return;
    }

    pigment_defer_destroy_tracked(pigment, destroy_pipeline_immediate, pipeline, &pipeline->tracker);
}

static void destroy_pipeline_immediate(Pigment* pigment, void* resource)
{
    PPipeline* pipeline = (PPipeline*) resource;
    if(pipeline->pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(pigment->device->logical_device, pipeline->pipeline, &pigment->vk_alloc);
    }
    pigment_resource_tracker_destroy(pigment, &pipeline->tracker);
    P_FREE(pigment, pipeline);
}

void pigment_bind_pipeline(Pigment* pigment, PCommandBuffer* cmd, PPipeline* pipeline)
{
    if(pigment == NULL || cmd == NULL || pipeline == NULL)
    {
        return;
    }

    pigment_cmd_use(pigment, cmd, &pipeline->tracker);

    vkCmdBindPipeline(cmd->buffer, (VkPipelineBindPoint) pipeline->bind_point, pipeline->pipeline);

    if(pipeline->bind_point != P_PIPELINE_BIND_POINT_GRAPHICS)
    {
        return;
    }

    vkCmdSetCullMode(cmd->buffer, VK_CULL_MODE_BACK_BIT);
    vkCmdSetFrontFace(cmd->buffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetDepthTestEnable(cmd->buffer, VK_TRUE);
    vkCmdSetDepthWriteEnable(cmd->buffer, VK_TRUE);
    vkCmdSetDepthCompareOp(cmd->buffer, VK_COMPARE_OP_GREATER);
    vkCmdSetStencilTestEnable(cmd->buffer, VK_FALSE);
    vkCmdSetStencilOp(cmd->buffer, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
    vkCmdSetDepthBiasEnable(cmd->buffer, VK_FALSE);
    vkCmdSetRasterizerDiscardEnable(cmd->buffer, VK_FALSE);
    if(pigment->device->features[P_FEATURE_DEPTH_BOUNDS_TEST])
    {
        vkCmdSetDepthBoundsTestEnable(cmd->buffer, VK_FALSE);
        vkCmdSetDepthBounds(cmd->buffer, 0.0f, 1.0f);
    }
}

static VkShaderModule create_shader_module(Pigment* pigment, const uint32_t* code, uint32_t shader_size)
{
    VkShaderModule shader_module;

    VkShaderModuleCreateInfo create_info = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_size,
        .pCode    = code
    };

    if(vkCreateShaderModule(pigment->device->logical_device, &create_info, &pigment->vk_alloc, &shader_module) != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create shader module!");
        return NULL;
    }

    return shader_module;
}

static PResult configure_shader_stage_create_info(Pigment* pigment, VkShaderModule shader_module, VkShaderStageFlagBits stage, const char* entry_point, const PSpecializationInfo* specialization, VkSpecializationMapEntry** out_vk_entries, VkSpecializationInfo* out_vk_spec_info, VkPipelineShaderStageCreateInfo* out_stage)
{
    *out_stage = (VkPipelineShaderStageCreateInfo) {
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = stage,
        .module = shader_module,
        .pName  = entry_point,
    };

    if(specialization != NULL && specialization->entry_count > 0)
    {
        if(build_specialization(pigment, specialization, out_vk_entries, out_vk_spec_info) != PIGMENT_SUCCESS)
        {
            return PIGMENT_ERROR_OUT_OF_MEMORY;
        }
        out_stage->pSpecializationInfo = out_vk_spec_info;
    }

    return PIGMENT_SUCCESS;
}

static VkPipelineVertexInputStateCreateInfo configure_vertex_input_state_create_info(const VkVertexInputBindingDescription* bindings, uint32_t binding_count, const VkVertexInputAttributeDescription* attributes, uint32_t attribute_count)
{
    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = binding_count,
        .pVertexBindingDescriptions      = bindings,
        .vertexAttributeDescriptionCount = attribute_count,
        .pVertexAttributeDescriptions    = attributes,
    };

    return vertex_input_state_create_info;
}

static VkPipelineInputAssemblyStateCreateInfo configure_input_assembly_state_create_info(PTopology topology)
{
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = (VkPrimitiveTopology) topology,
        .primitiveRestartEnable = VK_FALSE,
    };

    return input_assembly_state_create_info;
}

static VkPipelineViewportStateCreateInfo configure_viewport_state_create_info(void)
{
    // viewportCount / scissorCount are dynamic via VK_DYNAMIC_STATE_*_WITH_COUNT.
    VkPipelineViewportStateCreateInfo viewport_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    };

    return viewport_state_create_info;
}

static VkPipelineRasterizationStateCreateInfo configure_rasterizer_state_create_info(PPolygonMode polygon_mode)
{
    VkPipelineRasterizationStateCreateInfo rasterizer_state_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,    // placeholder
        .polygonMode             = (VkPolygonMode) polygon_mode,
        .lineWidth               = 1.0f,
        .cullMode                = VK_CULL_MODE_BACK_BIT,              // placeholder
        .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,    // placeholder
        .depthBiasEnable         = VK_FALSE,                           // placeholder
    };

    return rasterizer_state_create_info;
}

static VkPipelineMultisampleStateCreateInfo configure_multisampling_state_create_info(PSampleCount sample_count, PBool sample_shading_enable, float min_sample_shading)
{
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    switch(sample_count)
    {
        case P_SAMPLE_COUNT_1:
            samples = VK_SAMPLE_COUNT_1_BIT;
            break;
        case P_SAMPLE_COUNT_2:
            samples = VK_SAMPLE_COUNT_2_BIT;
            break;
        case P_SAMPLE_COUNT_4:
            samples = VK_SAMPLE_COUNT_4_BIT;
            break;
        case P_SAMPLE_COUNT_8:
            samples = VK_SAMPLE_COUNT_8_BIT;
            break;
        case P_SAMPLE_COUNT_16:
            samples = VK_SAMPLE_COUNT_16_BIT;
            break;
        case P_SAMPLE_COUNT_32:
            samples = VK_SAMPLE_COUNT_32_BIT;
            break;
        case P_SAMPLE_COUNT_64:
            samples = VK_SAMPLE_COUNT_64_BIT;
            break;
    }

    VkPipelineMultisampleStateCreateInfo multisampling_state_create_info = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = samples,
        .sampleShadingEnable  = sample_shading_enable ? VK_TRUE : VK_FALSE,
        .minSampleShading     = sample_shading_enable ? min_sample_shading : 0.0f,
    };

    return multisampling_state_create_info;
}

static VkPipelineDepthStencilStateCreateInfo configure_depth_stencil_state_create_info(void)
{
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_TRUE,                  // placeholder
        .depthWriteEnable      = VK_TRUE,                  // placeholder
        .depthCompareOp        = VK_COMPARE_OP_GREATER,    // placeholder
        .depthBoundsTestEnable = VK_FALSE,                 // placeholder
        .stencilTestEnable     = VK_FALSE,                 // placeholder
    };

    return depth_stencil_state_create_info;
}

static VkPipelineColorBlendAttachmentState configure_color_blend_attachment_state_create_info(PBlendMode mode)
{
    VkPipelineColorBlendAttachmentState color_blend_attachment_state_create_info = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable    = VK_FALSE
    };

    switch(mode)
    {
        case P_BLEND_MODE_OPAQUE:
            color_blend_attachment_state_create_info.blendEnable = VK_FALSE;
            break;
        case P_BLEND_MODE_ALPHA:
            color_blend_attachment_state_create_info.blendEnable         = VK_TRUE;
            color_blend_attachment_state_create_info.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            color_blend_attachment_state_create_info.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_blend_attachment_state_create_info.colorBlendOp        = VK_BLEND_OP_ADD;
            color_blend_attachment_state_create_info.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            color_blend_attachment_state_create_info.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_blend_attachment_state_create_info.alphaBlendOp        = VK_BLEND_OP_ADD;
            break;
        case P_BLEND_MODE_PREMULTIPLIED_ALPHA:
            color_blend_attachment_state_create_info.blendEnable         = VK_TRUE;
            color_blend_attachment_state_create_info.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            color_blend_attachment_state_create_info.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_blend_attachment_state_create_info.colorBlendOp        = VK_BLEND_OP_ADD;
            color_blend_attachment_state_create_info.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            color_blend_attachment_state_create_info.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_blend_attachment_state_create_info.alphaBlendOp        = VK_BLEND_OP_ADD;
            break;
        case P_BLEND_MODE_ADDITIVE:
            color_blend_attachment_state_create_info.blendEnable         = VK_TRUE;
            color_blend_attachment_state_create_info.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            color_blend_attachment_state_create_info.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            color_blend_attachment_state_create_info.colorBlendOp        = VK_BLEND_OP_ADD;
            color_blend_attachment_state_create_info.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            color_blend_attachment_state_create_info.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            color_blend_attachment_state_create_info.alphaBlendOp        = VK_BLEND_OP_ADD;
            break;
    }

    return color_blend_attachment_state_create_info;
}

static VkPipelineColorBlendStateCreateInfo configure_color_blend_state_create_info(VkPipelineColorBlendAttachmentState* attachments, uint32_t attachment_count)
{
    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable     = VK_FALSE,
        .logicOp           = VK_LOGIC_OP_COPY,
        .attachmentCount   = attachment_count,
        .pAttachments      = attachments,
        .blendConstants[0] = 0.0f,
        .blendConstants[1] = 0.0f,
        .blendConstants[2] = 0.0f,
        .blendConstants[3] = 0.0f,
    };

    return color_blend_state_create_info;
}

static VkPipelineDynamicStateCreateInfo configure_dynamic_state_create_info(VkDynamicState* dynamic_states, uint32_t dynamic_states_size)
{
    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamic_states_size,
        .pDynamicStates    = dynamic_states,
    };

    return dynamic_state_create_info;
}

PResult pigment_create_pipeline_cache(Pigment* pigment, const void* initial_data, uint64_t size, PPipelineCache** out)
{
    if(pigment == NULL || out == NULL)
    {
        return PIGMENT_ERROR;
    }

    *out = NULL;

    PPipelineCache* cache = P_NEW_FOR_OBJECT(pigment, cache);
    if(cache == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    VkPipelineCacheCreateInfo create_info = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = (size_t) size,
        .pInitialData    = initial_data,
    };

    VkResult result = vkCreatePipelineCache(pigment->device->logical_device, &create_info, &pigment->vk_alloc, &cache->cache);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create pipeline cache (result: %d)", result);
        P_FREE(pigment, cache);
        return PIGMENT_ERROR_VULKAN;
    }

    *out = cache;
    return PIGMENT_SUCCESS;
}

void pigment_destroy_pipeline_cache(Pigment* pigment, PPipelineCache* cache)
{
    if(pigment == NULL || cache == NULL)
    {
        return;
    }
    if(cache->cache != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(pigment->device->logical_device, cache->cache, &pigment->vk_alloc);
    }
    P_FREE(pigment, cache);
}

PResult pigment_pipeline_cache_get_data(Pigment* pigment, PPipelineCache* cache, void* out_data, uint64_t* out_size)
{
    if(pigment == NULL || cache == NULL || out_size == NULL)
    {
        return PIGMENT_ERROR;
    }

    size_t size     = (size_t) *out_size;
    VkResult result = vkGetPipelineCacheData(pigment->device->logical_device, cache->cache, &size, out_data);
    *out_size       = (uint64_t) size;

    if(result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        PLOG_ERROR(pigment, "Failed to query pipeline cache data (result: %d)", result);
        return PIGMENT_ERROR_VULKAN;
    }

    return PIGMENT_SUCCESS;
}

PLayout* pigment_create_layout(Pigment* pigment, const PLayoutDesc* desc)
{
    if(pigment == NULL || desc == NULL)
    {
        return NULL;
    }

    VkShaderStageFlags vk_stages = (VkShaderStageFlags) desc->push_stages;

    VkPushConstantRange range = {
        .stageFlags = vk_stages,
        .offset     = 0,
        .size       = desc->push_size,
    };

    VkDescriptorSetLayout* vk_set_layouts = NULL;
    if(desc->set_layout_count > 0)
    {
        vk_set_layouts = P_NEW_ARRAY_FOR_COMMAND(pigment, vk_set_layouts, desc->set_layout_count);
        if(vk_set_layouts == NULL)
        {
            return NULL;
        }

        for(uint32_t i = 0; i < desc->set_layout_count; i++)
        {
            vk_set_layouts[i] = desc->set_layouts[i]->layout;
        }
    }

    VkPipelineLayoutCreateInfo create_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = desc->set_layout_count,
        .pSetLayouts            = vk_set_layouts,
        .pushConstantRangeCount = (desc->push_size > 0) ? 1 : 0,
        .pPushConstantRanges    = (desc->push_size > 0) ? &range : NULL,
    };

    VkPipelineLayout vk_layout = VK_NULL_HANDLE;
    VkResult result            = vkCreatePipelineLayout(pigment->device->logical_device, &create_info, &pigment->vk_alloc, &vk_layout);

    P_FREE(pigment, vk_set_layouts);

    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create pipeline layout! (result: %d)", result);
        return NULL;
    }

    PLayout* layout = P_NEW_FOR_OBJECT(pigment, layout);
    if(layout == NULL)
    {
        vkDestroyPipelineLayout(pigment->device->logical_device, vk_layout, &pigment->vk_alloc);
        return NULL;
    }

    layout->layout      = vk_layout;
    layout->push_size   = desc->push_size;
    layout->push_stages = vk_stages;

    set_object_name(pigment->device->logical_device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t) vk_layout, desc->name);

    return layout;
}

void pigment_destroy_layout(Pigment* pigment, PLayout* layout)
{
    if(pigment == NULL || layout == NULL)
    {
        return;
    }

    pigment_defer_destroy(pigment, destroy_layout_immediate, layout);
}

VkPipeline pigment_vk_pipeline(PPipeline* pipeline)
{
    return (pipeline != NULL) ? pipeline->pipeline : VK_NULL_HANDLE;
}

VkPipelineLayout pigment_vk_pipeline_layout(PLayout* layout)
{
    return (layout != NULL) ? layout->layout : VK_NULL_HANDLE;
}

VkPipelineCache pigment_vk_pipeline_cache(PPipelineCache* cache)
{
    return (cache != NULL) ? cache->cache : VK_NULL_HANDLE;
}

static void destroy_layout_immediate(Pigment* pigment, void* resource)
{
    PLayout* layout = (PLayout*) resource;
    if(layout->layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(pigment->device->logical_device, layout->layout, &pigment->vk_alloc);
    }
    P_FREE(pigment, layout);
}
