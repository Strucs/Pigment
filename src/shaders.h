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

#ifndef SHADERS_H
#define SHADERS_H

#include "defines.h"
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>

#define DEFAULT_VERTEX_SHADER \
"#version 450\n" \
"\n" \
"layout (binding = 0) uniform UniformBufferObject {\n" \
"    mat4 model;\n" \
"    mat4 view;\n" \
"    mat4 proj;\n" \
"} ubo;\n" \
"\n" \
"layout (location = 0) in vec3 inPosition;\n" \
"layout (location = 1) in vec3 inColor;\n" \
"layout (location = 2) in vec2 inTexCoord;\n" \
"layout (location = 3) in int inTextureIndex;\n" \
"layout (location = 4) in int inSamplerIndex;\n" \
"\n" \
"layout (location = 0) out vec3 fragColor;\n" \
"layout (location = 1) out vec2 fragTexCoord;\n" \
"layout (location = 2) flat out int fragTexIndex;\n" \
"layout (location = 3) flat out int fragSamplerIndex;\n" \
"\n" \
"void main()\n" \
"{\n" \
"    fragColor = inColor;\n" \
"    fragTexCoord = inTexCoord;\n" \
"    fragTexIndex = inTextureIndex;\n" \
"    fragSamplerIndex = inSamplerIndex;\n" \
"    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);\n" \
"}\n"

#define DEFAULT_FRAGMENT_SHADER \
"#version 450\n" \
"#extension GL_EXT_nonuniform_qualifier : require\n" \
"#define MAX_SAMPLERS 2\n" \
"\n" \
"layout (binding = 1) uniform sampler _sampler[MAX_SAMPLERS];\n" \
"layout (binding = 2) uniform texture2D _texture[];\n" \
"\n" \
"layout (location = 0) in vec3 fragColor;\n" \
"layout (location = 1) in vec2 fragTexCoord;\n" \
"layout (location = 2) flat in int inTexIndex;\n" \
"layout (location = 3) flat in int inSamplerIndex;\n" \
"\n" \
"layout (location = 0) out vec4 outColor;\n" \
"\n" \
"void main()\n" \
"{\n" \
"    int samplerIndex;\n" \
"    if (inTexIndex <= 0)\n" \
"    {\n" \
"        samplerIndex = 0;\n" \
"    }\n" \
"    else\n" \
"    {\n" \
"        samplerIndex = inSamplerIndex;\n" \
"    }\n" \
"    outColor = texture(sampler2D(_texture[nonuniformEXT(inTexIndex)], _sampler[samplerIndex]), fragTexCoord);\n" \
"    if (outColor.w < 0.8)\n" \
"    {\n" \
"        discard;\n" \
"    }\n" \
"}\n"

char* get_shader_code(const char* file_path, uint32_t* shader_size);
uint32_t* compile_glsl_to_spv(const char* source_code, uint32_t source_size, shaderc_shader_kind kind, const char* file_name, uint32_t* spv_size);
VkShaderModule create_shader_module(VkDevice device, const uint32_t* code, uint32_t shader_size);

#endif
