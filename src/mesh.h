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

#ifndef MESH_H
#define MESH_H

#include "defines.h"

PMeshBuffers* pigment_upload_mesh(Pigment* pigment, const void* vertices, size_t vertices_size,
                                   const uint32_t* indices, uint32_t index_count);
void pigment_destroy_mesh(Pigment* pigment, PMeshBuffers* mesh);
void pigment_draw(Pigment* pigment, PDrawCall* draw_cmds, uint32_t draw_cmd_count);

#endif
