/**
 * Copyright 2026 Angel-Leduc TA
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

#include "pipeline_loader.h"
#include "internal.h"
#include "pigment.h"
#include "std_internal.h"

#include <stdalign.h>
#include <string.h>
#include <shaderc/shaderc.h>

alignas(uint32_t) static constexpr unsigned char default_vertex_spv[] = {
    #embed <default_vert.spv>
};
alignas(uint32_t) static constexpr unsigned char default_fragment_spv[] = {
    #embed <default_frag.spv>
};
alignas(uint32_t) static constexpr unsigned char light_gizmo_vertex_spv[] = {
    #embed <light_gizmo_vert.spv>
};
alignas(uint32_t) static constexpr unsigned char light_gizmo_fragment_spv[] = {
    #embed <light_gizmo_frag.spv>
};
alignas(uint32_t) static constexpr unsigned char skybox_vertex_spv[] = {
    #embed <skybox_vert.spv>
};
alignas(uint32_t) static constexpr unsigned char skybox_fragment_spv[] = {
    #embed <skybox_frag.spv>
};
alignas(uint32_t) static constexpr unsigned char crt_vertex_spv[] = {
    #embed <crt_vert.spv>
};
alignas(uint32_t) static constexpr unsigned char crt_fragment_spv[] = {
    #embed <crt_frag.spv>
};

char* load_shader_code(Pigment* pigment, const IOCallbacks* io, const char* file_path, uint32_t* shader_size)
{
    IOCallbacks default_io = pigment_std_default_file_io(pigment);
    if(io == NULL)
    {
        io = &default_io;
    }

    uint64_t size       = 0;
    unsigned char* data = io->read_file(io->user_data, file_path, &size);
    if(data == NULL)
    {
        return NULL;
    }

    char* shader_code = P_ALLOC_OBJECT(pigment, size + 1, _Alignof(char));
    if(shader_code != NULL)
    {
        memcpy(shader_code, data, (size_t) size);
        shader_code[size] = '\0';
        *shader_size      = (uint32_t) size;
    }

    io->free_file(io->user_data, data);

    return shader_code;
}

uint32_t* compile_glsl_to_spv(Pigment* pigment, const char* source_code, uint32_t source_size, PShaderType type, const char* file_name, uint32_t* spv_size)
{
    shaderc_compiler_t compiler         = NULL;
    shaderc_compile_options_t options   = NULL;
    shaderc_compilation_result_t result = NULL;

    compiler = shaderc_compiler_initialize();
    if(compiler == NULL)
    {
        PLOG_ERROR(pigment, "Failed to initialize shader compiler.");
        goto ERROR;
    }

    options = shaderc_compile_options_initialize();
    if(options == NULL)
    {
        PLOG_ERROR(pigment, "Failed to initialize shader compile options.");
        goto ERROR;
    }

    shaderc_shader_kind kind = (type == P_SHADER_TYPE_FRAGMENT) ? shaderc_glsl_fragment_shader : shaderc_glsl_vertex_shader;
    const char* input_name   = file_name ? file_name : "default";
    const char* entry_point  = "main";

    result = shaderc_compile_into_spv(compiler, source_code, source_size, kind, input_name, entry_point, options);

    if(shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success)
    {
        PLOG_ERROR(pigment, "GLSL compilation error: %s", shaderc_result_get_error_message(result));
        goto ERROR;
    }

    *spv_size             = shaderc_result_get_length(result);
    const uint32_t* bytes = (const uint32_t*) shaderc_result_get_bytes(result);

    uint32_t* spv = P_ALLOC_OBJECT(pigment, *spv_size, _Alignof(uint32_t));
    if(spv != NULL)
    {
        memcpy(spv, bytes, *spv_size);
    }

    shaderc_result_release(result);
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);

    return spv;

ERROR:
    shaderc_result_release(result);
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);

    return NULL;
}

static PStdPipelineLayouts* pipeline_layouts_get(Pigment* pigment, PStdBindless* bindless)
{
    PStdPipelineLayouts** slot = pigment_std_bindless_pipeline_layouts_slot(bindless);
    if(slot == NULL)
    {
        return NULL;
    }

    if(*slot == NULL)
    {
        *slot = P_NEW_FOR_OBJECT(pigment, *slot);
    }

    return *slot;
}

PLayout* default_pipeline_layout(Pigment* pigment, PStdBindless* bindless)
{
    PStdPipelineLayouts* layouts = pipeline_layouts_get(pigment, bindless);
    if(layouts == NULL)
    {
        return NULL;
    }

    if(layouts->default_layout != NULL)
    {
        return layouts->default_layout;
    }

    PDescriptorSetLayout* set_layouts[1] = {pigment_std_bindless_layout(bindless)};
    PLayoutDesc desc                     = {
        .set_layouts      = set_layouts,
        .set_layout_count = 1,
        .push_size        = sizeof(PStdPushConstants),
        .push_stages      = P_SHADER_STAGE_VERTEX_BIT | P_SHADER_STAGE_FRAGMENT_BIT,
    };

    layouts->default_layout = pigment_create_layout(pigment, &desc);
    return layouts->default_layout;
}

PPipelineDesc default_graphic_pipeline_desc(Pigment* pigment, PStdBindless* bindless, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format, PSampleCount samples)
{
    PPipelineDesc desc = {0};

    desc.layout = default_pipeline_layout(pigment, bindless);

    desc.vertex_spv        = (const uint32_t*) default_vertex_spv;
    desc.vertex_spv_size   = (uint32_t) sizeof(default_vertex_spv);
    desc.fragment_spv      = (const uint32_t*) default_fragment_spv;
    desc.fragment_spv_size = (uint32_t) sizeof(default_fragment_spv);

    desc.color_formats      = color_formats;
    desc.color_format_count = color_format_count;
    desc.depth_format       = depth_format;
    desc.polygon_mode       = P_POLYGON_MODE_FILL;
    desc.topology           = P_TOPOLOGY_TRIANGLE_LIST;
    desc.blend_modes        = NULL;
    desc.blend_mode_count   = 0;
    desc.sample_count       = samples;

    return desc;
}

PLayout* default_light_gizmo_pipeline_layout(Pigment* pigment, PStdBindless* bindless)
{
    PStdPipelineLayouts* layouts = pipeline_layouts_get(pigment, bindless);
    if(layouts == NULL)
    {
        return NULL;
    }

    if(layouts->gizmo_layout != NULL)
    {
        return layouts->gizmo_layout;
    }

    PDescriptorSetLayout* set_layouts[1] = {pigment_std_bindless_layout(bindless)};
    PLayoutDesc desc                     = {
        .set_layouts      = set_layouts,
        .set_layout_count = 1,
        .push_size        = sizeof(PStdGizmoPushConstants),
        .push_stages      = P_SHADER_STAGE_VERTEX_BIT | P_SHADER_STAGE_FRAGMENT_BIT,
    };

    layouts->gizmo_layout = pigment_create_layout(pigment, &desc);
    return layouts->gizmo_layout;
}

PPipelineDesc default_light_gizmo_pipeline_desc(Pigment* pigment, PStdBindless* bindless, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format, PSampleCount samples)
{
    PPipelineDesc desc = {0};

    desc.layout = default_light_gizmo_pipeline_layout(pigment, bindless);

    desc.vertex_spv        = (const uint32_t*) light_gizmo_vertex_spv;
    desc.vertex_spv_size   = (uint32_t) sizeof(light_gizmo_vertex_spv);
    desc.fragment_spv      = (const uint32_t*) light_gizmo_fragment_spv;
    desc.fragment_spv_size = (uint32_t) sizeof(light_gizmo_fragment_spv);

    desc.color_formats      = color_formats;
    desc.color_format_count = color_format_count;
    desc.depth_format       = depth_format;
    desc.polygon_mode       = P_POLYGON_MODE_FILL;
    desc.topology           = P_TOPOLOGY_TRIANGLE_LIST;
    desc.blend_modes        = NULL;
    desc.blend_mode_count   = 0;
    desc.sample_count       = samples;

    return desc;
}

PLayout* default_skybox_pipeline_layout(Pigment* pigment, PStdBindless* bindless)
{
    PStdPipelineLayouts* layouts = pipeline_layouts_get(pigment, bindless);
    if(layouts == NULL)
    {
        return NULL;
    }

    if(layouts->skybox_layout != NULL)
    {
        return layouts->skybox_layout;
    }

    PDescriptorSetLayout* set_layouts[1] = {pigment_std_bindless_layout(bindless)};
    PLayoutDesc desc                     = {
        .set_layouts      = set_layouts,
        .set_layout_count = 1,
        .push_size        = sizeof(PStdSkyboxPushConstants),
        .push_stages      = P_SHADER_STAGE_VERTEX_BIT | P_SHADER_STAGE_FRAGMENT_BIT,
    };
    layouts->skybox_layout = pigment_create_layout(pigment, &desc);
    return layouts->skybox_layout;
}

PPipelineDesc default_skybox_pipeline_desc(Pigment* pigment, PStdBindless* bindless, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format, PSampleCount samples)
{
    PPipelineDesc desc = {0};

    desc.layout = default_skybox_pipeline_layout(pigment, bindless);

    desc.vertex_spv        = (const uint32_t*) skybox_vertex_spv;
    desc.vertex_spv_size   = (uint32_t) sizeof(skybox_vertex_spv);
    desc.fragment_spv      = (const uint32_t*) skybox_fragment_spv;
    desc.fragment_spv_size = (uint32_t) sizeof(skybox_fragment_spv);

    desc.color_formats      = color_formats;
    desc.color_format_count = color_format_count;
    desc.depth_format       = depth_format;
    desc.polygon_mode       = P_POLYGON_MODE_FILL;
    desc.topology           = P_TOPOLOGY_TRIANGLE_LIST;
    desc.blend_modes        = NULL;
    desc.blend_mode_count   = 0;
    desc.sample_count       = samples;

    return desc;
}

static PLayout* default_crt_pipeline_layout(Pigment* pigment, PStdBindless* bindless)
{
    PStdPipelineLayouts* layouts = pipeline_layouts_get(pigment, bindless);
    if(layouts == NULL)
    {
        return NULL;
    }

    if(layouts->crt_layout != NULL)
    {
        return layouts->crt_layout;
    }

    PDescriptorSetLayout* set_layouts[1] = {pigment_std_bindless_layout(bindless)};
    PLayoutDesc desc                     = {
        .set_layouts      = set_layouts,
        .set_layout_count = 1,
        .push_size        = sizeof(PStdCrtPushConstants),
        .push_stages      = P_SHADER_STAGE_FRAGMENT_BIT,
    };
    layouts->crt_layout = pigment_create_layout(pigment, &desc);
    return layouts->crt_layout;
}

PPipelineDesc default_crt_pipeline_desc(Pigment* pigment, PStdBindless* bindless, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format, PSampleCount samples)
{
    PPipelineDesc desc = {0};

    desc.layout = default_crt_pipeline_layout(pigment, bindless);

    desc.vertex_spv        = (const uint32_t*) crt_vertex_spv;
    desc.vertex_spv_size   = (uint32_t) sizeof(crt_vertex_spv);
    desc.fragment_spv      = (const uint32_t*) crt_fragment_spv;
    desc.fragment_spv_size = (uint32_t) sizeof(crt_fragment_spv);

    desc.color_formats      = color_formats;
    desc.color_format_count = color_format_count;
    desc.depth_format       = depth_format;
    desc.polygon_mode       = P_POLYGON_MODE_FILL;
    desc.topology           = P_TOPOLOGY_TRIANGLE_LIST;
    desc.blend_modes        = NULL;
    desc.blend_mode_count   = 0;
    desc.sample_count       = samples;

    return desc;
}
