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

#include "pigment_shaderc.h"

#include <shaderc/shaderc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PShadercCompiler* pigment_shaderc_create(void)
{
    PShadercCompiler* compiler = shaderc_compiler_initialize();
    if(compiler == NULL)
    {
        fprintf(stderr, "pigment_shaderc: failed to initialize shader compiler.\n");
    }

    return compiler;
}

void pigment_shaderc_destroy(PShadercCompiler* compiler)
{
    if(compiler != NULL)
    {
        shaderc_compiler_release(compiler);
    }
}

uint32_t* pigment_shaderc_compile_glsl(
    PShadercCompiler* compiler,
    const char* source_code,
    uint32_t source_size,
    PShaderStageFlags stage,
    const char* file_name,
    uint32_t* spv_size
)
{
    if(spv_size == NULL)
    {
        return NULL;
    }
    *spv_size = 0;
    if(compiler == NULL || source_code == NULL)
    {
        return NULL;
    }

    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    if(options == NULL)
    {
        fprintf(stderr, "pigment_shaderc: failed to initialize shader compile options.\n");
        return NULL;
    }

    shaderc_compilation_result_t result = NULL;
    uint32_t* spv                       = NULL;
    shaderc_shader_kind kind;
    switch(stage)
    {
        case P_SHADER_STAGE_VERTEX_BIT:
            kind = shaderc_glsl_vertex_shader;
            break;
        case P_SHADER_STAGE_FRAGMENT_BIT:
            kind = shaderc_glsl_fragment_shader;
            break;
        case P_SHADER_STAGE_COMPUTE_BIT:
            kind = shaderc_glsl_compute_shader;
            break;
        case P_SHADER_STAGE_MESH_BIT:
            kind = shaderc_glsl_mesh_shader;
            shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
            break;
        case P_SHADER_STAGE_TASK_BIT:
            kind = shaderc_glsl_task_shader;
            shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
            break;
        default:
            fprintf(stderr, "pigment_shaderc: expected one supported shader stage.\n");
            goto FREE;
    }

    const char* input_name = file_name ? file_name : "default";
    result                 = shaderc_compile_into_spv(compiler, source_code, source_size, kind, input_name, "main", options);
    if(result == NULL)
    {
        fprintf(stderr, "pigment_shaderc: failed to allocate shader compilation result.\n");
        goto FREE;
    }

    if(shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success)
    {
        fprintf(stderr, "pigment_shaderc: GLSL compilation error: %s\n", shaderc_result_get_error_message(result));
        goto FREE;
    }

    size_t size = shaderc_result_get_length(result);
    if(size > UINT32_MAX)
    {
        fprintf(stderr, "pigment_shaderc: SPIR-V result exceeds the maximum size.\n");
        goto FREE;
    }

    spv = malloc(size);
    if(spv != NULL)
    {
        memcpy(spv, shaderc_result_get_bytes(result), size);
        *spv_size = (uint32_t) size;
    }

FREE:
    if(result != NULL)
    {
        shaderc_result_release(result);
    }

    shaderc_compile_options_release(options);

    return spv;
}

void pigment_shaderc_free(uint32_t* spv)
{
    free(spv);
}
