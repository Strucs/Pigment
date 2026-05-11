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

#ifndef PIGMENT_STD_PRIMITIVES_H
#define PIGMENT_STD_PRIMITIVES_H

#include "defines.h"
#include "vertex.h"
#include "mesh.h"

typedef struct PMeshData {
    PVertex* vertices;
    uint32_t vertex_count;
    uint32_t* indices;
    uint32_t index_count;
} PMeshData;

PMeshData pigment_cube_mesh(void);
PMeshData pigment_quad_mesh(void);
PMeshData pigment_plane_mesh(uint32_t segments);
PMeshData pigment_sphere_mesh(uint32_t lat_segments, uint32_t lon_segments);

void pigment_free_mesh_data(PMeshData* mesh);

PMeshBuffers* pigment_upload_mesh_data(Pigment* pigment, PCommandPool* pool, const PMeshData* data);

#endif
