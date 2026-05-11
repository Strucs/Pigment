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

#ifndef PIGMENT_STD_MESH_H
#define PIGMENT_STD_MESH_H

#include "defines.h"
#include "buffers.h"

#include <stdlib.h>

typedef struct PMeshBuffers {
    PBuffer* vertex_buffer;
    PBuffer* index_buffer;
    uint32_t index_count;
    PIndexType index_type;
} PMeshBuffers;

PMeshBuffers* pigment_std_upload_mesh(Pigment* pigment, PCommandPool* pool, const void* vertices, size_t vertices_size, const uint32_t* indices, uint32_t index_count);
void pigment_std_destroy_mesh(Pigment* pigment, PMeshBuffers* mesh);

uint64_t pigment_std_mesh_vertex_address(PMeshBuffers* mesh);

#endif
