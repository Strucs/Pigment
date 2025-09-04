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

#include "shaders.h"
#include "structs.h"

char* get_shader_code(const char* file_path, uint32_t* shader_size)
{
    FILE* fd = fopen(file_path, "rb");

    if(fd == NULL)
    {
        return NULL;
    }

    fseek(fd, 0l, SEEK_END);
    *shader_size = (uint32_t) ftell(fd);
    rewind(fd);

    char* shader_code = malloc((*shader_size) * sizeof(*shader_code) + 1);
    if(shader_code != NULL)
    {
        fread(shader_code, 1, *shader_size, fd);
        shader_code[*shader_size] = '\0';
    }

    fclose(fd);

    return shader_code;
}

uint32_t* compile_glsl_to_spv(const char* source_code, uint32_t source_size, shaderc_shader_kind kind, const char* file_name, uint32_t* spv_size)
{

    shaderc_compiler_t compiler = NULL;
    shaderc_compile_options_t options = NULL;
    shaderc_compilation_result_t result = NULL;

    compiler = shaderc_compiler_initialize();
    if (compiler == NULL)
    {
        fprintf(stderr, "Failed to initialize shader compiler.\n");
        goto ERROR;
    }

    options = shaderc_compile_options_initialize();
    if (options == NULL)
    {
        fprintf(stderr, "Failed to initialize shader compile options.\n");
        goto ERROR;
    }

    const char* input_name = file_name ? file_name : "default";

    const char* entry_point = "main";

    result = shaderc_compile_into_spv(compiler, source_code, source_size, kind, input_name, entry_point, options);

    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success)
    {
        fprintf(stderr, "GLSL compilation error: %s\n", shaderc_result_get_error_message(result));
        goto ERROR;
    }

    *spv_size = shaderc_result_get_length(result);
    const uint32_t* bytes = (const uint32_t*)shaderc_result_get_bytes(result);

    uint32_t* spv = malloc(*spv_size);
    if (spv != NULL)
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

VkShaderModule create_shader_module(VkDevice device, const uint32_t* code, uint32_t shader_size)
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
