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
static PLayout* get_or_create_default_layout(Pigment* pigment);
static void retain_layout(PLayout* layout);
static void release_layout(PLayoutList* list, PLayout* layout, PDevice* device);
static void pipeline_list_destroy(PPipelineList* list, PLayoutList* layouts, PPipeline* pipeline, PDevice* device);

#define PIGMENT_PIPELINE_LIST_INITIAL_CAPACITY 4

PPipelineList* create_pipeline_list(void)
{
    return calloc(1, sizeof(PPipelineList));
}

void destroy_pipeline_list(PPipelineList* list, PLayoutList* layouts, PDevice* device)
{
    if(list == NULL)
    {
        return;
    }

    while(list->count > 0)
    {
        pipeline_list_destroy(list, layouts, list->pipelines[0], device);
    }
    free(list->pipelines);
    free(list);
}

PLayoutList* create_layout_list(void)
{
    return calloc(1, sizeof(PLayoutList));
}

void destroy_layout_list(PLayoutList* list, PDevice* device)
{
    if(list == NULL)
    {
        return;
    }
    for(uint32_t i = 0; i < list->count; i++)
    {
        PLayout* entry = list->entries[i];
        if(entry != NULL)
        {
            if(entry->layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device->logical_device, entry->layout, NULL);
            }
            free(entry);
        }
    }
    free(list->entries);
    free(list);
}

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

    build->layout = get_or_create_default_layout(pigment);
    if(build->layout == NULL)
    {
        goto ERROR;
    }

    retain_layout(build->layout);

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
        release_layout(pigment->layouts, build->layout, pigment->device);
    }

    free(build->dynamic_state_list);
    free(build);
}

int pigment_create_graphic_pipelines(Pigment* pigment, PPipelineBuild** builds, uint32_t count, PPipeline** out)
{
    VkGraphicsPipelineCreateInfo* pipeline_create_infos = NULL;
    VkPipeline* vk_pipelines                            = NULL;
    PPipeline** temp_pipelines                          = NULL;
    uint32_t temp_pipelines_allocated                   = 0;
    int status                                          = PIGMENT_ERROR;

    if(pigment == NULL || builds == NULL || out == NULL || count == 0)
    {
        return PIGMENT_ERROR;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        if(builds[i] == NULL)
        {
            fprintf(stderr, "pigment_create_graphic_pipelines: builds[%u] is NULL\n", i);
            goto FREE;
        }
    }

    VkDevice device     = pigment->device->logical_device;
    PPipelineList* list = pigment->pipelines;
    uint32_t needed     = list->count + count;

    if(needed > list->capacity)
    {
        uint32_t new_capacity = list->capacity == 0 ? PIGMENT_PIPELINE_LIST_INITIAL_CAPACITY : list->capacity;
        while(new_capacity < needed)
        {
            new_capacity *= 2;
        }

        PPipeline** new_ptr = realloc(list->pipelines, new_capacity * sizeof(*new_ptr));

        if(new_ptr == NULL)
        {
            perror("pigment_create_graphic_pipelines");
            goto FREE;
        }

        list->pipelines = new_ptr;
        list->capacity  = new_capacity;
    }

    vk_pipelines          = calloc(count, sizeof(*vk_pipelines));
    temp_pipelines        = calloc(count, sizeof(*temp_pipelines));
    pipeline_create_infos = calloc(count, sizeof(*pipeline_create_infos));
    if(vk_pipelines == NULL || temp_pipelines == NULL || pipeline_create_infos == NULL)
    {
        goto FREE;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        temp_pipelines[i] = calloc(1, sizeof(**temp_pipelines));
        if(temp_pipelines[i] == NULL)
        {
            perror("pigment_create_graphic_pipelines");
            goto FREE;
        }
        temp_pipelines_allocated++;

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
            .basePipelineHandle  = VK_NULL_HANDLE
        };
    }

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, count, pipeline_create_infos, NULL, vk_pipelines);
    if(result != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create graphics pipelines! (result: %d)\n", result);
        for(uint32_t i = 0; i < count; i++)
        {
            if(vk_pipelines[i] != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, vk_pipelines[i], NULL);
            }
        }
        goto FREE;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        temp_pipelines[i]->pipeline    = vk_pipelines[i];
        temp_pipelines[i]->layout      = builds[i]->layout;
        list->pipelines[list->count++] = temp_pipelines[i];
        out[i]                         = temp_pipelines[i];

        retain_layout(builds[i]->layout);
    }

    temp_pipelines_allocated = 0;

    status = PIGMENT_SUCCESS;

FREE:
    if(status != PIGMENT_SUCCESS)
    {
        for(uint32_t i = 0; i < temp_pipelines_allocated; i++)
        {
            free(temp_pipelines[i]);
        }
    }
    free(temp_pipelines);
    free(vk_pipelines);
    free(pipeline_create_infos);

    for(uint32_t i = 0; i < count; i++)
    {
        if(builds[i] != NULL)
        {
            pigment_pipeline_build_destroy(pigment, builds[i]);
            builds[i] = NULL;
        }
    }

    return status;
}

void pigment_destroy_pipeline(Pigment* pigment, PPipeline* pipeline)
{
    if(pigment == NULL || pipeline == NULL)
    {
        return;
    }

    vkDeviceWaitIdle(pigment->device->logical_device);
    pipeline_list_destroy(pigment->pipelines, pigment->layouts, pipeline, pigment->device);
}

void pigment_bind_pipeline(Pigment* pigment, uint32_t window_index, PPipeline* pipeline)
{
    if(pigment == NULL || pipeline == NULL || window_index >= pigment->window_count)
    {
        return;
    }

    PWindowRenderer* renderer = pigment->renderers[window_index];
    uint32_t current_frame    = renderer->swapchain->current_frame;
    VkCommandBuffer cmd       = renderer->command_buffers->buffers[current_frame];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout->layout, 0, 1, &pigment->descriptor->descriptor_sets[current_frame], 0, NULL);
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

static PLayout* get_or_create_default_layout(Pigment* pigment)
{
    PLayoutList* list = pigment->layouts;

    if(list->count > 0)
    {
        return list->entries[0];
    }

    if(list->count >= list->capacity)
    {
        uint32_t new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        PLayout** new_ptr     = realloc(list->entries, new_capacity * sizeof(*new_ptr));

        if(new_ptr == NULL)
        {
            perror("get_or_create_default_layout");
            return NULL;
        }

        list->entries  = new_ptr;
        list->capacity = new_capacity;
    }

    VkPushConstantRange range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = sizeof(PDrawPushConstants),
    };

    VkPipelineLayoutCreateInfo create_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &pigment->descriptor->descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &range,
    };

    VkPipelineLayout vk_layout = VK_NULL_HANDLE;
    VkResult result            = vkCreatePipelineLayout(pigment->device->logical_device, &create_info, NULL, &vk_layout);
    if(result != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create pipeline layout! (result: %d)\n", result);
        return NULL;
    }

    PLayout* layout = calloc(1, sizeof(*layout));
    if(layout == NULL)
    {
        perror("get_or_create_default_layout");
        vkDestroyPipelineLayout(pigment->device->logical_device, vk_layout, NULL);
        return NULL;
    }

    layout->layout      = vk_layout;
    layout->push_size   = sizeof(PDrawPushConstants);
    layout->push_stages = VK_SHADER_STAGE_VERTEX_BIT;
    layout->refcount    = 0;

    list->entries[list->count++] = layout;

    return layout;
}

static void retain_layout(PLayout* layout)
{
    if(layout == NULL)
    {
        return;
    }
    layout->refcount++;
}

static void release_layout(PLayoutList* list, PLayout* layout, PDevice* device)
{
    if(layout == NULL)
    {
        return;
    }

    if(layout->refcount > 0)
    {
        layout->refcount--;
    }
    if(layout->refcount > 0)
    {
        return;
    }

    for(uint32_t i = 0; i < list->count; i++)
    {
        if(list->entries[i] == layout)
        {
            list->entries[i] = list->entries[list->count - 1];
            list->count--;
            break;
        }
    }

    if(layout->layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device->logical_device, layout->layout, NULL);
    }
    free(layout);
}

static void pipeline_list_destroy(PPipelineList* list, PLayoutList* layouts, PPipeline* pipeline, PDevice* device)
{
    for(uint32_t i = 0; i < list->count; i++)
    {
        if(list->pipelines[i] == pipeline)
        {
            PLayout* layout    = pipeline->layout;
            list->pipelines[i] = list->pipelines[list->count - 1];
            list->count--;

            if(pipeline->pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device->logical_device, pipeline->pipeline, NULL);
            }

            free(pipeline);
            release_layout(layouts, layout, device);
            return;
        }
    }
}
