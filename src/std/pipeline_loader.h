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

#ifndef LOADER_H
#define LOADER_H

#include "defines.h"
#include "pipeline.h"

#define DEFAULT_VERTEX_SHADER \
"#version 450\n" \
"#extension GL_EXT_buffer_reference : require\n" \
"\n" \
"struct Vertex {\n" \
"    vec3  pos;\n" \
"    float uv_x;\n" \
"    vec3  normal;\n" \
"    float uv_y;\n" \
"    vec4  color;\n" \
"};\n" \
"\n" \
"layout(buffer_reference, std430) readonly buffer VertexBuffer {\n" \
"    Vertex vertices[];\n" \
"};\n" \
"\n" \
"layout(buffer_reference, std430) readonly buffer TransformBuffer {\n" \
"    mat4 transforms[];\n" \
"};\n" \
"\n" \
"layout(buffer_reference, std430) readonly buffer CameraBuffer {\n" \
"    mat4 view;\n" \
"    mat4 proj;\n" \
"};\n" \
"\n" \
"layout(push_constant) uniform constants {\n" \
"    VertexBuffer    vertex_buffer;\n" \
"    TransformBuffer transform_buffer;\n" \
"    CameraBuffer    camera_buffer;\n" \
"    int             image_index;\n" \
"    int             sampler_index;\n" \
"} push;\n" \
"\n" \
"layout(location = 0) out vec4 fragColor;\n" \
"layout(location = 1) out vec2 fragTexCoord;\n" \
"layout(location = 2) flat out int fragImageIndex;\n" \
"layout(location = 3) flat out int fragSamplerIndex;\n" \
"\n" \
"void main()\n" \
"{\n" \
"    Vertex v          = push.vertex_buffer.vertices[gl_VertexIndex];\n" \
"    mat4 world_matrix = push.transform_buffer.transforms[gl_InstanceIndex];\n" \
"\n" \
"    fragColor        = v.color;\n" \
"    fragTexCoord     = vec2(v.uv_x, v.uv_y);\n" \
"    fragImageIndex   = push.image_index;\n" \
"    fragSamplerIndex = push.sampler_index;\n" \
"\n" \
"    gl_Position = push.camera_buffer.proj * push.camera_buffer.view * world_matrix * vec4(v.pos, 1.0);\n" \
"}\n"

#define DEFAULT_FRAGMENT_SHADER \
"#version 450\n" \
"\n" \
"#extension GL_EXT_nonuniform_qualifier : require\n" \
"#define MAX_SAMPLERS 16\n" \
"\n" \
"layout (binding = 1) uniform sampler _sampler[MAX_SAMPLERS];\n" \
"layout (binding = 2) uniform texture2D _image[];\n" \
"\n" \
"layout (location = 0) in vec4 fragColor;\n" \
"layout (location = 1) in vec2 fragTexCoord;\n" \
"layout (location = 2) flat in int inImageIndex;\n" \
"layout (location = 3) flat in int inSamplerIndex;\n" \
"\n" \
"layout (location = 0) out vec4 outColor;\n" \
"\n" \
"void main()\n" \
"{\n" \
"    if (inImageIndex == -1)\n" \
"    {\n" \
"        vec2 c        = floor(fragTexCoord * 8.0);\n" \
"        float checker = mod(c.x + c.y, 2.0);\n" \
"        outColor      = mix(vec4(0.0, 0.0, 0.0, 1.0), vec4(1.0, 0.0, 1.0, 1.0), checker);\n" \
"        return;\n" \
"    }\n" \
"\n" \
"    outColor = texture(sampler2D(_image[nonuniformEXT(inImageIndex)], _sampler[inSamplerIndex]), fragTexCoord) * fragColor;\n" \
"    if (outColor.w < 0.8)\n" \
"    {\n" \
"        discard;\n" \
"    }\n" \
"}\n"

typedef enum PShaderType {
    P_SHADER_TYPE_VERTEX   = 0,
    P_SHADER_TYPE_FRAGMENT = 1,
} PShaderType;

char* load_shader_code(const char* file_path, uint32_t* shader_size);
uint32_t* compile_glsl_to_spv(Pigment* pigment, const char* source_code, uint32_t source_size, PShaderType type, const char* file_name, uint32_t* spv_size);

PPipelineDesc default_graphic_pipeline_desc(Pigment* pigment, const PFormat* color_formats, uint32_t color_format_count, PFormat depth_format);

#endif
