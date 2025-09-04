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

#include "pipeline.h"
#include "structs.h"
#include "shaders.h"

VkPipelineShaderStageCreateInfo configure_shader_stage_create_info(VkShaderModule shader_module, char type, const char* entry_point);
VkPipelineVertexInputStateCreateInfo configure_vertex_input_state_create_info(PVertexDescription* vertex_description);
VkPipelineInputAssemblyStateCreateInfo configure_input_assembly_state_create_info(void);
VkPipelineViewportStateCreateInfo configure_viewport_state_create_info(void);
VkPipelineRasterizationStateCreateInfo configure_rasterizer_state_create_info(void);
VkPipelineMultisampleStateCreateInfo configure_multisampling_state_create_info(void);
VkPipelineDepthStencilStateCreateInfo configure_depth_stencil_state_create_info(void);
VkPipelineColorBlendAttachmentState configure_color_blend_attachment_state_create_info(void);
VkPipelineColorBlendStateCreateInfo configure_color_blend_state_create_info(VkPipelineColorBlendAttachmentState* color_blend_attachment_state_create_info);
VkPipelineDynamicStateCreateInfo configure_dynamic_state_create_info(VkDynamicState* dynamic_states, uint32_t dynamic_states_size);
VkPipelineLayout create_pipeline_layout(VkDescriptorSetLayout* descriptor_set_layout, VkDevice device);

PPipeline* create_graphic_pipeline(PRenderPass* render_pass, PDescriptor* descriptor, PDevice* device, PVertexDescription* vertex_description)
{
    PPipeline* pipeline = NULL;
    char* vertex_shader_code = NULL;
    char* fragment_shader_code = NULL;
    VkShaderModule vertex_shader_module = NULL;
    VkShaderModule fragment_shader_module = NULL;
    uint32_t vertex_shader_code_size;
    uint32_t fragment_shader_code_size;
    uint32_t* vertex_spv = NULL;
    uint32_t* fragment_spv = NULL;
    uint32_t vertex_spv_size;
    uint32_t fragment_spv_size;


    vertex_shader_code   = get_shader_code("shaders/shader.vert", &vertex_shader_code_size);
    if(vertex_shader_code == NULL)
    {
        printf("File shaders/shader.vert missing, using default vertex shader\n");
        vertex_shader_code_size = strlen(DEFAULT_VERTEX_SHADER);
        vertex_shader_code = malloc(vertex_shader_code_size + 1);
        if (vertex_shader_code == NULL)
        {
            perror("malloc");
            goto ERROR;
        }
        memcpy(vertex_shader_code, DEFAULT_VERTEX_SHADER, vertex_shader_code_size + 1);
    }

    vertex_spv = compile_glsl_to_spv(vertex_shader_code, vertex_shader_code_size, shaderc_glsl_vertex_shader, "shaders/vert.spv", &vertex_spv_size);
    if (vertex_spv == NULL) {
        fprintf(stderr, "Failed to compile vertex shader to SPIR-V.\n");
        goto ERROR;
    }

    fragment_shader_code = get_shader_code("shaders/shader.frag", &fragment_shader_code_size);
    if(fragment_shader_code == NULL)
    {
        printf("File shaders/shader.frag missing, using default fragment shader\n");
        fragment_shader_code_size = strlen(DEFAULT_FRAGMENT_SHADER);
        fragment_shader_code = malloc(fragment_shader_code_size + 1);
        if (fragment_shader_code == NULL)
        {
            perror("malloc");
            goto ERROR;
        }
        memcpy(fragment_shader_code, DEFAULT_FRAGMENT_SHADER, fragment_shader_code_size + 1);
    }

    fragment_spv = compile_glsl_to_spv(fragment_shader_code, fragment_shader_code_size, shaderc_glsl_fragment_shader, "shaders/frag.spv", &fragment_spv_size);
    if (fragment_spv == NULL) {
        fprintf(stderr, "Failed to compile fragment shader to SPIR-V.\n");
        goto ERROR;
    }

    vertex_shader_module   = create_shader_module(device->logical_device, vertex_spv, vertex_spv_size);
    if(vertex_shader_module == NULL)
    {
        goto ERROR;
    }
    fragment_shader_module = create_shader_module(device->logical_device, fragment_spv, fragment_spv_size);
    if(fragment_shader_code == NULL)
    {
        goto ERROR;
    }

    VkPipelineShaderStageCreateInfo shader_stages_create_info[] = {
        configure_shader_stage_create_info(vertex_shader_module, VERTEX_SHADER_TYPE, "main"),
        configure_shader_stage_create_info(fragment_shader_module, FRAGMENT_SHADER_TYPE, "main")
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info          = configure_vertex_input_state_create_info(vertex_description);
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info      = configure_input_assembly_state_create_info();
    VkPipelineViewportStateCreateInfo viewport_state_create_info                 = configure_viewport_state_create_info();
    VkPipelineRasterizationStateCreateInfo rasterizer_state_create_info          = configure_rasterizer_state_create_info();
    VkPipelineMultisampleStateCreateInfo multisampling_state_create_info         = configure_multisampling_state_create_info();
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info        = configure_depth_stencil_state_create_info();
    VkPipelineColorBlendAttachmentState color_blend_attachment_state_create_info = configure_color_blend_attachment_state_create_info();
    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info            = configure_color_blend_state_create_info(&color_blend_attachment_state_create_info);

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    uint32_t dynamic_states_size = sizeof(dynamic_states) / sizeof(dynamic_states[0]);

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = configure_dynamic_state_create_info(dynamic_states, dynamic_states_size);


    pipeline = malloc(sizeof(*pipeline));
    if(pipeline == NULL)
    {
        perror("create_graphic_pipeline: malloc: ");
        goto ERROR;
    }

    pipeline->pipeline_layout = create_pipeline_layout(&descriptor->descriptor_set_layout, device->logical_device);

    VkGraphicsPipelineCreateInfo pipeline_create_info = {
        .sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount                   = sizeof(shader_stages_create_info) / sizeof(shader_stages_create_info[0]),
        .pStages                      = shader_stages_create_info,
        .pVertexInputState            = &vertex_input_state_create_info,
        .pInputAssemblyState          = &input_assembly_state_create_info,
        .pViewportState               = &viewport_state_create_info,
        .pRasterizationState          = &rasterizer_state_create_info,
        .pMultisampleState            = &multisampling_state_create_info,
        .pDepthStencilState           = &depth_stencil_state_create_info,
        .pColorBlendState             = &color_blend_state_create_info,
        .pDynamicState                = &dynamic_state_create_info,
        .layout                       = pipeline->pipeline_layout,
        .renderPass                   = render_pass->render_pass,
        .subpass                      = 0,
        .basePipelineHandle           = VK_NULL_HANDLE
    };

    if(vkCreateGraphicsPipelines(device->logical_device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &(pipeline->graphic_pipeline)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create graphics pipeline!\n");
        goto ERROR;
    }

    goto FREE;

ERROR:
    free(pipeline);
    pipeline = NULL;

FREE:
    if(fragment_shader_module != NULL)
        vkDestroyShaderModule(device->logical_device, fragment_shader_module, NULL);
    if(vertex_shader_module != NULL)
        vkDestroyShaderModule(device->logical_device, vertex_shader_module, NULL);
    free(vertex_shader_code);
    free(fragment_shader_code);

    return pipeline;
}

void destroy_pipeline(PPipeline* pipeline, PDevice* device)
{
    if(pipeline == NULL)
    {
        return;
    }
    vkDestroyPipeline(device->logical_device, pipeline->graphic_pipeline, NULL);
    vkDestroyPipelineLayout(device->logical_device, pipeline->pipeline_layout, NULL);
    free(pipeline);
}

VkPipelineShaderStageCreateInfo configure_shader_stage_create_info(VkShaderModule shader_module, char type, const char* entry_point)
{
    VkPipelineShaderStageCreateInfo shader_stage_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .module = shader_module,
        .pName  = entry_point
    };

    shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    if(type == VERTEX_SHADER_TYPE)
    {
        shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    }
    else if(type == FRAGMENT_SHADER_TYPE)
    {
        shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    return shader_stage_info;
}

VkPipelineVertexInputStateCreateInfo configure_vertex_input_state_create_info(PVertexDescription* vertex_description)
{
    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = {
        .sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount        = 1,
        .pVertexBindingDescriptions           = &(vertex_description->binding_description),
        .vertexAttributeDescriptionCount      = vertex_description->attribute_descriptions_size,
        .pVertexAttributeDescriptions         = vertex_description->attribute_descriptions
    };

    return vertex_input_state_create_info;
}

VkPipelineInputAssemblyStateCreateInfo configure_input_assembly_state_create_info(void)
{
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info = {
        .sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable                 = VK_FALSE
    };

    return input_assembly_state_create_info;
}

VkPipelineViewportStateCreateInfo configure_viewport_state_create_info(void)
{
    VkPipelineViewportStateCreateInfo viewport_state_create_info = {
        .sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount                     = 1,
        .scissorCount                      = 1
    };

    return viewport_state_create_info;
}

VkPipelineRasterizationStateCreateInfo configure_rasterizer_state_create_info(void)
{
    VkPipelineRasterizationStateCreateInfo rasterizer_state_create_info = {
        .sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable                       = VK_FALSE,
        .rasterizerDiscardEnable                = VK_FALSE,
        .polygonMode                            = VK_POLYGON_MODE_FILL,
        .lineWidth                              = 1.0f,
        .cullMode                               = VK_CULL_MODE_BACK_BIT,
        .frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable                        = VK_FALSE
    };

    return rasterizer_state_create_info;
}

VkPipelineMultisampleStateCreateInfo configure_multisampling_state_create_info(void)
{
    VkPipelineMultisampleStateCreateInfo multisampling_state_create_info = {
        .sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable                  = VK_FALSE,
        .rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT
    };

    return multisampling_state_create_info;
}

VkPipelineDepthStencilStateCreateInfo configure_depth_stencil_state_create_info(void)
{
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info = {
        .sType                                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable                       = VK_TRUE,
        .depthWriteEnable                      = VK_TRUE,
        .depthCompareOp                        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable                 = VK_FALSE,
        .stencilTestEnable                     = VK_FALSE
    };

    return depth_stencil_state_create_info;
}

VkPipelineColorBlendAttachmentState configure_color_blend_attachment_state_create_info(void)
{
    VkPipelineColorBlendAttachmentState color_blend_attachment_state_create_info = {
        .colorWriteMask                      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable                         = VK_FALSE
    };

    return color_blend_attachment_state_create_info;
}

VkPipelineColorBlendStateCreateInfo configure_color_blend_state_create_info(VkPipelineColorBlendAttachmentState* color_blend_attachment_state_create_info)
{
    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {
        .sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable                       = VK_FALSE,
        .logicOp                             = VK_LOGIC_OP_COPY,
        .attachmentCount                     = 1,
        .pAttachments                        = color_blend_attachment_state_create_info,
        .blendConstants[0]                   = 0.0f,
        .blendConstants[1]                   = 0.0f,
        .blendConstants[2]                   = 0.0f,
        .blendConstants[3]                   = 0.0f
    };

    return color_blend_state_create_info;
}

VkPipelineDynamicStateCreateInfo configure_dynamic_state_create_info(VkDynamicState* dynamic_states, uint32_t dynamic_states_size)
{
    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {
        .sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount                = dynamic_states_size,
        .pDynamicStates                   = dynamic_states,
    };

    return dynamic_state_create_info;
}

VkPipelineLayout create_pipeline_layout(VkDescriptorSetLayout* descriptor_set_layout, VkDevice device)
{
    VkPipelineLayout pipeline_layout;

    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount             = 1,
        .pSetLayouts                = descriptor_set_layout,
        .pushConstantRangeCount     = 0
    };

    if(vkCreatePipelineLayout(device, &pipeline_layout_create_info, NULL, &pipeline_layout) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create pipeline layout!\n");
        return NULL;
    }

    return pipeline_layout;
}
