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

#ifndef PIGMENT_STD_LIGHTS_H
#define PIGMENT_STD_LIGHTS_H

#include <cglm/types.h>
#include "defines.h"
#include "mesh.h"
#include "camera.h"

typedef struct PLights PLights;

typedef enum PLightType {
    P_LIGHT_TYPE_INVALID     = 0,
    P_LIGHT_TYPE_DIRECTIONAL = 1,
    P_LIGHT_TYPE_POINT       = 2,
    P_LIGHT_TYPE_SPOT        = 3,
} PLightType;

typedef struct PLightDesc {
    vec3 position;
    uint32_t type;

    vec3 direction;
    float range;

    vec3 color;
    float intensity;

    float inner_cone_cos;
    float outer_cone_cos;
} __attribute__((aligned(16))) PLightDesc;

PLights* pigment_std_create_lights(Pigment* pigment, uint32_t max_lights);
void pigment_std_destroy_lights(Pigment* pigment, PLights* lights);

void pigment_std_set_ambient(Pigment* pigment, PLights* lights, vec3 color);

uint32_t pigment_std_light_create(Pigment* pigment, PLights* lights, const PLightDesc* desc);
void pigment_std_light_update(Pigment* pigment, PLights* lights, uint32_t id, const PLightDesc* desc);
void pigment_std_light_destroy(Pigment* pigment, PLights* lights, uint32_t id);

uint64_t pigment_std_light_address(PLights* lights);

void pigment_std_draw_light_gizmos(Pigment* pigment, PWindowRenderer* renderer, PPipeline* pipeline, PCamera* camera, PLights* lights, PMeshBuffers* sphere_mesh, uint32_t sphere_index_count, float scale);

#endif
