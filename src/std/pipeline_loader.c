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

#include "std_internal.h"

#include "pigment/pigment.h"

#include <string.h>

#include "default_vert_spv.h"
#include "default_frag_spv.h"
#include "light_gizmo_vert_spv.h"
#include "light_gizmo_frag_spv.h"
#include "skybox_vert_spv.h"
#include "skybox_frag_spv.h"
#include "crt_vert_spv.h"
#include "crt_frag_spv.h"

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

    desc.vertex_shader        = (const uint32_t*) default_vert_spv;
    desc.vertex_shader_size   = (uint32_t) default_vert_spv_size;
    desc.fragment_shader      = (const uint32_t*) default_frag_spv;
    desc.fragment_shader_size = (uint32_t) default_frag_spv_size;

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

    desc.vertex_shader        = (const uint32_t*) light_gizmo_vert_spv;
    desc.vertex_shader_size   = (uint32_t) light_gizmo_vert_spv_size;
    desc.fragment_shader      = (const uint32_t*) light_gizmo_frag_spv;
    desc.fragment_shader_size = (uint32_t) light_gizmo_frag_spv_size;

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

    desc.vertex_shader        = (const uint32_t*) skybox_vert_spv;
    desc.vertex_shader_size   = (uint32_t) skybox_vert_spv_size;
    desc.fragment_shader      = (const uint32_t*) skybox_frag_spv;
    desc.fragment_shader_size = (uint32_t) skybox_frag_spv_size;

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

    desc.vertex_shader        = (const uint32_t*) crt_vert_spv;
    desc.vertex_shader_size   = (uint32_t) crt_vert_spv_size;
    desc.fragment_shader      = (const uint32_t*) crt_frag_spv;
    desc.fragment_shader_size = (uint32_t) crt_frag_spv_size;

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
