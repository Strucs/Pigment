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

uint32_t* pigment_shaderc_compile_glsl(const char* source_code, uint32_t source_size, PShaderType type, const char* file_name, uint32_t* spv_size)
{
    shaderc_compiler_t compiler         = NULL;
    shaderc_compile_options_t options   = NULL;
    shaderc_compilation_result_t result = NULL;
    uint32_t* spv                       = NULL;

    compiler = shaderc_compiler_initialize();
    if(compiler == NULL)
    {
        fprintf(stderr, "pigment_shaderc: failed to initialize shader compiler\n");
        goto FREE;
    }

    options = shaderc_compile_options_initialize();
    if(options == NULL)
    {
        fprintf(stderr, "pigment_shaderc: failed to initialize shader compile options\n");
        goto FREE;
    }

    shaderc_shader_kind kind = (type == P_SHADER_TYPE_FRAGMENT) ? shaderc_glsl_fragment_shader : shaderc_glsl_vertex_shader;
    const char* input_name   = file_name ? file_name : "default";

    result = shaderc_compile_into_spv(compiler, source_code, source_size, kind, input_name, "main", options);

    if(shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success)
    {
        fprintf(stderr, "pigment_shaderc: GLSL compilation error: %s\n", shaderc_result_get_error_message(result));
        goto FREE;
    }

    *spv_size             = (uint32_t) shaderc_result_get_length(result);
    const uint32_t* bytes = (const uint32_t*) shaderc_result_get_bytes(result);

    spv = (uint32_t*) malloc(*spv_size);
    if(spv != NULL)
    {
        memcpy(spv, bytes, *spv_size);
    }

FREE:
    shaderc_result_release(result);
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);

    return spv;
}

void pigment_shaderc_free(uint32_t* spv)
{
    free(spv);
}
