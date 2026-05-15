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

#ifndef PIGMENT_STD_INTERNAL_H
#define PIGMENT_STD_INTERNAL_H

#include "pigment/defines.h"

typedef struct PStdPushConstants {
    uint64_t vertex_buffer;
    uint64_t instance_buffer;
    uint64_t camera_buffer;
    uint64_t material_buffer;
    uint64_t light_buffer;
} PStdPushConstants;

typedef struct PStdGizmoPushConstants {
    uint64_t vertex_buffer;
    uint64_t camera_buffer;
    uint64_t light_buffer;
    float scale;
} PStdGizmoPushConstants;

typedef struct PStdSkyboxPushConstants {
    uint64_t camera_buffer;
    uint32_t cubemap_id;
    uint32_t sampler_id;
} PStdSkyboxPushConstants;

typedef struct PStdCrtPushConstants {
    uint32_t texture_id;
    uint32_t sampler_id;
    float time;
    float aspect;
    float resolution_x;
    float resolution_y;
} PStdCrtPushConstants;

struct PStdPipelineLayouts {
    PLayout* default_layout;
    PLayout* gizmo_layout;
    PLayout* skybox_layout;
    PLayout* crt_layout;
};

#endif
