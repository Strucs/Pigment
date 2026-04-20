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
#include "structs.h"

static VkShaderModule create_shader_module(VkDevice device, const uint32_t* code, uint32_t shader_size);
static VkPipelineShaderStageCreateInfo configure_shader_stage_create_info(VkShaderModule shader_module, VkShaderStageFlagBits stage, const char* entry_point);
static VkPipelineVertexInputStateCreateInfo configure_vertex_input_state_create_info(void);
static VkPipelineInputAssemblyStateCreateInfo configure_input_assembly_state_create_info(PTopology topology);
static VkPipelineViewportStateCreateInfo configure_viewport_state_create_info(void);
static VkPipelineRasterizationStateCreateInfo configure_rasterizer_state_create_info(PPolygonMode polygon_mode, PCullMode cull_mode);
static VkPipelineMultisampleStateCreateInfo configure_multisampling_state_create_info(void);
static VkPipelineDepthStencilStateCreateInfo configure_depth_stencil_state_create_info(bool depth_test, bool depth_write, PCompareOp depth_compare_op, bool stencil_test);
static VkPipelineColorBlendAttachmentState configure_color_blend_attachment_state_create_info(PBlendMode mode);
static VkPipelineColorBlendStateCreateInfo configure_color_blend_state_create_info(VkPipelineColorBlendAttachmentState* color_blend_attachment_state_create_info);
static VkPipelineDynamicStateCreateInfo configure_dynamic_state_create_info(VkDynamicState* dynamic_states, uint32_t dynamic_states_size);
static VkPipelineLayout create_pipeline_layout(VkDescriptorSetLayout* descriptor_set_layout, VkDevice device);

PPipelineBuild* pigment_pipeline_build_from_desc(Pigment* pigment, PPipelineDesc* desc)
{
    if(pigment == NULL || desc == NULL || desc->vertex_spv == NULL)
    {
        return NULL;
    }

    VkDevice device       = pigment->device->logical_device;
    PPipelineBuild* build = calloc(1, sizeof(*build));
    if(build == NULL)
    {
        perror("pigment_pipeline_build_from_desc");
        return NULL;
    }

    build->vertex_module = create_shader_module(device, desc->vertex_spv, desc->vertex_spv_size);
    if(build->vertex_module == NULL)
    {
        goto ERROR;
    }
    build->shader_stages[build->shader_stage_count++] = configure_shader_stage_create_info(build->vertex_module, VK_SHADER_STAGE_VERTEX_BIT, "main");

    if(desc->fragment_spv != NULL)
    {
        build->fragment_module = create_shader_module(device, desc->fragment_spv, desc->fragment_spv_size);
        if(build->fragment_module == NULL)
        {
            goto ERROR;
        }
        build->shader_stages[build->shader_stage_count++] = configure_shader_stage_create_info(build->fragment_module, VK_SHADER_STAGE_FRAGMENT_BIT, "main");
    }

    build->vertex_input     = configure_vertex_input_state_create_info();
    build->input_assembly   = configure_input_assembly_state_create_info(desc->topology);
    build->viewport         = configure_viewport_state_create_info();
    build->rasterizer       = configure_rasterizer_state_create_info(desc->polygon_mode, desc->cull_mode);
    build->multisample      = configure_multisampling_state_create_info();
    build->depth_stencil    = configure_depth_stencil_state_create_info(desc->depth_test, desc->depth_write, desc->depth_compare_op, desc->stencil_test);
    build->blend_attachment = configure_color_blend_attachment_state_create_info(desc->blend_mode);
    build->color_blend      = configure_color_blend_state_create_info(&build->blend_attachment);

    build->dynamic_state_count = 2;
    build->dynamic_state_list  = malloc(build->dynamic_state_count * sizeof(*build->dynamic_state_list));
    if(build->dynamic_state_list == NULL)
    {
        perror("pigment_pipeline_build_from_desc");
        goto ERROR;
    }

    build->dynamic_state_list[0] = VK_DYNAMIC_STATE_VIEWPORT;
    build->dynamic_state_list[1] = VK_DYNAMIC_STATE_SCISSOR;
    build->dynamic               = configure_dynamic_state_create_info(build->dynamic_state_list, build->dynamic_state_count);

    build->layout = create_pipeline_layout(&pigment->descriptor->descriptor_set_layout, device);
    if(build->layout == NULL)
    {
        goto ERROR;
    }

    build->color_format = (VkFormat) desc->color_format;
    build->rendering    = (VkPipelineRenderingCreateInfoKHR) {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &build->color_format,
        .depthAttachmentFormat   = (VkFormat) desc->depth_format
    };

    return build;

ERROR:
    pigment_pipeline_build_destroy(pigment, build);
    return NULL;
}

void pigment_pipeline_build_destroy(Pigment* pigment, PPipelineBuild* build)
{
    if(pigment == NULL || build == NULL)
    {
        return;
    }

    VkDevice device = pigment->device->logical_device;

    if(build->vertex_module != NULL)
    {
        vkDestroyShaderModule(device, build->vertex_module, NULL);
    }
    if(build->fragment_module != NULL)
    {
        vkDestroyShaderModule(device, build->fragment_module, NULL);
    }
    if(build->layout != NULL)
    {
        vkDestroyPipelineLayout(device, build->layout, NULL);
    }

    free(build->dynamic_state_list);
    free(build);
}

PPipelines* pigment_create_graphic_pipelines(Pigment* pigment, PPipelineBuild** builds, uint32_t count)
{
    if(pigment == NULL || builds == NULL || count == 0)
    {
        return NULL;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        if(builds[i] == NULL)
        {
            fprintf(stderr, "pigment_create_graphic_pipelines: builds[%u] is NULL\n", i);
            return NULL;
        }
    }

    VkDevice device                                     = pigment->device->logical_device;
    PPipelines* pipelines                               = NULL;
    VkGraphicsPipelineCreateInfo* pipeline_create_infos = NULL;

    pipelines = calloc(1, sizeof(*pipelines));
    if(pipelines == NULL)
    {
        goto ERROR;
    }

    pipelines->count             = count;
    pipelines->graphic_pipelines = calloc(count, sizeof(*pipelines->graphic_pipelines));
    pipelines->pipeline_layouts  = calloc(count, sizeof(*pipelines->pipeline_layouts));

    if(pipelines->graphic_pipelines == NULL || pipelines->pipeline_layouts == NULL)
    {
        goto ERROR;
    }

    pipeline_create_infos = calloc(count, sizeof(*pipeline_create_infos));
    if(pipeline_create_infos == NULL)
    {
        goto ERROR;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        VkGraphicsPipelineCreateInfo pipeline_create_info = {
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
            .layout              = builds[i]->layout,
            .renderPass          = VK_NULL_HANDLE,
            .basePipelineHandle  = VK_NULL_HANDLE
        };

        pipeline_create_infos[i] = pipeline_create_info;
    }

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, count, pipeline_create_infos, NULL, pipelines->graphic_pipelines);
    if(result != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create graphics pipelines! (result: %d)\n", result);
        goto ERROR;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        pipelines->pipeline_layouts[i] = builds[i]->layout;
        builds[i]->layout              = VK_NULL_HANDLE;
    }

    goto FREE;

ERROR:
    perror("pigment_create_graphic_pipelines");
    if(pipelines != NULL)
    {
        if(pipelines->graphic_pipelines != NULL)
        {
            for(uint32_t i = 0; i < count; i++)
            {
                if(pipelines->graphic_pipelines[i] != NULL)
                {
                    vkDestroyPipeline(device, pipelines->graphic_pipelines[i], NULL);
                }
            }
        }
        free(pipelines->graphic_pipelines);
        free(pipelines->pipeline_layouts);
        free(pipelines);
        pipelines = NULL;
    }

FREE:
    free(pipeline_create_infos);

    for(uint32_t i = 0; i < count; i++)
    {
        if(builds[i] != NULL)
        {
            pigment_pipeline_build_destroy(pigment, builds[i]);
            builds[i] = NULL;
        }
    }

    return pipelines;
}

void pigment_destroy_pipelines(Pigment* pigment, PPipelines* pipelines)
{
    if(pigment == NULL || pipelines == NULL)
    {
        return;
    }

    VkDevice device = pigment->device->logical_device;

    for(uint32_t i = 0; i < pipelines->count; i++)
    {
        if(pipelines->graphic_pipelines[i] != NULL)
        {
            vkDestroyPipeline(device, pipelines->graphic_pipelines[i], NULL);
        }
        if(pipelines->pipeline_layouts[i] != NULL)
        {
            vkDestroyPipelineLayout(device, pipelines->pipeline_layouts[i], NULL);
        }
    }

    free(pipelines->graphic_pipelines);
    free(pipelines->pipeline_layouts);
    free(pipelines);
}

void pigment_bind_pipeline(Pigment* pigment, PPipelines* pipelines, uint32_t pipeline_id)
{
    if(pigment == NULL || pipelines == NULL || pipeline_id >= pipelines->count)
    {
        return;
    }

    PWindowRenderer* renderer = pigment->window_renderer;
    uint32_t current_frame    = renderer->swapchain->current_frame;
    VkCommandBuffer cmd       = renderer->command_buffers->buffers[current_frame];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->graphic_pipelines[pipeline_id]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->pipeline_layouts[pipeline_id], 0, 1, &pigment->descriptor->descriptor_sets[current_frame], 0, NULL);
}

static VkShaderModule create_shader_module(VkDevice device, const uint32_t* code, uint32_t shader_size)
{
    VkShaderModule shader_module;

    VkShaderModuleCreateInfo create_info = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_size,
        .pCode    = code
    };

    if(vkCreateShaderModule(device, &create_info, NULL, &shader_module) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create shader module!\n");
        return NULL;
    }

    return shader_module;
}

static VkPipelineShaderStageCreateInfo configure_shader_stage_create_info(VkShaderModule shader_module, VkShaderStageFlagBits stage, const char* entry_point)
{
    VkPipelineShaderStageCreateInfo shader_stage_info = {
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = stage,
        .module = shader_module,
        .pName  = entry_point
    };

    return shader_stage_info;
}

static VkPipelineVertexInputStateCreateInfo configure_vertex_input_state_create_info(void)
{
    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    return vertex_input_state_create_info;
}

static VkPipelineInputAssemblyStateCreateInfo configure_input_assembly_state_create_info(PTopology topology)
{
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = (VkPrimitiveTopology) topology,
        .primitiveRestartEnable = VK_FALSE
    };

    return input_assembly_state_create_info;
}

static VkPipelineViewportStateCreateInfo configure_viewport_state_create_info(void)
{
    VkPipelineViewportStateCreateInfo viewport_state_create_info = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1
    };

    return viewport_state_create_info;
}

static VkPipelineRasterizationStateCreateInfo configure_rasterizer_state_create_info(PPolygonMode polygon_mode, PCullMode cull_mode)
{
    VkPipelineRasterizationStateCreateInfo rasterizer_state_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = (VkPolygonMode) polygon_mode,
        .lineWidth               = 1.0f,
        .cullMode                = (VkCullModeFlags) cull_mode,
        .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE
    };

    return rasterizer_state_create_info;
}

static VkPipelineMultisampleStateCreateInfo configure_multisampling_state_create_info(void)
{
    VkPipelineMultisampleStateCreateInfo multisampling_state_create_info = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable  = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    return multisampling_state_create_info;
}

static VkPipelineDepthStencilStateCreateInfo configure_depth_stencil_state_create_info(bool depth_test, bool depth_write, PCompareOp depth_compare_op, bool stencil_test)
{
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = depth_test ? VK_TRUE : VK_FALSE,
        .depthWriteEnable      = depth_write ? VK_TRUE : VK_FALSE,
        .depthCompareOp        = (VkCompareOp) depth_compare_op,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = stencil_test ? VK_TRUE : VK_FALSE
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

static VkPipelineColorBlendStateCreateInfo configure_color_blend_state_create_info(VkPipelineColorBlendAttachmentState* color_blend_attachment_state_create_info)
{
    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable     = VK_FALSE,
        .logicOp           = VK_LOGIC_OP_COPY,
        .attachmentCount   = 1,
        .pAttachments      = color_blend_attachment_state_create_info,
        .blendConstants[0] = 0.0f,
        .blendConstants[1] = 0.0f,
        .blendConstants[2] = 0.0f,
        .blendConstants[3] = 0.0f
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

static VkPipelineLayout create_pipeline_layout(VkDescriptorSetLayout* descriptor_set_layout, VkDevice device)
{
    VkPipelineLayout pipeline_layout;

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = sizeof(PDrawPushConstants)
    };

    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &push_constant_range
    };

    VkResult result;
    if((result = vkCreatePipelineLayout(device, &pipeline_layout_create_info, NULL, &pipeline_layout)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create pipeline layout! (result: %d)\n", result);
        return NULL;
    }

    return pipeline_layout;
}
